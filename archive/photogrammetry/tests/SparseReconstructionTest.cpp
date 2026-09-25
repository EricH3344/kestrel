#include "photogrammetry/sfm/SparseReconstruction.h"

#include <opencv2/calib3d.hpp>

#include <algorithm>
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

kestrel::CameraCalibration calibration(bool usable = true)
{
    kestrel::CameraCalibration value;
    if (!usable) {
        return value;
    }
    value.imageSize = {1280, 960};
    value.intrinsic = cv::Matx33d(1000.0, 0.0, 640.0,
                                  0.0, 1000.0, 480.0,
                                  0.0, 0.0, 1.0);
    value.quality = kestrel::CalibrationQuality::MetadataCalibrated;
    return value;
}

cv::Point2d project(const cv::Point3d &point,
                    const cv::Matx33d &rotation,
                    const cv::Vec3d &translation)
{
    const cv::Vec3d camera = rotation
        * cv::Vec3d(point.x, point.y, point.z) + translation;
    return {1000.0 * camera[0] / camera[2] + 640.0,
            1000.0 * camera[1] / camera[2] + 480.0};
}

cv::Point3d scenePoint(int index)
{
    return {-2.5 + (index % 10) * 0.55,
            -1.6 + ((index / 10) % 9) * 0.4,
            8.0 + (index % 7) * 0.65};
}

double rotationErrorDegrees(const cv::Matx33d &estimated,
                            const cv::Matx33d &expected);

void syntheticScene(std::vector<kestrel::CameraCalibration> *calibrations,
                    std::vector<kestrel::FeatureTrack> *tracks,
                    cv::Matx33d *expectedRotation,
                    cv::Vec3d *expectedTranslation)
{
    calibrations->assign({calibration(), calibration(), calibration(false)});
    const double angle = 3.0 * CV_PI / 180.0;
    *expectedRotation = cv::Matx33d(std::cos(angle), 0.0, std::sin(angle),
                                    0.0, 1.0, 0.0,
                                    -std::sin(angle), 0.0, std::cos(angle));
    const cv::Vec3d secondCentre(1.0, 0.05, 0.02);
    *expectedTranslation = -(*expectedRotation) * secondCentre;

    for (int index = 0; index < 90; ++index) {
        const cv::Point3d point = scenePoint(index);
        cv::Point2d first = project(point, cv::Matx33d::eye(), {});
        cv::Point2d second = project(point, *expectedRotation,
                                     *expectedTranslation);
        first.x += 0.08 * std::sin(index * 0.7);
        first.y += 0.08 * std::cos(index * 0.4);
        second.x += 0.08 * std::cos(index * 0.6);
        second.y += 0.08 * std::sin(index * 0.3);
        if (index >= 84) {
            second.x += 80.0 + index;
            second.y -= 45.0;
        }
        tracks->push_back({
            index,
            {{0, index, first},
             {1, index + 1000, second},
             {2, index + 2000, first}}});
    }
}

