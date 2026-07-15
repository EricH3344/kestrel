#ifndef REFLECTANCECALIBRATOR_H
#define REFLECTANCECALIBRATOR_H

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace kestrel {

enum class IrradianceSource
{
    Unknown,
    ReflectancePanel,
    DownwellingLightSensor
};

struct SpectralIrradiance
{
    std::string bandName;
    double centreWavelengthNanometres = 0.0;
    double wattsPerSquareMetrePerNanometre = 0.0;
    IrradianceSource source = IrradianceSource::Unknown;
};

struct ReflectanceCalibrationResult
{
    bool success = false;
    std::string message;
    cv::Mat reflectance; // CV_32F with the same ordered channels as radiance.
    cv::Mat validityMask; // CV_8UC1; invalid radiance is excluded.
    size_t invalidPixelCount = 0;
};

class ReflectanceCalibrator
{
public:
    // MicaSense's spectral relation is reflectance = pi * radiance /
    // irradiance. Irradiance must already be corrected for the capture and
    // supplied for every ordered radiance band; it is never inferred.
    static ReflectanceCalibrationResult calibrate(
        const cv::Mat &radiance,
        const cv::Mat &radianceValidityMask,
        const std::vector<std::string> &bandNames,
        const std::vector<double> &centreWavelengthsNanometres,
        const std::vector<SpectralIrradiance> &irradiances);

    // A panel observation estimates incident irradiance from its measured
    // mean radiance and the certified panel reflectance for one band.
    static bool irradianceFromPanel(double meanPanelRadiance,
                                    double panelReflectance,
                                    double *irradiance,
                                    std::string *errorMessage = nullptr);
};

} // namespace kestrel

#endif // REFLECTANCECALIBRATOR_H
