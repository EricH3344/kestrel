#include "photogrammetry/geo/ProjectedCoordinates.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kestrel {

namespace {

constexpr double kSemiMajorAxis = 6378137.0;
constexpr double kFlattening = 1.0 / 298.257223563;
constexpr double kEccentricitySquared =
    kFlattening * (2.0 - kFlattening);
constexpr double kSecondEccentricitySquared =
    kEccentricitySquared / (1.0 - kEccentricitySquared);
constexpr double kScaleFactor = 0.9996;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

void setError(std::string *errorMessage, const std::string &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

int zoneFor(const GeoCoordinate &coordinate)
{
    int zone = static_cast<int>(std::floor(
        (coordinate.longitudeDegrees + 180.0) / 6.0)) + 1;
    zone = std::clamp(zone, 1, 60);
    // Standard UTM exceptions keep Norway and Svalbard within coherent zones.
    if (coordinate.latitudeDegrees >= 56.0
        && coordinate.latitudeDegrees < 64.0
        && coordinate.longitudeDegrees >= 3.0
        && coordinate.longitudeDegrees < 12.0) {
        zone = 32;
    }
    if (coordinate.latitudeDegrees >= 72.0
        && coordinate.latitudeDegrees < 84.0) {
        if (coordinate.longitudeDegrees >= 0.0
            && coordinate.longitudeDegrees < 9.0) zone = 31;
        else if (coordinate.longitudeDegrees < 21.0) zone = 33;
        else if (coordinate.longitudeDegrees < 33.0) zone = 35;
        else if (coordinate.longitudeDegrees < 42.0) zone = 37;
    }
    return zone;
}

bool finiteMatrix(const cv::Matx44d &matrix)
{
    for (double value : matrix.val) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

Wgs84UtmProjection::Wgs84UtmProjection(const GeoCoordinate &reference)
{
    // UTM is defined from 80 degrees south to 84 degrees north.
    if (!reference.isValid() || reference.latitudeDegrees < -80.0
        || reference.latitudeDegrees > 84.0) {
        return;
    }
    m_zone = zoneFor(reference);
    m_northernHemisphere = reference.latitudeDegrees >= 0.0;
}

bool Wgs84UtmProjection::isValid() const
{
    return m_zone >= 1 && m_zone <= 60;
}

int Wgs84UtmProjection::zone() const { return m_zone; }
bool Wgs84UtmProjection::northernHemisphere() const
{
    return m_northernHemisphere;
}

int Wgs84UtmProjection::epsgCode() const
{
    return isValid() ? (m_northernHemisphere ? 32600 : 32700) + m_zone : 0;
}

std::string Wgs84UtmProjection::name() const
{
    return isValid() ? "WGS 84 / UTM zone " + std::to_string(m_zone)
        + (m_northernHemisphere ? "N" : "S") : std::string();
}

bool Wgs84UtmProjection::project(const GeoCoordinate &coordinate,
                                 UtmCoordinate *projected,
                                 std::string *errorMessage) const
{
    if (!projected || !isValid() || !coordinate.isValid()
        || coordinate.latitudeDegrees < -80.0
        || coordinate.latitudeDegrees > 84.0) {
        setError(errorMessage, "UTM projection input or fixed zone is invalid.");
        return false;
    }
    const double latitude = coordinate.latitudeDegrees * kDegreesToRadians;
    const double longitude = coordinate.longitudeDegrees * kDegreesToRadians;
    const double centralMeridian =
        (m_zone * 6.0 - 183.0) * kDegreesToRadians;
    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double tanLatitude = std::tan(latitude);
    const double n = kSemiMajorAxis / std::sqrt(
        1.0 - kEccentricitySquared * sinLatitude * sinLatitude);
    const double t = tanLatitude * tanLatitude;
    const double c = kSecondEccentricitySquared
        * cosLatitude * cosLatitude;
    const double a = cosLatitude * (longitude - centralMeridian);
    const double e4 = kEccentricitySquared * kEccentricitySquared;
    const double e6 = e4 * kEccentricitySquared;
    const double meridionalArc = kSemiMajorAxis * (
        (1.0 - kEccentricitySquared / 4.0 - 3.0 * e4 / 64.0
         - 5.0 * e6 / 256.0) * latitude
        - (3.0 * kEccentricitySquared / 8.0 + 3.0 * e4 / 32.0
           + 45.0 * e6 / 1024.0) * std::sin(2.0 * latitude)
        + (15.0 * e4 / 256.0 + 45.0 * e6 / 1024.0)
            * std::sin(4.0 * latitude)
        - (35.0 * e6 / 3072.0) * std::sin(6.0 * latitude));
    const double a2 = a * a;
    const double a3 = a2 * a;
    const double a4 = a2 * a2;
    const double a5 = a4 * a;
    const double a6 = a3 * a3;
    projected->eastingMetres = 500000.0 + kScaleFactor * n * (
        a + (1.0 - t + c) * a3 / 6.0
        + (5.0 - 18.0 * t + t * t + 72.0 * c
           - 58.0 * kSecondEccentricitySquared) * a5 / 120.0);
    projected->northingMetres = kScaleFactor * (
        meridionalArc + n * tanLatitude * (
            a2 / 2.0 + (5.0 - t + 9.0 * c + 4.0 * c * c)
                * a4 / 24.0
            + (61.0 - 58.0 * t + t * t + 600.0 * c
               - 330.0 * kSecondEccentricitySquared) * a6 / 720.0));
    if (!m_northernHemisphere) {
        projected->northingMetres += 10000000.0;
    }
    projected->zone = m_zone;
    projected->northernHemisphere = m_northernHemisphere;
    projected->epsgCode = epsgCode();
    if (!std::isfinite(projected->eastingMetres)
        || !std::isfinite(projected->northingMetres)) {
        setError(errorMessage, "UTM projection produced non-finite coordinates.");
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

bool ProjectedRasterTransform::isValid() const
{
    return epsgCode > 0 && !crsName.empty()
        && finiteMatrix(rasterToProjected)
        && std::isfinite(localLinearizationErrorMetres)
        && localLinearizationErrorMetres >= 0.0;
}

bool createProjectedRasterTransform(
    const LocalTangentPlane &localFrame,
    const cv::Point2d &pixelZeroCentreEnu,
    const cv::Vec2d &pixelSizeEnu,
    const cv::Size &rasterSize,
    ProjectedRasterTransform *transform,
    std::string *errorMessage)
{
    if (!transform || !localFrame.isValid()
        || rasterSize.width <= 0 || rasterSize.height <= 0
        || !std::isfinite(pixelZeroCentreEnu.x)
        || !std::isfinite(pixelZeroCentreEnu.y)
        || !std::isfinite(pixelSizeEnu[0])
        || !std::isfinite(pixelSizeEnu[1])
        || std::abs(pixelSizeEnu[0]) < 1e-12
        || std::abs(pixelSizeEnu[1]) < 1e-12) {
        setError(errorMessage, "Projected raster geometry or ENU frame is invalid.");
        return false;
    }
    const Wgs84UtmProjection projection(localFrame.origin());
    if (!projection.isValid()) {
        setError(errorMessage, "The ENU origin is outside supported UTM latitude bounds.");
        return false;
    }
    const auto projectEnu = [&](double east, double north,
                                cv::Point2d *point) {
        GeoCoordinate coordinate;
        UtmCoordinate utm;
        if (!localFrame.fromEnu({east, north, 0.0}, &coordinate)
            || !projection.project(coordinate, &utm)) {
            return false;
        }
        *point = {utm.eastingMetres, utm.northingMetres};
        return true;
    };
    cv::Point2d origin;
    cv::Point2d eastMinus, eastPlus, northMinus, northPlus;
    if (!projectEnu(0.0, 0.0, &origin)
        || !projectEnu(-1.0, 0.0, &eastMinus)
        || !projectEnu(1.0, 0.0, &eastPlus)
        || !projectEnu(0.0, -1.0, &northMinus)
        || !projectEnu(0.0, 1.0, &northPlus)) {
        setError(errorMessage, "Could not linearize ENU coordinates into UTM.");
        return false;
    }
    const cv::Point2d eastAxis = 0.5 * (eastPlus - eastMinus);
    const cv::Point2d northAxis = 0.5 * (northPlus - northMinus);
    const double cornerEast = pixelZeroCentreEnu.x - 0.5 * pixelSizeEnu[0];
    const double cornerNorth = pixelZeroCentreEnu.y - 0.5 * pixelSizeEnu[1];
    const cv::Point2d projectedCorner = origin
        + eastAxis * cornerEast + northAxis * cornerNorth;
    cv::Matx44d matrix = cv::Matx44d::eye();
    matrix(0, 0) = eastAxis.x * pixelSizeEnu[0];
    matrix(0, 1) = northAxis.x * pixelSizeEnu[1];
    matrix(0, 3) = projectedCorner.x;
    matrix(1, 0) = eastAxis.y * pixelSizeEnu[0];
    matrix(1, 1) = northAxis.y * pixelSizeEnu[1];
    matrix(1, 3) = projectedCorner.y;

    double maximumError = 0.0;
    const cv::Point2d localCorners[] = {
        {cornerEast, cornerNorth},
        {cornerEast + rasterSize.width * pixelSizeEnu[0], cornerNorth},
        {cornerEast, cornerNorth + rasterSize.height * pixelSizeEnu[1]},
        {cornerEast + rasterSize.width * pixelSizeEnu[0],
         cornerNorth + rasterSize.height * pixelSizeEnu[1]}};
    for (const cv::Point2d &local : localCorners) {
        cv::Point2d exact;
        if (!projectEnu(local.x, local.y, &exact)) {
            setError(errorMessage, "Could not validate ENU-to-UTM linearization.");
            return false;
        }
        const cv::Point2d affine = origin
            + eastAxis * local.x + northAxis * local.y;
        maximumError = std::max(maximumError, cv::norm(exact - affine));
    }
    transform->epsgCode = projection.epsgCode();
    transform->crsName = projection.name();
    transform->rasterToProjected = matrix;
    transform->localLinearizationErrorMetres = maximumError;
    if (errorMessage) errorMessage->clear();
    return true;
}

} // namespace kestrel
