#include "photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.h"

#include "photogrammetry/terrain/OrthophotoSeamBlender.h"
#include "photogrammetry/tiling/TileLayout.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace kestrel {

namespace {

bool validBand(const SpectralOrthophotoBand &band, int expectedType)
{
    return !band.name.empty() && std::isfinite(band.centreWavelengthNanometres)
           && band.centreWavelengthNanometres > 0.0
           && !band.image.empty() && band.image.channels() == 1
           && band.image.type() == expectedType
           && (band.validityMask.empty()
               || (band.validityMask.type() == CV_8UC1
                   && band.validityMask.size() == band.image.size()));
}

bool validate(const std::vector<MultispectralOrthophotoSource> &sources,
              const OrthophotoGrid &outputGrid,
              const SharedGeometryMultispectralOptions &options,
              std::string *message)
{
    if (sources.empty() || sources.front().bands.empty()) {
        *message = "Shared-geometry multispectral output requires complete captures.";
        return false;
    }
    const size_t bandCount = sources.front().bands.size();
    if (bandCount > CV_CN_MAX || options.referenceBandIndex >= bandCount
        || outputGrid.size.width <= 0 || outputGrid.size.height <= 0
        || options.multibandLevels <= 0 || options.multibandLevels > 16) {
        *message = "Multispectral band count, reference band, grid, or options are invalid.";
        return false;
    }
    const int expectedType = sources.front().bands.front().image.type();
    std::set<int> imageIndices;
    for (const MultispectralOrthophotoSource &source : sources) {
        if (source.imageIndex < 0 || source.bands.size() != bandCount
            || !std::isfinite(source.exposureQuality)
            || source.exposureQuality < 0.0
            || !imageIndices.insert(source.imageIndex).second) {
            *message = "Multispectral captures require unique indices and identical band layouts.";
            return false;
        }
        for (size_t index = 0; index < bandCount; ++index) {
            const SpectralOrthophotoBand &band = source.bands[index];
            const SpectralOrthophotoBand &expected =
                sources.front().bands[index];
            if (!validBand(band, expectedType) || band.name != expected.name
                || std::abs(band.centreWavelengthNanometres
                            - expected.centreWavelengthNanometres) > 1e-6) {
                *message = "Multispectral bands must have matching names, wavelengths, and one-channel types.";
                return false;
            }
        }
    }
    return true;
}

int fixedBlendHalo(int levels)
{
    return 4 * (1 << (std::clamp(levels, 1, 16) - 1));
}

std::vector<ProcessingTile> processingTiles(
    const OrthophotoGrid &grid,
    const SharedGeometryMultispectralOptions &options,
    int *halo, std::string *message)
{
    const bool tiled = options.geometryOptions.tileSize.width > 0
                       || options.geometryOptions.tileSize.height > 0;
    if (!tiled) {
        *halo = 0;
        ProcessingTile tile;
        tile.index = 0;
        tile.core = cv::Rect(0, 0, grid.size.width, grid.size.height);
        tile.expanded = tile.core;
        return {tile};
    }
    *halo = std::max(
        fixedBlendHalo(options.multibandLevels),
        options.geometryOptions.tileHaloPixels > 0
            ? options.geometryOptions.tileHaloPixels
            : MultiCameraOrthorectifier::recommendedTileHaloPixels(
                  options.geometryOptions));
    std::vector<ProcessingTile> tiles;
    if (!TileLayout::build(grid.size, options.geometryOptions.tileSize,
                           *halo, &tiles, message)) {
        return {};
    }
    const int alignment = 1 << (options.multibandLevels - 1);
    const cv::Rect canvas(0, 0, grid.size.width, grid.size.height);
    for (ProcessingTile &tile : tiles) {
        const int left = tile.expanded.x / alignment * alignment;
        const int top = tile.expanded.y / alignment * alignment;
        const int right = std::min(
            grid.size.width,
            ((tile.expanded.x + tile.expanded.width + alignment - 1)
             / alignment) * alignment);
        const int bottom = std::min(
            grid.size.height,
            ((tile.expanded.y + tile.expanded.height + alignment - 1)
             / alignment) * alignment);
        tile.expanded = cv::Rect(left, top, right - left, bottom - top)
                        & canvas;
    }
    return tiles;
}

bool warpBand(const SpectralOrthophotoBand &band,
              const TerrainGrid &terrain, const OrthophotoGrid &grid,
              int interpolation, cv::Mat *warped, cv::Mat *validity,
              std::string *message)
{
    if (!TerrainOrthorectifier::orthorectify(
            band.image, band.camera, terrain, grid, warped, validity,
            message, interpolation)) {
        return false;
    }
    if (band.validityMask.empty()) {
        return true;
    }
    cv::Mat warpedSensorValidity;
    cv::Mat projectedSensorValidity;
    if (!TerrainOrthorectifier::orthorectify(
            band.validityMask, band.camera, terrain, grid,
            &warpedSensorValidity, &projectedSensorValidity, message,
            cv::INTER_NEAREST)) {
        return false;
    }
    cv::bitwise_and(*validity, projectedSensorValidity, *validity);
    cv::bitwise_and(*validity, warpedSensorValidity, *validity);
    return true;
}

bool warpLayer(const MultispectralOrthophotoSource &source,
               const TerrainGrid &terrain, const OrthophotoGrid &grid,
               int interpolation, OrthophotoBlendLayer *layer,
               std::string *message)
{
    std::vector<cv::Mat> warpedBands;
    warpedBands.reserve(source.bands.size());
    cv::Mat completeValidity;
    for (const SpectralOrthophotoBand &band : source.bands) {
        cv::Mat warped;
        cv::Mat validity;
        if (!warpBand(band, terrain, grid, interpolation,
                      &warped, &validity, message)) {
            return false;
        }
        if (completeValidity.empty()) {
            completeValidity = validity;
        } else {
            cv::bitwise_and(completeValidity, validity, completeValidity);
        }
        warpedBands.push_back(std::move(warped));
    }
    cv::merge(warpedBands, layer->image);
    completeValidity.convertTo(
        layer->quality, CV_32F, source.exposureQuality / 255.0);
    layer->imageIndex = source.imageIndex;
    return true;
}

} // namespace

