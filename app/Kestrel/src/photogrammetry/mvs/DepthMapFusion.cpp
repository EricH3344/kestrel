#include "photogrammetry/mvs/DepthMapFusion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>

namespace kestrel {

namespace {

cv::Matx33d scaledIntrinsic(const DenseMvsView &view,
                            const DenseDepthMap &map)
{
    const cv::Size sourceSize = !view.image.empty()
        ? view.image.size() : view.calibration.imageSize;
    cv::Matx33d result = view.calibration.intrinsic;
    const double scaleX = static_cast<double>(map.depth.cols)
                          / sourceSize.width;
    const double scaleY = static_cast<double>(map.depth.rows)
                          / sourceSize.height;
    for (int column = 0; column < 3; ++column) {
        result(0, column) *= scaleX;
        result(1, column) *= scaleY;
    }
    return result;
}

bool backProject(const DenseMvsView &view, const DenseDepthMap &map,
                 int column, int row, double depth, cv::Point3d *world)
{
    if (!world || !(depth > 0.0) || !std::isfinite(depth)) {
        return false;
    }
    const cv::Vec3d ray = scaledIntrinsic(view, map).inv()
        * cv::Vec3d(column, row, 1.0);
    const cv::Vec3d cameraPoint = ray * depth;
    const cv::Vec3d value = view.pose.worldToCameraRotation.t()
        * (cameraPoint - view.pose.worldToCameraTranslation);
    if (!std::isfinite(value[0]) || !std::isfinite(value[1])
        || !std::isfinite(value[2])) {
        return false;
    }
    *world = {value[0], value[1], value[2]};
    return true;
}

struct VoxelKey
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    bool operator<(const VoxelKey &other) const
    {
        if (x != other.x) {
            return x < other.x;
        }
        if (y != other.y) {
            return y < other.y;
        }
        return z < other.z;
    }
};

struct ViewContribution
{
    int imageIndex = -1;
    cv::Vec3d weightedPosition{0.0, 0.0, 0.0};
    double weight = 0.0;
    double confidenceSum = 0.0;
    int sampleCount = 0;
    DensePointSource representative;
};

struct VoxelAccumulator
{
    std::vector<ViewContribution> views;
};

ViewContribution *contributionFor(VoxelAccumulator *voxel, int imageIndex)
{
    const auto existing = std::find_if(
        voxel->views.begin(), voxel->views.end(),
        [imageIndex](const ViewContribution &value) {
            return value.imageIndex == imageIndex;
        });
    if (existing != voxel->views.end()) {
        return &*existing;
    }
    voxel->views.push_back({});
    voxel->views.back().imageIndex = imageIndex;
    voxel->views.back().representative.imageIndex = imageIndex;
    return &voxel->views.back();
}

bool validMap(const DenseDepthMap &map)
{
    return !map.depth.empty() && map.depth.type() == CV_32FC1
           && map.validityMask.type() == CV_8UC1
           && map.validityMask.size() == map.depth.size()
           && map.confidence.type() == CV_32FC1
           && map.confidence.size() == map.depth.size();
}

bool voxelCoordinate(double value, double voxelSize, std::int64_t *coordinate)
{
    const double scaled = value / voxelSize;
    constexpr double limit = static_cast<double>(
        std::numeric_limits<std::int64_t>::max()) - 1.0;
    if (!coordinate || !std::isfinite(scaled) || std::abs(scaled) > limit) {
        return false;
    }
    *coordinate = std::llround(scaled);
    return true;
}

} // namespace

