#ifndef MICASENSERADIOMETRICCALIBRATOR_H
#define MICASENSERADIOMETRICCALIBRATOR_H

#include "photogrammetry/io/TiffImageMetadata.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>

namespace kestrel {

struct RadiometricCalibrationResult
{
    bool success = false;
    std::string message;
    cv::Mat radiance; // CV_32FC1, W / m^2 / sr / nm.
    cv::Mat validityMask; // CV_8UC1; saturated/invalid pixels are zero.
    size_t blackClampedPixelCount = 0;
    size_t saturatedPixelCount = 0;
    size_t invalidModelPixelCount = 0;
    bool usedLegacyExposureCorrection = false;
    double blackLevelDn = 0.0;
    double exposureTimeSeconds = 0.0;
    double gain = 0.0;
};

class MicaSenseRadiometricCalibrator
{
public:
    // Implements MicaSense's camera-radiance model. pixelOrigin locates a
    // cropped input in the original sensor coordinates so vignette and row
    // corrections remain physically correct.
    static RadiometricCalibrationResult calibrateToRadiance(
        const cv::Mat &rawImage,
        const TiffImageMetadata &metadata,
        cv::Point pixelOrigin = {});
};

} // namespace kestrel

#endif // MICASENSERADIOMETRICCALIBRATOR_H
