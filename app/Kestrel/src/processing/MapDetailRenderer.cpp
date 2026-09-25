#include "processing/MapDetailRenderer.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {
QString odmHome()
{
    const QString configured = qEnvironmentVariable("KESTREL_ODM_HOME");
    if (!configured.isEmpty())
        return QDir(configured).absolutePath();
#ifdef Q_OS_WIN
    return QStringLiteral("C:/ODM");
#else
    return QStringLiteral("/opt/odm");
#endif
}

bool runGdal(const QString &program, const QStringList &arguments, QByteArray *output)
{
    QProcess process;
#ifdef Q_OS_WIN
    const QString environment = QDir(odmHome()).filePath("win32env.bat");
    const QString shell = QStandardPaths::findExecutable("cmd.exe");
    if (shell.isEmpty() || !QFileInfo(environment).isFile())
        return false;
    process.setProgram(shell);
    QStringList command{"/d", "/c", "call", QDir::toNativeSeparators(environment),
                        "&&", QDir::toNativeSeparators(program)};
    command += arguments;
    process.setArguments(command);
#else
    process.setProgram(program);
    process.setArguments(arguments);
#endif
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(30000))
        return false;
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    *output = process.readAll();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString gdalTool(const QString &name)
{
#ifdef Q_OS_WIN
    const QString home = odmHome();
    QString path = QDir(home).filePath(".pixi/envs/gpu-prod/Library/bin/" + name + ".exe");
    if (!QFileInfo(path).isFile())
        path = QDir(home).filePath("SuperBuild/install/bin/" + name + ".exe");
    return QFileInfo(path).isFile() ? path : QString();
#else
    return QStandardPaths::findExecutable(name);
#endif
}
}

MapDetailRenderer::MapDetailRenderer(QObject *parent) : QObject(parent) {}

void MapDetailRenderer::request(const QString &projectPath, double left, double top,
                                double width, double height, int outputWidth, int outputHeight)
{
    if (projectPath.isEmpty() || !std::isfinite(left) || !std::isfinite(top)
        || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0
        || outputWidth <= 0 || outputHeight <= 0 || !m_directory.isValid())
        return;
    m_latest = {projectPath, left, top, width, height,
                std::min(outputWidth, 4096), std::min(outputHeight, 4096), ++m_serial};
    if (!m_busy)
        start(m_latest);
}

void MapDetailRenderer::start(const Request &request)
{
    m_busy = true;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, request] {
        const Result result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        if (request.serial == m_latest.serial) {
            if (result.error.isEmpty()) {
                m_images.append(result.url.toLocalFile());
                emit detailReady(request.projectPath, result.url, request.left, request.top,
                                 request.width, request.height);
                while (m_images.size() > 8)
                    QFile::remove(m_images.takeFirst());
            } else {
                emit detailFailed(result.error);
            }
        } else if (!result.url.isEmpty()) {
            QFile::remove(result.url.toLocalFile());
        }
        if (request.serial != m_latest.serial)
            start(m_latest);
    });
    watcher->setFuture(QtConcurrent::run([request, directory = m_directory.path()] {
        return render(request, directory);
    }));
}

MapDetailRenderer::Result MapDetailRenderer::render(const Request &request,
                                                    const QString &directory)
{
    const QString tif = QDir(request.projectPath).filePath(
        "processed_images/odm/odm_orthophoto/odm_orthophoto.tif");
    if (!QFileInfo(tif).isFile())
        return {{}, "The ODM GeoTIFF is missing."};
    const QString infoTool = gdalTool("gdalinfo");
    const QString translateTool = gdalTool("gdal_translate");
    if (infoTool.isEmpty() || translateTool.isEmpty())
        return {{}, "Native ODM's GDAL tools are needed for full-resolution map detail."};

    QByteArray info;
    if (!runGdal(infoTool, {tif}, &info))
        return {{}, "Could not inspect the ODM GeoTIFF."};
    const QString description = QString::fromUtf8(info);
    const auto size = QRegularExpression("Size is (\\d+), (\\d+)").match(description);
    const auto type = QRegularExpression("Band 1 Block=[^\\n]*Type=(\\w+)").match(description);
    if (!size.hasMatch() || !type.hasMatch())
        return {{}, "Could not read the ODM GeoTIFF dimensions or RGB type."};
    const int rasterWidth = size.captured(1).toInt();
    const int rasterHeight = size.captured(2).toInt();
    if (rasterWidth <= 0 || rasterHeight <= 0)
        return {{}, "The ODM GeoTIFF dimensions are invalid."};
    const int x = std::clamp(static_cast<int>(std::floor(request.left * rasterWidth)), 0, rasterWidth - 1);
    const int y = std::clamp(static_cast<int>(std::floor(request.top * rasterHeight)), 0, rasterHeight - 1);
    const int right = std::clamp(static_cast<int>(std::ceil((request.left + request.width) * rasterWidth)), x + 1, rasterWidth);
    const int bottom = std::clamp(static_cast<int>(std::ceil((request.top + request.height) * rasterHeight)), y + 1, rasterHeight);
    QStringList args{"-of", "PNG", "-ot", "Byte", "-b", "1", "-b", "2", "-b", "3"};
    const auto alpha = QRegularExpression("Band (\\d+) Block=[^\\n]*ColorInterp=Alpha").match(description);
    if (alpha.hasMatch())
        args += {"-b", alpha.captured(1)};
    if (type.captured(1) == "UInt16") {
        for (int band = 1; band <= 3; ++band)
            args += {"-scale_" + QString::number(band), "0", "65535", "0", "255"};
    } else if (type.captured(1) != "Byte") {
        return {{}, "Unsupported ODM GeoTIFF RGB type: " + type.captured(1)};
    }
    const QString output = QDir(directory).filePath(QString::number(request.serial) + ".png");
    args += {"-srcwin", QString::number(x), QString::number(y),
             QString::number(right - x), QString::number(bottom - y),
             "-outsize", QString::number(request.outputWidth),
             QString::number(request.outputHeight), "-ovr", "NONE", "-colorinterp",
             alpha.hasMatch() ? "red,green,blue,alpha" : "red,green,blue", tif, output};
    QByteArray log;
    if (!runGdal(translateTool, args, &log) || !QFileInfo(output).isFile())
        return {{}, "Could not render GeoTIFF detail: " + QString::fromUtf8(log).right(300)};
    return {QUrl::fromLocalFile(output), {}};
}
