#include "photogrammetry/radiometry/MicaSenseRadiometricCalibrator.h"

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

kestrel::TiffImageMetadata metadata()
{
    kestrel::TiffImageMetadata result;
    result.imageWidth = 4;
    result.imageHeight = 4;
    result.cameraModel = "RedEdge-M";
    result.bitsPerSample = 16;
    result.hasBitsPerSample = true;
    result.exposureTimeSeconds = 0.01;
    result.hasExposureTime = true;
    result.isoSpeed = 200.0;
    result.hasIsoSpeed = true;
    result.blackLevels = {90.0, 100.0, 110.0, 100.0};
    result.vignettingCenter = {0.0, 0.0};
    result.vignettingPolynomial = {0.1, 0.0, 0.0, 0.0, 0.0, 0.0};
    result.radiometricCalibration = {0.001, 0.001, 0.0};
    return result;
}

bool formulaAndMaskTest()
{
    cv::Mat raw(4, 4, CV_16U, cv::Scalar(1100));
    raw.at<ushort>(0, 0) = 50;
    raw.at<ushort>(0, 1) = 65535;
    const auto result =
        kestrel::MicaSenseRadiometricCalibrator::calibrateToRadiance(
            raw, metadata());
    const double radius = std::hypot(2.0, 1.0);
    const double vignetteDenominator = 1.0 + 0.1 * radius;
    const double rowDenominator = 1.0 + 0.001 * 1.0 / 0.01;
    const double expected = 1000.0 / vignetteDenominator / rowDenominator
        * 0.001 / (2.0 * 0.01 * 65536.0);
    return expect(result.success && result.radiance.type() == CV_32FC1,
                  "Valid metadata should produce float spectral radiance.")
           && expect(std::abs(result.radiance.at<float>(1, 2) - expected)
                         < 1e-8,
                     "Radiance should apply black, vignette, row, exposure, gain, and bit-depth factors.")
           && expect(result.validityMask.at<uchar>(0, 1) == 0
                         && result.saturatedPixelCount == 1,
                     "Saturated pixels must be excluded and counted.")
           && expect(result.validityMask.at<uchar>(0, 0) == 255
                         && result.radiance.at<float>(0, 0) == 0.0f
                         && result.blackClampedPixelCount == 1,
                     "Below-black noise should be clamped without inventing negative radiance.");
}

bool cropCoordinatesTest()
{
    cv::Mat raw(4, 4, CV_16U, cv::Scalar(1100));
    const auto full =
        kestrel::MicaSenseRadiometricCalibrator::calibrateToRadiance(
            raw, metadata());
    const cv::Rect crop(2, 1, 2, 2);
    const auto cropped =
        kestrel::MicaSenseRadiometricCalibrator::calibrateToRadiance(
            raw(crop), metadata(), crop.tl());
    return expect(cropped.success
                      && cv::norm(full.radiance(crop), cropped.radiance,
                                  cv::NORM_INF) == 0.0,
                  "Cropped calibration must retain original sensor coordinates.");
}

bool legacyExposureAndValidationTest()
{
    cv::Mat raw(4, 4, CV_16U, cv::Scalar(1100));
    auto legacy = metadata();
    legacy.exposureTimeSeconds = 1.0 / 6329.0;
    const auto corrected =
        kestrel::MicaSenseRadiometricCalibrator::calibrateToRadiance(
            raw, legacy);
    auto incomplete = metadata();
    incomplete.radiometricCalibration.clear();
    const auto rejected =
        kestrel::MicaSenseRadiometricCalibrator::calibrateToRadiance(
            raw, incomplete);
    return expect(corrected.success && corrected.usedLegacyExposureCorrection
                      && std::abs(corrected.exposureTimeSeconds - 0.000274)
                             < 1e-12,
                  "Known legacy RedEdge exposure metadata should be corrected.")
           && expect(!rejected.success,
                     "Incomplete radiometric metadata must fail explicitly.");
}

} // namespace

int main()
{
    if (formulaAndMaskTest() && cropCoordinatesTest()
        && legacyExposureAndValidationTest()) {
        std::cout << "MicaSense radiometric calibration tests passed.\n";
        return 0;
    }
    return 1;
}
