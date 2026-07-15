#include "photogrammetry/radiometry/MicaSenseRadiometricCalibrator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kestrel {

namespace {

bool finiteValues(const QVector<double> &values)
{
    return std::all_of(values.cbegin(), values.cend(),
                       [](double value) { return std::isfinite(value); });
}

double rawValue(const cv::Mat &image, int row, int column)
{
    if (image.depth() == CV_8U) {
        return image.at<uchar>(row, column);
    }
    return image.at<ushort>(row, column);
}

double legacyCorrectedExposure(const TiffImageMetadata &metadata,
                               bool *corrected)
{
    double exposure = metadata.exposureTimeSeconds;
    *corrected = false;
    // MicaSense's reference implementation documents this firmware metadata
    // defect for non-Altum RedEdge images.
    if (metadata.cameraModel.compare("Altum", Qt::CaseInsensitive) != 0
        && std::abs(exposure - 1.0 / 6329.0) < 1e-6) {
        exposure = 0.000274;
        *corrected = true;
    }
    return exposure;
}

} // namespace

RadiometricCalibrationResult
MicaSenseRadiometricCalibrator::calibrateToRadiance(
    const cv::Mat &rawImage,
    const TiffImageMetadata &metadata,
    cv::Point pixelOrigin)
{
    RadiometricCalibrationResult result;
    if (rawImage.empty() || rawImage.channels() != 1
        || (rawImage.depth() != CV_8U && rawImage.depth() != CV_16U)) {
        result.message = "Radiometric calibration requires one-channel unsigned raw pixels.";
        return result;
    }
    if (!metadata.hasRadiometricCalibration()
        || !finiteValues(metadata.blackLevels)
        || !finiteValues(metadata.vignettingCenter)
        || !finiteValues(metadata.vignettingPolynomial)
        || !finiteValues(metadata.radiometricCalibration)) {
        result.message = "Required MicaSense radiometric metadata is missing or invalid.";
        return result;
    }
    if (pixelOrigin.x < 0 || pixelOrigin.y < 0
        || pixelOrigin.x + rawImage.cols > metadata.imageWidth
        || pixelOrigin.y + rawImage.rows > metadata.imageHeight) {
        result.message = "Radiometric crop lies outside the original sensor image.";
        return result;
    }

    for (double value : metadata.blackLevels) {
        result.blackLevelDn += value;
    }
    result.blackLevelDn /= metadata.blackLevels.size();
    result.exposureTimeSeconds = legacyCorrectedExposure(
        metadata, &result.usedLegacyExposureCorrection);
    result.gain = metadata.isoSpeed / 100.0;
    const double a1 = metadata.radiometricCalibration[0];
    const double a2 = metadata.radiometricCalibration[1];
    const double a3 = metadata.radiometricCalibration[2];
    const double dnScale = std::ldexp(1.0, metadata.bitsPerSample);
    if (!std::isfinite(result.blackLevelDn)
        || !std::isfinite(result.exposureTimeSeconds)
        || result.exposureTimeSeconds <= 0.0
        || !std::isfinite(result.gain) || result.gain <= 0.0
        || !std::isfinite(a1) || a1 <= 0.0
        || !std::isfinite(dnScale) || dnScale <= 1.0) {
        result.message = "MicaSense radiometric scale parameters are invalid.";
        return result;
    }

    result.radiance = cv::Mat::zeros(rawImage.size(), CV_32F);
    result.validityMask = cv::Mat::zeros(rawImage.size(), CV_8U);
    const double saturationDn = dnScale - 1.0;
    const double radianceScale = a1
        / (result.gain * result.exposureTimeSeconds * dnScale);
    for (int row = 0; row < rawImage.rows; ++row) {
        float *radianceRow = result.radiance.ptr<float>(row);
        uchar *validityRow = result.validityMask.ptr<uchar>(row);
        const double sensorY = pixelOrigin.y + row;
        const double rowDenominator = 1.0
            + a2 * sensorY / result.exposureTimeSeconds - a3 * sensorY;
        for (int column = 0; column < rawImage.cols; ++column) {
            const double raw = rawValue(rawImage, row, column);
            if (raw >= saturationDn) {
                ++result.saturatedPixelCount;
                continue;
            }
            const double sensorX = pixelOrigin.x + column;
            const double dx = sensorX - metadata.vignettingCenter[0];
            const double dy = sensorY - metadata.vignettingCenter[1];
            const double radius = std::hypot(dx, dy);
            double vignettePolynomial = 1.0;
            double power = radius;
            for (double coefficient : metadata.vignettingPolynomial) {
                vignettePolynomial += coefficient * power;
                power *= radius;
            }
            if (!std::isfinite(vignettePolynomial)
                || vignettePolynomial <= 1e-12
                || !std::isfinite(rowDenominator)
                || rowDenominator <= 1e-12) {
                ++result.invalidModelPixelCount;
                continue;
            }
            double signal = raw - result.blackLevelDn;
            if (signal < 0.0) {
                signal = 0.0;
                ++result.blackClampedPixelCount;
            }
            const double radiance = signal / vignettePolynomial
                / rowDenominator * radianceScale;
            if (!std::isfinite(radiance)
                || radiance > std::numeric_limits<float>::max()) {
                ++result.invalidModelPixelCount;
                continue;
            }
            radianceRow[column] = static_cast<float>(radiance);
            validityRow[column] = 255;
        }
    }
    result.success = cv::countNonZero(result.validityMask) > 0;
    result.message = result.success
        ? "Raw MicaSense pixels converted to camera-calibrated spectral radiance."
        : "No raw pixel survived radiometric calibration.";
    return result;
}

} // namespace kestrel