SharedGeometryMultispectralResult
SharedGeometryMultispectralOrthorectifier::orthorectify(
    const std::vector<MultispectralOrthophotoSource> &sources,
    const TerrainGrid &terrain, const OrthophotoGrid &outputGrid,
    const SharedGeometryMultispectralOptions &options)
{
    SharedGeometryMultispectralResult result;
    result.outputGrid = outputGrid;
    if (!validate(sources, outputGrid, options, &result.message)) {
        return result;
    }
    for (const SpectralOrthophotoBand &band : sources.front().bands) {
        result.bandNames.push_back(band.name);
        result.centreWavelengthsNanometres.push_back(
            band.centreWavelengthNanometres);
    }

    std::vector<OrthophotoSource> referenceSources;
    referenceSources.reserve(sources.size());
    for (const MultispectralOrthophotoSource &source : sources) {
        const SpectralOrthophotoBand &referenceBand =
            source.bands[options.referenceBandIndex];
        cv::Mat confidence = source.referenceConfidence;
        if (!referenceBand.validityMask.empty()) {
            cv::Mat radiometricConfidence;
            referenceBand.validityMask.convertTo(
                radiometricConfidence, CV_32F, 1.0 / 255.0);
            confidence = confidence.empty()
                ? radiometricConfidence : confidence.mul(radiometricConfidence);
        }
        referenceSources.push_back({source.imageIndex, referenceBand.image,
                                    referenceBand.camera, confidence,
                                    source.exposureQuality});
    }
    result.referenceGeometry = MultiCameraOrthorectifier::orthorectify(
        referenceSources, terrain, outputGrid, options.geometryOptions);
    if (!result.referenceGeometry.success) {
        result.message = "Reference-band geometry failed: "
                         + result.referenceGeometry.message;
        return result;
    }

    int halo = 0;
    std::vector<ProcessingTile> tiles = processingTiles(
        outputGrid, options, &halo, &result.message);
    if (tiles.empty()) return result;
    result.tileHaloPixels = halo;
    result.processedTileCount = static_cast<int>(tiles.size());
    result.orthophoto = cv::Mat::zeros(
        outputGrid.size,
        CV_MAKETYPE(sources.front().bands.front().image.depth(),
                    static_cast<int>(sources.front().bands.size())));
    result.validityMask = cv::Mat::zeros(outputGrid.size, CV_8U);
    result.primarySourceImageIndex = cv::Mat(
        outputGrid.size, CV_32S, cv::Scalar(-1));

    for (const ProcessingTile &tile : tiles) {
        OrthophotoGrid tileGrid = outputGrid;
        tileGrid.size = tile.expanded.size();
        tileGrid.origin.x += tile.expanded.x * outputGrid.pixelSize[0];
        tileGrid.origin.y += tile.expanded.y * outputGrid.pixelSize[1];
        std::vector<OrthophotoBlendLayer> layers;
        layers.reserve(sources.size());
        for (const MultispectralOrthophotoSource &source : sources) {
            OrthophotoBlendLayer layer;
            if (!warpLayer(source, terrain, tileGrid,
                           options.bandInterpolation, &layer,
                           &result.message)) {
                return result;
            }
            layers.push_back(std::move(layer));
        }
        const cv::Mat referenceLabels =
            result.referenceGeometry.primarySourceImageIndex(tile.expanded);
        if (cv::countNonZero(referenceLabels >= 0) == 0) continue;
        const OrthophotoSeamBlendResult blended =
            OrthophotoSeamBlender::blendWithPrimarySources(
                layers, referenceLabels, options.multibandLevels);
        if (!blended.success) {
            if (blended.message
                == "No fixed source label has usable layer coverage.") {
                continue;
            }
            result.message = "Multispectral tile "
                + std::to_string(tile.index) + " failed: " + blended.message;
            return result;
        }
        const cv::Rect sourceCore = tile.coreInExpanded();
        blended.image(sourceCore).copyTo(result.orthophoto(tile.core));
        blended.validityMask(sourceCore).copyTo(result.validityMask(tile.core));
        blended.primarySourceImageIndex(sourceCore).copyTo(
            result.primarySourceImageIndex(tile.core));
        if (blended.unavailableSourceFallbackCount > 0) {
            const cv::Mat fallbackCore =
                blended.primarySourceImageIndex(sourceCore)
                != result.referenceGeometry.primarySourceImageIndex(tile.core);
            result.unavailableSourceFallbackCount += static_cast<size_t>(
                cv::countNonZero(
                    fallbackCore & (blended.validityMask(sourceCore) > 0)));
        }
    }
    result.success = cv::countNonZero(result.validityMask) > 0;
    result.message = result.success
        ? "All spectral sensors terrain-warped with shared reference-band capture labels."
        : "No output pixel has complete multispectral sensor coverage.";
    return result;
}

} // namespace kestrel

