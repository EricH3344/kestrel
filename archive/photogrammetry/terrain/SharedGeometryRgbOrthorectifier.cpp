#include "photogrammetry/terrain/SharedGeometryRgbOrthorectifier.h"

#include "photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.h"

#include <utility>

namespace kestrel {

SharedGeometryRgbResult SharedGeometryRgbOrthorectifier::orthorectify(
    const std::vector<RgbOrthophotoSource> &sources,
    const TerrainGrid &terrain,
    const OrthophotoGrid &outputGrid,
    const SharedGeometryRgbOptions &options)
{
    std::vector<MultispectralOrthophotoSource> multispectralSources;
    multispectralSources.reserve(sources.size());
    for (const RgbOrthophotoSource &source : sources) {
        MultispectralOrthophotoSource converted;
        converted.imageIndex = source.imageIndex;
        converted.bands = {
            {"Blue", 475.0, source.blue, source.blueCamera,
             source.blueValidity},
            {"Green", 560.0, source.green, source.greenCamera,
             source.greenValidity},
            {"Red", 668.0, source.red, source.redCamera,
             source.redValidity}};
        converted.referenceConfidence = source.greenConfidence;
        converted.exposureQuality = source.exposureQuality;
        multispectralSources.push_back(std::move(converted));
    }
    SharedGeometryMultispectralOptions multispectralOptions;
    multispectralOptions.referenceBandIndex = 1;
    multispectralOptions.geometryOptions = options.geometryOptions;
    multispectralOptions.bandInterpolation = options.bandInterpolation;
    multispectralOptions.multibandLevels = options.multibandLevels;
    SharedGeometryMultispectralResult multispectral =
        SharedGeometryMultispectralOrthorectifier::orthorectify(
            multispectralSources, terrain, outputGrid, multispectralOptions);

    SharedGeometryRgbResult result;
    result.success = multispectral.success;
    result.message = multispectral.message;
    result.outputGrid = multispectral.outputGrid;
    result.bgrOrthophoto = multispectral.orthophoto;
    result.validityMask = multispectral.validityMask;
    result.primarySourceImageIndex = multispectral.primarySourceImageIndex;
    result.referenceGeometry = std::move(multispectral.referenceGeometry);
    result.unavailableSourceFallbackCount =
        multispectral.unavailableSourceFallbackCount;
    result.processedTileCount = multispectral.processedTileCount;
    result.tileHaloPixels = multispectral.tileHaloPixels;
    return result;
}

} // namespace kestrel
