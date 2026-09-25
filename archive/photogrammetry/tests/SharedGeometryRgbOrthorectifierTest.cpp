#include "photogrammetry/terrain/SharedGeometryRgbOrthorectifier.h"
#include "photogrammetry/terrain/SharedGeometryMultispectralOrthorectifier.h"

#include <opencv2/core.hpp>

#include <iostream>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

kestrel::PinholeCamera camera(double principalX = 49.5)
{
    kestrel::PinholeCamera value;
    value.intrinsic = cv::Matx33d(40.0, 0.0, principalX,
                                  0.0, 40.0, 49.5,
                                  0.0, 0.0, 1.0);
    value.worldToCameraRotation = cv::Matx33d(
        1.0, 0.0, 0.0,
        0.0, -1.0, 0.0,
        0.0, 0.0, -1.0);
    value.worldToCameraTranslation = {0.0, 0.0, 10.0};
    return value;
}

kestrel::TerrainGrid terrain()
{
    kestrel::TerrainGrid value;
    value.elevation = cv::Mat::zeros(21, 61, CV_32F);
    value.validityMask = cv::Mat(21, 61, CV_8U, cv::Scalar(255));
    value.origin = {-3.0, -1.0};
    value.spacing = {0.1, 0.1};
    return value;
}

cv::Mat horizontalRamp(int offset)
{
    cv::Mat result(100, 100, CV_16U);
    for (int row = 0; row < result.rows; ++row) {
        for (int column = 0; column < result.cols; ++column) {
            result.at<ushort>(row, column) = static_cast<ushort>(
                offset + column);
        }
    }
    return result;
}

kestrel::RgbOrthophotoSource source(int imageIndex, int offset)
{
    kestrel::RgbOrthophotoSource result;
    result.imageIndex = imageIndex;
    result.blue = horizontalRamp(offset);
    result.green = horizontalRamp(offset + 1000);
    result.red = horizontalRamp(offset + 2000);
    result.blueCamera = camera(39.5);
    result.greenCamera = camera(49.5);
    result.redCamera = camera(59.5);
    return result;
}

kestrel::OrthophotoGrid grid(int width = 1)
{
    kestrel::OrthophotoGrid result;
    result.size = {width, 5};
    result.origin = {-2.0, 0.2};
    result.pixelSize = {0.1, -0.1};
    return result;
}

kestrel::SharedGeometryRgbOptions options()
{
    kestrel::SharedGeometryRgbOptions result;
    result.geometryOptions.minimumViewCosine = 0.1;
    result.geometryOptions.borderMarginPixels = 1.0;
    result.geometryOptions.occlusionClearanceMetres = 0.02;
    result.geometryOptions.occlusionSampleSpacingMetres = 0.1;
    result.geometryOptions.maximumSourcesPerPixel = 2;
    result.geometryOptions.interpolation = cv::INTER_NEAREST;
    result.geometryOptions.useSeamBlending = true;
    result.geometryOptions.seamBlendOptions.multibandLevels = 2;
    result.bandInterpolation = cv::INTER_NEAREST;
    result.multibandLevels = 2;
    return result;
}

bool independentSensorProjectionTest()
{
    const auto result =
        kestrel::SharedGeometryRgbOrthorectifier::orthorectify(
            {source(7, 0)}, terrain(), grid(), options());
    const cv::Vec<ushort, 3> pixel = result.success
        ? result.bgrOrthophoto.at<cv::Vec<ushort, 3>>(2, 0)
        : cv::Vec<ushort, 3>();
    return expect(result.success && result.bgrOrthophoto.type() == CV_16UC3,
                  "Shared RGB should preserve the original 16-bit depth.")
           && expect(pixel == cv::Vec<ushort, 3>(32, 1042, 2052),
                     "Each colour must use its independent calibrated sensor camera.")
           && expect(result.primarySourceImageIndex.at<int>(2, 0) == 7,
                     "RGB output should retain physical-capture ownership.");
}

