#ifndef TIFFIMAGEMETADATA_H
#define TIFFIMAGEMETADATA_H

#include "photogrammetry/geo/GeoCoordinates.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace kestrel {

struct TiffImageMetadata
{
    int imageWidth = 0;
    int imageHeight = 0;
    int orientation = 1;
    bool hasOrientation = false;
    int bitsPerSample = 0;
    bool hasBitsPerSample = false;

    QString cameraMake;
    QString cameraModel;
    QString cameraSerial;
    QString captureTime;
    double exposureTimeSeconds = 0.0;
    bool hasExposureTime = false;
    double isoSpeed = 0.0;
    bool hasIsoSpeed = false;
    QVector<double> blackLevels;

    double focalLengthMillimetres = 0.0;
    bool hasFocalLength = false;
    double focalPlanePixelsPerMillimetreX = 0.0;
    double focalPlanePixelsPerMillimetreY = 0.0;
    bool hasFocalPlaneResolution = false;
    double calibratedFocalLengthMillimetres = 0.0;
    bool hasCalibratedFocalLength = false;
    QVector<double> principalPointMillimetres;
    QVector<double> perspectiveDistortion;

    QString captureId;
    QString bandName;
    int rigCameraIndex = -1;
    QVector<double> rigRelatives;
    QVector<double> vignettingCenter;
    QVector<double> vignettingPolynomial;
    QVector<double> radiometricCalibration;
    double bandSensitivity = 0.0;
    bool hasBandSensitivity = false;
    double centralWavelengthNanometres = 0.0;
    bool hasCentralWavelength = false;

    GeoCoordinate gps;
    bool hasGps = false;
    bool hasGpsAltitude = false;

    QString cameraIdentifier() const;
    bool hasRadiometricCalibration() const;
    QStringList validationWarnings() const;
};

class TiffMetadataReader
{
public:
    static bool read(const QString &filePath,
                     TiffImageMetadata *metadata,
                     QString *errorMessage = nullptr);

    // Public for deterministic unit tests and future sidecar-XMP support.
    static void parseXmpPacket(const QByteArray &packet,
                               TiffImageMetadata *metadata);
};

} // namespace kestrel

#endif // TIFFIMAGEMETADATA_H
