#include "photogrammetry/terrain/MultiCameraOrthorectifier.h"
#include "photogrammetry/tiling/TileLayout.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kestrel {

namespace {

struct SourceMaps
{
    cv::Mat mapX;
    cv::Mat mapY;
    cv::Mat weight;
};

struct Candidate
{
    int sourceIndex = -1;
    cv::Point2d imagePoint;
    double score = 0.0;
};

cv::Point3d cameraCentre(const PinholeCamera &camera)
{
    const cv::Vec3d centre = -camera.worldToCameraRotation.t()
                             * camera.worldToCameraTranslation;
    return {centre[0], centre[1], centre[2]};
}

bool sampleConfidence(const OrthophotoSource &source,
                      const cv::Point2d &point, double *confidence)
{
    if (!confidence) {
        return false;
    }
    if (source.confidence.empty()) {
        *confidence = 1.0;
        return true;
    }
    if (source.confidence.type() != CV_32FC1
        || source.confidence.size() != source.image.size()
        || point.x < 0.0 || point.y < 0.0
        || point.x >= source.confidence.cols - 1
        || point.y >= source.confidence.rows - 1) {
        return false;
    }
    const int left = static_cast<int>(std::floor(point.x));
    const int top = static_cast<int>(std::floor(point.y));
    const double alphaX = point.x - left;
    const double alphaY = point.y - top;
    double value = 0.0;
    for (int rowOffset = 0; rowOffset <= 1; ++rowOffset) {
        for (int columnOffset = 0; columnOffset <= 1; ++columnOffset) {
            const float sample = source.confidence.at<float>(
                top + rowOffset, left + columnOffset);
            if (!std::isfinite(sample)) {
                return false;
            }
            value += sample
                * (columnOffset ? alphaX : 1.0 - alphaX)
                * (rowOffset ? alphaY : 1.0 - alphaY);
        }
    }
    *confidence = std::clamp(value, 0.0, 1.0);
    return true;
}

cv::Vec3d terrainNormal(const TerrainGrid &terrain,
                        double worldX, double worldY, double centreHeight)
{
    const double stepX = std::abs(terrain.spacing[0]);
    const double stepY = std::abs(terrain.spacing[1]);
    double left = centreHeight;
    double right = centreHeight;
    double bottom = centreHeight;
    double top = centreHeight;
    terrain.sample(worldX - stepX, worldY, &left);
    terrain.sample(worldX + stepX, worldY, &right);
    terrain.sample(worldX, worldY - stepY, &bottom);
    terrain.sample(worldX, worldY + stepY, &top);
    cv::Vec3d normal(-(right - left) / (2.0 * stepX),
                     -(top - bottom) / (2.0 * stepY), 1.0);
    const double length = cv::norm(normal);
    return length > 1e-12 ? normal / length : cv::Vec3d(0.0, 0.0, 1.0);
}

bool insideTerrain(const TerrainGrid &terrain, double worldX, double worldY)
{
    const double oppositeX = terrain.origin.x
        + (terrain.elevation.cols - 1) * terrain.spacing[0];
    const double oppositeY = terrain.origin.y
        + (terrain.elevation.rows - 1) * terrain.spacing[1];
    return worldX >= std::min(terrain.origin.x, oppositeX)
           && worldX <= std::max(terrain.origin.x, oppositeX)
           && worldY >= std::min(terrain.origin.y, oppositeY)
           && worldY <= std::max(terrain.origin.y, oppositeY);
}

bool occludedByTerrain(const TerrainGrid &terrain,
                       const cv::Point3d &worldPoint,
                       const cv::Point3d &camera,
                       const MultiCameraOrthophotoOptions &options)
{
    const double dx = camera.x - worldPoint.x;
    const double dy = camera.y - worldPoint.y;
    const double horizontalDistance = std::hypot(dx, dy);
    if (horizontalDistance < 1e-9) {
        return false;
    }
    const double derivedSpacing = 0.5 * std::min(
        std::abs(terrain.spacing[0]), std::abs(terrain.spacing[1]));
    const double spacing = options.occlusionSampleSpacingMetres > 0.0
        ? options.occlusionSampleSpacingMetres
        : std::max(1e-4, derivedSpacing);
    const int samples = std::clamp(
        static_cast<int>(std::ceil(horizontalDistance / spacing)),
        2, options.maximumOcclusionSamples);
    for (int index = 1; index < samples; ++index) {
        const double alpha = static_cast<double>(index) / samples;
        const double x = worldPoint.x + alpha * dx;
        const double y = worldPoint.y + alpha * dy;
        if (!insideTerrain(terrain, x, y)) {
            break;
        }
        double terrainHeight = 0.0;
        if (!terrain.sample(x, y, &terrainHeight)) {
            if (options.rejectUnknownTerrainAlongRay) {
                return true;
            }
            continue;
        }
        const double rayHeight = worldPoint.z
            + alpha * (camera.z - worldPoint.z);
        if (terrainHeight > rayHeight + options.occlusionClearanceMetres) {
            return true;
        }
    }
    return false;
}

bool validOptions(const MultiCameraOrthophotoOptions &options)
{
    return std::isfinite(options.minimumViewCosine)
           && options.minimumViewCosine >= 0.0
           && options.minimumViewCosine <= 1.0
           && std::isfinite(options.viewAngleExponent)
           && options.viewAngleExponent >= 0.0
           && std::isfinite(options.borderMarginPixels)
           && options.borderMarginPixels > 0.0
           && std::isfinite(options.borderExponent)
           && options.borderExponent >= 0.0
           && std::isfinite(options.occlusionClearanceMetres)
           && options.occlusionClearanceMetres >= 0.0
           && std::isfinite(options.occlusionSampleSpacingMetres)
           && options.occlusionSampleSpacingMetres >= 0.0
           && options.maximumOcclusionSamples >= 2
           && options.maximumSourcesPerPixel > 0
           && std::isfinite(options.minimumSourceScore)
           && options.minimumSourceScore >= 0.0
           && options.tileSize.width >= 0
           && options.tileSize.height >= 0
           && ((options.tileSize.width == 0 && options.tileSize.height == 0)
               || (options.tileSize.width > 0 && options.tileSize.height > 0))
           && options.tileHaloPixels >= 0;
}

bool validInputs(const std::vector<OrthophotoSource> &sources,
                 const TerrainGrid &terrain,
                 const OrthophotoGrid &grid,
                 const MultiCameraOrthophotoOptions &options,
                 std::string *message)
{
    if (sources.empty() || terrain.elevation.empty()
        || terrain.elevation.type() != CV_32FC1
        || grid.size.width <= 0 || grid.size.height <= 0
        || !std::isfinite(terrain.origin.x)
        || !std::isfinite(terrain.origin.y)
        || !std::isfinite(terrain.spacing[0])
        || !std::isfinite(terrain.spacing[1])
        || std::abs(terrain.spacing[0]) < 1e-12
        || std::abs(terrain.spacing[1]) < 1e-12
        || !std::isfinite(grid.origin.x) || !std::isfinite(grid.origin.y)
        || !std::isfinite(grid.pixelSize[0])
        || !std::isfinite(grid.pixelSize[1])
        || std::abs(grid.pixelSize[0]) < 1e-12
        || std::abs(grid.pixelSize[1]) < 1e-12
        || !validOptions(options)) {
        *message = "Multi-camera orthorectification input or options are invalid.";
        return false;
    }
    const int type = sources.front().image.type();
    if (sources.front().image.empty()) {
        *message = "An orthophoto source image is empty.";
        return false;
    }
    for (const OrthophotoSource &source : sources) {
        if (source.image.empty() || source.image.type() != type
            || !std::isfinite(source.exposureQuality)
            || source.exposureQuality < 0.0) {
            *message = "Orthophoto sources require a common image type and valid quality.";
            return false;
        }
        if (!source.confidence.empty()
            && (source.confidence.type() != CV_32FC1
                || source.confidence.size() != source.image.size())) {
            *message = "Source confidence must be CV_32FC1 and match its image.";
            return false;
        }
    }
    return true;
}

} // namespace

