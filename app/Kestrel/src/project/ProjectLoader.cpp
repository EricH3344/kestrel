#include "project/ProjectLoader.h"
#include <QFile>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

#include <cmath>

ProjectLoader::ProjectLoader(QObject *parent)
    : QObject(parent)
{
}

bool ProjectLoader::isValidProjectFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    quint32 magicNumber;
    stream >> magicNumber;
    
    file.close();
    
    // Check for "KPRO" magic number
    return magicNumber == 0x4B50524F;
}

ProjectData ProjectLoader::loadProject(const QString &kprojFilePath)
{
    ProjectData data;
    
    QFile file(kprojFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit projectLoadFailed("Cannot open project file");
        return data;
    }
    
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // Magic number
    quint32 magicNumber;
    stream >> magicNumber;
    
    if (magicNumber != 0x4B50524F) { // "KPRO"
        emit projectLoadFailed("Invalid project file format");
        file.close();
        return data;
    }
    
    // Version
    quint16 majorVersion, minorVersion;
    stream >> majorVersion >> minorVersion;
    
    if (majorVersion != 1) {
        emit projectLoadFailed("Unsupported project file version");
        file.close();
        return data;
    }
    
    // Project metadata
    stream >> data.projectName;
    stream >> data.projectPath;
    stream >> data.created;
    
    // Directory structure
    stream >> data.rawImagesPath;
    stream >> data.processedImagesPath;
    stream >> data.outputPath;
    stream >> data.metadataPath;
    
    // Imported files (efficient even with thousands)
    quint32 fileCount;
    stream >> fileCount;
    if (stream.status() != QDataStream::Ok || fileCount > 1000000) {
        emit projectLoadFailed("Project file is incomplete or contains an invalid image count");
        return {};
    }
    data.importedFiles.reserve(fileCount);
    for (quint32 i = 0; i < fileCount; ++i) {
        QString filePath;
        stream >> filePath;
        data.importedFiles.append(filePath);
    }
    
    if (stream.status() != QDataStream::Ok || data.projectName.isEmpty()) {
        emit projectLoadFailed("Project file is incomplete or corrupt");
        return {};
    }
    file.close();
    
    emit projectLoaded(data.projectName);
    return data;
}

QVariantMap ProjectLoader::openProject(const QString &kprojFilePath)
{
    const ProjectData data = loadProject(kprojFilePath);
    if (data.projectName.isEmpty()) {
        return {};
    }

    // Use the selected file's location: the project directory may have moved
    // since its absolute path was saved inside the .kproj file.
    const QString projectPath = QFileInfo(kprojFilePath).absolutePath();
    const QString previewPath = QDir(projectPath).filePath(
        "processed_images/odm/odm_orthophoto/odm_rgb_preview.png");
    QVariantMap project;
    project.insert("projectName", data.projectName);
    project.insert("projectPath", projectPath);
    project.insert("hasOrthophoto", QFileInfo(QDir(projectPath).filePath(
        "processed_images/odm/odm_orthophoto/odm_orthophoto.tif")).isFile());
    project.insert("previewUrl", QFileInfo(previewPath).isFile()
        ? QUrl::fromLocalFile(previewPath).toString() : QString());
    return project;
}

QVariantMap ProjectLoader::mapMetadata(const QString &projectPath)
{
    const QString previewPath = QDir(projectPath).filePath(
        "processed_images/odm/odm_orthophoto/odm_rgb_preview.png");
    const QSize imageSize = QImageReader(previewPath).size();
    QFile sidecar(previewPath + ".aux.xml");
    if (!imageSize.isValid() || !sidecar.open(QIODevice::ReadOnly)) {
        return {};
    }

    QXmlStreamReader xml(&sidecar);
    QString transform;
    QString srs;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("GeoTransform")) {
            transform = xml.readElementText();
        } else if (xml.isStartElement() && xml.name() == QStringLiteral("SRS")) {
            srs = xml.readElementText();
        }
    }
    if (xml.hasError() || transform.isEmpty()) {
        return {};
    }
    const QStringList parts = transform.split(',', Qt::SkipEmptyParts);
    if (parts.size() != 6) {
        return {};
    }
    double values[6]{};
    for (int i = 0; i < 6; ++i) {
        bool valid = false;
        values[i] = parts[i].trimmed().toDouble(&valid);
        if (!valid || !std::isfinite(values[i])) {
            return {};
        }
    }
    if (std::abs(values[1] * values[5] - values[2] * values[4]) < 1e-20) {
        return {};
    }

    QString epsg;
    const QRegularExpression epsgPattern(
        "(?:AUTHORITY\\[\"EPSG\",\"(\\d+)\"\\]|ID\\[\"EPSG\",(\\d+)\\])");
    auto matches = epsgPattern.globalMatch(srs);
    while (matches.hasNext()) {
        const auto match = matches.next();
        epsg = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    }
    QString unit = "units";
    if (srs.contains("UNIT[\"metre\"", Qt::CaseInsensitive)
        || srs.contains("LENGTHUNIT[\"metre\"", Qt::CaseInsensitive)) {
        unit = "m";
    } else if (srs.contains("foot", Qt::CaseInsensitive)) {
        unit = "ft";
    } else if (srs.startsWith("GEOGCS") || srs.startsWith("GEOGCRS")) {
        unit = "deg";
    }

    return {{"width", imageSize.width()}, {"height", imageSize.height()},
            {"originX", values[0]}, {"pixelWidth", values[1]},
            {"rotationX", values[2]}, {"originY", values[3]},
            {"rotationY", values[4]}, {"pixelHeight", values[5]},
            {"crs", epsg.isEmpty() ? QStringLiteral("Unknown CRS") : "EPSG:" + epsg},
            {"unit", unit}};
}
