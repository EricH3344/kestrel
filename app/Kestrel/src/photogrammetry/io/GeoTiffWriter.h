#ifndef GEOTIFFWRITER_H
#define GEOTIFFWRITER_H

#include "photogrammetry/geo/ProjectedCoordinates.h"

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace kestrel {

struct GeoTiffWriteOptions
{
    bool useDeflateCompression = true;
    bool writeValidityAsAlpha = true;
    bool threeChannelInputIsBgr = true;
    std::string noDataValue = "0";
    std::vector<std::string> bandNames;
    std::vector<double> centreWavelengthsNanometres;
};

class GeoTiffWriter
{
public:
    // Writes ordered OpenCV raster bands plus an optional alpha mask.
    // GeoTIFF 1.1 ModelTransformation and projected-CRS keys are embedded;
    // libtiff is only the container/codec boundary.
    static bool write(const std::filesystem::path &path,
                      const cv::Mat &image,
                      const cv::Mat &validityMask,
                      const ProjectedRasterTransform &reference,
                      const GeoTiffWriteOptions &options = {},
                      std::string *errorMessage = nullptr);
};

} // namespace kestrel

#endif // GEOTIFFWRITER_H