bool incrementalRegistrationTest()
{
    std::vector<kestrel::CameraCalibration> calibrations;
    std::vector<kestrel::FeatureTrack> tracks;
    cv::Matx33d secondRotation;
    cv::Vec3d secondTranslation;
    syntheticScene(&calibrations, &tracks,
                   &secondRotation, &secondTranslation);
    calibrations = {calibration(), calibration(), calibration(),
                    calibration(false)};

    const double angle = -2.0 * CV_PI / 180.0;
    const cv::Matx33d thirdRotation(
        std::cos(angle), 0.0, std::sin(angle),
        0.0, 1.0, 0.0,
        -std::sin(angle), 0.0, std::cos(angle));
    const cv::Vec3d thirdCentre(-0.7, 0.2, 0.05);
    const cv::Vec3d thirdTranslation = -thirdRotation * thirdCentre;

    for (int index = 0; index < static_cast<int>(tracks.size()); ++index) {
        tracks[index].observations.resize(2);
        if (index < 55) {
            cv::Point2d third = project(scenePoint(index), thirdRotation,
                                        thirdTranslation);
            third.x += 0.07 * std::sin(index * 0.2);
            third.y += 0.07 * std::cos(index * 0.5);
            if (index >= 51) {
                third.x -= 110.0;
                third.y += 60.0;
            }
            tracks[index].observations.push_back(
                {2, index + 2000, third});
        }
        tracks[index].observations.push_back(
            {3, index + 3000, tracks[index].observations[0].imagePoint});
    }
    // These tracks are invisible to seed camera 1. They can only become 3D
    // after camera 2 is registered and retriangulation runs.
    for (int index = 90; index < 105; ++index) {
        const cv::Point3d point = scenePoint(index);
        tracks.push_back({
            index,
            {{0, index, project(point, cv::Matx33d::eye(), {})},
             {2, index + 2000,
              project(point, thirdRotation, thirdTranslation)},
             {3, index + 3000,
              project(point, cv::Matx33d::eye(), {})}}});
    }

    const kestrel::SparseInitializationResult result =
        kestrel::SparseReconstructor::reconstruct(calibrations, tracks);
    if (!expect(result.success,
                "Incremental sparse reconstruction should retain its seed.")) {
        std::cerr << result.message << '\n';
        return false;
    }
    const auto thirdCamera = std::find_if(
        result.cameras.begin(), result.cameras.end(),
        [](const kestrel::SparseCameraPose &camera) {
            return camera.imageIndex == 2;
        });
    const bool createdNewPoints = std::any_of(
        result.points.begin(), result.points.end(),
        [](const kestrel::SparsePoint &point) { return point.trackId >= 90; });
    return expect(result.cameras.size() == 3,
                  "PnP should register the connected third camera.")
           && expect(thirdCamera != result.cameras.end(),
                     "The third camera pose should be persisted.")
           && expect(thirdCamera != result.cameras.end()
                         && thirdCamera->pnpInliers >= 45,
                     "PnP should retain the geometrically consistent correspondences.")
           && expect(thirdCamera != result.cameras.end()
                         && rotationErrorDegrees(
                                thirdCamera->worldToCameraRotation,
                                thirdRotation) < 1.0,
                     "PnP rotation should be close to ground truth.")
           && expect(createdNewPoints,
                     "Retriangulation should add tracks enabled by the new camera.")
           && expect(std::find(result.unregisteredImageIndices.begin(),
                               result.unregisteredImageIndices.end(), 3)
                         != result.unregisteredImageIndices.end(),
                     "A disconnected invalid camera should be reported.");
}

bool disconnectedComponentTest()
{
    std::vector<kestrel::CameraCalibration> calibrations{
        calibration(), calibration(), calibration(), calibration()};
    std::vector<kestrel::FeatureTrack> tracks;

    const double firstAngle = 2.5 * CV_PI / 180.0;
    const cv::Matx33d firstRotation(
        std::cos(firstAngle), 0.0, std::sin(firstAngle),
        0.0, 1.0, 0.0,
        -std::sin(firstAngle), 0.0, std::cos(firstAngle));
    const cv::Vec3d firstTranslation =
        -firstRotation * cv::Vec3d(1.0, 0.0, 0.0);
    for (int index = 0; index < 70; ++index) {
        const cv::Point3d point = scenePoint(index);
        tracks.push_back({
            index,
            {{0, index, project(point, cv::Matx33d::eye(), {})},
             {1, index + 1000,
              project(point, firstRotation, firstTranslation)}}});
    }

    const double secondAngle = -4.0 * CV_PI / 180.0;
    const cv::Matx33d secondRotation(
        std::cos(secondAngle), 0.0, std::sin(secondAngle),
        0.0, 1.0, 0.0,
        -std::sin(secondAngle), 0.0, std::cos(secondAngle));
    const cv::Vec3d secondTranslation =
        -secondRotation * cv::Vec3d(1.2, 0.1, 0.0);
    for (int index = 100; index < 150; ++index) {
        const cv::Point3d point = scenePoint(index);
        tracks.push_back({
            index,
            {{2, index + 2000,
              project(point, cv::Matx33d::eye(), {})},
             {3, index + 3000,
              project(point, secondRotation, secondTranslation)}}});
    }

    const auto result = kestrel::SparseReconstructor::reconstruct(
        calibrations, tracks);
    const int secondComponentCameras = static_cast<int>(std::count_if(
        result.cameras.begin(), result.cameras.end(),
        [](const kestrel::SparseCameraPose &camera) {
            return camera.componentId == 1;
        }));
    const bool hasSecondComponentPoint = std::any_of(
        result.points.begin(), result.points.end(),
        [](const kestrel::SparsePoint &point) {
            return point.componentId == 1 && point.trackId >= 100;
        });
    return expect(result.success,
                  "The primary sparse component should reconstruct.")
           && expect(result.componentCount == 2,
                     "Two disconnected image networks should remain two components.")
           && expect(result.cameras.size() == 4
                         && secondComponentCameras == 2,
                     "Both cameras in the secondary network should be recovered.")
           && expect(hasSecondComponentPoint,
                     "Secondary sparse points need an explicit component identity.")
           && expect(result.unregisteredImageIndices.empty(),
                     "Recovered component cameras should not be reported as missing.");
}

