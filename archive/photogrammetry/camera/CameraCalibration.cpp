#include "photogrammetry/camera/CameraCalibration.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr double kPi = 3.14159265358979323846;

cv::Matx33d rotationX(double angle)
{
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return cv::Matx33d(1.0, 0.0, 0.0,
                       0.0, cosine, -sine,
                       0.0, sine, cosine);
}

cv::Matx33d rotationY(double angle)
{
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return cv::Matx33d(cosine, 0.0, sine,
                       0.0, 1.0, 0.0,
                       -sine, 0.0, cosine);
}

cv::Matx33d rotationZ(double angle)
{
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return cv::Matx33d(cosine, -sine, 0.0,
                       sine, cosine, 0.0,
                       0.0, 0.0, 1.0);
}

bool finiteMatrix(const cv::Matx33d &matrix)
{
    for (double value : matrix.val) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool CameraCalibration::isUsable() const
{
    return quality != CalibrationQuality::Invalid
           && imageSize.width > 0 && imageSize.height > 0
           && finiteMatrix(intrinsic)
           && intrinsic(0, 0) > 0.0 && intrinsic(1, 1) > 0.0
           && std::abs(intrinsic(2, 2) - 1.0) < 1e-12;
}

CameraCalibration CameraCalibrationFactory::fromMetadata(
    const TiffImageMetadata &metadata)
{
    CameraCalibration calibration;
    calibration.imageSize = cv::Size(metadata.imageWidth, metadata.imageHeight);
    if (metadata.imageWidth <= 0 || metadata.imageHeight <= 0) {
        calibration.warnings.append("Camera calibration requires valid image dimensions.");
        return calibration;
    }

    double pixelsPerMillimetreX = metadata.focalPlanePixelsPerMillimetreX;
    double pixelsPerMillimetreY = metadata.focalPlanePixelsPerMillimetreY;
    bool metricScaleKnown = metadata.hasFocalPlaneResolution
                            && pixelsPerMillimetreX > 0.0
                            && pixelsPerMillimetreY > 0.0;

    // RedEdge-M/MX use a 4.8 x 3.6 mm, 1280 x 960 sensor. This fallback is
    // used only when an older file omits EXIF focal-plane resolution.
    const bool knownRedEdge = metadata.cameraMake.compare(
                                  "MicaSense", Qt::CaseInsensitive) == 0
                              && metadata.cameraModel.startsWith(
                                  "RedEdge-M", Qt::CaseInsensitive)
                              && metadata.imageWidth == 1280
                              && metadata.imageHeight == 960;
    if (!metricScaleKnown && knownRedEdge) {
        pixelsPerMillimetreX = 1280.0 / 4.8;
        pixelsPerMillimetreY = 960.0 / 3.6;
        metricScaleKnown = true;
        calibration.warnings.append(
            "Using the known RedEdge-M 4.8 x 3.6 mm sensor dimensions.");
    }

    const double focalLengthMillimetres = metadata.hasCalibratedFocalLength
        ? metadata.calibratedFocalLengthMillimetres
        : metadata.focalLengthMillimetres;
    if (metricScaleKnown && focalLengthMillimetres > 0.0) {
        calibration.sensorSizeMillimetres = cv::Size2d(
            metadata.imageWidth / pixelsPerMillimetreX,
            metadata.imageHeight / pixelsPerMillimetreY);
        const double focalX = focalLengthMillimetres * pixelsPerMillimetreX;
        const double focalY = focalLengthMillimetres * pixelsPerMillimetreY;
        double principalX = (metadata.imageWidth - 1) * 0.5;
        double principalY = (metadata.imageHeight - 1) * 0.5;
        if (metadata.principalPointMillimetres.size() == 2) {
            principalX = metadata.principalPointMillimetres[0]
                         * pixelsPerMillimetreX;
            principalY = metadata.principalPointMillimetres[1]
                         * pixelsPerMillimetreY;
        } else {
            calibration.warnings.append(
                "Calibrated principal point is missing; using the image centre.");
        }
        calibration.intrinsic = cv::Matx33d(
            focalX, 0.0, principalX,
            0.0, focalY, principalY,
            0.0, 0.0, 1.0);
        calibration.quality = CalibrationQuality::MetadataCalibrated;
        calibration.source = metadata.hasFocalPlaneResolution
            ? "EXIF/XMP" : "MicaSense RedEdge-M fallback";
    } else {
        const double approximateFocal = 1.2
            * std::max(metadata.imageWidth, metadata.imageHeight);
        calibration.intrinsic = cv::Matx33d(
            approximateFocal, 0.0, (metadata.imageWidth - 1) * 0.5,
            0.0, approximateFocal, (metadata.imageHeight - 1) * 0.5,
            0.0, 0.0, 1.0);
        calibration.quality = CalibrationQuality::Approximate;
        calibration.source = "explicit approximate fallback";
        calibration.warnings.append(
            "Metric sensor calibration is unavailable; focal length must be refined.");
    }

    if (metadata.perspectiveDistortion.size() == 5) {
        // Pix4D/MicaSense stores k1,k2,k3,p1,p2. OpenCV/Brown-Conrady uses
        // k1,k2,p1,p2,k3.
        calibration.distortion = {
            metadata.perspectiveDistortion[0],
            metadata.perspectiveDistortion[1],
            metadata.perspectiveDistortion[3],
            metadata.perspectiveDistortion[4],
            metadata.perspectiveDistortion[2]};
    } else {
        calibration.warnings.append(
            "Lens distortion is missing; starting from zero distortion.");
    }

    if (metadata.rigRelatives.size() == 3) {
        const double x = metadata.rigRelatives[0] * kPi / 180.0;
        const double y = metadata.rigRelatives[1] * kPi / 180.0;
        const double z = metadata.rigRelatives[2] * kPi / 180.0;
        calibration.rigPose.rotationToReference =
            rotationX(x) * rotationY(y) * rotationZ(z);
        calibration.rigPose.hasRotation = true;
        calibration.warnings.append(
            "Rig translation is unavailable in XMP and remains uncalibrated.");
    }

    return calibration;
}

} // namespace kestrel
