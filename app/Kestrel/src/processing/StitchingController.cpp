#include "processing/StitchingController.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QThread>
#include <QUdpSocket>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>

namespace {

QString odmHome()
{
    const QString configured = qEnvironmentVariable("KESTREL_ODM_HOME");
    if (!configured.isEmpty()) {
        return QDir(configured).absolutePath();
    }
#ifdef Q_OS_WIN
    return QStringLiteral("C:/ODM");
#else
    return QStringLiteral("/opt/odm");
#endif
}

bool unsafeForCmd(const QString &value)
{
    return value.contains('"') || value.contains('%') || value.contains('!')
           || value.contains('\n') || value.contains('\r');
}

QString runProcess(const QString &program, const QStringList &arguments,
                   const QString &logPath, int *exitCode,
                   const std::function<void(const QString &)> &onLine,
                   const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment(),
                   const std::function<void(double)> &onProgress = {})
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProcessEnvironment(environment);

    // ODM broadcasts its pipeline percentage as PGUP/<pid>/<dataset>/<value>
    // over localhost UDP. Bind before launching so early updates are not lost.
    std::unique_ptr<QUdpSocket> progressSocket;
    if (onProgress) {
        progressSocket = std::make_unique<QUdpSocket>();
        if (!progressSocket->bind(QHostAddress::LocalHost, 6367,
                                  QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            progressSocket.reset();
        }
    }

    QFile log;
    if (!logPath.isEmpty()) {
        log.setFileName(logPath);
        if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            *exitCode = -1;
            return "Could not create ODM log: " + logPath;
        }
    }

    process.start();
    if (!process.waitForStarted(30000)) {
        *exitCode = -1;
        return QString("Could not start %1 (process error %2): %3")
            .arg(program).arg(static_cast<int>(process.error())).arg(process.errorString());
    }

    QByteArray pending;
    QString lastOutput;
    double lastProgress = 0.0;
    const auto drainProgress = [&] {
        if (!progressSocket) {
            return;
        }
        // QtConcurrent workers do not run an event loop, so pump the UDP
        // socket explicitly instead of relying on readyRead signals.
        progressSocket->waitForReadyRead(1);
        while (progressSocket->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(progressSocket->pendingDatagramSize());
            progressSocket->readDatagram(datagram.data(), datagram.size());
            const QList<QByteArray> fields = datagram.trimmed().split('/');
            // ODM broadcasts from its Python child, not the cmd.exe launcher.
            if (fields.size() != 4 || fields[0] != "PGUP" || fields[2] != "odm") {
                continue;
            }
            bool valid = false;
            const double value = fields[3].toDouble(&valid);
            if (valid && std::isfinite(value) && value > lastProgress) {
                lastProgress = std::clamp(value, 0.0, 100.0);
                onProgress(lastProgress);
            }
        }
    };
    const auto drain = [&] {
        const QByteArray chunk = process.readAll();
        if (log.isOpen()) {
            log.write(chunk);
        }
        pending += chunk;
        int newline = -1;
        while ((newline = pending.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(pending.left(newline)).trimmed();
            pending.remove(0, newline + 1);
            if (!line.isEmpty()) {
                lastOutput = line;
                if (onLine) {
                    onLine(line);
                }
            }
        }
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(1000);
        drain();
        drainProgress();
    }
    drain();
    drainProgress();
    if (!pending.trimmed().isEmpty()) {
        lastOutput = QString::fromUtf8(pending).trimmed();
        if (onLine) {
            onLine(lastOutput);
        }
    }
    process.waitForFinished();
    *exitCode = process.exitStatus() == QProcess::NormalExit
        ? process.exitCode() : -1;
    return lastOutput;
}

} // namespace

StitchingController::StitchingController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<MosaicResult>::finished, this, [this] {
        const MosaicResult result = m_watcher.result();
        if (result.errorMessage.isEmpty()) {
            emit stitchingCompleted(result.projectPath, result.previewUrl);
        } else {
            emit stitchingFailed(result.projectPath, result.errorMessage);
        }
    });
}

