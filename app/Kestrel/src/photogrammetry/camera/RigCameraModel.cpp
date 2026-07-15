#include "photogrammetry/camera/RigCameraModel.h"

#include <cmath>

namespace kestrel {

namespace {

bool finiteMatrix(const cv::Matx33d &matrix)
{
    for (double value : matrix.val) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

bool finiteVector(const cv::Vec3d &vector)
{
    return std::isfinite(vector[0]) && std::isfinite(vector[1])
           && std::isfinite(vector[2]);
}

} // namespace

bool RigCameraModel::deriveFromReference(
    const PinholeCamera &referenceCamera,
    const CameraCalibration &referenceCalibration,
    const CameraCalibration &bandCalibration,
    RigCameraDerivation *result,
    std::string *errorMessage)
{
    if (!result || !referenceCalibration.isUsable()
        || !bandCalibration.isUsable()
        || !finiteMatrix(referenceCamera.worldToCameraRotation)
        || !finiteVector(referenceCamera.worldToCameraTranslation)) {
        if (errorMessage) {
            *errorMessage = "Rig camera derivation requires usable reference and band calibrations.";
        }
        return false;
    }

    const cv::Matx33d referenceToRig =
        referenceCalibration.rigPose.hasRotation
            ? referenceCalibration.rigPose.rotationToReference
            : cv::Matx33d::eye();
    const cv::Matx33d bandToRig = bandCalibration.rigPose.hasRotation
        ? bandCalibration.rigPose.rotationToReference
        : cv::Matx33d::eye();
    if (!finiteMatrix(referenceToRig) || !finiteMatrix(bandToRig)) {
        if (errorMessage) {
            *errorMessage = "Rig-relative rotation contains a non-finite value.";
        }
        return false;
    }
    // p_rig = R_referenceToRig * p_reference + t_referenceToRig
    //       = R_bandToRig * p_band + t_bandToRig.
    const cv::Matx33d bandFromReference =
        bandToRig.t() * referenceToRig;
    cv::Vec3d bandFromReferenceTranslation{0.0, 0.0, 0.0};
    result->usedRelativeTranslation =
        referenceCalibration.rigPose.hasTranslation
        && bandCalibration.rigPose.hasTranslation;
    if (result->usedRelativeTranslation) {
        bandFromReferenceTranslation = bandToRig.t()
            * (referenceCalibration.rigPose.translationToReference
               - bandCalibration.rigPose.translationToReference);
    }

    result->camera.intrinsic = bandCalibration.intrinsic;
    result->camera.distortion = bandCalibration.distortion;
    result->camera.worldToCameraRotation = bandFromReference
        * referenceCamera.worldToCameraRotation;
    result->camera.worldToCameraTranslation = bandFromReference
        * referenceCamera.worldToCameraTranslation
        + bandFromReferenceTranslation;
    result->usedRelativeRotation =
        referenceCalibration.rigPose.hasRotation
        || bandCalibration.rigPose.hasRotation;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace kestrel