namespace {

MultiCameraOrthophotoResult orthorectifySingleTile(
    const std::vector<OrthophotoSource> &sources,
    const TerrainGrid &terrain,
    const OrthophotoGrid &outputGrid,
    const MultiCameraOrthophotoOptions &options)
{
    MultiCameraOrthophotoResult result;
    result.outputGrid = outputGrid;
    if (!validInputs(sources, terrain, outputGrid, options,
                     &result.message)) {
        return result;
    }

    std::vector<SourceMaps> maps(sources.size());
    for (SourceMaps &source : maps) {
        source.mapX = cv::Mat(outputGrid.size, CV_32F, cv::Scalar(-1.0f));
        source.mapY = cv::Mat(outputGrid.size, CV_32F, cv::Scalar(-1.0f));
        source.weight = cv::Mat::zeros(outputGrid.size, CV_32F);
    }
    result.primarySourceImageIndex = cv::Mat(
        outputGrid.size, CV_32S, cv::Scalar(-1));
    result.sourceCount = cv::Mat::zeros(outputGrid.size, CV_8U);
    result.projectedCandidateCountMap = cv::Mat::zeros(
        outputGrid.size, CV_16U);
    result.occludedCandidateCountMap = cv::Mat::zeros(
        outputGrid.size, CV_16U);
    result.accumulatedQuality = cv::Mat::zeros(outputGrid.size, CV_32F);

    std::vector<cv::Point3d> centres;
    centres.reserve(sources.size());
    for (const OrthophotoSource &source : sources) {
        centres.push_back(cameraCentre(source.camera));
    }

    for (int row = 0; row < outputGrid.size.height; ++row) {
        const double worldY = outputGrid.origin.y
            + row * outputGrid.pixelSize[1];
        for (int column = 0; column < outputGrid.size.width; ++column) {
            const double worldX = outputGrid.origin.x
                + column * outputGrid.pixelSize[0];
            double height = 0.0;
            if (!terrain.sample(worldX, worldY, &height)) {
                continue;
            }
            const cv::Point3d world(worldX, worldY, height);
            const cv::Vec3d normal = terrainNormal(
                terrain, worldX, worldY, height);
            std::vector<Candidate> candidates;
            candidates.reserve(sources.size());
            for (int sourceIndex = 0;
                 sourceIndex < static_cast<int>(sources.size());
                 ++sourceIndex) {
                const OrthophotoSource &source = sources[sourceIndex];
                cv::Point2d imagePoint;
                if (!source.camera.project(world, &imagePoint)
                    || imagePoint.x < 0.0 || imagePoint.y < 0.0
                    || imagePoint.x > source.image.cols - 1.0
                    || imagePoint.y > source.image.rows - 1.0) {
                    continue;
                }
                ++result.projectedCandidateCount;
                ushort &projectedAtPixel =
                    result.projectedCandidateCountMap.at<ushort>(row, column);
                if (projectedAtPixel < std::numeric_limits<ushort>::max()) {
                    ++projectedAtPixel;
                }
                const cv::Vec3d toCamera(
                    centres[sourceIndex].x - world.x,
                    centres[sourceIndex].y - world.y,
                    centres[sourceIndex].z - world.z);
                const double distance = cv::norm(toCamera);
                if (!(distance > 1e-9)) {
                    continue;
                }
                const double viewCosine = normal.dot(toCamera / distance);
                if (viewCosine < options.minimumViewCosine) {
                    continue;
                }
                if (occludedByTerrain(terrain, world, centres[sourceIndex],
                                      options)) {
                    ++result.occludedCandidateCount;
                    ushort &occludedAtPixel =
                        result.occludedCandidateCountMap.at<ushort>(row, column);
                    if (occludedAtPixel < std::numeric_limits<ushort>::max()) {
                        ++occludedAtPixel;
                    }
                    continue;
                }
                const double borderDistance = std::min({
                    imagePoint.x, imagePoint.y,
                    source.image.cols - 1.0 - imagePoint.x,
                    source.image.rows - 1.0 - imagePoint.y});
                const double borderQuality = std::clamp(
                    borderDistance / options.borderMarginPixels, 0.0, 1.0);
                double confidence = 0.0;
                if (!sampleConfidence(source, imagePoint, &confidence)) {
                    continue;
                }
                const double focal = std::sqrt(std::max(
                    0.0, source.camera.intrinsic(0, 0)
                         * source.camera.intrinsic(1, 1)));
                const double score = source.exposureQuality * confidence
                    * std::pow(viewCosine, options.viewAngleExponent)
                    * std::pow(borderQuality, options.borderExponent)
                    * focal / distance;
                if (score >= options.minimumSourceScore
                    && std::isfinite(score)) {
                    candidates.push_back({sourceIndex, imagePoint, score});
                }
            }
            std::sort(candidates.begin(), candidates.end(),
                      [&sources](const Candidate &first,
                                 const Candidate &second) {
                          if (first.score != second.score) {
                              return first.score > second.score;
                          }
                          return sources[first.sourceIndex].imageIndex
                                 < sources[second.sourceIndex].imageIndex;
                      });
            if (static_cast<int>(candidates.size())
                > options.maximumSourcesPerPixel) {
                candidates.resize(options.maximumSourcesPerPixel);
            }
            double qualitySum = 0.0;
            for (const Candidate &candidate : candidates) {
                maps[candidate.sourceIndex].mapX.at<float>(row, column) =
                    static_cast<float>(candidate.imagePoint.x);
                maps[candidate.sourceIndex].mapY.at<float>(row, column) =
                    static_cast<float>(candidate.imagePoint.y);
                maps[candidate.sourceIndex].weight.at<float>(row, column) =
                    static_cast<float>(candidate.score);
                qualitySum += candidate.score;
                ++result.selectedContributionCount;
            }
            if (!candidates.empty()) {
                result.primarySourceImageIndex.at<int>(row, column) =
                    sources[candidates.front().sourceIndex].imageIndex;
                result.sourceCount.at<uchar>(row, column) =
                    static_cast<uchar>(std::min<size_t>(255, candidates.size()));
                result.accumulatedQuality.at<float>(row, column) =
                    static_cast<float>(qualitySum);
            }
        }
    }

    const int channels = sources.front().image.channels();
    std::vector<cv::Mat> warpedSources(sources.size());
    for (int sourceIndex = 0;
         sourceIndex < static_cast<int>(sources.size()); ++sourceIndex) {
        cv::remap(sources[sourceIndex].image, warpedSources[sourceIndex],
                  maps[sourceIndex].mapX, maps[sourceIndex].mapY,
                  options.interpolation, cv::BORDER_CONSTANT,
                  cv::Scalar::all(0));
    }

    if (options.useSeamBlending) {
        std::vector<OrthophotoBlendLayer> layers;
        layers.reserve(sources.size());
        for (int sourceIndex = 0;
             sourceIndex < static_cast<int>(sources.size()); ++sourceIndex) {
            layers.push_back({sources[sourceIndex].imageIndex,
                              warpedSources[sourceIndex],
                              maps[sourceIndex].weight});
        }
        OrthophotoSeamBlendResult blended = OrthophotoSeamBlender::blend(
            layers, options.seamBlendOptions);
        if (!blended.success) {
            result.message = "Orthophoto seam blending failed: "
                + blended.message;
            return result;
        }
        result.orthophoto = std::move(blended.image);
        result.validityMask = std::move(blended.validityMask);
        result.primarySourceImageIndex = std::move(
            blended.primarySourceImageIndex);
        result.seamMask = std::move(blended.seamMask);
        result.usedSeamBlending = true;
        result.seamOptimizationIterations = blended.optimizationIterations;
        result.seamLabelChangeCount = blended.labelChangeCount;
        result.seamPixelCount = blended.seamPixelCount;
        result.success = true;
        result.message = "Multi-camera terrain orthophoto produced "
            "occlusion-aware, seam-optimized multiband coverage.";
        return result;
    }

    cv::Mat denominator = cv::Mat::zeros(outputGrid.size, CV_32F);
    std::vector<cv::Mat> accumulated(channels);
    for (cv::Mat &channel : accumulated) {
        channel = cv::Mat::zeros(outputGrid.size, CV_32F);
    }
    for (int sourceIndex = 0;
         sourceIndex < static_cast<int>(sources.size()); ++sourceIndex) {
        cv::Mat warped;
        warpedSources[sourceIndex].convertTo(
            warped, CV_MAKETYPE(CV_32F, channels));
        std::vector<cv::Mat> warpedChannels;
        cv::split(warped, warpedChannels);
        for (int channel = 0; channel < channels; ++channel) {
            accumulated[channel] += warpedChannels[channel].mul(
                maps[sourceIndex].weight);
        }
        denominator += maps[sourceIndex].weight;
    }
    result.validityMask = denominator > 0.0f;
    cv::Mat safeDenominator = denominator.clone();
    safeDenominator.setTo(1.0f, result.validityMask == 0);
    for (cv::Mat &channel : accumulated) {
        cv::divide(channel, safeDenominator, channel);
        channel.setTo(0.0f, result.validityMask == 0);
    }
    cv::Mat floatOutput;
    cv::merge(accumulated, floatOutput);
    floatOutput.convertTo(
        result.orthophoto,
        CV_MAKETYPE(sources.front().image.depth(), channels));
    result.success = cv::countNonZero(result.validityMask) > 0;
    result.message = result.success
        ? "Multi-camera terrain orthophoto produced scored, occlusion-aware weighted coverage."
        : "No terrain pixel had a usable orthophoto source.";
    return result;
}

} // namespace

