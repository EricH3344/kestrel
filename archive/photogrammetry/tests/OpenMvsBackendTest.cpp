#include "photogrammetry/mvs/DensePointCloudBackend.h"

#include <QCoreApplication>
#include <QDir>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

bool require(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

std::string readText(const fs::path &path)
{
    std::ifstream stream(path);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

template<typename Value>
void writeBinary(std::ofstream &stream, const Value &value)
{
    stream.write(reinterpret_cast<const char *>(&value), sizeof(Value));
}

bool testColmapExport()
{
    const QString workspacePath = QDir::current().filePath(
        "kestrel-openmvs-export-test");
    QDir workspace(workspacePath);
    if (workspace.exists()) workspace.removeRecursively();
    if (!require(QDir().mkpath(workspacePath),
                 "Could not create a temporary workspace.")) {
        return false;
    }

    kestrel::DenseReconstructionInput input;
    kestrel::CameraCalibration calibration;
    calibration.imageSize = {32, 24};
    calibration.intrinsic = {40.0, 0.0, 15.5,
                             0.0, 41.0, 11.5,
                             0.0, 0.0, 1.0};
    cv::Mat firstImage(24, 32, CV_16UC1, cv::Scalar(1000));
    cv::Mat secondImage(24, 32, CV_16UC1, cv::Scalar(1200));
    cv::rectangle(firstImage, {8, 6, 10, 10}, cv::Scalar(4000), cv::FILLED);
    cv::rectangle(secondImage, {9, 6, 10, 10}, cv::Scalar(4000), cv::FILLED);

    kestrel::SparseCameraPose firstPose;
    firstPose.imageIndex = 10;
    kestrel::SparseCameraPose secondPose;
    secondPose.imageIndex = 20;
    secondPose.worldToCameraTranslation = {-1.0, 0.0, 0.0};
    input.views.push_back({10, firstImage, calibration, firstPose});
    input.views.push_back({20, secondImage, calibration, secondPose});
    input.sparsePoints.push_back({7, {0.0, 0.0, 5.0}, 0.25, 8.0, 0});
    kestrel::FeatureTrack track;
    track.id = 7;
    track.observations.push_back({10, 0, {15.5, 11.5}});
    track.observations.push_back({20, 1, {7.3, 11.5}});
    input.tracks.push_back(track);

    std::string error;
    const bool exportedProject =
        kestrel::OpenMvsPointCloudBackend::exportColmapProject(
            input, workspacePath.toStdString(), &error);
    if (!require(exportedProject, "COLMAP export failed: " + error)) {
        return false;
    }

    const fs::path root(workspacePath.toStdString());
    const std::string cameras = readText(root / "sparse" / "cameras.txt");
    const std::string images = readText(root / "sparse" / "images.txt");
    const std::string points = readText(root / "sparse" / "points3D.txt");
    bool valid = true;
    valid &= require(cameras.find("1 PINHOLE 32 24 40 41 16 12")
                         != std::string::npos,
                     "COLMAP camera did not preserve Kestrel intrinsics and pixel-centre convention.");
    valid &= require(images.find("1 1 0 0 0 0 0 0 1 image_000001.png")
                         != std::string::npos,
                     "First COLMAP camera pose was not exported as expected.");
    valid &= require(images.find("2 1 0 0 0 -1 0 0 2 image_000002.png")
                         != std::string::npos,
                     "Second COLMAP camera pose was not exported as expected.");
    valid &= require(points.find("1 0 0 5 128 128 128 0.25 1 0 2 0")
                         != std::string::npos,
                     "Sparse COLMAP visibility track was not exported correctly.");
    const cv::Mat exported = cv::imread(
        (root / "images" / "image_000001.png").string(),
        cv::IMREAD_UNCHANGED);
    valid &= require(!exported.empty() && exported.type() == CV_8UC3,
                     "OpenMVS image export was not an 8-bit three-channel PNG.");
    return valid;
}

bool testBinaryPlyImport()
{
    const QString workspacePath = QDir::current().filePath(
        "kestrel-openmvs-ply-test");
    QDir workspace(workspacePath);
    if (workspace.exists()) workspace.removeRecursively();
    if (!require(QDir().mkpath(workspacePath),
                 "Could not create a PLY workspace.")) {
        return false;
    }
    const fs::path path = fs::path(workspacePath.toStdString()) / "dense.ply";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << "ply\n"
           << "format binary_little_endian 1.0\n"
           << "element vertex 2\n"
           << "property float x\n"
           << "property float y\n"
           << "property float z\n"
           << "property uchar red\n"
           << "property uchar green\n"
           << "property uchar blue\n"
           << "property list uchar uint view_indices\n"
           << "property list uchar float view_weights\n"
           << "end_header\n";
    const auto vertex = [&stream](float x, float y, float z,
                                  uint32_t firstView,
                                  uint32_t secondView) {
        writeBinary(stream, x);
        writeBinary(stream, y);
        writeBinary(stream, z);
        const uint8_t colour = 128;
        writeBinary(stream, colour);
        writeBinary(stream, colour);
        writeBinary(stream, colour);
        const uint8_t count = 2;
        writeBinary(stream, count);
        writeBinary(stream, firstView);
        writeBinary(stream, secondView);
        writeBinary(stream, count);
        const float firstWeight = 0.8f;
        const float secondWeight = 0.7f;
        writeBinary(stream, firstWeight);
        writeBinary(stream, secondWeight);
    };
    vertex(1.0f, 2.0f, 3.0f, 0, 1);
    vertex(-4.0f, 5.0f, 6.0f, 1, 0);
    stream.close();

    std::vector<kestrel::DenseMvsView> views(2);
    views[0].imageIndex = 10;
    views[1].imageIndex = 20;
    std::vector<kestrel::FusedDensePoint> points;
    std::string error;
    const bool loaded = kestrel::OpenMvsPointCloudBackend::loadDensePointCloud(
        path.string(), views, &points, &error);
    bool valid = require(loaded, "Binary OpenMVS PLY import failed: " + error);
    valid &= require(points.size() == 2,
                     "Binary OpenMVS PLY import returned the wrong point count.");
    if (points.size() == 2) {
        valid &= require(points[0].position == cv::Point3d(1.0, 2.0, 3.0),
                         "Binary OpenMVS PLY position was corrupted.");
        valid &= require(points[0].observationCount == 2
                             && points[0].sources.size() == 2
                             && points[0].sources[0].imageIndex == 10
                             && points[0].sources[1].imageIndex == 20,
                         "OpenMVS view provenance was not mapped to Kestrel image indices.");
    }
    return valid;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    if (!testColmapExport() || !testBinaryPlyImport()) {
        return 1;
    }
    std::cout << "OpenMVS backend format tests passed.\n";
    return 0;
}
