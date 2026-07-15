#include "photogrammetry/camera/CameraCalibration.h"

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

bool calibratedMetadataTest()
{
    kestrel::TiffImageMetadata metadata;
    metadata.imageWidth = 1280;
    metadata.imageHeight = 960;
    metadata.cameraMake = "MicaSense";
    metadata.cameraModel = "RedEdge-MX";
    metadata.hasFocalPlaneResolution = true;
    metadata.focalPlanePixelsPerMillimetreX = 800.0 / 3.0;
    metadata.focalPlanePixelsPerMillimetreY = 800.0 / 3.0;
    metadata.hasCalibratedFocalLength = true;
    metadata.calibratedFocalLengthMillimetres = 5.4;
    metadata.principalPointMillimetres = {2.4, 1.8};
    metadata.perspectiveDistortion = {-0.1, 0.2, -0.03, 0.001, -0.002};
    metadata.rigRelatives = {0.0, 0.0, 90.0};

    const kestrel::CameraCalibration calibration =
        kestrel::CameraCalibrationFactory::fromMetadata(metadata);
    return expect(calibration.isUsable(), "Metadata calibration should be usable.")
           && expect(calibration.quality
                         == kestrel::CalibrationQuality::MetadataCalibrated,
                     "Metric EXIF/XMP should produce calibrated quality.")
           && expect(std::abs(calibration.intrinsic(0, 0) - 1440.0) < 1e-8,
                     "Focal length should be converted from mm to pixels.")
           && expect(std::abs(calibration.intrinsic(0, 2) - 640.0) < 1e-8,
                     "Principal point X should be converted to pixels.")
           && expect(std::abs(calibration.intrinsic(1, 2) - 480.0) < 1e-8,
                     "Principal point Y should be converted to pixels.")
           && expect(std::abs(calibration.distortion[2] - 0.001) < 1e-12
                         && std::abs(calibration.distortion[4] + 0.03) < 1e-12,
                     "MicaSense distortion should map to OpenCV ordering.")
           && expect(calibration.rigPose.hasRotation
                         && !calibration.rigPose.hasTranslation,
                     "XMP rig rotation must not invent a rig translation.")
           && expect(std::abs(calibration.rigPose.rotationToReference(0, 1) + 1.0)
                         < 1e-10,
                     "Rig relative angles should produce Rx*Ry*Rz rotation.");
}

bool fallbackTest()
{
    kestrel::TiffImageMetadata metadata;
    metadata.imageWidth = 4000;
    metadata.imageHeight = 3000;
    const kestrel::CameraCalibration calibration =
        kestrel::CameraCalibrationFactory::fromMetadata(metadata);
    return expect(calibration.isUsable(), "Unknown cameras need a usable initializer.")
           && expect(calibration.quality == kestrel::CalibrationQuality::Approximate,
                     "Unknown calibration must remain explicitly approximate.")
           && expect(std::abs(calibration.intrinsic(0, 0) - 4800.0) < 1e-8,
                     "Fallback focal length should be deterministic.")
           && expect(!calibration.warnings.isEmpty(),
                     "Approximate calibration should be accompanied by a warning.");
}

bool invalidDimensionsTest()
{
    const kestrel::CameraCalibration calibration =
        kestrel::CameraCalibrationFactory::fromMetadata({});
    return expect(!calibration.isUsable(), "Missing dimensions must be invalid.")
           && expect(calibration.quality == kestrel::CalibrationQuality::Invalid,
                     "Invalid metadata must not silently become approximate.");
}

} // namespace

int main()
{
    const bool success = calibratedMetadataTest() && fallbackTest()
                         && invalidDimensionsTest();
    if (success) {
        std::cout << "Camera calibration tests passed.\n";
        return 0;
    }
    return 1;
}
