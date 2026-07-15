#include "photogrammetry/terrain/MultiCameraOrthorectifier.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

kestrel::PinholeCamera downwardCamera(double x, double y, double altitude)
{
    kestrel::PinholeCamera camera;
    camera.intrinsic = cv::Matx33d(40.0, 0.0, 49.5,
                                   0.0, 40.0, 49.5,
                                   0.0, 0.0, 1.0);
    camera.worldToCameraRotation = cv::Matx33d(
        1.0, 0.0, 0.0,
        0.0, -1.0, 0.0,
        0.0, 0.0, -1.0);
    camera.worldToCameraTranslation = {-x, y, altitude};
    return camera;
}

kestrel::TerrainGrid flatTerrain()
{
    kestrel::TerrainGrid terrain;
    terrain.elevation = cv::Mat::zeros(41, 61, CV_32F);
    terrain.validityMask = cv::Mat(41, 61, CV_8U, cv::Scalar(255));
    terrain.origin = {-1.0, -2.0};
    terrain.spacing = {0.1, 0.1};
    return terrain;
}

kestrel::OrthophotoSource source(int imageIndex, float value,
                                 const kestrel::PinholeCamera &camera,
                                 double exposureQuality = 1.0)
{
    kestrel::OrthophotoSource result;
    result.imageIndex = imageIndex;
    result.image = cv::Mat(100, 100, CV_32F, cv::Scalar(value)).clone();
    result.camera = camera;
    result.exposureQuality = exposureQuality;
    return result;
}

kestrel::OrthophotoGrid onePixelGrid(double x = 0.0, double y = 0.0)
{
    kestrel::OrthophotoGrid grid;
    grid.size = {1, 1};
    grid.origin = {x, y};
    grid.pixelSize = {0.1, -0.1};
    return grid;
}

kestrel::MultiCameraOrthophotoOptions options()
{
    kestrel::MultiCameraOrthophotoOptions value;
    value.minimumViewCosine = 0.1;
    value.borderMarginPixels = 10.0;
    value.occlusionClearanceMetres = 0.02;
    value.occlusionSampleSpacingMetres = 0.05;
    value.maximumSourcesPerPixel = 3;
    value.interpolation = cv::INTER_LINEAR;
    value.useSeamBlending = false;
    return value;
}

bool symmetricBlendTest()
{
    const auto terrain = flatTerrain();
    const std::vector<kestrel::OrthophotoSource> sources{
        source(10, 50.0f, downwardCamera(-1.0, 0.0, 10.0)),
        source(20, 150.0f, downwardCamera(1.0, 0.0, 10.0))};
    const auto result = kestrel::MultiCameraOrthorectifier::orthorectify(
        sources, terrain, onePixelGrid(), options());
    return expect(result.success && result.validityMask.at<uchar>(0, 0) == 255,
                  "Symmetric cameras should cover the terrain point.")
           && expect(result.sourceCount.at<uchar>(0, 0) == 2,
                     "Both equally useful cameras should contribute.")
           && expect(result.primarySourceImageIndex.at<int>(0, 0) == 10,
                     "Equal-score source selection should be deterministic.")
           && expect(std::abs(result.orthophoto.at<float>(0, 0) - 100.0f)
                         < 1e-4,
                     "Equal camera scores should produce an even blend.");
}

bool qualityWeightTest()
{
    const auto terrain = flatTerrain();
    const std::vector<kestrel::OrthophotoSource> sources{
        source(10, 50.0f, downwardCamera(-1.0, 0.0, 10.0)),
        source(20, 150.0f, downwardCamera(1.0, 0.0, 10.0), 2.0)};
    const auto result = kestrel::MultiCameraOrthorectifier::orthorectify(
        sources, terrain, onePixelGrid(), options());
    const double expected = (50.0 + 2.0 * 150.0) / 3.0;
    return expect(result.success
                      && result.primarySourceImageIndex.at<int>(0, 0) == 20,
                  "The higher-quality source should become primary.")
           && expect(std::abs(result.orthophoto.at<float>(0, 0) - expected)
                         < 1e-3,
                     "Exposure quality should weight the blended value.");
}

