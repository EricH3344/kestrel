#include "photogrammetry/mvs/DepthMapFusion.h"

#include <cmath>
#include <iostream>
#include <set>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

kestrel::CameraCalibration calibration()
{
    kestrel::CameraCalibration value;
    value.imageSize = {96, 72};
    value.intrinsic = cv::Matx33d(120.0, 0.0, 47.5,
                                  0.0, 120.0, 35.5,
                                  0.0, 0.0, 1.0);
    value.quality = kestrel::CalibrationQuality::Approximate;
    value.source = "synthetic";
    return value;
}

kestrel::DenseMvsView view(int imageIndex, double centreX)
{
    kestrel::SparseCameraPose pose;
    pose.imageIndex = imageIndex;
    pose.worldToCameraTranslation = {-centreX, 0.0, 0.0};
    return {imageIndex, cv::Mat(72, 96, CV_8U, cv::Scalar(128)),
            calibration(), pose};
}

kestrel::DenseDepthMap planeMap(int imageIndex, cv::Size size,
                                float depth, float confidence)
{
    kestrel::DenseDepthMap map;
    map.imageIndex = imageIndex;
    map.depth = cv::Mat(size, CV_32F, cv::Scalar(depth)).clone();
    map.confidence = cv::Mat(size, CV_32F, cv::Scalar(confidence)).clone();
    map.validityMask = cv::Mat(size, CV_8U, cv::Scalar(255)).clone();
    map.consistentViewCount = cv::Mat(size, CV_8U, cv::Scalar(2)).clone();
    return map;
}

bool planeFusionTest()
{
    const std::vector<kestrel::DenseMvsView> views{
        view(0, 0.0), view(1, 0.4), view(2, -0.35)};
    const std::vector<kestrel::DenseDepthMap> maps{
        planeMap(0, {48, 36}, 8.0f, 0.8f),
        planeMap(1, {48, 36}, 8.0f, 0.7f),
        planeMap(2, {48, 36}, 8.0f, 0.6f)};
    kestrel::DepthMapFusionOptions options;
    options.voxelSizeMetres = 0.15;
    options.minimumObservations = 2;
    const auto result = kestrel::DepthMapFusion::fuse(views, maps, options);
    if (!expect(result.success && result.points.size() > 300,
                "Overlapping plane depth maps should form a dense cloud.")) {
        return false;
    }
    for (const kestrel::FusedDensePoint &point : result.points) {
        if (!expect(std::abs(point.position.z - 8.0) < 1e-9,
                    "A fused plane should preserve its world height.")) {
            return false;
        }
        std::set<int> sources;
        for (const kestrel::DensePointSource &source : point.sources) {
            sources.insert(source.imageIndex);
        }
        if (!expect(point.observationCount >= 2
                        && sources.size() == point.sources.size(),
                    "Fused support and provenance must use distinct cameras.")) {
            return false;
        }
    }
    return expect(result.validInputSampleCount == 48u * 36u * 3u,
                  "Every valid synthetic depth pixel should be considered.");
}

bool confidenceWeightingTest()
{
    const std::vector<kestrel::DenseMvsView> views{
        view(0, 0.0), view(1, 0.0)};
    std::vector<kestrel::DenseDepthMap> maps{
        planeMap(0, {1, 1}, 8.0f, 0.75f),
        planeMap(1, {1, 1}, 10.0f, 0.25f)};
    maps[0].consistentViewCount.setTo(0);
    maps[1].consistentViewCount.setTo(0);
    kestrel::DepthMapFusionOptions options;
    options.voxelSizeMetres = 100.0;
    options.minimumObservations = 2;
    const auto result = kestrel::DepthMapFusion::fuse(views, maps, options);
    return expect(result.success && result.points.size() == 1,
                  "Two co-located observations should form one fused point.")
           && expect(std::abs(result.points[0].position.z - 8.5) < 1e-9,
                     "Fusion should weight camera observations by confidence.")
           && expect(result.points[0].observationCount == 2
                         && result.points[0].sampleCount == 2,
                     "Fusion should distinguish camera observations from samples.");
}

bool supportAndValidationTest()
{
    const std::vector<kestrel::DenseMvsView> views{view(0, 0.0)};
    const std::vector<kestrel::DenseDepthMap> maps{
        planeMap(0, {4, 4}, 8.0f, 0.8f)};
    kestrel::DepthMapFusionOptions options;
    options.minimumObservations = 2;
    const auto unsupported = kestrel::DepthMapFusion::fuse(
        views, maps, options);
    options.voxelSizeMetres = 0.0;
    const auto invalid = kestrel::DepthMapFusion::fuse(views, maps, options);
    return expect(!unsupported.success && unsupported.points.empty()
                      && unsupported.underSupportedVoxelCount > 0,
                  "Single-view voxels should not pass two-view fusion.")
           && expect(!invalid.success,
                     "Invalid metric voxel sizes should be rejected.");
}

bool streamingFusionTest()
{
    const std::vector<kestrel::DenseMvsView> views{
        view(0, 0.0), view(1, 0.4), view(2, -0.35)};
    const std::vector<kestrel::DenseDepthMap> maps{
        planeMap(0, {24, 18}, 8.0f, 0.8f),
        planeMap(1, {24, 18}, 8.0f, 0.7f),
        planeMap(2, {24, 18}, 8.0f, 0.6f)};
    kestrel::DepthMapFusionOptions options;
    options.voxelSizeMetres = 0.2;
    options.minimumObservations = 2;
    const auto resident = kestrel::DepthMapFusion::fuse(
        views, maps, options);
    size_t loaderCalls = 0;
    const auto streamed = kestrel::DepthMapFusion::fuseStreaming(
        views, maps.size(),
        [&maps, &loaderCalls](size_t index,
                              kestrel::DenseDepthMap *map,
                              std::string *) {
            ++loaderCalls;
            *map = maps[index];
            return true;
        }, options);
    if (!expect(streamed.success && resident.success
                    && loaderCalls == maps.size(),
                "Streaming fusion should load every map exactly once.")) {
        return false;
    }
    if (!expect(streamed.validInputSampleCount
                    == resident.validInputSampleCount
                    && streamed.candidateVoxelCount
                        == resident.candidateVoxelCount
                    && streamed.points.size() == resident.points.size(),
                "Streaming and resident fusion diagnostics must agree.")) {
        return false;
    }
    for (size_t index = 0; index < resident.points.size(); ++index) {
        const cv::Point3d difference = streamed.points[index].position
                                      - resident.points[index].position;
        if (!expect(cv::norm(difference) < 1e-12
                        && streamed.points[index].observationCount
                            == resident.points[index].observationCount,
                    "Streaming fusion must reproduce resident fusion exactly.")) {
            return false;
        }
    }

    const auto failed = kestrel::DepthMapFusion::fuseStreaming(
        views, 1,
        [](size_t, kestrel::DenseDepthMap *, std::string *error) {
            *error = "synthetic read failure";
            return false;
        }, options);
    return expect(!failed.success
                      && failed.message.find("synthetic read failure")
                          != std::string::npos,
                  "A streaming loader failure should remain actionable.");
}

} // namespace

int main()
{
    if (planeFusionTest() && confidenceWeightingTest()
        && supportAndValidationTest() && streamingFusionTest()) {
        std::cout << "Depth-map fusion tests passed.\n";
        return 0;
    }
    return 1;
}
