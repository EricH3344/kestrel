#include "project/ProjectLoader.h"
#include <QFile>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDirIterator>
#include <QEventLoop>
#include <QImageReader>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantList>
#include <QXmlStreamReader>

#include <cmath>
#include <algorithm>

namespace {
QString flightRoot(const QString &projectPath, const FlightData &flight)
{
    return flight.relativePath == "." ? projectPath
                                       : QDir(projectPath).filePath(flight.relativePath);
}

QString gdalInfo(const QString &file)
{
#ifdef Q_OS_WIN
    const QString home = qEnvironmentVariable("KESTREL_ODM_HOME", "C:/ODM");
    QString tool = QDir(home).filePath(".pixi/envs/gpu-prod/Library/bin/gdalinfo.exe");
    if (!QFileInfo(tool).isFile())
        tool = QDir(home).filePath("SuperBuild/install/bin/gdalinfo.exe");
    const QString environment = QDir(home).filePath("win32env.bat");
    if (!QFileInfo(tool).isFile() || !QFileInfo(environment).isFile())
        return {};
    QProcess process;
    process.setProgram(QStandardPaths::findExecutable("cmd.exe"));
    process.setArguments({"/d", "/c", "call", QDir::toNativeSeparators(environment),
                          "&&", QDir::toNativeSeparators(tool), "-mdd", "all", file});
#else
    const QString tool = QStandardPaths::findExecutable("gdalinfo");
    if (tool.isEmpty())
        return {};
    QProcess process;
    process.setProgram(tool);
    process.setArguments({"-mdd", "all", file});
#endif
    process.start();
    if (!process.waitForStarted(3000) || !process.waitForFinished(30000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAllStandardOutput());
}

QString capture(const QString &description, const QString &pattern)
{
    return QRegularExpression(pattern).match(description).captured(1).trimmed();
}
}

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
    
