#ifndef SHAREDGEOMETRYRGBORTHORECTIFIER_H
#define SHAREDGEOMETRYRGBORTHORECTIFIER_H

#include "photogrammetry/terrain/MultiCameraOrthorectifier.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace kestrel {

// The three images are independent one-channel sensors belonging to one
// physical capture. Their cameras may have different intrinsics, distortion,
// and rig-relative extrinsics. Output channel order follows OpenCV: B, G, R.
struct RgbOrthophotoSource
{
    int imageIndex = -1;
    cv::Mat blue;
    cv::Mat green;
    cv::Mat red;
    PinholeCamera blueCamera;
    PinholeCamera greenCamera;
    PinholeCamera redCamera;
    cv::Mat blueValidity; // Optional CV_8UC1 source-sensor validity.
    cv::Mat greenValidity;
    cv::Mat redValidity;
    cv::Mat greenConfidence;
    double exposureQuality = 1.0;
};

struct SharedGeometryRgbOptions
{
    MultiCameraOrthophotoOptions geometryOptions;
    int bandInterpolation = cv::INTER_LANCZOS4;
    int multibandLevels = 5;
};

struct SharedGeometryRgbResult
{
    bool success = false;
    std::string message;
    OrthophotoGrid outputGrid;
    cv::Mat bgrOrthophoto;
    cv::Mat validityMask; // All B/G/R sensors are valid for the chosen capture.
    cv::Mat primarySourceImageIndex; // Shared physical-capture ownership.
    MultiCameraOrthophotoResult referenceGeometry;
    size_t unavailableSourceFallbackCount = 0;
    int processedTileCount = 0;
    int tileHaloPixels = 0;
};

class SharedGeometryRgbOrthorectifier
{
public:
    // Selects source ownership once from the green reference sensors, projects
    // every physical B/G/R sensor through the same DSM/output grid, then uses
    // the reference labels for a single colour-consistent multiband blend.
    static SharedGeometryRgbResult orthorectify(
        const std::vector<RgbOrthophotoSource> &sources,
        const TerrainGrid &terrain,
        const OrthophotoGrid &outputGrid,
        const SharedGeometryRgbOptions &options = {});
};

} // namespace kestrel

#endif // SHAREDGEOMETRYRGBORTHORECTIFIER_H
