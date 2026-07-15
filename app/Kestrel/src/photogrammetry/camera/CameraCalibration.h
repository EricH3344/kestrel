#ifndef CAMERACALIBRATION_H
#define CAMERACALIBRATION_H

#include "photogrammetry/io/TiffImageMetadata.h"

#include <QString>
#include <QStringList>

#include <opencv2/core.hpp>

namespace kestrel {

enum class CalibrationQuality
{
    Invalid,
    Approximate,
    MetadataCalibrated
};

struct RelativeCameraPose
{
    cv::Matx33d rotationToReference = cv::Matx33d::eye();
    cv::Vec3d translationToReference{0.0, 0.0, 0.0};
    bool hasRotation = false;
    bool hasTranslation = false;
};

struct CameraCalibration
{
    cv::Size imageSize;
    cv::Size2d sensorSizeMillimetres;
    cv::Matx33d intrinsic = cv::Matx33d::eye();
    cv::Vec<double, 5> distortion{0.0, 0.0, 0.0, 0.0, 0.0};
    RelativeCameraPose rigPose;
    CalibrationQuality quality = CalibrationQuality::Invalid;
    QString source;
    QStringList warnings;

    bool isUsable() const;
};

class CameraCalibrationFactory
{
public:
    // Creates pixel-space intrinsics from EXIF/XMP metric calibration. Unknown
    // cameras receive an explicitly approximate centered pinhole model so SfM
    // can initialize it, but the result cannot be mistaken for survey-grade
    // calibration and must be refined during bundle adjustment.
    static CameraCalibration fromMetadata(const TiffImageMetadata &metadata);
};

} // namespace kestrel

#endif // CAMERACALIBRATION_H