    if (majorVersion != 1 || minorVersion > 1) {
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

    if (minorVersion == 1) {
        quint32 flightCount = 0;
        stream >> flightCount;
        if (stream.status() != QDataStream::Ok || flightCount == 0 || flightCount > 10000) {
            emit projectLoadFailed("Project file has an invalid flight count");
            return {};
        }
        for (quint32 i = 0; i < flightCount; ++i) {
            FlightData flight;
            stream >> flight.id >> flight.relativePath >> flight.capturedAt
                   >> flight.structure >> flight.importedFiles;
            if (flight.id.isEmpty()
                || (flight.relativePath != "."
                    && (!flight.relativePath.startsWith("flights/")
                        || flight.relativePath.contains("..")
                        || flight.relativePath.contains('\\')))) {
                emit projectLoadFailed("Project file has an invalid flight path");
                return {};
            }
            data.flights.append(flight);
        }
    } else {
        // Version 1.0 projects used the project root as their only flight.
        data.flights.append({"original", ".", data.created, {}, data.importedFiles});
    }
    file.close();
    
    emit projectLoaded(data.projectName);
    return data;
}

bool ProjectLoader::writeProject(const QString &kprojFilePath, const ProjectData &data)
{
    QSaveFile file(kprojFilePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x4B50524F) << quint16(1) << quint16(1);
    stream << data.projectName << data.projectPath << data.created;
    stream << data.rawImagesPath << data.processedImagesPath
           << data.outputPath << data.metadataPath;
    stream << quint32(data.importedFiles.size());
    for (const QString &path : data.importedFiles)
        stream << path;
    stream << quint32(data.flights.size());
    for (const FlightData &flight : data.flights)
        stream << flight.id << flight.relativePath << flight.capturedAt
               << flight.structure << flight.importedFiles;
    return stream.status() == QDataStream::Ok && file.commit();
}

FlightData ProjectLoader::inspectFlight(const QStringList &files)
{
    FlightData flight;
    flight.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    flight.capturedAt = QDateTime::currentDateTime();
    QStringList sorted = files;
    sorted.sort(Qt::CaseInsensitive);
    QDateTime earliest;
    const int samples = std::min(64, static_cast<int>(sorted.size()));
    for (int i = 0; i < samples; ++i) {
        // Inspect the beginning and the full span of the flight. This catches
        // interleaved as well as grouped camera bands without processing every frame.
        const int index = i < samples / 2 ? i
            : static_cast<int>(static_cast<qint64>(i - samples / 2) * (sorted.size() - 1)
                               / std::max(1, samples - samples / 2 - 1));
        const QString &path = sorted.at(index);
        const QString description = gdalInfo(path);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (description.isEmpty())
            continue;
        const QString size = capture(description, "Size is (\\d+, \\d+)");
        const QString type = capture(description, "Band 1 Block=[^\\n]*Type=(\\w+)");
        int bandCount = 0;
        auto bands = QRegularExpression("^Band \\d+ Block=",
                                        QRegularExpression::MultilineOption).globalMatch(description);
        while (bands.hasNext()) {
            bands.next();
            ++bandCount;
        }
        const QString rig = capture(description, "<Camera:RigName>([^<]+)");
        const QString band = capture(description, "<Camera:BandName>([^<]+)");
        const QString wavelength = capture(description, "<Camera:CentralWavelength>([^<]+)");
        if (!size.isEmpty() && !type.isEmpty()) {
            const QString layout = QString("%1|%2|%3|%4|%5|%6")
                                       .arg(rig, band, wavelength, size, type,
                                            QString::number(bandCount));
            if (!flight.structure.contains(layout))
                flight.structure.append(layout);
        }
        const QString timestamp = capture(description, "EXIF_DateTimeOriginal=(\\d{4}:\\d{2}:\\d{2} \\d{2}:\\d{2}:\\d{2})");
        const QDateTime captured = QDateTime::fromString(timestamp, "yyyy:MM:dd HH:mm:ss");
        if (captured.isValid() && (!earliest.isValid() || captured < earliest))
            earliest = captured;
    }
    flight.structure.sort(Qt::CaseInsensitive);
    if (earliest.isValid())
        flight.capturedAt = earliest;
    for (const QString &file : files)
        flight.importedFiles.append(QFileInfo(file).fileName());
    return flight;
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
    QList<FlightData> flights = data.flights;
    const auto baseline = std::min_element(flights.begin(), flights.end(),
        [](const FlightData &a, const FlightData &b) { return a.capturedAt < b.capturedAt; });
    const QStringList baselineStructure = baseline == flights.end() ? QStringList{} : baseline->structure;
    const QString baselineId = baseline == flights.end() ? QString{} : baseline->id;
    std::stable_sort(flights.begin(), flights.end(),
        [](const FlightData &a, const FlightData &b) { return a.capturedAt > b.capturedAt; });
    QVariantList entries;
    for (const FlightData &flight : flights) {
        const QString root = flightRoot(projectPath, flight);
        const QString preview = QDir(root).filePath(
            "processed_images/odm/odm_orthophoto/odm_rgb_preview.png");
        const QString tif = QDir(root).filePath(
            "processed_images/odm/odm_orthophoto/odm_orthophoto.tif");
        const QString compatibility = flight.id == baselineId ? "baseline"
            : baselineStructure.isEmpty() || flight.structure.isEmpty() ? "unknown"
            : flight.structure == baselineStructure ? "compatible" : "different";
        entries.append(QVariantMap{{"id", flight.id}, {"date", flight.capturedAt.toString("yyyy-MM-dd")},
                                   {"flightPath", root}, {"compatibility", compatibility},
                                   {"hasOrthophoto", QFileInfo(tif).isFile()},
                                   {"previewUrl", QFileInfo(preview).isFile()
                                       ? QUrl::fromLocalFile(preview).toString() : QString()}});
    }
    const QString selectedRoot = flightRoot(projectPath, flights.first());
    const QString previewPath = QDir(selectedRoot).filePath(
        "processed_images/odm/odm_orthophoto/odm_rgb_preview.png");
    QVariantMap project;
    project.insert("projectName", data.projectName);
    project.insert("projectPath", projectPath);
    project.insert("projectFile", kprojFilePath);
    project.insert("flightPath", selectedRoot);
    project.insert("flights", entries);
    project.insert("hasOrthophoto", QFileInfo(QDir(selectedRoot).filePath(
        "processed_images/odm/odm_orthophoto/odm_orthophoto.tif")).isFile());
    project.insert("previewUrl", QFileInfo(previewPath).isFile()
        ? QUrl::fromLocalFile(previewPath).toString() : QString());
    return project;
}

QVariantMap ProjectLoader::addFlight(const QString &kprojFilePath, const QStringList &files)
{
    ProjectData data = loadProject(kprojFilePath);
    if (data.projectName.isEmpty())
        return {};
    if (files.isEmpty() || files.size() > 1000000) {
        emit projectLoadFailed("Select a folder containing TIFF flight images");
        return {};
    }
    const QString projectPath = QFileInfo(kprojFilePath).absolutePath();
    QSet<QString> names;
    for (const QString &file : files) {
        const QString suffix = QFileInfo(file).suffix().toLower();
        if (!QFileInfo(file).isFile() || (suffix != "tif" && suffix != "tiff")) {
            emit projectLoadFailed("The selected flight contains a missing or non-TIFF file");
            return {};
        }
        const QString name = QFileInfo(file).fileName().toCaseFolded();
        if (names.contains(name)) {
            emit projectLoadFailed("The flight contains duplicate TIFF filenames: "
                                   + QFileInfo(file).fileName());
            return {};
        }
        names.insert(name);
    }
    if (data.flights.size() == 1 && data.flights.first().structure.isEmpty()) {
        QStringList original;
        QDirIterator images(QDir(projectPath).filePath("raw_images"),
                            {"*.tif", "*.tiff", "*.TIF", "*.TIFF"}, QDir::Files);
        while (images.hasNext())
            original.append(images.next());
        if (!original.isEmpty()) {
            const FlightData inspected = inspectFlight(original);
            data.flights.first().structure = inspected.structure;
            data.flights.first().capturedAt = inspected.capturedAt;
        }
    }
    FlightData flight = inspectFlight(files);
    flight.relativePath = "flights/" + flight.id;
    const QString root = flightRoot(projectPath, flight);
    const QString raw = QDir(root).filePath("raw_images");
    if (!QDir().mkpath(raw) || !QDir().mkpath(QDir(root).filePath("processed_images"))) {
        emit projectLoadFailed("Could not create the new flight directory");
        return {};
    }
    for (const QString &file : files) {
        const QString target = QDir(raw).filePath(QFileInfo(file).fileName());
        if (QFileInfo::exists(target) || !QFile::copy(file, target)) {
            emit projectLoadFailed("Could not copy flight image (possibly a duplicate filename): "
                                   + QFileInfo(file).fileName());
            return {};
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    data.projectPath = projectPath;
    data.rawImagesPath = QDir(projectPath).filePath("raw_images");
    data.processedImagesPath = QDir(projectPath).filePath("processed_images");
    data.outputPath = QDir(projectPath).filePath("output");
    data.metadataPath = QDir(projectPath).filePath("metadata");
    data.flights.append(flight);
    if (!writeProject(kprojFilePath, data)) {
        emit projectLoadFailed("Could not save the updated project file");
        return {};
    }
    return {{"flightPath", root}, {"flightId", flight.id}};
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
