#ifndef GEOCOORDINATES_H
#define GEOCOORDINATES_H

#include <opencv2/core.hpp>

#include <string>

namespace kestrel {

struct GeoCoordinate
{
    double latitudeDegrees = 0.0;
    double longitudeDegrees = 0.0;
    double altitudeMetres = 0.0;

    bool isValid() const;
};

// Converts WGS-84 latitude/longitude/ellipsoid altitude to a local Cartesian
// East/North/Up frame. Keeping reconstruction coordinates close to the origin
// avoids the precision loss caused by optimizing directly in Earth-centred
// coordinates.
class LocalTangentPlane
{
public:
    LocalTangentPlane() = default;
    explicit LocalTangentPlane(const GeoCoordinate &origin);

    bool isValid() const;
    const GeoCoordinate &origin() const;

    bool toEnu(const GeoCoordinate &coordinate,
               cv::Point3d *eastNorthUp,
               std::string *errorMessage = nullptr) const;
    bool fromEnu(const cv::Point3d &eastNorthUp,
                 GeoCoordinate *coordinate,
                 std::string *errorMessage = nullptr) const;

private:
    GeoCoordinate m_origin;
    cv::Point3d m_originEcef;
    double m_sinLatitude = 0.0;
    double m_cosLatitude = 1.0;
    double m_sinLongitude = 0.0;
    double m_cosLongitude = 1.0;
    bool m_valid = false;
};

} // namespace kestrel

#endif // GEOCOORDINATES_H