DepthMapFusionResult DepthMapFusion::fuseStreaming(
    const std::vector<DenseMvsView> &views,
    size_t depthMapCount,
    const DepthMapLoader &loader,
    const DepthMapFusionOptions &options)
{
    DepthMapFusionResult result;
    if (views.empty() || depthMapCount == 0 || !loader) {
        result.message = "Depth-map fusion requires views and depth maps.";
        return result;
    }
    if (!(options.voxelSizeMetres >= 1e-9)
        || !std::isfinite(options.voxelSizeMetres)
        || options.minimumObservations <= 0 || options.pixelStride <= 0
        || options.maximumProvenanceViews < 0
        || !std::isfinite(options.minimumInputConfidence)
        || options.minimumInputConfidence < 0.0f
        || options.minimumInputConfidence > 1.0f) {
        result.message = "Depth-map fusion options are invalid.";
        return result;
    }

    std::map<int, const DenseMvsView *> viewByImage;
    for (const DenseMvsView &view : views) {
        const cv::Size sourceSize = !view.image.empty()
            ? view.image.size() : view.calibration.imageSize;
        if (sourceSize.width > 0 && sourceSize.height > 0) {
            viewByImage[view.imageIndex] = &view;
        }
    }

    std::map<VoxelKey, VoxelAccumulator> voxels;
    for (size_t mapIndex = 0; mapIndex < depthMapCount; ++mapIndex) {
        DenseDepthMap map;
        std::string loadError;
        if (!loader(mapIndex, &map, &loadError)) {
            result.message = "Could not load depth map "
                + std::to_string(mapIndex) + ": "
                + (loadError.empty() ? "unknown loader error" : loadError);
            return result;
        }
        const auto viewEntry = viewByImage.find(map.imageIndex);
        if (viewEntry == viewByImage.end() || !validMap(map)) {
            result.rejectedInputSampleCount += map.depth.total();
            continue;
        }
        const DenseMvsView &view = *viewEntry->second;
        const bool hasConsistency = map.consistentViewCount.type() == CV_8UC1
            && map.consistentViewCount.size() == map.depth.size();
        for (int row = 0; row < map.depth.rows; row += options.pixelStride) {
            const float *depths = map.depth.ptr<float>(row);
            const float *confidences = map.confidence.ptr<float>(row);
            const uchar *valid = map.validityMask.ptr<uchar>(row);
            const uchar *consistent = hasConsistency
                ? map.consistentViewCount.ptr<uchar>(row) : nullptr;
            for (int column = 0; column < map.depth.cols;
                 column += options.pixelStride) {
                const float confidence = confidences[column];
                if (!valid[column] || !(depths[column] > 0.0f)
                    || !std::isfinite(depths[column])
                    || !std::isfinite(confidence)
                    || confidence < options.minimumInputConfidence) {
                    ++result.rejectedInputSampleCount;
                    continue;
                }
                cv::Point3d world;
                if (!backProject(view, map, column, row, depths[column],
                                 &world)) {
                    ++result.rejectedInputSampleCount;
                    continue;
                }
                VoxelKey key;
                if (!voxelCoordinate(world.x, options.voxelSizeMetres, &key.x)
                    || !voxelCoordinate(world.y, options.voxelSizeMetres, &key.y)
                    || !voxelCoordinate(world.z, options.voxelSizeMetres,
                                        &key.z)) {
                    ++result.rejectedInputSampleCount;
                    continue;
                }
                ViewContribution *contribution = contributionFor(
                    &voxels[key], map.imageIndex);
                const double geometricSupport = consistent
                    ? 1.0 + consistent[column] : 1.0;
                const double weight = std::max(1e-6f, confidence)
                                      * geometricSupport;
                contribution->weightedPosition += weight * cv::Vec3d(
                    world.x, world.y, world.z);
                contribution->weight += weight;
                contribution->confidenceSum += confidence;
                ++contribution->sampleCount;
                if (confidence >= contribution->representative.confidence) {
                    contribution->representative.depthPixel = {
                        static_cast<float>(column), static_cast<float>(row)};
                    contribution->representative.confidence = confidence;
                }
                ++result.validInputSampleCount;
            }
        }
    }

    result.candidateVoxelCount = voxels.size();
    result.points.reserve(voxels.size());
    for (auto &[key, voxel] : voxels) {
        (void)key;
        if (static_cast<int>(voxel.views.size())
            < options.minimumObservations) {
            ++result.underSupportedVoxelCount;
            continue;
        }
        FusedDensePoint point;
        cv::Vec3d weightedPosition(0.0, 0.0, 0.0);
        double totalWeight = 0.0;
        double confidenceSum = 0.0;
        std::sort(voxel.views.begin(), voxel.views.end(),
                  [](const ViewContribution &first,
                     const ViewContribution &second) {
                      return first.imageIndex < second.imageIndex;
                  });
        for (const ViewContribution &view : voxel.views) {
            if (!(view.weight > 0.0) || view.sampleCount <= 0) {
                continue;
            }
            const cv::Vec3d viewPosition = view.weightedPosition / view.weight;
            const double viewConfidence = view.confidenceSum / view.sampleCount;
            const double viewWeight = std::max(1e-6, viewConfidence);
            weightedPosition += viewWeight * viewPosition;
            totalWeight += viewWeight;
            confidenceSum += viewConfidence;
            point.sampleCount += view.sampleCount;
            if (static_cast<int>(point.sources.size())
                < options.maximumProvenanceViews) {
                point.sources.push_back(view.representative);
            }
        }
        point.observationCount = static_cast<int>(voxel.views.size());
        if (!(totalWeight > 0.0)
            || point.observationCount < options.minimumObservations) {
            ++result.underSupportedVoxelCount;
            continue;
        }
        const cv::Vec3d position = weightedPosition / totalWeight;
        point.position = {position[0], position[1], position[2]};
        point.confidence = static_cast<float>(
            confidenceSum / point.observationCount);
        result.points.push_back(std::move(point));
    }

    result.success = !result.points.empty();
    result.message = result.success
        ? "Depth maps fused into a confidence-weighted world-frame point cloud."
        : "Depth-map fusion produced no sufficiently supported points.";
    return result;
}

DepthMapFusionResult DepthMapFusion::fuse(
    const std::vector<DenseMvsView> &views,
    const std::vector<DenseDepthMap> &depthMaps,
    const DepthMapFusionOptions &options)
{
    return fuseStreaming(
        views, depthMaps.size(),
        [&depthMaps](size_t index, DenseDepthMap *depthMap,
                     std::string *errorMessage) {
            if (!depthMap || index >= depthMaps.size()) {
                if (errorMessage) {
                    *errorMessage = "Depth-map vector index is invalid.";
                }
                return false;
            }
            // cv::Mat uses shared ownership, so this adapter does not copy the
            // pixel buffers used by the original resident-vector API.
            *depthMap = depthMaps[index];
            if (errorMessage) {
                errorMessage->clear();
            }
            return true;
        }, options);
}

} // namespace kestrel
