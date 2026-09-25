#include "photogrammetry/geo/GeoCoordinates.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr double kWgs84SemiMajorAxis = 6378137.0;
constexpr double kWgs84Flattening = 1.0 / 298.257223563;
constexpr double kWgs84EccentricitySquared =
    kWgs84Flattening * (2.0 - kWgs84Flattening);
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;

void setError(std::string *errorMessage, const std::string &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

cv::Point3d geodeticToEcef(const GeoCoordinate &coordinate)
{
    const double latitude = coordinate.latitudeDegrees * kDegreesToRadians;
    const double longitude = coordinate.longitudeDegrees * kDegreesToRadians;
    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double primeVerticalRadius = kWgs84SemiMajorAxis
        / std::sqrt(1.0 - kWgs84EccentricitySquared
                              * sinLatitude * sinLatitude);

    return cv::Point3d(
        (primeVerticalRadius + coordinate.altitudeMetres) * cosLatitude
            * std::cos(longitude),
        (primeVerticalRadius + coordinate.altitudeMetres) * cosLatitude
            * std::sin(longitude),
        (primeVerticalRadius * (1.0 - kWgs84EccentricitySquared)
             + coordinate.altitudeMetres)
            * sinLatitude);
}

GeoCoordinate ecefToGeodetic(const cv::Point3d &ecef)
{
    const double longitude = std::atan2(ecef.y, ecef.x);
    const double horizontalDistance = std::hypot(ecef.x, ecef.y);
    double latitude = std::atan2(
        ecef.z, horizontalDistance * (1.0 - kWgs84EccentricitySquared));
    double altitude = 0.0;

    // The fixed-point latitude update converges rapidly for points near the
    // Earth surface. Eight iterations are ample for sub-millimetre round trips.
    for (int iteration = 0; iteration < 8; ++iteration) {
        const double sinLatitude = std::sin(latitude);
        const double primeVerticalRadius = kWgs84SemiMajorAxis
            / std::sqrt(1.0 - kWgs84EccentricitySquared
                                  * sinLatitude * sinLatitude);
        const double cosLatitude = std::cos(latitude);
        if (std::abs(cosLatitude) > 1e-12) {
            altitude = horizontalDistance / cosLatitude - primeVerticalRadius;
        } else {
            altitude = std::abs(ecef.z)
                       - primeVerticalRadius
                             * (1.0 - kWgs84EccentricitySquared);
        }
        latitude = std::atan2(
            ecef.z,
            horizontalDistance
                * (1.0 - kWgs84EccentricitySquared * primeVerticalRadius
                             / (primeVerticalRadius + altitude)));
    }

    return {latitude * kRadiansToDegrees,
            longitude * kRadiansToDegrees,
            altitude};
}

} // namespace

bool GeoCoordinate::isValid() const
{
    return std::isfinite(latitudeDegrees)
           && std::isfinite(longitudeDegrees)
           && std::isfinite(altitudeMetres)
           && latitudeDegrees >= -90.0 && latitudeDegrees <= 90.0
           && longitudeDegrees >= -180.0 && longitudeDegrees <= 180.0;
}

LocalTangentPlane::LocalTangentPlane(const GeoCoordinate &origin)
    : m_origin(origin)
{
    if (!origin.isValid()) {
        return;
    }
    const double latitude = origin.latitudeDegrees * kDegreesToRadians;
    const double longitude = origin.longitudeDegrees * kDegreesToRadians;
    m_sinLatitude = std::sin(latitude);
    m_cosLatitude = std::cos(latitude);
    m_sinLongitude = std::sin(longitude);
    m_cosLongitude = std::cos(longitude);
    m_originEcef = geodeticToEcef(origin);
    m_valid = true;
}

bool LocalTangentPlane::isValid() const
{
    return m_valid;
}

const GeoCoordinate &LocalTangentPlane::origin() const
{
    return m_origin;
}

bool LocalTangentPlane::toEnu(const GeoCoordinate &coordinate,
                              cv::Point3d *eastNorthUp,
                              std::string *errorMessage) const
{
    if (!eastNorthUp) {
        setError(errorMessage, "An ENU output coordinate is required.");
        return false;
    }
    if (!m_valid || !coordinate.isValid()) {
        setError(errorMessage, "The ENU origin or input WGS-84 coordinate is invalid.");
        return false;
    }

    const cv::Point3d ecef = geodeticToEcef(coordinate);
    const cv::Point3d delta = ecef - m_originEcef;
    eastNorthUp->x = -m_sinLongitude * delta.x
                     + m_cosLongitude * delta.y;
    eastNorthUp->y = -m_sinLatitude * m_cosLongitude * delta.x
                     - m_sinLatitude * m_sinLongitude * delta.y
                     + m_cosLatitude * delta.z;
    eastNorthUp->z = m_cosLatitude * m_cosLongitude * delta.x
                     + m_cosLatitude * m_sinLongitude * delta.y
                     + m_sinLatitude * delta.z;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool LocalTangentPlane::fromEnu(const cv::Point3d &eastNorthUp,
                                GeoCoordinate *coordinate,
                                std::string *errorMessage) const
{
    if (!coordinate) {
        setError(errorMessage, "A WGS-84 output coordinate is required.");
        return false;
    }
    if (!m_valid || !std::isfinite(eastNorthUp.x)
        || !std::isfinite(eastNorthUp.y) || !std::isfinite(eastNorthUp.z)) {
        setError(errorMessage, "The ENU origin or input coordinate is invalid.");
        return false;
    }

    const cv::Point3d delta(
        -m_sinLongitude * eastNorthUp.x
            - m_sinLatitude * m_cosLongitude * eastNorthUp.y
            + m_cosLatitude * m_cosLongitude * eastNorthUp.z,
        m_cosLongitude * eastNorthUp.x
            - m_sinLatitude * m_sinLongitude * eastNorthUp.y
            + m_cosLatitude * m_sinLongitude * eastNorthUp.z,
        m_cosLatitude * eastNorthUp.y + m_sinLatitude * eastNorthUp.z);
    *coordinate = ecefToGeodetic(m_originEcef + delta);
    if (!coordinate->isValid()) {
        setError(errorMessage, "The ENU coordinate could not be converted to WGS-84.");
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace kestrel
