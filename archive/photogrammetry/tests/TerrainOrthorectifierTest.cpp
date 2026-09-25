#include "photogrammetry/terrain/TerrainOrthorectifier.h"

#include <opencv2/core.hpp>

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

kestrel::PinholeCamera testCamera()
{
    kestrel::PinholeCamera camera;
    camera.intrinsic = cv::Matx33d(50.0, 0.0, 49.5,
                                   0.0, 50.0, 49.5,
                                   0.0, 0.0, 1.0);
    camera.worldToCameraTranslation = cv::Vec3d(0.0, 0.0, 10.0);
    return camera;
}

cv::Mat horizontalCoordinateImage()
{
    cv::Mat image(100, 100, CV_32F);
    for (int row = 0; row < image.rows; ++row) {
        float *values = image.ptr<float>(row);
        for (int column = 0; column < image.cols; ++column) {
            values[column] = static_cast<float>(column);
        }
    }
    return image;
}

bool flatTerrainTest()
{
    kestrel::TerrainGrid terrain;
    terrain.elevation = cv::Mat::zeros(21, 21, CV_32F);
    terrain.origin = cv::Point2d(-2.0, -2.0);
    terrain.spacing = cv::Vec2d(0.2, 0.2);

    kestrel::OrthophotoGrid grid;
    grid.size = cv::Size(20, 20);
    grid.origin = cv::Point2d(-1.9, -1.9);
    grid.pixelSize = cv::Vec2d(0.2, 0.2);

    cv::Mat orthophoto;
    cv::Mat mask;
    std::string error;
    const bool success = kestrel::TerrainOrthorectifier::orthorectify(
        horizontalCoordinateImage(), testCamera(), terrain, grid,
        &orthophoto, &mask, &error, cv::INTER_LINEAR);
    return expect(success, error.c_str())
           && expect(cv::countNonZero(mask) == grid.size.area(),
                     "Flat terrain should project every output pixel.")
           && expect(std::abs(orthophoto.at<float>(10, 0) - 40.0f) < 1e-4,
                     "Flat projection should map the first column exactly.")
           && expect(std::abs(orthophoto.at<float>(10, 19) - 59.0f) < 1e-4,
                     "Flat projection should map the last column exactly.");
}

bool slopedTerrainTest()
{
    kestrel::TerrainGrid terrain;
    terrain.elevation = cv::Mat(21, 21, CV_32F);
    terrain.origin = cv::Point2d(-2.0, -2.0);
    terrain.spacing = cv::Vec2d(0.2, 0.2);
    for (int row = 0; row < terrain.elevation.rows; ++row) {
        float *heights = terrain.elevation.ptr<float>(row);
        for (int column = 0; column < terrain.elevation.cols; ++column) {
            const double worldX = terrain.origin.x + column * terrain.spacing[0];
            heights[column] = static_cast<float>(0.5 * worldX);
        }
    }

    kestrel::OrthophotoGrid grid;
    grid.size = cv::Size(1, 1);
    grid.origin = cv::Point2d(1.0, 0.0);
    grid.pixelSize = cv::Vec2d(0.2, 0.2);

    cv::Mat orthophoto;
    cv::Mat mask;
    std::string error;
    const bool success = kestrel::TerrainOrthorectifier::orthorectify(
        horizontalCoordinateImage(), testCamera(), terrain, grid,
        &orthophoto, &mask, &error, cv::INTER_LINEAR);
    const double expectedSourceX = 49.5 + 50.0 / 10.5;
    return expect(success, error.c_str())
           && expect(mask.at<uchar>(0, 0) == 255,
                     "Sloped terrain projection should be valid.")
           && expect(std::abs(orthophoto.at<float>(0, 0) - expectedSourceX) < 0.05,
                     "DSM elevation must change the projected source coordinate.");
}

bool missingTerrainTest()
{
    kestrel::TerrainGrid terrain;
    terrain.elevation = cv::Mat::zeros(2, 2, CV_32F);
    terrain.elevation.at<float>(0, 0) = std::numeric_limits<float>::quiet_NaN();
    terrain.origin = cv::Point2d(0.0, 0.0);

    kestrel::OrthophotoGrid grid;
    grid.size = cv::Size(1, 1);
    grid.origin = cv::Point2d(0.0, 0.0);

    cv::Mat orthophoto;
    cv::Mat mask;
    std::string error;
    const bool success = kestrel::TerrainOrthorectifier::orthorectify(
        horizontalCoordinateImage(), testCamera(), terrain, grid,
        &orthophoto, &mask, &error, cv::INTER_LINEAR);
    return expect(success, error.c_str())
           && expect(mask.at<uchar>(0, 0) == 0,
                     "Missing DSM cells must remain invalid in the output mask.");
}

bool outputGridTest()
{
    kestrel::TerrainGrid terrain;
    terrain.elevation = cv::Mat::zeros(2, 3, CV_32F);
    terrain.origin = cv::Point2d(100.0, 200.0);
    terrain.spacing = cv::Vec2d(2.0, -3.0);

    kestrel::OrthophotoGrid grid;
    std::string error;
    const bool success = kestrel::TerrainOrthorectifier::createOutputGrid(
        terrain, 1.0, 1.0, &grid, &error);
    return expect(success, error.c_str())
           && expect(grid.size == cv::Size(7, 6),
                     "Derived grid should cover the padded terrain bounds.")
           && expect(grid.origin == cv::Point2d(99.0, 201.0),
                     "Derived grid should begin at the north-west bound.")
           && expect(grid.pixelSize == cv::Vec2d(1.0, -1.0),
                     "Derived grid should be north-up at the requested GSD.");
}

bool explicitValidityMaskTest()
{
    kestrel::TerrainGrid terrain;
    terrain.elevation = cv::Mat::zeros(2, 2, CV_32F);
    terrain.validityMask = cv::Mat(2, 2, CV_8U, cv::Scalar(255));
    terrain.validityMask.at<uchar>(1, 1) = 0;
    terrain.origin = cv::Point2d(0.0, 0.0);

    double height = 0.0;
    return expect(terrain.sample(0.0, 0.0, &height),
                  "An exact valid cell must not require zero-weight neighbours.")
           && expect(!terrain.sample(0.5, 0.5, &height),
                     "Interpolation must reject a footprint containing an invalid DSM cell.");
}

} // namespace

int main()
{
    const bool success = flatTerrainTest() && slopedTerrainTest()
                         && missingTerrainTest() && outputGridTest()
                         && explicitValidityMaskTest();
    if (success) {
        std::cout << "Terrain orthorectification synthetic tests passed.\n";
        return 0;
    }
    return 1;
}