bool occlusionTest()
{
    auto terrain = flatTerrain();
    // A narrow ridge crosses the ray from (0, 0, 0) to camera (2, 0, 2).
    const int ridgeColumn = cvRound((1.0 - terrain.origin.x)
                                    / terrain.spacing[0]);
    for (int offset = -1; offset <= 1; ++offset) {
        terrain.elevation.col(ridgeColumn + offset).setTo(1.5f);
    }
    const std::vector<kestrel::OrthophotoSource> sources{
        source(10, 50.0f, downwardCamera(2.0, 0.0, 2.0)),
        source(20, 200.0f, downwardCamera(0.0, 0.0, 2.0))};
    auto testOptions = options();
    testOptions.maximumSourcesPerPixel = 2;
    const auto result = kestrel::MultiCameraOrthorectifier::orthorectify(
        sources, terrain, onePixelGrid(), testOptions);
    return expect(result.success && result.occludedCandidateCount >= 1,
                  "The DSM ridge should occlude the oblique source.")
           && expect(result.sourceCount.at<uchar>(0, 0) == 1
                         && result.primarySourceImageIndex.at<int>(0, 0) == 20,
                     "Only the unobstructed camera should be selected.")
           && expect(std::abs(result.orthophoto.at<float>(0, 0) - 200.0f)
                         < 1e-4,
                     "An occluded source must not contaminate the output.");
}

bool multiChannelTest()
{
    kestrel::OrthophotoSource rgb;
    rgb.imageIndex = 7;
    rgb.image = cv::Mat(100, 100, CV_8UC3,
                        cv::Scalar(20, 80, 140)).clone();
    rgb.camera = downwardCamera(0.0, 0.0, 5.0);
    auto testOptions = options();
    testOptions.maximumSourcesPerPixel = 1;
    const auto result = kestrel::MultiCameraOrthorectifier::orthorectify(
        {rgb}, flatTerrain(), onePixelGrid(), testOptions);
    const cv::Vec3b value = result.success
        ? result.orthophoto.at<cv::Vec3b>(0, 0) : cv::Vec3b();
    if (result.success && value != cv::Vec3b(20, 80, 140)) {
        std::cerr << "RGB output: " << static_cast<int>(value[0]) << ", "
                  << static_cast<int>(value[1]) << ", "
                  << static_cast<int>(value[2]) << '\n';
    }
    return expect(result.success && result.orthophoto.type() == CV_8UC3,
                  "Multi-camera output should preserve source channel type.")
           && expect(value == cv::Vec3b(20, 80, 140),
                     "Float accumulation should preserve constant RGB values.");
}

bool seamIntegrationTest()
{
    auto first = source(10, 50.0f, downwardCamera(0.0, 0.0, 10.0));
    auto second = source(20, 150.0f, downwardCamera(0.0, 0.0, 10.0));
    first.confidence = cv::Mat(100, 100, CV_32F);
    second.confidence = cv::Mat(100, 100, CV_32F);
    for (int row = 0; row < 100; ++row) {
        for (int column = 0; column < 100; ++column) {
            first.confidence.at<float>(row, column) = std::clamp(
                static_cast<float>((65.0 - column) / 20.0), 0.05f, 1.0f);
            second.confidence.at<float>(row, column) = std::clamp(
                static_cast<float>((column - 45.0) / 20.0), 0.05f, 1.0f);
        }
    }
    kestrel::OrthophotoGrid grid;
    grid.size = {61, 11};
    grid.origin = {-1.0, 0.0};
    grid.pixelSize = {0.1, -0.1};
    auto testOptions = options();
    testOptions.useSeamBlending = true;
    testOptions.seamBlendOptions.smoothnessCost = 0.20;
    testOptions.seamBlendOptions.multibandLevels = 4;
    const auto result = kestrel::MultiCameraOrthorectifier::orthorectify(
        {first, second}, flatTerrain(), grid, testOptions);
    auto tiledOptions = testOptions;
    tiledOptions.tileSize = {24, 8};
    const auto tiled = kestrel::MultiCameraOrthorectifier::orthorectify(
        {first, second}, flatTerrain(), grid, tiledOptions);
    const int middleRow = grid.size.height / 2;
    const float left = result.success
        ? result.orthophoto.at<float>(middleRow, 0) : 0.0f;
    const float right = result.success
        ? result.orthophoto.at<float>(middleRow, grid.size.width - 1) : 0.0f;
    return expect(result.success && result.usedSeamBlending,
                  "The multi-camera path should invoke seam blending.")
           && expect(result.seamPixelCount > 0,
                     "Crossing source quality should produce a seam.")
           && expect(result.primarySourceImageIndex.at<int>(middleRow, 0) == 10
                         && result.primarySourceImageIndex.at<int>(
                                middleRow, grid.size.width - 1) == 20,
                     "The seam should retain the strongest source on each side.")
           && expect(left < 80.0f && right > 120.0f,
                     "Multiband blending should preserve distant source interiors.")
           && expect(tiled.success && tiled.processedTileCount == 6
                         && tiled.tileHaloPixels
                                == kestrel::MultiCameraOrthorectifier::
                                    recommendedTileHaloPixels(tiledOptions),
                     "Orthophoto tiling should use deterministic cores and an automatic halo.")
           && expect(cv::countNonZero(
                         result.primarySourceImageIndex
                         != tiled.primarySourceImageIndex) == 0,
                     "Overlap halos should preserve seam labels across tile boundaries.")
           && expect([&]() {
                         const double difference = cv::norm(
                             result.orthophoto, tiled.orthophoto,
                             cv::NORM_INF);
                         if (difference >= 1e-3) {
                             std::cerr << "Tiled seam blend maximum difference: "
                                       << difference << '\n';
                         }
                         return difference < 1e-3;
                     }(),
                     "Overlap-halo multiband output should match untiled output.");
}

