#ifndef MULTICAMERAORTHORECTIFIER_H
#define MULTICAMERAORTHORECTIFIER_H

#include "photogrammetry/terrain/TerrainOrthorectifier.h"
#include "photogrammetry/terrain/OrthophotoSeamBlender.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace kestrel {

struct OrthophotoSource
{
    int imageIndex = -1;
    cv::Mat image;
    PinholeCamera camera;
    cv::Mat confidence; // Optional CV_32FC1 in source-image coordinates.
    double exposureQuality = 1.0; // Non-negative relative quality weight.
};

struct MultiCameraOrthophotoOptions
{
    double minimumViewCosine = 0.20;
    double viewAngleExponent = 2.0;
    double borderMarginPixels = 24.0;
    double borderExponent = 1.0;
    double occlusionClearanceMetres = 0.15;
    double occlusionSampleSpacingMetres = 0.0; // Zero derives it from DSM GSD.
    int maximumOcclusionSamples = 512;
    bool rejectUnknownTerrainAlongRay = true;
    int maximumSourcesPerPixel = 3;
    double minimumSourceScore = 1e-9;
    int interpolation = cv::INTER_LANCZOS4;
    bool useSeamBlending = true;
    OrthophotoSeamBlendOptions seamBlendOptions;
    cv::Size tileSize; // Empty disables tiling.
    int tileHaloPixels = 0; // Zero derives halo from seam/blend support.
};

struct MultiCameraOrthophotoResult
{
    bool success = false;
    std::string message;
    // Retained with the pixels so downstream GeoTIFF/export stages cannot
    // accidentally lose or reconstruct the raster's metric geometry.
    OrthophotoGrid outputGrid;
    cv::Mat orthophoto;
    cv::Mat validityMask; // CV_8UC1.
    cv::Mat primarySourceImageIndex; // CV_32SC1; -1 means none.
    cv::Mat sourceCount; // CV_8UC1.
    cv::Mat projectedCandidateCountMap; // CV_16UC1.
    cv::Mat occludedCandidateCountMap; // CV_16UC1.
    cv::Mat accumulatedQuality; // CV_32FC1.
    cv::Mat seamMask; // CV_8UC1; source-label boundaries are 255.
    size_t projectedCandidateCount = 0;
    size_t occludedCandidateCount = 0;
    size_t selectedContributionCount = 0;
    bool usedSeamBlending = false;
    int seamOptimizationIterations = 0;
    size_t seamLabelChangeCount = 0;
    size_t seamPixelCount = 0;
    int processedTileCount = 0;
    int tileHaloPixels = 0;
};

class MultiCameraOrthorectifier
{
public:
    static int recommendedTileHaloPixels(
        const MultiCameraOrthophotoOptions &options = {});

    // Scores every visible source by terrain-normal view angle, projected
    // resolution, border distance, exposure quality, and optional confidence.
    // DSM ray tests reject occluded candidates before the strongest sources
    // are remapped, assigned coherent seams, and blended across frequency
    // bands. Weighted float blending remains available as a diagnostic
    // fallback through MultiCameraOrthophotoOptions.
    static MultiCameraOrthophotoResult orthorectify(
        const std::vector<OrthophotoSource> &sources,
        const TerrainGrid &terrain,
        const OrthophotoGrid &outputGrid,
        const MultiCameraOrthophotoOptions &options = {});
};

} // namespace kestrel

#endif // MULTICAMERAORTHORECTIFIER_H
