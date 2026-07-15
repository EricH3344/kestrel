#include "photogrammetry/terrain/TerrainModelBuilder.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool rasterizationTest()
{
    const std::vector<kestrel::TerrainPoint> points{
        {{100.0, 200.0, 4.0}},
        {{100.0, 200.0, 7.5}},
        {{102.0, 200.0, 5.0}},
        {{100.0, 197.0, 3.0}},
        {{1000.0, 1000.0, 99.0}},
        {{std::numeric_limits<double>::quiet_NaN(), 200.0, 12.0}}};
    kestrel::DsmGridDefinition definition;
    definition.size = cv::Size(3, 2);
    definition.origin = cv::Point2d(100.0, 200.0);
    definition.spacing = cv::Vec2d(2.0, -3.0);

    kestrel::TerrainGrid terrain;
    cv::Mat sampleCounts;
    std::string error;
    const bool success = kestrel::TerrainModelBuilder::rasterizeDsm(
        points, definition, &terrain, &sampleCounts, &error);
    return expect(success, error.c_str())
           && expect(terrain.elevation.at<float>(0, 0) == 7.5f,
                     "A DSM cell should retain its highest surface sample.")
           && expect(sampleCounts.at<int>(0, 0) == 2,
                     "The DSM should retain per-cell sample counts.")
           && expect(terrain.elevation.at<float>(0, 1) == 5.0f,
                     "Positive X spacing should select the expected column.")
           && expect(terrain.elevation.at<float>(1, 0) == 3.0f,
                     "Negative Y spacing should select the expected row.")
           && expect(terrain.validityMask.at<uchar>(0, 2) == 0
                         && std::isnan(terrain.elevation.at<float>(0, 2)),
                     "Cells without points must stay explicitly invalid.");
}

bool minimumSupportTest()
{
    const std::vector<kestrel::TerrainPoint> points{
        {{0.0, 0.0, 1.0}}, {{1.0, 0.0, 2.0}}, {{1.0, 0.0, 2.5}}};
    kestrel::DsmGridDefinition definition;
    definition.size = cv::Size(2, 1);
    definition.origin = cv::Point2d(0.0, 0.0);
    definition.spacing = cv::Vec2d(1.0, -1.0);
    definition.minimumSamplesPerCell = 2;

    kestrel::TerrainGrid terrain;
    cv::Mat counts;
    const bool success = kestrel::TerrainModelBuilder::rasterizeDsm(
        points, definition, &terrain, &counts);
    return expect(success, "Minimum-support DSM should rasterize.")
           && expect(terrain.validityMask.at<uchar>(0, 0) == 0,
                     "An undersampled cell must be invalid.")
           && expect(terrain.validityMask.at<uchar>(0, 1) == 255,
                     "A sufficiently sampled cell must be valid.");
}

bool boundedHoleFillTest()
{
    kestrel::TerrainGrid terrain;
    terrain.elevation = cv::Mat(5, 5, CV_32F);
    terrain.validityMask = cv::Mat(5, 5, CV_8U, cv::Scalar(255));
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            terrain.elevation.at<float>(row, column) =
                static_cast<float>(column + 2 * row);
        }
    }
    terrain.elevation.at<float>(2, 2) = std::numeric_limits<float>::quiet_NaN();
    terrain.validityMask.at<uchar>(2, 2) = 0;
    terrain.elevation.at<float>(0, 4) = std::numeric_limits<float>::quiet_NaN();
    terrain.validityMask.at<uchar>(0, 4) = 0;

    cv::Mat filledMask;
    const bool success = kestrel::TerrainModelBuilder::fillBoundedHoles(
        &terrain, 4, &filledMask);
    return expect(success, "Bounded DSM holes should fill.")
           && expect(terrain.validityMask.at<uchar>(2, 2) == 255
                         && std::abs(terrain.elevation.at<float>(2, 2) - 6.0f) < 1e-4,
                     "An enclosed hole should interpolate from its boundary.")
           && expect(terrain.validityMask.at<uchar>(0, 4) == 0,
                     "A border-connected gap must remain invalid.")
           && expect(cv::countNonZero(filledMask) == 1,
                     "The synthesized-cell mask should identify only filled terrain.");
}

} // namespace

int main()
{
    const bool success = rasterizationTest() && minimumSupportTest()
                         && boundedHoleFillTest();
    if (success) {
        std::cout << "Dense-point DSM rasterization tests passed.\n";
        return 0;
    }
    return 1;
}
