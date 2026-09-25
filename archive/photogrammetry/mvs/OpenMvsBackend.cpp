#include "photogrammetry/mvs/DensePointCloudBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace kestrel {
namespace {

namespace fs = std::filesystem;

fs::path filesystemPath(const QString &path)
{
#ifdef Q_OS_WIN
    return fs::path(path.toStdWString());
#else
    return fs::path(path.toStdString());
#endif
}

struct Quaternion
{
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct ColmapObservation
{
    uint64_t pointId = 0;
    cv::Point2d imagePoint;
};

struct ColmapPoint
{
    uint64_t id = 0;
    const SparsePoint *point = nullptr;
    std::vector<std::pair<uint32_t, size_t>> track;
};

enum class PlyScalarType
{
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Float32,
    Float64,
    Invalid
};

struct PlyProperty
{
    bool list = false;
    PlyScalarType countType = PlyScalarType::Invalid;
    PlyScalarType valueType = PlyScalarType::Invalid;
    std::string name;
};

void setError(std::string *errorMessage, const std::string &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
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

bool finiteVector(const cv::Vec3d &vector)
{
    return std::isfinite(vector[0]) && std::isfinite(vector[1])
        && std::isfinite(vector[2]);
}

bool hasDistortion(const cv::Vec<double, 5> &distortion)
{
    for (double coefficient : distortion.val) {
        if (!std::isfinite(coefficient)) {
            return false;
        }
        if (std::abs(coefficient) > 1e-12) {
            return true;
        }
    }
    return false;
}

bool finiteDistortion(const cv::Vec<double, 5> &distortion)
{
    for (double coefficient : distortion.val) {
        if (!std::isfinite(coefficient)) return false;
    }
    return true;
}

Quaternion quaternionFromRotation(const cv::Matx33d &rotation)
{
    Quaternion quaternion;
    const double trace = rotation(0, 0) + rotation(1, 1) + rotation(2, 2);
    if (trace > 0.0) {
        const double scale = std::sqrt(trace + 1.0) * 2.0;
        quaternion.w = 0.25 * scale;
        quaternion.x = (rotation(2, 1) - rotation(1, 2)) / scale;
        quaternion.y = (rotation(0, 2) - rotation(2, 0)) / scale;
        quaternion.z = (rotation(1, 0) - rotation(0, 1)) / scale;
    } else if (rotation(0, 0) > rotation(1, 1)
               && rotation(0, 0) > rotation(2, 2)) {
        const double scale = std::sqrt(
            1.0 + rotation(0, 0) - rotation(1, 1) - rotation(2, 2)) * 2.0;
        quaternion.w = (rotation(2, 1) - rotation(1, 2)) / scale;
        quaternion.x = 0.25 * scale;
        quaternion.y = (rotation(0, 1) + rotation(1, 0)) / scale;
        quaternion.z = (rotation(0, 2) + rotation(2, 0)) / scale;
    } else if (rotation(1, 1) > rotation(2, 2)) {
        const double scale = std::sqrt(
            1.0 + rotation(1, 1) - rotation(0, 0) - rotation(2, 2)) * 2.0;
        quaternion.w = (rotation(0, 2) - rotation(2, 0)) / scale;
        quaternion.x = (rotation(0, 1) + rotation(1, 0)) / scale;
        quaternion.y = 0.25 * scale;
        quaternion.z = (rotation(1, 2) + rotation(2, 1)) / scale;
    } else {
        const double scale = std::sqrt(
            1.0 + rotation(2, 2) - rotation(0, 0) - rotation(1, 1)) * 2.0;
        quaternion.w = (rotation(1, 0) - rotation(0, 1)) / scale;
        quaternion.x = (rotation(0, 2) + rotation(2, 0)) / scale;
        quaternion.y = (rotation(1, 2) + rotation(2, 1)) / scale;
        quaternion.z = 0.25 * scale;
    }
    const double norm = std::sqrt(
        quaternion.w * quaternion.w + quaternion.x * quaternion.x
        + quaternion.y * quaternion.y + quaternion.z * quaternion.z);
    if (norm > 0.0 && std::isfinite(norm)) {
        quaternion.w /= norm;
        quaternion.x /= norm;
        quaternion.y /= norm;
        quaternion.z /= norm;
    }
    if (quaternion.w < 0.0) {
        quaternion.w = -quaternion.w;
        quaternion.x = -quaternion.x;
        quaternion.y = -quaternion.y;
        quaternion.z = -quaternion.z;
    }
    return quaternion;
}

cv::Point2d undistortedPoint(const cv::Point2d &point,
                             const CameraCalibration &calibration)
{
    if (!hasDistortion(calibration.distortion)) {
        return point;
    }
    std::vector<cv::Point2d> source{point};
    std::vector<cv::Point2d> destination;
    cv::undistortPoints(source, destination, cv::Mat(calibration.intrinsic),
                        cv::Mat(calibration.distortion), cv::noArray(),
                        cv::Mat(calibration.intrinsic));
    return destination.empty() ? point : destination.front();
}

cv::Mat convertForOpenMvs(const cv::Mat &input,
                          const CameraCalibration &calibration)
{
    cv::Mat undistorted;
    if (hasDistortion(calibration.distortion)) {
        cv::undistort(input, undistorted, cv::Mat(calibration.intrinsic),
                      cv::Mat(calibration.distortion),
                      cv::Mat(calibration.intrinsic));
    } else {
        undistorted = input;
    }

    cv::Mat eightBit;
    if (undistorted.depth() == CV_8U) {
        eightBit = undistorted;
    } else {
        cv::Mat scalar;
        if (undistorted.channels() == 1) {
            scalar = undistorted;
        } else {
            cv::cvtColor(undistorted, scalar,
                         undistorted.channels() == 4
                             ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        }
        double minimum = 0.0;
        double maximum = 0.0;
        cv::minMaxLoc(scalar, &minimum, &maximum);
        if (!std::isfinite(minimum) || !std::isfinite(maximum)
            || maximum <= minimum) {
            scalar.convertTo(eightBit, CV_8U);
        } else {
            scalar.convertTo(eightBit, CV_8U,
                             255.0 / (maximum - minimum),
                             -minimum * 255.0 / (maximum - minimum));
        }
    }

    cv::Mat colour;
    if (eightBit.channels() == 1) {
        cv::cvtColor(eightBit, colour, cv::COLOR_GRAY2BGR);
    } else if (eightBit.channels() == 4) {
        cv::cvtColor(eightBit, colour, cv::COLOR_BGRA2BGR);
    } else {
        colour = eightBit;
    }
    return colour;
}

QString executableName(const QString &baseName)
{
#ifdef Q_OS_WIN
    return baseName + ".exe";
#else
    return baseName;
#endif
}

QStringList executableSearchDirectories(
    const DensePointCloudReconstructionOptions &options)
{
    QStringList directories;
    const auto add = [&directories](const QString &directory) {
        if (!directory.isEmpty() && !directories.contains(directory)) {
            directories.push_back(QDir::cleanPath(directory));
        }
    };
    add(QString::fromStdString(options.openMvsExecutableDirectory));
    add(qEnvironmentVariable("KESTREL_OPENMVS_BIN"));
#ifdef KESTREL_OPENMVS_DEFAULT_BIN_DIR
    add(QStringLiteral(KESTREL_OPENMVS_DEFAULT_BIN_DIR));
#endif
    if (QCoreApplication::instance()) {
        const QString applicationDirectory =
            QCoreApplication::applicationDirPath();
        add(QDir(applicationDirectory).filePath("openmvs"));
        add(applicationDirectory);
    }
    return directories;
}

QString resolveExecutable(
    const QString &baseName,
    const DensePointCloudReconstructionOptions &options)
{
    const QString name = executableName(baseName);
    for (const QString &directory : executableSearchDirectories(options)) {
        const QString candidate = QDir(directory).filePath(name);
        const QFileInfo information(candidate);
        if (information.exists() && information.isFile()
            && information.isExecutable()) {
            return information.absoluteFilePath();
        }
    }
    return QStandardPaths::findExecutable(name);
}

bool runProcess(const QString &executable, const QStringList &arguments,
                const QString &workingDirectory, int timeoutMilliseconds,
                std::string *combinedLog, std::string *errorMessage)
{
    // Qt's MinGW build can fail while creating QProcess's anonymous capture
    // pipes for an MSVC-built child process. File-backed capture works across
    // that toolchain boundary and prevents verbose MVS output from blocking.
    QTemporaryFile standardOutput(
        QDir(workingDirectory).filePath("kestrel-openmvs-stdout-XXXXXX.log"));
    QTemporaryFile standardError(
        QDir(workingDirectory).filePath("kestrel-openmvs-stderr-XXXXXX.log"));
    if (!standardOutput.open() || !standardError.open()) {
        setError(errorMessage, "Could not create OpenMVS process log files in "
                 + workingDirectory.toStdString() + ".");
        return false;
    }
    const QString outputPath = standardOutput.fileName();
    const QString errorPath = standardError.fileName();
    standardOutput.close();
    standardError.close();

    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.setStandardOutputFile(outputPath, QIODevice::Truncate);
    process.setStandardErrorFile(errorPath, QIODevice::Truncate);
    process.start(executable, arguments);
    if (!process.waitForStarted(30000)) {
        setError(errorMessage, "Could not start "
                 + executable.toStdString() + ": "
                 + process.errorString().toStdString());
        return false;
    }

    const int pollMilliseconds = 200;
    int elapsedMilliseconds = 0;
    while (!process.waitForFinished(pollMilliseconds)) {
        if (timeoutMilliseconds > 0) {
            elapsedMilliseconds += pollMilliseconds;
            if (elapsedMilliseconds >= timeoutMilliseconds) {
                process.kill();
                process.waitForFinished(5000);
                setError(errorMessage, executable.toStdString()
                         + " exceeded its configured timeout.");
                return false;
            }
        }
    }
    if (combinedLog) {
        for (const QString &path : {outputPath, errorPath}) {
            QFile logFile(path);
            if (logFile.open(QIODevice::ReadOnly)) {
                const QByteArray output = logFile.readAll();
                combinedLog->append(output.constData(),
                                    static_cast<size_t>(output.size()));
            }
        }
    }
    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        setError(errorMessage, executable.toStdString()
                 + " failed with exit code "
                 + std::to_string(process.exitCode()) + ".");
        return false;
    }
    return true;
}

PlyScalarType parsePlyType(const std::string &name)
{
    if (name == "char" || name == "int8") return PlyScalarType::Int8;
    if (name == "uchar" || name == "uint8") return PlyScalarType::UInt8;
    if (name == "short" || name == "int16") return PlyScalarType::Int16;
    if (name == "ushort" || name == "uint16") return PlyScalarType::UInt16;
    if (name == "int" || name == "int32") return PlyScalarType::Int32;
    if (name == "uint" || name == "uint32") return PlyScalarType::UInt32;
    if (name == "float" || name == "float32") return PlyScalarType::Float32;
    if (name == "double" || name == "float64") return PlyScalarType::Float64;
    return PlyScalarType::Invalid;
}

template<typename Value>
bool readBinaryValue(std::istream &stream, Value *value)
{
    stream.read(reinterpret_cast<char *>(value), sizeof(Value));
    return static_cast<bool>(stream);
}

bool readPlyBinaryScalar(std::istream &stream, PlyScalarType type,
                         double *value)
{
    switch (type) {
    case PlyScalarType::Int8: {
        int8_t item = 0;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::UInt8: {
        uint8_t item = 0;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::Int16: {
        int16_t item = 0;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::UInt16: {
        uint16_t item = 0;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::Int32: {
        int32_t item = 0;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::UInt32: {
        uint32_t item = 0;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::Float32: {
        float item = 0.0f;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::Float64: {
        double item = 0.0;
        if (!readBinaryValue(stream, &item)) return false;
        *value = item;
        return true;
    }
    case PlyScalarType::Invalid:
        return false;
    }
    return false;
}

bool readPlyAsciiScalar(std::istream &stream, PlyScalarType type,
                        double *value)
{
    if (type == PlyScalarType::Invalid) return false;
    std::string token;
    if (!(stream >> token)) return false;
    try {
        *value = std::stod(token);
    } catch (...) {
        return false;
    }
    return true;
}

bool readPlyScalar(std::istream &stream, bool binary, PlyScalarType type,
                   double *value)
{
    return binary ? readPlyBinaryScalar(stream, type, value)
                  : readPlyAsciiScalar(stream, type, value);
}

} // namespace

bool OpenMvsPointCloudBackend::isAvailable(
    const DensePointCloudReconstructionOptions &options,
    std::string *reason)
{
    const QString interfaceExecutable = resolveExecutable(
        "InterfaceCOLMAP", options);
    const QString densifyExecutable = resolveExecutable(
        "DensifyPointCloud", options);
    if (interfaceExecutable.isEmpty() || densifyExecutable.isEmpty()) {
        std::string message = "OpenMVS executables were not found";
        if (interfaceExecutable.isEmpty()) {
            message += " (missing InterfaceCOLMAP)";
        }
        if (densifyExecutable.isEmpty()) {
            message += " (missing DensifyPointCloud)";
        }
        message += ". Set KESTREL_OPENMVS_BIN or configure the bundled build.";
        setError(reason, message);
        return false;
    }
    if (reason) reason->clear();
    return true;
}

bool OpenMvsPointCloudBackend::exportColmapProject(
    const DenseReconstructionInput &input,
    const std::string &workspaceDirectory,
    std::string *errorMessage)
{
    if (input.views.size() < 2 || input.sparsePoints.empty()
        || input.tracks.empty()) {
        setError(errorMessage,
                 "OpenMVS export requires at least two registered images, sparse points, and tracks.");
        return false;
    }
    if (workspaceDirectory.empty()) {
        setError(errorMessage, "OpenMVS workspace directory is empty.");
        return false;
    }

    const int componentId = input.views.front().pose.componentId;
    std::map<int, size_t> viewByImage;
    for (size_t index = 0; index < input.views.size(); ++index) {
        const DenseMvsView &view = input.views[index];
        if (view.image.empty() || view.imageIndex < 0
            || view.pose.componentId != componentId
            || !finiteMatrix(view.pose.worldToCameraRotation)
            || !finiteVector(view.pose.worldToCameraTranslation)
            || !finiteMatrix(view.calibration.intrinsic)
            || !finiteDistortion(view.calibration.distortion)
            || view.calibration.intrinsic(0, 0) <= 0.0
            || view.calibration.intrinsic(1, 1) <= 0.0) {
            setError(errorMessage,
                     "OpenMVS export received an invalid or mixed-component camera view.");
            return false;
        }
        if (!viewByImage.emplace(view.imageIndex, index).second) {
            setError(errorMessage, "OpenMVS export received duplicate image indices.");
            return false;
        }
    }

    std::unordered_map<int, const SparsePoint *> sparseByTrack;
    for (const SparsePoint &point : input.sparsePoints) {
        if (point.componentId == componentId
            && std::isfinite(point.position.x)
            && std::isfinite(point.position.y)
            && std::isfinite(point.position.z)) {
            sparseByTrack.emplace(point.trackId, &point);
        }
    }

    std::vector<std::vector<ColmapObservation>> imageObservations(
        input.views.size());
    std::vector<ColmapPoint> points;
    uint64_t nextPointId = 1;
    for (const FeatureTrack &track : input.tracks) {
        const auto sparse = sparseByTrack.find(track.id);
        if (sparse == sparseByTrack.end()) continue;

        std::vector<std::pair<size_t, cv::Point2d>> observations;
        std::set<size_t> usedViews;
        for (const FeatureObservation &observation : track.observations) {
            const auto view = viewByImage.find(observation.imageIndex);
            if (view == viewByImage.end()
                || !usedViews.insert(view->second).second
                || !std::isfinite(observation.imagePoint.x)
                || !std::isfinite(observation.imagePoint.y)) {
                continue;
            }
            observations.emplace_back(
                view->second,
                undistortedPoint(observation.imagePoint,
                                 input.views[view->second].calibration));
        }
        if (observations.size() < 2) continue;

        ColmapPoint point;
        point.id = nextPointId++;
        point.point = sparse->second;
        for (const auto &[viewIndex, imagePoint] : observations) {
            const size_t point2dIndex = imageObservations[viewIndex].size();
            imageObservations[viewIndex].push_back({point.id, imagePoint});
            point.track.emplace_back(static_cast<uint32_t>(viewIndex + 1),
                                     point2dIndex);
        }
        points.push_back(std::move(point));
    }
    if (points.empty()) {
        setError(errorMessage,
                 "No sparse point has observations in at least two exported views.");
        return false;
    }

    QDir rootDirectory(QString::fromStdString(workspaceDirectory));
    if (!rootDirectory.isAbsolute()) {
        rootDirectory = QDir(QDir::current().absoluteFilePath(
            QString::fromStdString(workspaceDirectory)));
    }
    if (!QDir().mkpath(rootDirectory.filePath("sparse"))) {
        setError(errorMessage, "Could not create OpenMVS sparse directory: "
                 + rootDirectory.filePath("sparse").toStdString());
        return false;
    }
    if (!QDir().mkpath(rootDirectory.filePath("images"))) {
        setError(errorMessage, "Could not create OpenMVS image directory: "
                 + rootDirectory.filePath("images").toStdString());
        return false;
    }
    const fs::path root = filesystemPath(rootDirectory.absolutePath());
    const fs::path sparseDirectory = root / "sparse";
    const fs::path imageDirectory = root / "images";

    std::ofstream cameras(sparseDirectory / "cameras.txt",
                          std::ios::trunc);
    std::ofstream images(sparseDirectory / "images.txt", std::ios::trunc);
    std::ofstream pointsFile(sparseDirectory / "points3D.txt",
                             std::ios::trunc);
    if (!cameras || !images || !pointsFile) {
        setError(errorMessage, "Could not create the OpenMVS COLMAP text model.");
        return false;
    }
    cameras << std::setprecision(17)
            << "# Camera list with one line of data per camera:\n"
            << "# CAMERA_ID, MODEL, WIDTH, HEIGHT, PARAMS[]\n";
    images << std::setprecision(17)
           << "# Image list with two lines of data per image:\n"
           << "# IMAGE_ID, QW, QX, QY, QZ, TX, TY, TZ, CAMERA_ID, NAME\n";
    pointsFile << std::setprecision(17)
               << "# 3D point list with one line of data per point:\n"
               << "# POINT3D_ID, X, Y, Z, R, G, B, ERROR, TRACK[]\n";

    for (size_t index = 0; index < input.views.size(); ++index) {
        const DenseMvsView &view = input.views[index];
        const uint32_t id = static_cast<uint32_t>(index + 1);
        const cv::Matx33d &intrinsic = view.calibration.intrinsic;
        // InterfaceCOLMAP subtracts 0.5 when converting COLMAP's pixel-centre
        // convention to OpenMVS/OpenCV coordinates. Add it here so Kestrel's
        // calibrated principal point survives the round trip exactly.
        cameras << id << " PINHOLE " << view.image.cols << ' '
                << view.image.rows << ' ' << intrinsic(0, 0) << ' '
                << intrinsic(1, 1) << ' ' << intrinsic(0, 2) + 0.5 << ' '
                << intrinsic(1, 2) + 0.5 << '\n';

        const Quaternion quaternion = quaternionFromRotation(
            view.pose.worldToCameraRotation);
        const cv::Vec3d &translation = view.pose.worldToCameraTranslation;
        const std::string imageName = "image_"
            + std::to_string(1000000 + id).substr(1) + ".png";
        images << id << ' ' << quaternion.w << ' ' << quaternion.x << ' '
               << quaternion.y << ' ' << quaternion.z << ' '
               << translation[0] << ' ' << translation[1] << ' '
               << translation[2] << ' ' << id << ' ' << imageName << '\n';
        for (const ColmapObservation &observation : imageObservations[index]) {
            images << observation.imagePoint.x << ' '
                   << observation.imagePoint.y << ' '
                   << observation.pointId << ' ';
        }
        images << '\n';

        const cv::Mat exported = convertForOpenMvs(
            view.image, view.calibration);
        if (exported.empty()
            || !cv::imwrite((imageDirectory / imageName).string(), exported)) {
            setError(errorMessage, "Could not write OpenMVS image "
                     + imageName + ".");
            return false;
        }
    }

    for (const ColmapPoint &point : points) {
        pointsFile << point.id << ' ' << point.point->position.x << ' '
                   << point.point->position.y << ' '
                   << point.point->position.z << " 128 128 128 "
                   << std::max(0.0, point.point->reprojectionRmsPixels);
        for (const auto &[imageId, point2dIndex] : point.track) {
            pointsFile << ' ' << imageId << ' ' << point2dIndex;
        }
        pointsFile << '\n';
    }
    if (!cameras || !images || !pointsFile) {
        setError(errorMessage, "Could not finish writing the OpenMVS COLMAP model.");
        return false;
    }
    return true;
}

bool OpenMvsPointCloudBackend::loadDensePointCloud(
    const std::string &filePath, const std::vector<DenseMvsView> &views,
    std::vector<FusedDensePoint> *points, std::string *errorMessage)
{
    if (!points) {
        setError(errorMessage, "Dense point-cloud output pointer is null.");
        return false;
    }
    points->clear();
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream) {
        setError(errorMessage, "Could not open OpenMVS point cloud: " + filePath);
        return false;
    }

    std::string line;
    if (!std::getline(stream, line) || line != "ply") {
        setError(errorMessage, "OpenMVS point cloud is not a PLY file.");
        return false;
    }
    bool binary = false;
    bool formatSeen = false;
    bool inVertexElement = false;
    bool headerComplete = false;
    size_t vertexCount = 0;
    std::vector<PlyProperty> properties;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream parser(line);
        std::string keyword;
        parser >> keyword;
        if (keyword == "format") {
            std::string format;
            parser >> format;
            if (format == "ascii") {
                binary = false;
            } else if (format == "binary_little_endian") {
                binary = true;
            } else {
                setError(errorMessage,
                         "Only ASCII and little-endian OpenMVS PLY files are supported.");
                return false;
            }
            formatSeen = true;
        } else if (keyword == "element") {
            std::string name;
            size_t count = 0;
            parser >> name >> count;
            inVertexElement = name == "vertex";
            if (inVertexElement) vertexCount = count;
        } else if (keyword == "property" && inVertexElement) {
            std::string first;
            parser >> first;
            PlyProperty property;
            if (first == "list") {
                std::string countType;
                std::string valueType;
                parser >> countType >> valueType >> property.name;
                property.list = true;
                property.countType = parsePlyType(countType);
                property.valueType = parsePlyType(valueType);
            } else {
                parser >> property.name;
                property.valueType = parsePlyType(first);
            }
            if (property.valueType == PlyScalarType::Invalid
                || (property.list
                    && property.countType == PlyScalarType::Invalid)) {
                setError(errorMessage, "Unsupported property type in OpenMVS PLY header.");
                return false;
            }
            properties.push_back(std::move(property));
        } else if (keyword == "end_header") {
            headerComplete = true;
            break;
        }
    }
    if (!formatSeen || !headerComplete || vertexCount == 0
        || properties.empty()) {
        setError(errorMessage, "OpenMVS PLY header has no usable vertices.");
        return false;
    }
    if (vertexCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
        setError(errorMessage, "OpenMVS point cloud is too large to load safely.");
        return false;
    }

    points->reserve(vertexCount);
    for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
        FusedDensePoint point;
        point.confidence = 1.0f;
        std::vector<uint32_t> viewIndices;
        bool hasX = false;
        bool hasY = false;
        bool hasZ = false;
        for (const PlyProperty &property : properties) {
            if (!property.list) {
                double value = 0.0;
                if (!readPlyScalar(stream, binary, property.valueType, &value)) {
                    setError(errorMessage, "OpenMVS PLY ended inside vertex data.");
                    points->clear();
                    return false;
                }
                if (property.name == "x") {
                    point.position.x = value;
                    hasX = true;
                } else if (property.name == "y") {
                    point.position.y = value;
                    hasY = true;
                } else if (property.name == "z") {
                    point.position.z = value;
                    hasZ = true;
                } else if (property.name == "confidence"
                           && std::isfinite(value)) {
                    point.confidence = static_cast<float>(
                        std::clamp(value, 0.0, 1.0));
                }
                continue;
            }

            double countValue = 0.0;
            if (!readPlyScalar(stream, binary, property.countType,
                               &countValue)
                || !std::isfinite(countValue) || countValue < 0.0
                || countValue > 1000000.0) {
                setError(errorMessage, "OpenMVS PLY contains an invalid list length.");
                points->clear();
                return false;
            }
            const size_t count = static_cast<size_t>(countValue);
            if (property.name == "view_indices") viewIndices.reserve(count);
            for (size_t itemIndex = 0; itemIndex < count; ++itemIndex) {
                double value = 0.0;
                if (!readPlyScalar(stream, binary, property.valueType, &value)) {
                    setError(errorMessage, "OpenMVS PLY ended inside a list property.");
                    points->clear();
                    return false;
                }
                if (property.name == "view_indices" && value >= 0.0
                    && value <= std::numeric_limits<uint32_t>::max()) {
                    viewIndices.push_back(static_cast<uint32_t>(value));
                }
            }
        }
        if (!hasX || !hasY || !hasZ
            || !std::isfinite(point.position.x)
            || !std::isfinite(point.position.y)
            || !std::isfinite(point.position.z)) {
            continue;
        }
        point.observationCount = std::max(1, static_cast<int>(viewIndices.size()));
        point.sampleCount = point.observationCount;
        for (uint32_t viewIndex : viewIndices) {
            if (viewIndex >= views.size()) continue;
            DensePointSource source;
            source.imageIndex = views[viewIndex].imageIndex;
            source.depthPixel = {-1.0f, -1.0f};
            source.confidence = point.confidence;
            point.sources.push_back(source);
        }
        points->push_back(std::move(point));
    }
    if (points->empty()) {
        setError(errorMessage, "OpenMVS PLY contained no finite 3D points.");
        return false;
    }
    return true;
}

DensePointCloudReconstructionResult OpenMvsPointCloudBackend::reconstruct(
    const DenseReconstructionInput &input,
    const DensePointCloudReconstructionOptions &options) const
{
    DensePointCloudReconstructionResult result;
    result.backendName = "OpenMVS 2.4";
    result.workspaceDirectory = options.workspaceDirectory;
    if (options.workspaceDirectory.empty()) {
        result.message = "OpenMVS requires a project-specific workspace directory.";
        return result;
    }
    if (options.openMvsResolutionLevel < 0
        || options.openMvsMaximumResolution < 1
        || options.openMvsMinimumResolution < 1
        || options.openMvsNumberViews < 0
        || options.openMvsMinimumViewsFuse < 1
        || options.openMvsGeometricIterations < 0
        || options.openMvsMaximumThreads < 0
        || options.openMvsProcessTimeoutMilliseconds < 0) {
        result.message = "OpenMVS reconstruction options are invalid.";
        return result;
    }

    const QString interfaceExecutable = resolveExecutable(
        "InterfaceCOLMAP", options);
    const QString densifyExecutable = resolveExecutable(
        "DensifyPointCloud", options);
    if (interfaceExecutable.isEmpty() || densifyExecutable.isEmpty()) {
        isAvailable(options, &result.message);
        return result;
    }

    std::string exportError;
    if (!exportColmapProject(input, options.workspaceDirectory,
                             &exportError)) {
        result.message = exportError;
        return result;
    }
    const QString workspace = QDir::cleanPath(
        QString::fromStdString(fs::absolute(options.workspaceDirectory).string()));
    const QString sparseScene = QDir(workspace).filePath("scene.mvs");
    const QString denseScene = QDir(workspace).filePath("scene_dense.mvs");
    const QString densePointCloud = QDir(workspace).filePath("scene_dense.ply");

    QStringList interfaceArguments{
        "--input-file", workspace,
        "--output-file", sparseScene,
        "--working-folder", workspace,
        "--image-folder", "images/",
        "--archive-type", "-1"
    };
    std::string processError;
    if (!runProcess(interfaceExecutable, interfaceArguments, workspace,
                    options.openMvsProcessTimeoutMilliseconds,
                    &result.processLog, &processError)) {
        result.message = processError;
        return result;
    }

    QStringList densifyArguments{
        "--input-file", sparseScene,
        "--output-file", denseScene,
        "--working-folder", workspace,
        "--archive-type", "-1",
        "--resolution-level", QString::number(options.openMvsResolutionLevel),
        "--max-resolution", QString::number(options.openMvsMaximumResolution),
        "--min-resolution", QString::number(options.openMvsMinimumResolution),
        "--number-views", QString::number(options.openMvsNumberViews),
        "--number-views-fuse", QString::number(options.openMvsMinimumViewsFuse),
        "--geometric-iters", QString::number(options.openMvsGeometricIterations),
        "--postprocess-dmaps", "1",
        "--tower-mode", "0",
        "--estimate-roi", "0",
        "--crop-to-roi", "0"
    };
    if (options.openMvsMaximumThreads > 0) {
        densifyArguments << "--max-threads"
                         << QString::number(options.openMvsMaximumThreads);
    }
    if (!runProcess(densifyExecutable, densifyArguments, workspace,
                    options.openMvsProcessTimeoutMilliseconds,
                    &result.processLog, &processError)) {
        result.message = processError;
        return result;
    }

    result.densePointCloudPath = densePointCloud.toStdString();
    std::string loadError;
    if (!loadDensePointCloud(result.densePointCloudPath, input.views,
                             &result.points, &loadError)) {
        result.message = loadError;
        return result;
    }
    result.success = true;
    result.message = "OpenMVS produced "
        + std::to_string(result.points.size()) + " dense points.";
    return result;
}

} // namespace kestrel
