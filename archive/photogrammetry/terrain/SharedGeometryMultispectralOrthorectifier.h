#ifndef SHAREDGEOMETRYMULTISPECTRALORTHORECTIFIER_H
#define SHAREDGEOMETRYMULTISPECTRALORTHORECTIFIER_H

#include "photogrammetry/terrain/MultiCameraOrthorectifier.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace kestrel {

struct SpectralOrthophotoBand
{
    std::string name;
    double centreWavelengthNanometres = 0.0;
    cv::Mat image; // One channel; type must match every other band.
    PinholeCamera camera;
    cv::Mat validityMask; // Optional CV_8UC1 in sensor coordinates.
};

struct MultispectralOrthophotoSource
{
    int imageIndex = -1;
    std::vector<SpectralOrthophotoBand> bands;
    cv::Mat referenceConfidence; // Optional CV_32FC1.
    double exposureQuality = 1.0;
};

struct SharedGeometryMultispectralOptions
{
    size_t referenceBandIndex = 0;
    MultiCameraOrthophotoOptions geometryOptions;
    int bandInterpolation = cv::INTER_LANCZOS4;
    int multibandLevels = 5;
};

struct SharedGeometryMultispectralResult
{
    bool success = false;
    std::string message;
    OrthophotoGrid outputGrid;
    cv::Mat orthophoto; // Interleaved channels in bandNames order.
    cv::Mat validityMask;
    cv::Mat primarySourceImageIndex;
    std::vector<std::string> bandNames;
    std::vector<double> centreWavelengthsNanometres;
    MultiCameraOrthophotoResult referenceGeometry;
    size_t unavailableSourceFallbackCount = 0;
    int processedTileCount = 0;
    int tileHaloPixels = 0;
};

class SharedGeometryMultispectralOrthorectifier
{
public:
    // Chooses physical-capture ownership once from a reference band, projects
    // every sensor independently through the common terrain/output grid, and
    // blends all ordered channels using those shared labels.
    static SharedGeometryMultispectralResult orthorectify(
        const std::vector<MultispectralOrthophotoSource> &sources,
        const TerrainGrid &terrain,
        const OrthophotoGrid &outputGrid,
        const SharedGeometryMultispectralOptions &options = {});
};

} // namespace kestrel

#endif // SHAREDGEOMETRYMULTISPECTRALORTHORECTIFIER_H