bool tiledWeightedEquivalenceTest()
{
    const std::vector<kestrel::OrthophotoSource> sources{
        source(10, 50.0f, downwardCamera(-1.0, 0.0, 10.0)),
        source(20, 150.0f, downwardCamera(1.0, 0.0, 10.0), 1.5)};
    kestrel::OrthophotoGrid grid;
    grid.size = {53, 17};
    grid.origin = {-1.0, 0.0};
    grid.pixelSize = {0.1, -0.1};
    const auto untiled = kestrel::MultiCameraOrthorectifier::orthorectify(
        sources, flatTerrain(), grid, options());
    auto tiledOptions = options();
    tiledOptions.tileSize = {17, 7};
    const auto tiled = kestrel::MultiCameraOrthorectifier::orthorectify(
        sources, flatTerrain(), grid, tiledOptions);
    return expect(untiled.success && tiled.success,
                  "Weighted orthophotos should succeed with and without tiling.")
           && expect(untiled.outputGrid.size == grid.size
                         && untiled.outputGrid.origin == grid.origin
                         && untiled.outputGrid.pixelSize == grid.pixelSize
                         && tiled.outputGrid.size == grid.size
                         && tiled.outputGrid.origin == grid.origin
                         && tiled.outputGrid.pixelSize == grid.pixelSize,
                     "Orthophoto results must retain their exact metric output grid.")
           && expect(tiled.processedTileCount == 12,
                     "Non-divisible output sizes should create clipped edge tiles.")
           && expect(cv::countNonZero(
                         untiled.validityMask != tiled.validityMask) == 0
                         && cv::countNonZero(
                             untiled.primarySourceImageIndex
                             != tiled.primarySourceImageIndex) == 0,
                     "Tiling should preserve weighted coverage and source ownership.")
           && expect(cv::norm(untiled.orthophoto, tiled.orthophoto,
                              cv::NORM_INF) < 1e-5,
                     "Tiled weighted pixels should equal untiled pixels.");
}

bool validationTest()
{
    const auto terrain = flatTerrain();
    auto invalidOptions = options();
    invalidOptions.maximumSourcesPerPixel = 0;
    const auto invalid = kestrel::MultiCameraOrthorectifier::orthorectify(
        {source(0, 1.0f, downwardCamera(0.0, 0.0, 2.0))},
        terrain, onePixelGrid(), invalidOptions);
    const auto empty = kestrel::MultiCameraOrthorectifier::orthorectify(
        {}, terrain, onePixelGrid(), options());
    auto invalidTiling = options();
    invalidTiling.useSeamBlending = true;
    invalidTiling.tileSize = {8, 8};
    invalidTiling.tileHaloPixels = 1;
    const auto invalidTile =
        kestrel::MultiCameraOrthorectifier::orthorectify(
            {source(0, 1.0f, downwardCamera(0.0, 0.0, 2.0))},
            terrain, onePixelGrid(), invalidTiling);
    return expect(!invalid.success && !empty.success && !invalidTile.success,
                  "Invalid options and missing sources should fail safely.");
}

} // namespace

int main()
{
    if (symmetricBlendTest() && qualityWeightTest() && occlusionTest()
        && multiChannelTest() && seamIntegrationTest()
        && tiledWeightedEquivalenceTest() && validationTest()) {
        std::cout << "Multi-camera orthorectification tests passed.\n";
        return 0;
    }
    return 1;
}
