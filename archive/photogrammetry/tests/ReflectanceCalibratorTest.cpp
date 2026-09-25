#include "photogrammetry/radiometry/ReflectanceCalibrator.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::vector<kestrel::SpectralIrradiance> irradiances()
{
    return {
        {"Blue", 475.0, 10.0, kestrel::IrradianceSource::ReflectancePanel},
        {"Green", 560.0, 20.0, kestrel::IrradianceSource::ReflectancePanel},
        {"Red", 668.0, 40.0, kestrel::IrradianceSource::ReflectancePanel},
        {"NIR", 840.0, 80.0, kestrel::IrradianceSource::ReflectancePanel},
        {"Red edge", 717.0, 160.0, kestrel::IrradianceSource::ReflectancePanel}};
}

bool conversionTest()
{
    cv::Mat radiance(1, 2, CV_MAKETYPE(CV_32F, 5));
    const float values[] = {1, 2, 4, 8, 16, 2, 4, 8, 16, 32};
    std::copy(std::begin(values), std::end(values), radiance.ptr<float>());
    cv::Mat mask(1, 2, CV_8U, cv::Scalar(255));
    mask.at<uchar>(0, 1) = 0;
    const auto result = kestrel::ReflectanceCalibrator::calibrate(
        radiance, mask,
        {"Blue", "Green", "Red", "NIR", "Red edge"},
        {475.0, 560.0, 668.0, 840.0, 717.0}, irradiances());
    bool valuesOk = result.success && result.reflectance.channels() == 5
        && result.validityMask.at<uchar>(0, 0) == 255
        && result.validityMask.at<uchar>(0, 1) == 0;
    for (int channel = 0; channel < 5 && valuesOk; ++channel) {
        valuesOk = std::abs(result.reflectance.ptr<float>()[channel]
                            - static_cast<float>(3.14159265358979323846 / 10.0))
            < 1e-6f;
    }
    return expect(valuesOk,
                  "Every ordered radiance band should use its measured irradiance and preserve validity.");
}

bool strictInputGateTest()
{
    cv::Mat radiance = cv::Mat::zeros(1, 1, CV_MAKETYPE(CV_32F, 5));
    std::fill_n(radiance.ptr<float>(), 5, 1.0f);
    auto inputs = irradiances();
    inputs[3].source = kestrel::IrradianceSource::Unknown;
    const auto missing = kestrel::ReflectanceCalibrator::calibrate(
        radiance, {}, {"Blue", "Green", "Red", "NIR", "Red edge"},
        {475.0, 560.0, 668.0, 840.0, 717.0}, inputs);
    inputs = irradiances();
    inputs[4].bandName = "NIR";
    const auto mismatched = kestrel::ReflectanceCalibrator::calibrate(
        radiance, {}, {"Blue", "Green", "Red", "NIR", "Red edge"},
        {475.0, 560.0, 668.0, 840.0, 717.0}, inputs);
    return expect(!missing.success && missing.message.find("NIR") != std::string::npos,
                  "Missing measured irradiance must block reflectance output with the affected band.")
        && expect(!mismatched.success,
                  "Reordered or mismatched irradiance must not be silently accepted.");
}

bool invalidPixelAndPanelTest()
{
    cv::Mat radiance = cv::Mat::zeros(1, 1, CV_MAKETYPE(CV_32F, 5));
    std::fill_n(radiance.ptr<float>(), 5, 1.0f);
    radiance.ptr<float>()[2] = std::numeric_limits<float>::quiet_NaN();
    const auto result = kestrel::ReflectanceCalibrator::calibrate(
        radiance, {}, {"Blue", "Green", "Red", "NIR", "Red edge"},
        {475.0, 560.0, 668.0, 840.0, 717.0}, irradiances());
    double panelIrradiance = 0.0;
    std::string error;
    const bool panel = kestrel::ReflectanceCalibrator::irradianceFromPanel(
        2.0, 0.5, &panelIrradiance, &error);
    const bool badPanel = kestrel::ReflectanceCalibrator::irradianceFromPanel(
        2.0, 0.0, &panelIrradiance, &error);
    return expect(!result.success && result.invalidPixelCount == 1,
                  "Non-finite radiance should invalidate the whole spectral pixel.")
        && expect(panel && std::abs(panelIrradiance - 4.0 * 3.14159265358979323846) < 1e-12,
                  "Panel radiance and certified reflectance should produce incident irradiance.")
        && expect(!badPanel,
                  "An invalid panel reflectance must be rejected.");
}

} // namespace

int main()
{
    if (conversionTest() && strictInputGateTest()
        && invalidPixelAndPanelTest()) {
        std::cout << "Reflectance calibrator tests passed.\n";
        return 0;
    }
    return 1;
}