bool sharedLabelsAndTilingTest()
{
    auto first = source(10, 0);
    auto second = source(20, 100);
    first.greenConfidence = cv::Mat(100, 100, CV_32F);
    second.greenConfidence = cv::Mat(100, 100, CV_32F);
    for (int row = 0; row < 100; ++row) {
        for (int column = 0; column < 100; ++column) {
            first.greenConfidence.at<float>(row, column) =
                column < 50 ? 1.0f : 0.1f;
            second.greenConfidence.at<float>(row, column) =
                column < 50 ? 0.1f : 1.0f;
        }
    }
    const auto outputGrid = grid(41);
    auto untiledOptions = options();
    const auto untiled =
        kestrel::SharedGeometryRgbOrthorectifier::orthorectify(
            {first, second}, terrain(), outputGrid, untiledOptions);
    auto tiledOptions = untiledOptions;
    tiledOptions.geometryOptions.tileSize = {17, 3};
    const auto tiled =
        kestrel::SharedGeometryRgbOrthorectifier::orthorectify(
            {first, second}, terrain(), outputGrid, tiledOptions);
    return expect(untiled.success && tiled.success,
                  "Shared RGB should work with and without tiling.")
           && expect(cv::countNonZero(
                         untiled.referenceGeometry.primarySourceImageIndex
                         != untiled.primarySourceImageIndex) == 0,
                     "Every colour channel must reuse reference-band labels.")
           && expect(cv::countNonZero(
                         untiled.primarySourceImageIndex
                         != tiled.primarySourceImageIndex) == 0,
                     "Tiling must preserve shared capture ownership.")
           && expect(cv::norm(untiled.bgrOrthophoto, tiled.bgrOrthophoto,
                              cv::NORM_INF) == 0.0,
                     "Overlap-halo RGB tiles should equal untiled output.");
}

bool radiometricValidityFallbackTest()
{
    auto first = source(10, 0);
    auto second = source(20, 100);
    first.redValidity = cv::Mat::zeros(first.red.size(), CV_8U);
    const auto result =
        kestrel::SharedGeometryRgbOrthorectifier::orthorectify(
            {first, second}, terrain(), grid(), options());
    return expect(result.success,
                  "A second capture should cover radiometrically invalid pixels.")
           && expect(cv::countNonZero(
                         result.referenceGeometry.primarySourceImageIndex
                         != 10) == 0,
                     "Reference geometry should still select the first capture.")
           && expect(cv::countNonZero(result.primarySourceImageIndex != 20)
                         == 0,
                     "RGB ownership should fall back when any selected sensor is invalid.")
           && expect(result.unavailableSourceFallbackCount
                         == static_cast<size_t>(result.outputGrid.size.area()),
                     "Saturated/invalid sensor fallbacks should be counted exactly.");
}

bool fiveBandSharedGeometryTest()
{
    kestrel::MultispectralOrthophotoSource capture;
    capture.imageIndex = 31;
    const char *names[] = {"Blue", "Green", "Red", "NIR", "Red edge"};
    const double wavelengths[] = {475.0, 560.0, 668.0, 840.0, 717.0};
    for (int index = 0; index < 5; ++index) {
        kestrel::SpectralOrthophotoBand band;
        band.name = names[index];
        band.centreWavelengthNanometres = wavelengths[index];
        band.image = cv::Mat(100, 100, CV_16U,
                             cv::Scalar((index + 1) * 100)).clone();
        band.camera = camera();
        capture.bands.push_back(std::move(band));
    }
    kestrel::SharedGeometryMultispectralOptions testOptions;
    testOptions.referenceBandIndex = 1;
    testOptions.geometryOptions = options().geometryOptions;
    testOptions.bandInterpolation = cv::INTER_NEAREST;
    testOptions.multibandLevels = 2;
    const auto result =
        kestrel::SharedGeometryMultispectralOrthorectifier::orthorectify(
            {capture}, terrain(), grid(), testOptions);
    std::vector<cv::Mat> bands;
    if (result.success) cv::split(result.orthophoto, bands);
    return expect(result.success && result.orthophoto.channels() == 5,
                  "Shared geometry should support a complete five-band capture.")
           && expect(result.bandNames.size() == 5
                         && result.bandNames[3] == "NIR"
                         && result.centreWavelengthsNanometres[4] == 717.0,
                     "Multispectral output must retain ordered band metadata.")
           && expect(bands.size() == 5
                         && bands[0].at<ushort>(2, 0) == 100
                         && bands[4].at<ushort>(2, 0) == 500,
                     "Every spectral channel should survive the common terrain warp.");
}

bool validationTest()
{
    auto invalid = source(1, 0);
    invalid.red.release();
    const auto result =
        kestrel::SharedGeometryRgbOrthorectifier::orthorectify(
            {invalid}, terrain(), grid(), options());
    return expect(!result.success,
                  "Incomplete physical captures must fail safely.");
}

} // namespace

int main()
{
    if (independentSensorProjectionTest() && sharedLabelsAndTilingTest()
        && radiometricValidityFallbackTest() && fiveBandSharedGeometryTest()
        && validationTest()) {
        std::cout << "Shared-geometry RGB orthorectification tests passed.\n";
        return 0;
    }
    return 1;
}