double rotationErrorDegrees(const cv::Matx33d &estimated,
                            const cv::Matx33d &expected)
{
    const cv::Matx33d difference = estimated * expected.t();
    const double cosine = std::clamp(
        (cv::trace(cv::Mat(difference))[0] - 1.0) * 0.5, -1.0, 1.0);
    return std::acos(cosine) * 180.0 / CV_PI;
}

bool recoveryTest()
{
    std::vector<kestrel::CameraCalibration> calibrations;
    std::vector<kestrel::FeatureTrack> tracks;
    cv::Matx33d expectedRotation;
    cv::Vec3d expectedTranslation;
    syntheticScene(&calibrations, &tracks,
                   &expectedRotation, &expectedTranslation);

    const kestrel::SparseInitializationResult result =
        kestrel::SparseReconstructor::initialize(calibrations, tracks);
    if (!expect(result.success, "Synthetic initial pair should reconstruct.")) {
        std::cerr << result.message << '\n';
        return false;
    }
    const cv::Vec3d expectedDirection = expectedTranslation
                                        / cv::norm(expectedTranslation);
    const cv::Vec3d estimatedDirection =
        result.cameras[1].worldToCameraTranslation
        / cv::norm(result.cameras[1].worldToCameraTranslation);
    return expect(result.firstImageIndex == 0 && result.secondImageIndex == 1,
                  "Only the two calibrated images should seed reconstruction.")
           && expect(result.points.size() >= 75,
                     "Most noisy inlier tracks should triangulate.")
           && expect(rotationErrorDegrees(
                         result.cameras[1].worldToCameraRotation,
                         expectedRotation) < 0.5,
                     "Recovered rotation should be close to ground truth.")
           && expect(estimatedDirection.dot(expectedDirection) > 0.995,
                     "Recovered translation direction should match ground truth.")
           && expect(result.medianTriangulationAngleDegrees > 2.0,
                     "The selected pair should have useful parallax.")
           && expect(result.points.front().reprojectionRmsPixels < 1.0,
                     "Accepted points should have low reprojection error.");
}

bool insufficientTracksTest()
{
    std::vector<kestrel::CameraCalibration> calibrations;
    std::vector<kestrel::FeatureTrack> tracks;
    cv::Matx33d rotation;
    cv::Vec3d translation;
    syntheticScene(&calibrations, &tracks, &rotation, &translation);
    tracks.resize(10);
    const auto result = kestrel::SparseReconstructor::initialize(
        calibrations, tracks);
    return expect(!result.success,
                  "A weak pair must not initialize a reconstruction.")
           && expect(!result.message.empty(),
                     "Initialization failure should explain itself.");
}

bool triangulationAngleRejectionTest()
{
    std::vector<kestrel::CameraCalibration> calibrations;
    std::vector<kestrel::FeatureTrack> tracks;
    cv::Matx33d rotation;
    cv::Vec3d translation;
    syntheticScene(&calibrations, &tracks, &rotation, &translation);
    kestrel::SparseInitializationOptions options;
    options.minimumTriangulationAngleDegrees = 30.0;
    const auto result = kestrel::SparseReconstructor::initialize(
        calibrations, tracks, options);
    return expect(!result.success,
                  "Tracks below the configured triangulation angle must be rejected.")
           && expect(result.points.empty(),
                     "Rejected low-angle geometry must not leak into sparse points.");
}

} // namespace

int main()
{
    const bool success = recoveryTest() && incrementalRegistrationTest()
                         && disconnectedComponentTest()
                         && insufficientTracksTest()
                         && triangulationAngleRejectionTest();
    if (success) {
        std::cout << "Sparse reconstruction tests passed.\n";
        return 0;
    }
    return 1;
}