StitchingController::~StitchingController()
{
    m_watcher.waitForFinished();
}

void StitchingController::stitchProject(const QString &projectPath)
{
    if (m_watcher.isRunning()) {
        emit stitchingFailed(projectPath, "Another ODM job is already running.");
        return;
    }
    emit stitchingStarted(projectPath);
    m_watcher.setFuture(QtConcurrent::run([this, projectPath] {
        return runOdm(projectPath, false);
    }));
}

void StitchingController::refreshPreview(const QString &projectPath)
{
    if (m_watcher.isRunning()) {
        emit stitchingFailed(projectPath, "Another processing job is already running.");
        return;
    }
    emit stitchingStarted(projectPath);
    m_watcher.setFuture(QtConcurrent::run([this, projectPath] {
        return runOdm(projectPath, true);
    }));
}

void StitchingController::postStatus(const QString &message)
{
    QMetaObject::invokeMethod(this, [this, message] {
        emit stitchingStatusChanged(message);
    }, Qt::QueuedConnection);
}

void StitchingController::postProgress(double percent)
{
    QMetaObject::invokeMethod(this, [this, percent] {
        emit stitchingProgressChanged(percent);
    }, Qt::QueuedConnection);
}

StitchingController::MosaicResult StitchingController::runOdm(const QString &projectPath,
                                                               bool previewOnly)
{
    MosaicResult result;
    result.projectPath = projectPath;
    const QDir project(QFileInfo(projectPath).absoluteFilePath());
    const QString imagesPath = project.filePath("raw_images");
    if (!previewOnly && !QDir(imagesPath).exists()) {
        result.errorMessage = "The project has no raw_images folder.";
        return result;
    }

    int imageCount = 0;
    if (!previewOnly) {
        QDirIterator images(imagesPath, {"*.tif", "*.tiff", "*.TIF", "*.TIFF"},
                            QDir::Files);
        while (images.hasNext()) {
            images.next();
            ++imageCount;
        }
    }
    if (!previewOnly && imageCount == 0) {
        result.errorMessage = "No TIFF images were found in raw_images.";
        return result;
    }

    const QString home = odmHome();
#ifdef Q_OS_WIN
    const QString runner = QDir(home).filePath("run.bat");
    QString gdal = QDir(home).filePath(".pixi/envs/gpu-prod/Library/bin/gdal_translate.exe");
    if (!QFileInfo(gdal).isFile()) {
        gdal = QDir(home).filePath("SuperBuild/install/bin/gdal_translate.exe");
    }
    const QString environmentScript = QDir(home).filePath("win32env.bat");
    const QString shell = QStandardPaths::findExecutable("cmd.exe");
    if (!QFileInfo(runner).isFile() || !QFileInfo(environmentScript).isFile()
        || shell.isEmpty()) {
        result.errorMessage = "Native ODM was not found. Install the ODM Windows release, then set "
                              "KESTREL_ODM_HOME to its installation folder (normally C:\\ODM).";
        return result;
    }
    if (!QFileInfo(gdal).isFile()) {
        result.errorMessage = "Native ODM is missing gdal_translate.exe: " + gdal;
        return result;
    }
#else
    const QString runner = QDir(home).filePath("run.sh");
    const QString gdal = QStandardPaths::findExecutable("gdal_translate");
    if (!QFileInfo(runner).isFile()) {
        result.errorMessage = "Native ODM was not found. Set KESTREL_ODM_HOME to an ODM installation containing run.sh.";
        return result;
    }
#endif

    const QString outputRoot = project.filePath("processed_images");
    const QString odmProject = QDir(outputRoot).filePath("odm");
    if (!previewOnly && !QDir().mkpath(odmProject)) {
        result.errorMessage = "Could not create the ODM output folder: " + outputRoot;
        return result;
    }
    int exitCode = -1;
#ifdef Q_OS_WIN
    if (unsafeForCmd(runner) || unsafeForCmd(outputRoot)) {
        result.errorMessage = "ODM cannot launch from a path containing %, !, or a quotation mark.";
        return result;
    }
    QProcessEnvironment odmEnvironment = QProcessEnvironment::systemEnvironment();
    odmEnvironment.insert("ODM_NONINTERACTIVE", "1");
#endif
    if (!previewOnly) {
    const QString logPath = QDir(odmProject).filePath("odm-console.log");
    const QString odmImagesPath = QDir(odmProject).filePath("images");
    if (!QDir().mkpath(odmImagesPath)) {
        result.errorMessage = "Could not create ODM's images folder: " + odmImagesPath;
        return result;
    }
    QDirIterator sourceImages(imagesPath, {"*.tif", "*.tiff", "*.TIF", "*.TIFF"}, QDir::Files);
    while (sourceImages.hasNext()) {
        const QString source = sourceImages.next();
        const QString target = QDir(odmImagesPath).filePath(QFileInfo(source).fileName());
        if (QFileInfo::exists(target)) {
            continue;
        }
        std::error_code linkError;
#ifdef Q_OS_WIN
        std::filesystem::create_hard_link(std::filesystem::path(source.toStdWString()),
                                          std::filesystem::path(target.toStdWString()), linkError);
#else
        std::filesystem::create_hard_link(std::filesystem::u8path(source.toUtf8().constData()),
                                          std::filesystem::u8path(target.toUtf8().constData()), linkError);
#endif
        if (linkError && !QFile::copy(source, target)) {
            result.errorMessage = "Could not stage an ODM image: " + source;
            return result;
        }
    }

    const int concurrency = std::clamp(QThread::idealThreadCount() - 2, 1, 8);
    postStatus(QString("ODM: processing %1 imported TIFF images...").arg(imageCount));
    const QStringList odmArguments{
        "--project-path", outputRoot, "odm",
        "--auto-boundary",
        "--feature-quality", "ultra", "--pc-quality", "high",
        "--orthophoto-resolution", "1.0", "--dem-resolution", "2.0",
        "--dsm", "--skip-3dmodel",
        "--max-concurrency", QString::number(concurrency)};
#ifdef Q_OS_WIN
    const QString odmProgram = shell;
    QStringList launchArguments{"/d", "/c", "call", QDir::toNativeSeparators(runner)};
    launchArguments += odmArguments;
#else
    const QString odmProgram = runner;
    const QStringList launchArguments = odmArguments;
    const QProcessEnvironment odmEnvironment = QProcessEnvironment::systemEnvironment();
#endif
    const QString lastLine = runProcess(
        odmProgram, launchArguments, logPath, &exitCode,
        [this](const QString &line) {
            if (line.contains("[INFO]") || line.contains("[WARNING]")
                || line.contains("[ERROR]")) {
                postStatus("ODM: " + line.simplified().left(180));
            }
        }, odmEnvironment, [this](double percent) { postProgress(percent); });
    if (exitCode != 0) {
        result.errorMessage = QString("ODM failed (exit %1). %2 See %3")
                                  .arg(exitCode).arg(lastLine, logPath);
        return result;
    }
    }

    const QString orthophoto = QDir(odmProject).filePath(
        "odm_orthophoto/odm_orthophoto.tif");
    if (!QFileInfo::exists(orthophoto)) {
        result.errorMessage = "This project has no ODM orthophoto yet: " + orthophoto;
        return result;
    }

    postStatus("ODM: exporting the Map preview...");
    const QString preview = QDir(odmProject).filePath(
        "odm_orthophoto/odm_rgb_preview.png");
    QString rasterType;
    int alphaBand = 0;
    const auto inspectBand = [&rasterType, &alphaBand](const QString &line) {
        if (line.startsWith("Band 1 Block=") && line.contains("Type=")) {
            rasterType = line.section("Type=", 1).section(',', 0, 0).trimmed();
        }
        if (line.startsWith("Band ") && line.contains("ColorInterp=Alpha")) {
            alphaBand = line.section(' ', 1, 1).toInt();
        }
    };
#ifdef Q_OS_WIN
    const QString gdalInfo = QFileInfo(gdal).dir().filePath("gdalinfo.exe");
    if (!QFileInfo(gdalInfo).isFile()) {
        result.errorMessage = "Native ODM is missing gdalinfo.exe: " + gdalInfo;
        return result;
    }
    const auto runGdal = [&](const QString &tool, const QStringList &args,
                             const std::function<void(const QString &)> &onLine) {
        QStringList command{"/d", "/c", "call", QDir::toNativeSeparators(environmentScript),
                            "&&", QDir::toNativeSeparators(tool)};
        command += args;
        return runProcess(shell, command, {}, &exitCode, onLine, odmEnvironment);
    };
    runGdal(gdalInfo, {orthophoto}, inspectBand);
#else
    if (gdal.isEmpty()) {
        result.errorMessage = "Install GDAL's gdal_translate to export the ODM Map preview.";
        return result;
    }
    const QString gdalInfo = QStandardPaths::findExecutable("gdalinfo");
    if (gdalInfo.isEmpty()) {
        result.errorMessage = "Install GDAL's gdalinfo to inspect the ODM orthophoto.";
        return result;
    }
    runProcess(gdalInfo, {orthophoto}, {}, &exitCode, inspectBand);
#endif
    if (exitCode != 0 || rasterType.isEmpty()) {
        result.errorMessage = "Could not inspect the ODM orthophoto's RGB data type: " + orthophoto;
        return result;
    }
    QStringList pngArguments{"-of", "PNG", "-ot", "Byte", "-b", "1", "-b", "2", "-b", "3"};
    if (alphaBand > 0) {
        pngArguments += {"-b", QString::number(alphaBand)};
    }
    if (rasterType == "UInt16") {
        // Leave the alpha band unscaled: ODM commonly stores it as 0/255
        // even when the RGB channels themselves are UInt16.
        for (int band = 1; band <= 3; ++band) {
            pngArguments += {"-scale_" + QString::number(band), "0", "65535", "0", "255"};
        }
    } else if (rasterType != "Byte") {
        result.errorMessage = "Unsupported ODM orthophoto RGB data type: " + rasterType;
        return result;
    }
    // ODM's embedded overviews can be empty even when the full-resolution
    // GeoTIFF contains valid pixels. Always resample from the base image.
    const QString pendingPreview = preview + ".new.png";
    QFile::remove(pendingPreview);
    pngArguments += {"-outsize", "2048", "0", "-ovr", "NONE", "-colorinterp",
                     alphaBand > 0 ? "red,green,blue,alpha" : "red,green,blue",
                     orthophoto, pendingPreview};
#ifdef Q_OS_WIN
    const QString pngMessage = runGdal(gdal, pngArguments, {});
#else
    const QString pngMessage = runProcess(gdal, pngArguments, {}, &exitCode, {});
#endif
    if (exitCode != 0 || !QFileInfo::exists(pendingPreview)) {
        result.errorMessage = "ODM produced a GeoTIFF, but PNG preview export failed: "
                              + pngMessage + ". GeoTIFF: " + orthophoto;
        return result;
    }
    if ((QFileInfo::exists(preview) && !QFile::remove(preview))
        || !QFile::rename(pendingPreview, preview)) {
        result.errorMessage = "Could not replace the old mosaic preview: " + preview;
        return result;
    }
    QFile::remove(preview + ".aux.xml");
    QFile::rename(pendingPreview + ".aux.xml", preview + ".aux.xml");
    result.previewUrl = QUrl::fromLocalFile(preview);
    result.previewUrl.setQuery("v=" + QString::number(QDateTime::currentMSecsSinceEpoch()));
    return result;
}
