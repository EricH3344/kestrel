#ifndef PROJECTEDCOORDINATES_H
#define PROJECTEDCOORDINATES_H

#include "photogrammetry/geo/GeoCoordinates.h"

#include <opencv2/core.hpp>

#include <string>

namespace kestrel {

struct UtmCoordinate
{
    double eastingMetres = 0.0;
    double northingMetres = 0.0;
    int zone = 0;
    bool northernHemisphere = true;
    int epsgCode = 0;
};

// WGS 84 / UTM projection implemented in Kestrel. The selected zone is fixed
// at construction so every point in one project uses one projected CRS.
class Wgs84UtmProjection
{
public:
    Wgs84UtmProjection() = default;
    explicit Wgs84UtmProjection(const GeoCoordinate &reference);

    bool isValid() const;
    int zone() const;
    bool northernHemisphere() const;
    int epsgCode() const;
    std::string name() const;

    bool project(const GeoCoordinate &coordinate,
                 UtmCoordinate *projected,
                 std::string *errorMessage = nullptr) const;

private:
    int m_zone = 0;
    bool m_northernHemisphere = true;
};

struct ProjectedRasterTransform
{
    int epsgCode = 0;
    std::string crsName;
    // Maps raster corner coordinates (column, row, z, 1) into projected
    // easting/northing metres. Pixel (0,0)'s centre is at (0.5,0.5).
    cv::Matx44d rasterToProjected = cv::Matx44d::eye();
    double localLinearizationErrorMetres = 0.0;

    bool isValid() const;
};

// Derives an affine ENU-to-UTM transform at the flight origin and composes it
// with a north-up local raster grid. The reported corner error measures UTM
// projection curvature over the requested local extent.
bool createProjectedRasterTransform(
    const LocalTangentPlane &localFrame,
    const cv::Point2d &pixelZeroCentreEnu,
    const cv::Vec2d &pixelSizeEnu,
    const cv::Size &rasterSize,
    ProjectedRasterTransform *transform,
    std::string *errorMessage = nullptr);

} // namespace kestrel

#endif // PROJECTEDCOORDINATES_H
