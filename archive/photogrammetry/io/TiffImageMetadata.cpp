#include "photogrammetry/io/TiffImageMetadata.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include <cmath>

#ifdef KESTREL_HAS_LIBTIFF_GPS
#include <tiffio.h>
#endif

namespace kestrel {

namespace {

QString textValue(const char *value)
{
    return value ? QString::fromUtf8(value).trimmed() : QString();
}

QVector<double> numberList(const QString &text)
{
    QVector<double> result;
    const QStringList values = text.split(',', Qt::SkipEmptyParts);
    result.reserve(values.size());
    for (const QString &value : values) {
        bool converted = false;
        const double number = value.trimmed().toDouble(&converted);
        if (!converted || !std::isfinite(number)) {
            return {};
        }
        result.append(number);
    }
    return result;
}

void applyXmpValue(const QString &name, const QString &text,
                   TiffImageMetadata *metadata)
{
    if (name == "BandName") {
        metadata->bandName = text.trimmed();
    } else if (name == "CaptureId") {
        metadata->captureId = text.trimmed();
    } else if (name == "RigCameraIndex") {
        bool converted = false;
        const int value = text.trimmed().toInt(&converted);
        if (converted && value >= 0) {
            metadata->rigCameraIndex = value;
        }
    } else if (name == "CentralWavelength") {
        bool converted = false;
        const double value = text.trimmed().toDouble(&converted);
        if (converted && std::isfinite(value) && value > 0.0) {
            metadata->centralWavelengthNanometres = value;
            metadata->hasCentralWavelength = true;
        }
    } else if (name == "PerspectiveFocalLength") {
        bool converted = false;
        const double value = text.trimmed().toDouble(&converted);
        if (converted && std::isfinite(value) && value > 0.0) {
            metadata->calibratedFocalLengthMillimetres = value;
            metadata->hasCalibratedFocalLength = true;
        }
    } else if (name == "PrincipalPoint") {
        metadata->principalPointMillimetres = numberList(text);
    } else if (name == "PerspectiveDistortion") {
        // Store the camera vendor's original ordering. Conversion into a
        // Brown-Conrady vector belongs in a camera-specific calibration model.
        metadata->perspectiveDistortion = numberList(text);
    } else if (name == "RigRelatives") {
        metadata->rigRelatives = numberList(text);
    } else if (name == "VignettingCenter") {
        metadata->vignettingCenter = numberList(text);
    } else if (name == "VignettingPolynomial") {
        metadata->vignettingPolynomial = numberList(text);
    } else if (name == "RadiometricCalibration") {
        metadata->radiometricCalibration = numberList(text);
    } else if (name == "BandSensitivity") {
        bool converted = false;
        const double value = text.trimmed().toDouble(&converted);
        if (converted && std::isfinite(value) && value > 0.0) {
            metadata->bandSensitivity = value;
            metadata->hasBandSensitivity = true;
        }
    }
}

bool isSupportedXmpProperty(const QString &name)
{
    return name == "BandName" || name == "CaptureId"
           || name == "RigCameraIndex" || name == "CentralWavelength"
           || name == "PerspectiveFocalLength" || name == "PrincipalPoint"
           || name == "PerspectiveDistortion" || name == "RigRelatives"
           || name == "VignettingCenter"
           || name == "VignettingPolynomial"
           || name == "RadiometricCalibration"
           || name == "BandSensitivity";
}

} // namespace

QString TiffImageMetadata::cameraIdentifier() const
{
    QStringList parts;
    if (!cameraMake.isEmpty()) {
        parts.append(cameraMake);
    }
    if (!cameraModel.isEmpty()) {
        parts.append(cameraModel);
    }
    if (!cameraSerial.isEmpty()) {
        parts.append(cameraSerial);
    }
    return parts.join(' ');
}

bool TiffImageMetadata::hasRadiometricCalibration() const
{
    return hasBitsPerSample && bitsPerSample > 0 && bitsPerSample <= 32
           && hasExposureTime && std::isfinite(exposureTimeSeconds)
           && exposureTimeSeconds > 0.0
           && hasIsoSpeed && std::isfinite(isoSpeed) && isoSpeed > 0.0
           && !blackLevels.isEmpty()
           && vignettingCenter.size() == 2
           && vignettingPolynomial.size() == 6
           && radiometricCalibration.size() == 3;
}

QStringList TiffImageMetadata::validationWarnings() const
{
    QStringList warnings;
    if (imageWidth <= 0 || imageHeight <= 0) {
        warnings.append("image dimensions are missing or invalid");
    }
    if (!hasOrientation || orientation < 1 || orientation > 8) {
        warnings.append("EXIF orientation is missing or invalid");
    }
    if (cameraMake.isEmpty() || cameraModel.isEmpty()) {
        warnings.append("camera make/model is missing");
    }
    if (!hasFocalLength || !std::isfinite(focalLengthMillimetres)
        || focalLengthMillimetres <= 0.0) {
        warnings.append("EXIF focal length is missing or invalid");
    }
    if (captureTime.isEmpty()) {
        warnings.append("capture timestamp is missing");
    }
    if (!hasBitsPerSample || bitsPerSample <= 0) {
        warnings.append("sample bit depth is missing or invalid");
    }
    if (!perspectiveDistortion.isEmpty()
        && perspectiveDistortion.size() != 5) {
        warnings.append("perspective distortion must contain five coefficients");
    }
    if (!principalPointMillimetres.isEmpty()
        && principalPointMillimetres.size() != 2) {
        warnings.append("principal point must contain two coordinates");
    }
    if (!rigRelatives.isEmpty() && rigRelatives.size() != 3) {
        warnings.append("rig-relative pose must contain three values");
    }
    if (!vignettingCenter.isEmpty() && vignettingCenter.size() != 2) {
        warnings.append("vignetting center must contain two values");
    }
    if (!vignettingPolynomial.isEmpty()
        && vignettingPolynomial.size() != 6) {
        warnings.append("vignetting polynomial must contain six coefficients");
    }
    if (!radiometricCalibration.isEmpty()
        && radiometricCalibration.size() != 3) {
        warnings.append("radiometric calibration must contain three coefficients");
    }
    if (!bandName.isEmpty() && captureId.isEmpty()) {
        warnings.append("spectral band has no physical capture ID");
    }
    if (hasGps && !gps.isValid()) {
        warnings.append("GPS coordinate is invalid");
    }
    return warnings;
}

void TiffMetadataReader::parseXmpPacket(const QByteArray &packet,
                                        TiffImageMetadata *metadata)
{
    if (!metadata || packet.isEmpty()) {
        return;
    }
    QXmlStreamReader xml(packet);
    QString activeProperty;
    QString directText;
    QStringList sequenceValues;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            for (const QXmlStreamAttribute &attribute : xml.attributes()) {
                applyXmpValue(attribute.name().toString(),
                              attribute.value().toString(), metadata);
            }
            const QString name = xml.name().toString();
            if (isSupportedXmpProperty(name)) {
                activeProperty = name;
                directText.clear();
                sequenceValues.clear();
            } else if (!activeProperty.isEmpty() && name == "li") {
                sequenceValues.append(xml.readElementText().trimmed());
            }
        } else if ((xml.isCharacters() || xml.isCDATA())
                   && !activeProperty.isEmpty()) {
            directText += xml.text().toString();
        } else if (xml.isEndElement()
                   && xml.name().toString() == activeProperty) {
            applyXmpValue(activeProperty,
                          sequenceValues.isEmpty()
                              ? directText.trimmed() : sequenceValues.join(','),
                          metadata);
            activeProperty.clear();
            directText.clear();
            sequenceValues.clear();
        }
    }
}

