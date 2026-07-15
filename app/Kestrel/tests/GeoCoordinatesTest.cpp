#include "photogrammetry/geo/GeoCoordinates.h"
#include "photogrammetry/geo/ProjectedCoordinates.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool equatorScaleTest()
{
    const kestrel::LocalTangentPlane frame({0.0, 0.0, 0.0});
    cv::Point3d east;
    cv::Point3d north;
    return expect(frame.toEnu({0.0, 0.00001, 0.0}, &east),
                  "Equatorial east coordinate should convert.")
           && expect(frame.toEnu({0.00001, 0.0, 0.0}, &north),
                     "Equatorial north coordinate should convert.")
           && expect(std::abs(east.x - 1.1131949) < 1e-5,
                     "Longitude scale should match WGS-84 at the equator.")
           && expect(std::abs(north.y - 1.1057428) < 1e-5,
                     "Latitude scale should match WGS-84 at the equator.");
}

bool torontoRoundTripTest()
{
    const kestrel::GeoCoordinate origin{43.6532, -79.3832, 112.0};
    const kestrel::GeoCoordinate input{43.6541, -79.3817, 146.5};
    const kestrel::LocalTangentPlane frame(origin);
    cv::Point3d enu;
    kestrel::GeoCoordinate output;
    return expect(frame.toEnu(input, &enu), "Toronto coordinate should convert to ENU.")
           && expect(enu.x > 100.0 && enu.y > 90.0 && enu.z > 30.0,
                     "Toronto ENU coordinate should have the expected direction.")
           && expect(frame.fromEnu(enu, &output), "ENU should convert back to WGS-84.")
           && expect(std::abs(output.latitudeDegrees - input.latitudeDegrees) < 1e-9,
                     "Latitude should survive an ENU round trip.")
           && expect(std::abs(output.longitudeDegrees - input.longitudeDegrees) < 1e-9,
                     "Longitude should survive an ENU round trip.")
           && expect(std::abs(output.altitudeMetres - input.altitudeMetres) < 1e-5,
                     "Altitude should survive an ENU round trip.");
}

bool dateLineTest()
{
    const kestrel::LocalTangentPlane frame({45.0, 179.999, 10.0});
    cv::Point3d enu;
    return expect(frame.toEnu({45.0, -179.999, 10.0}, &enu),
                  "Coordinates across the date line should convert.")
           && expect(enu.x > 150.0 && enu.x < 160.0,
                     "The date line must not create a world-sized displacement.");
}

bool invalidCoordinateTest()
{
    const kestrel::LocalTangentPlane frame({43.0, -79.0, 0.0});
    cv::Point3d enu;
    std::string error;
    const bool converted = frame.toEnu(
        {91.0, -79.0, std::numeric_limits<double>::quiet_NaN()}, &enu, &error);
    return expect(!converted, "Invalid WGS-84 input must be rejected.")
           && expect(!error.empty(), "Invalid input should return a useful error.");
}

bool utmProjectionTest()
{
    const kestrel::GeoCoordinate centralMeridian{0.0, -81.0, 0.0};
    const kestrel::Wgs84UtmProjection projection(centralMeridian);
    kestrel::UtmCoordinate coordinate;
    if (!expect(projection.project(centralMeridian, &coordinate),
                "A UTM central-meridian coordinate should project.")) {
        return false;
    }
    return expect(projection.zone() == 17 && projection.epsgCode() == 32617,
                  "Longitude -81 at the equator should select EPSG:32617.")
           && expect(std::abs(coordinate.eastingMetres - 500000.0) < 1e-7
                         && std::abs(coordinate.northingMetres) < 1e-7,
                     "The northern UTM central meridian should use its standard false easting.")
           && expect(kestrel::Wgs84UtmProjection(
                         {-45.0, 147.0, 0.0}).epsgCode() == 32755,
                     "Southern coordinates should select the WGS 84 southern UTM EPSG range.");
}

bool projectedRasterTransformTest()
{
    const kestrel::LocalTangentPlane frame({0.0, -81.0, 0.0});
    kestrel::ProjectedRasterTransform transform;
    std::string error;
    const bool created = kestrel::createProjectedRasterTransform(
        frame, {0.0, 0.0}, {1.0, -1.0}, {100, 100},
        &transform, &error);
    return expect(created, error.c_str())
           && expect(transform.epsgCode == 32617,
                     "The raster should retain its projected EPSG code.")
           && expect(transform.rasterToProjected(0, 0) > 0.999
                         && transform.rasterToProjected(0, 0) < 1.001
                         && transform.rasterToProjected(1, 1) < -0.999
                         && transform.rasterToProjected(1, 1) > -1.001,
                     "One-metre ENU pixels should remain approximately one metre in UTM.")
           && expect(transform.rasterToProjected(0, 3) < 500000.0
                         && transform.rasterToProjected(1, 3) > 0.0,
                     "PixelIsArea corner georeferencing should offset half a pixel northwest.")
           && expect(transform.localLinearizationErrorMetres < 0.001,
                     "A 100-metre field should have negligible affine UTM linearization error.");
}

} // namespace

int main()
{
    const bool success = equatorScaleTest() && torontoRoundTripTest()
                         && dateLineTest() && invalidCoordinateTest()
                         && utmProjectionTest()
                         && projectedRasterTransformTest();
    if (success) {
        std::cout << "WGS-84/ENU coordinate tests passed.\n";
        return 0;
    }
    return 1;
}
