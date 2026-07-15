#include "photogrammetry/camera/RigCameraModel.h"

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

kestrel::CameraCalibration calibration()
{
    kestrel::CameraCalibration result;
    result.imageSize = {100, 80};
    result.intrinsic = cv::Matx33d(50.0, 0.0, 49.5,
                                   0.0, 50.0, 39.5,
                                   0.0, 0.0, 1.0);
    result.quality = kestrel::CalibrationQuality::MetadataCalibrated;
    return result;
}

cv::Matx33d rotationZ(double angle)
{
    return cv::Matx33d(std::cos(angle), -std::sin(angle), 0.0,
                       std::sin(angle), std::cos(angle), 0.0,
                       0.0, 0.0, 1.0);
}

bool rotationAndUnknownTranslationTest()
{
    kestrel::PinholeCamera referenceCamera;
    referenceCamera.worldToCameraTranslation = {1.0, 2.0, 3.0};
    auto reference = calibration();
    auto band = calibration();
    reference.rigPose.hasRotation = true;
    reference.rigPose.rotationToReference = rotationZ(0.10);
    band.rigPose.hasRotation = true;
    band.rigPose.rotationToReference = rotationZ(0.25);
    kestrel::RigCameraDerivation derived;
    std::string error;
    const bool success = kestrel::RigCameraModel::deriveFromReference(
        referenceCamera, reference, band, &derived, &error);
    const cv::Matx33d expected = rotationZ(-0.15);
    return expect(success && error.empty() && derived.usedRelativeRotation,
                  "Known rig rotations should be composed.")
           && expect(!derived.usedRelativeTranslation,
                     "Missing baselines must remain explicitly unknown.")
           && expect(cv::norm(cv::Mat(derived.camera.worldToCameraRotation),
                              cv::Mat(expected), cv::NORM_INF) < 1e-12,
                     "Band-from-reference rotation should use both rig poses.")
           && expect(cv::norm(derived.camera.worldToCameraTranslation
                              - expected
                                  * referenceCamera.worldToCameraTranslation)
                         < 1e-12,
                     "Unknown baseline should rotate, but not offset, the extrinsic translation.");
}

bool knownTranslationTest()
{
    kestrel::PinholeCamera referenceCamera;
    auto reference = calibration();
    auto band = calibration();
    reference.rigPose.hasTranslation = true;
    reference.rigPose.translationToReference = {0.02, 0.0, 0.0};
    band.rigPose.hasTranslation = true;
    band.rigPose.translationToReference = {-0.03, 0.0, 0.0};
    kestrel::RigCameraDerivation derived;
    const bool success = kestrel::RigCameraModel::deriveFromReference(
        referenceCamera, reference, band, &derived);
    return expect(success && derived.usedRelativeTranslation,
                  "A calibrated two-sensor baseline should be used.")
           && expect(cv::norm(derived.camera.worldToCameraTranslation
                              - cv::Vec3d(0.05, 0.0, 0.0)) < 1e-12,
                     "Rig translations should compose in band-camera coordinates.");
}

} // namespace

int main()
{
    if (rotationAndUnknownTranslationTest() && knownTranslationTest()) {
        std::cout << "Rig camera model tests passed.\n";
        return 0;
    }
    return 1;
}