int MultiCameraOrthorectifier::recommendedTileHaloPixels(
    const MultiCameraOrthophotoOptions &options)
{
    if (!options.useSeamBlending) {
        if (options.interpolation == cv::INTER_LANCZOS4) {
            return 4;
        }
        return options.interpolation == cv::INTER_NEAREST ? 1 : 2;
    }
    const int levels = std::clamp(
        options.seamBlendOptions.multibandLevels, 1, 16);
    const int pyramidSupport = 4 * (1 << (levels - 1));
    const int seamSupport = std::max(
        0, options.seamBlendOptions.maximumIterations);
    return std::max(1, pyramidSupport + seamSupport);
}

MultiCameraOrthophotoResult MultiCameraOrthorectifier::orthorectify(
    const std::vector<OrthophotoSource> &sources,
    const TerrainGrid &terrain,
    const OrthophotoGrid &outputGrid,
    const MultiCameraOrthophotoOptions &options)
{
    // The single-tile result records the grid itself.
    if (options.tileSize.width == 0 && options.tileSize.height == 0) {
        return orthorectifySingleTile(sources, terrain, outputGrid, options);
    }

    MultiCameraOrthophotoResult result;
    result.outputGrid = outputGrid;
    if (!validInputs(sources, terrain, outputGrid, options,
                     &result.message)) {
        return result;
    }
    const int minimumHalo = recommendedTileHaloPixels(options);
    const int halo = options.tileHaloPixels > 0
        ? options.tileHaloPixels : minimumHalo;
    if (halo < minimumHalo) {
        result.message = "Orthophoto tile halo is too small for seam and multiband support.";
        return result;
    }
    std::vector<ProcessingTile> tiles;
    if (!TileLayout::build(outputGrid.size, options.tileSize, halo,
                           &tiles, &result.message)) {
        return result;
    }
    // Align halo bounds to the coarsest pyramid lattice. Without this, two
    // tiles can sample identical global pixels with different pyrDown phases,
    // producing a faint numerical boundary despite ample spatial overlap.
    const int pyramidLevels = options.useSeamBlending
        ? std::clamp(options.seamBlendOptions.multibandLevels, 1, 16) : 1;
    const int pyramidAlignment = 1 << (pyramidLevels - 1);
    const cv::Rect canvas(0, 0, outputGrid.size.width, outputGrid.size.height);
    for (ProcessingTile &tile : tiles) {
        const int left = (tile.expanded.x / pyramidAlignment)
            * pyramidAlignment;
        const int top = (tile.expanded.y / pyramidAlignment)
            * pyramidAlignment;
        const int right = std::min(
            outputGrid.size.width,
            ((tile.expanded.x + tile.expanded.width + pyramidAlignment - 1)
             / pyramidAlignment) * pyramidAlignment);
        const int bottom = std::min(
            outputGrid.size.height,
            ((tile.expanded.y + tile.expanded.height + pyramidAlignment - 1)
             / pyramidAlignment) * pyramidAlignment);
        tile.expanded = cv::Rect(left, top, right - left, bottom - top)
            & canvas;
    }

    const int outputType = sources.front().image.type();
    result.orthophoto = cv::Mat::zeros(outputGrid.size, outputType);
    result.validityMask = cv::Mat::zeros(outputGrid.size, CV_8U);
    result.primarySourceImageIndex = cv::Mat(
        outputGrid.size, CV_32S, cv::Scalar(-1));
    result.sourceCount = cv::Mat::zeros(outputGrid.size, CV_8U);
    result.projectedCandidateCountMap = cv::Mat::zeros(
        outputGrid.size, CV_16U);
    result.occludedCandidateCountMap = cv::Mat::zeros(
        outputGrid.size, CV_16U);
    result.accumulatedQuality = cv::Mat::zeros(outputGrid.size, CV_32F);
    result.seamMask = cv::Mat::zeros(outputGrid.size, CV_8U);

    MultiCameraOrthophotoOptions tileOptions = options;
    tileOptions.tileSize = {};
    tileOptions.tileHaloPixels = 0;
    for (const ProcessingTile &tile : tiles) {
        OrthophotoGrid tileGrid = outputGrid;
        tileGrid.size = tile.expanded.size();
        tileGrid.origin.x += tile.expanded.x * outputGrid.pixelSize[0];
        tileGrid.origin.y += tile.expanded.y * outputGrid.pixelSize[1];
        const MultiCameraOrthophotoResult tileResult =
            orthorectifySingleTile(sources, terrain, tileGrid, tileOptions);
        result.seamLabelChangeCount += tileResult.seamLabelChangeCount;
        result.seamOptimizationIterations = std::max(
            result.seamOptimizationIterations,
            tileResult.seamOptimizationIterations);
        if (!tileResult.success) {
            if (tileResult.selectedContributionCount == 0) {
                const cv::Rect sourceCore = tile.coreInExpanded();
                if (!tileResult.projectedCandidateCountMap.empty()) {
                    tileResult.projectedCandidateCountMap(sourceCore).copyTo(
                        result.projectedCandidateCountMap(tile.core));
                    tileResult.occludedCandidateCountMap(sourceCore).copyTo(
                        result.occludedCandidateCountMap(tile.core));
                }
                continue;
            }
            result.message = "Orthophoto tile " + std::to_string(tile.index)
                + " failed: " + tileResult.message;
            return result;
        }
        const cv::Rect sourceCore = tile.coreInExpanded();
        tileResult.orthophoto(sourceCore).copyTo(
            result.orthophoto(tile.core));
        tileResult.validityMask(sourceCore).copyTo(
            result.validityMask(tile.core));
        tileResult.primarySourceImageIndex(sourceCore).copyTo(
            result.primarySourceImageIndex(tile.core));
        tileResult.sourceCount(sourceCore).copyTo(
            result.sourceCount(tile.core));
        tileResult.projectedCandidateCountMap(sourceCore).copyTo(
            result.projectedCandidateCountMap(tile.core));
        tileResult.occludedCandidateCountMap(sourceCore).copyTo(
            result.occludedCandidateCountMap(tile.core));
        tileResult.accumulatedQuality(sourceCore).copyTo(
            result.accumulatedQuality(tile.core));
        if (!tileResult.seamMask.empty()) {
            tileResult.seamMask(sourceCore).copyTo(
                result.seamMask(tile.core));
        }
        result.usedSeamBlending = result.usedSeamBlending
            || tileResult.usedSeamBlending;
    }

    result.processedTileCount = static_cast<int>(tiles.size());
    result.tileHaloPixels = halo;
    result.seamPixelCount = static_cast<size_t>(
        cv::countNonZero(result.seamMask));
    for (int row = 0; row < result.sourceCount.rows; ++row) {
        const uchar *counts = result.sourceCount.ptr<uchar>(row);
        const ushort *projected =
            result.projectedCandidateCountMap.ptr<ushort>(row);
        const ushort *occluded =
            result.occludedCandidateCountMap.ptr<ushort>(row);
        for (int column = 0; column < result.sourceCount.cols; ++column) {
            result.selectedContributionCount += counts[column];
            result.projectedCandidateCount += projected[column];
            result.occludedCandidateCount += occluded[column];
        }
    }
    result.success = cv::countNonZero(result.validityMask) > 0;
    result.message = result.success
        ? "Multi-camera terrain orthophoto produced overlap-halo tiled, seam-optimized multiband coverage."
        : "No orthophoto tile had usable terrain coverage.";
    return result;
}

} // namespace kestrel
