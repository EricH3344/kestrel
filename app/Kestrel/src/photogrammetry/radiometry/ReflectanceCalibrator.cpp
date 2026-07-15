#include "photogrammetry/radiometry/ReflectanceCalibrator.h"

#include <cmath>
#include <limits>

namespace kestrel {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool sameWavelength(double lhs, double rhs)
{
    // Metadata sources commonly round nominal centre wavelengths to an
    // integer, so use a small matching tolerance without reordering bands.
    return std::abs(lhs - rhs) <= 1.0;
}

} // namespace

ReflectanceCalibrationResult ReflectanceCalibrator::calibrate(
    const cv::Mat &radiance,
    const cv::Mat &radianceValidityMask,
    const std::vector<std::string> &bandNames,
    const std::vector<double> &centreWavelengthsNanometres,
    const std::vector<SpectralIrradiance> &irradiances)
{
    ReflectanceCalibrationResult result;
    const int channelCount = radiance.channels();
    if (radiance.empty() || radiance.depth() != CV_32F
        || channelCount <= 0 || channelCount > CV_CN_MAX
        || (!radianceValidityMask.empty()
            && (radianceValidityMask.type() != CV_8UC1
                || radianceValidityMask.size() != radiance.size()))) {
        result.message = "Reflectance calibration requires a float radiance raster and matching validity mask.";
        return result;
    }
    if (bandNames.size() != static_cast<size_t>(channelCount)
        || centreWavelengthsNanometres.size()
               != static_cast<size_t>(channelCount)
        || irradiances.size() != static_cast<size_t>(channelCount)) {
        result.message = "Reflectance calibration requires one named wavelength and irradiance for every ordered band.";
        return result;
    }
    for (int channel = 0; channel < channelCount; ++channel) {
        const SpectralIrradiance &irradiance = irradiances[channel];
        if (irradiance.source == IrradianceSource::Unknown
            || irradiance.bandName != bandNames[channel]
            || !std::isfinite(centreWavelengthsNanometres[channel])
            || centreWavelengthsNanometres[channel] <= 0.0
            || !std::isfinite(irradiance.centreWavelengthNanometres)
            || !sameWavelength(irradiance.centreWavelengthNanometres,
                               centreWavelengthsNanometres[channel])
            || !std::isfinite(irradiance.wattsPerSquareMetrePerNanometre)
            || irradiance.wattsPerSquareMetrePerNanometre <= 0.0) {
            result.message = "Band " + bandNames[channel]
                + " is missing a valid, band-matched panel or DLS irradiance measurement.";
            return result;
        }
    }

    result.reflectance = cv::Mat::zeros(
        radiance.size(), CV_MAKETYPE(CV_32F, channelCount));
    result.validityMask = radianceValidityMask.empty()
        ? cv::Mat(radiance.size(), CV_8UC1, cv::Scalar(255))
        : radianceValidityMask.clone();
    for (int row = 0; row < radiance.rows; ++row) {
        const float *input = radiance.ptr<float>(row);
        float *output = result.reflectance.ptr<float>(row);
        uchar *valid = result.validityMask.ptr<uchar>(row);
        for (int column = 0; column < radiance.cols; ++column) {
            if (!valid[column]) continue;
            bool pixelValid = true;
            for (int channel = 0; channel < channelCount; ++channel) {
                const double value = input[column * channelCount + channel];
                const double reflectance = value * kPi
                    / irradiances[channel].wattsPerSquareMetrePerNanometre;
                if (!std::isfinite(value) || value < 0.0
                    || !std::isfinite(reflectance)
                    || reflectance > std::numeric_limits<float>::max()) {
                    pixelValid = false;
                    break;
                }
                output[column * channelCount + channel] =
                    static_cast<float>(reflectance);
            }
            if (!pixelValid) {
                valid[column] = 0;
                for (int channel = 0; channel < channelCount; ++channel) {
                    output[column * channelCount + channel] = 0.0f;
                }
                ++result.invalidPixelCount;
            }
        }
    }
    result.success = cv::countNonZero(result.validityMask) > 0;
    result.message = result.success
        ? "Spectral radiance converted to reflectance using measured irradiance for every band."
        : "No valid pixel survived reflectance calibration.";
    return result;
}

bool ReflectanceCalibrator::irradianceFromPanel(
    double meanPanelRadiance,
    double panelReflectance,
    double *irradiance,
    std::string *errorMessage)
{
    if (!irradiance || !std::isfinite(meanPanelRadiance)
        || meanPanelRadiance <= 0.0 || !std::isfinite(panelReflectance)
        || panelReflectance <= 0.0 || panelReflectance > 1.0) {
        if (errorMessage) {
            *errorMessage = "Panel irradiance requires positive panel radiance and certified reflectance in (0, 1].";
        }
        return false;
    }
    *irradiance = meanPanelRadiance * kPi / panelReflectance;
    if (errorMessage) errorMessage->clear();
    return true;
}

} // namespace kestrel