bool TiffMetadataReader::read(const QString &filePath,
                              TiffImageMetadata *metadata,
                              QString *errorMessage)
{
    if (!metadata) {
        if (errorMessage) {
            *errorMessage = "A metadata output object is required.";
        }
        return false;
    }
    *metadata = {};
#ifdef KESTREL_HAS_LIBTIFF_GPS
#ifdef Q_OS_WIN
    TIFF *tiff = TIFFOpenW(reinterpret_cast<const wchar_t *>(filePath.utf16()), "r");
#else
    const QByteArray encodedPath = QFile::encodeName(filePath);
    TIFF *tiff = TIFFOpen(encodedPath.constData(), "r");
#endif
    if (!tiff) {
        if (errorMessage) {
            *errorMessage = "Could not open TIFF metadata for "
                            + QFileInfo(filePath).fileName() + ".";
        }
        return false;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t orientation = ORIENTATION_TOPLEFT;
    uint16_t bitsPerSample = 0;
    char *make = nullptr;
    char *model = nullptr;
    char *dateTime = nullptr;
    uint64_t exifOffset = 0;
    uint64_t gpsOffset = 0;
    uint32_t xmpSize = 0;
    void *xmpData = nullptr;

    if (TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width)) {
        metadata->imageWidth = static_cast<int>(width);
    }
    if (TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height)) {
        metadata->imageHeight = static_cast<int>(height);
    }
    if (TIFFGetField(tiff, TIFFTAG_ORIENTATION, &orientation)) {
        metadata->orientation = orientation;
        metadata->hasOrientation = true;
    }
    if (TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitsPerSample)
        && bitsPerSample > 0) {
        metadata->bitsPerSample = bitsPerSample;
        metadata->hasBitsPerSample = true;
    }
    uint16_t blackLevelCount = 0;
    float *blackLevels = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_BLACKLEVEL,
                     &blackLevelCount, &blackLevels)
        && blackLevels && blackLevelCount > 0) {
        metadata->blackLevels.reserve(blackLevelCount);
        for (uint16_t index = 0; index < blackLevelCount; ++index) {
            if (std::isfinite(blackLevels[index])) {
                metadata->blackLevels.append(blackLevels[index]);
            }
        }
    }
    uint16_t exposureCount = 0;
    float *exposureValues = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_EP_EXPOSURETIME,
                     &exposureCount, &exposureValues)
        && exposureValues && exposureCount > 0
        && std::isfinite(exposureValues[0]) && exposureValues[0] > 0.0f) {
        metadata->exposureTimeSeconds = exposureValues[0];
        metadata->hasExposureTime = true;
    }
    uint16_t isoSpeed = 0;
    if (TIFFGetField(tiff, TIFFTAG_EP_ISOSPEEDRATINGS, &isoSpeed)
        && isoSpeed > 0) {
        metadata->isoSpeed = isoSpeed;
        metadata->hasIsoSpeed = true;
    }
    if (TIFFGetField(tiff, TIFFTAG_MAKE, &make)) {
        metadata->cameraMake = textValue(make);
    }
    if (TIFFGetField(tiff, TIFFTAG_MODEL, &model)) {
        metadata->cameraModel = textValue(model);
    }
    if (TIFFGetField(tiff, TIFFTAG_DATETIME, &dateTime)) {
        metadata->captureTime = textValue(dateTime);
    }
    TIFFGetField(tiff, TIFFTAG_EXIFIFD, &exifOffset);
    TIFFGetField(tiff, TIFFTAG_GPSIFD, &gpsOffset);
    if (TIFFGetField(tiff, TIFFTAG_XMLPACKET, &xmpSize, &xmpData)
        && xmpData && xmpSize > 0) {
        parseXmpPacket(QByteArray(static_cast<const char *>(xmpData),
                                  static_cast<qsizetype>(xmpSize)), metadata);
    }

    if (exifOffset != 0
        && TIFFReadEXIFDirectory(tiff, static_cast<toff_t>(exifOffset))) {
        float focalLength = 0.0f;
        float exposureTime = 0.0f;
        uint16_t isoCount = 0;
        uint16_t *isoValues = nullptr;
        uint32_t isoSpeed32 = 0;
        float focalPlaneResolutionX = 0.0f;
        float focalPlaneResolutionY = 0.0f;
        uint16_t focalPlaneResolutionUnit = 0;
        char *originalTime = nullptr;
        char *serial = nullptr;
        if (!metadata->hasExposureTime
            && TIFFGetField(tiff, EXIFTAG_EXPOSURETIME, &exposureTime)
            && std::isfinite(exposureTime) && exposureTime > 0.0f) {
            metadata->exposureTimeSeconds = exposureTime;
            metadata->hasExposureTime = true;
        }
        if (!metadata->hasIsoSpeed
            && TIFFGetField(tiff, EXIFTAG_ISOSPEEDRATINGS,
                         &isoCount, &isoValues)
            && isoValues && isoCount > 0 && isoValues[0] > 0) {
            metadata->isoSpeed = isoValues[0];
            metadata->hasIsoSpeed = true;
        }
        if (!metadata->hasIsoSpeed
            && TIFFGetField(tiff, EXIFTAG_ISOSPEED, &isoSpeed32)
            && isoSpeed32 > 0) {
            metadata->isoSpeed = isoSpeed32;
            metadata->hasIsoSpeed = true;
        }
        if (!metadata->hasIsoSpeed
            && TIFFGetField(tiff, EXIFTAG_RECOMMENDEDEXPOSUREINDEX,
                            &isoSpeed32)
            && isoSpeed32 > 0) {
            metadata->isoSpeed = isoSpeed32;
            metadata->hasIsoSpeed = true;
        }
        if (TIFFGetField(tiff, EXIFTAG_FOCALLENGTH, &focalLength)
            && std::isfinite(focalLength) && focalLength > 0.0f) {
            metadata->focalLengthMillimetres = focalLength;
            metadata->hasFocalLength = true;
        }
        if (TIFFGetField(tiff, EXIFTAG_FOCALPLANEXRESOLUTION,
                         &focalPlaneResolutionX)
            && TIFFGetField(tiff, EXIFTAG_FOCALPLANEYRESOLUTION,
                            &focalPlaneResolutionY)
            && TIFFGetField(tiff, EXIFTAG_FOCALPLANERESOLUTIONUNIT,
                            &focalPlaneResolutionUnit)
            && std::isfinite(focalPlaneResolutionX)
            && std::isfinite(focalPlaneResolutionY)
            && focalPlaneResolutionX > 0.0f && focalPlaneResolutionY > 0.0f) {
            double unitsPerMillimetre = 0.0;
            switch (focalPlaneResolutionUnit) {
            case 2: unitsPerMillimetre = 1.0 / 25.4; break; // inch
            case 3: unitsPerMillimetre = 1.0 / 10.0; break; // centimetre
            case 4: unitsPerMillimetre = 1.0; break;        // millimetre
            case 5: unitsPerMillimetre = 1000.0; break;     // micrometre
            default: break;
            }
            if (unitsPerMillimetre > 0.0) {
                metadata->focalPlanePixelsPerMillimetreX =
                    focalPlaneResolutionX * unitsPerMillimetre;
                metadata->focalPlanePixelsPerMillimetreY =
                    focalPlaneResolutionY * unitsPerMillimetre;
                metadata->hasFocalPlaneResolution = true;
            }
        }
        if (TIFFGetField(tiff, EXIFTAG_DATETIMEORIGINAL, &originalTime)) {
            metadata->captureTime = textValue(originalTime);
        }
        if (TIFFGetField(tiff, EXIFTAG_BODYSERIALNUMBER, &serial)) {
            metadata->cameraSerial = textValue(serial);
        }
    }

    if (gpsOffset != 0
        && TIFFReadGPSDirectory(tiff, static_cast<toff_t>(gpsOffset))) {
        char *latitudeRef = nullptr;
        char *longitudeRef = nullptr;
        double *latitudeParts = nullptr;
        double *longitudeParts = nullptr;
        if (TIFFGetField(tiff, GPSTAG_LATITUDEREF, &latitudeRef)
            && TIFFGetField(tiff, GPSTAG_LATITUDE, &latitudeParts)
            && TIFFGetField(tiff, GPSTAG_LONGITUDEREF, &longitudeRef)
            && TIFFGetField(tiff, GPSTAG_LONGITUDE, &longitudeParts)
            && latitudeRef && longitudeRef && latitudeParts && longitudeParts) {
            metadata->gps.latitudeDegrees = latitudeParts[0]
                + latitudeParts[1] / 60.0 + latitudeParts[2] / 3600.0;
            metadata->gps.longitudeDegrees = longitudeParts[0]
                + longitudeParts[1] / 60.0 + longitudeParts[2] / 3600.0;
            if (latitudeRef[0] == 'S' || latitudeRef[0] == 's') {
                metadata->gps.latitudeDegrees = -metadata->gps.latitudeDegrees;
            }
            if (longitudeRef[0] == 'W' || longitudeRef[0] == 'w') {
                metadata->gps.longitudeDegrees = -metadata->gps.longitudeDegrees;
            }
            metadata->hasGps = std::isfinite(metadata->gps.latitudeDegrees)
                               && std::isfinite(metadata->gps.longitudeDegrees)
                               && std::abs(metadata->gps.latitudeDegrees) <= 90.0
                               && std::abs(metadata->gps.longitudeDegrees) <= 180.0;
            uint8_t altitudeReference = 0;
            double altitude = 0.0;
            if (metadata->hasGps
                && TIFFGetField(tiff, GPSTAG_ALTITUDEREF, &altitudeReference)
                && TIFFGetField(tiff, GPSTAG_ALTITUDE, &altitude)
                && std::isfinite(altitude)) {
                metadata->gps.altitudeMetres = altitudeReference == 1
                                                   ? -altitude : altitude;
                metadata->hasGpsAltitude = true;
            }
        }
    }

    TIFFClose(tiff);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
#else
    if (errorMessage) {
        *errorMessage = "This build does not include libtiff metadata support.";
    }
    Q_UNUSED(filePath);
    return false;
#endif
}

} // namespace kestrel
