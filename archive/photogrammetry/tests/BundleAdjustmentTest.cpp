#include "photogrammetry/sfm/BundleAdjustment.h"

#include <opencv2/calib3d.hpp>

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

kestrel::CameraCalibration calibration()
{
    kestrel::CameraCalibration value;
    value.imageSize = {1280, 960};
    value.intrinsic = cv::Matx33d(1000.0, 0.0, 640.0,
                                  0.0, 1000.0, 480.0,
                                  0.0, 0.0, 1.0);
    value.distortion = {-0.045, 0.012, 0.0005, -0.0003, -0.002};
    value.quality = kestrel::CalibrationQuality::MetadataCalibrated;
    return value;
}

cv::Point2d project(const cv::Point3d &point,
                    const kestrel::SparseCameraPose &camera,
                    const kestrel::CameraCalibration &calibration)
{
    const cv::Vec3d value = camera.worldToCameraRotation
        * cv::Vec3d(point.x, point.y, point.z)
        + camera.worldToCameraTranslation;
    const double x = value[0] / value[2];
    const double y = value[1] / value[2];
    const double r2 = x * x + y * y;
    const double radial = 1.0 + calibration.distortion[0] * r2
        + calibration.distortion[1] * r2 * r2
        + calibration.distortion[4] * r2 * r2 * r2;
    const double distortedX = x * radial
        + 2.0 * calibration.distortion[2] * x * y
        + calibration.distortion[3] * (r2 + 2.0 * x * x);
    const double distortedY = y * radial
        + calibration.distortion[2] * (r2 + 2.0 * y * y)
        + 2.0 * calibration.distortion[3] * x * y;
    return {calibration.intrinsic(0, 0) * distortedX
                + calibration.intrinsic(0, 2),
            calibration.intrinsic(1, 1) * distortedY
                + calibration.intrinsic(1, 2)};
}

cv::Point3d centre(const kestrel::SparseCameraPose &camera)
{
    const cv::Vec3d value = -camera.worldToCameraRotation.t()
                             * camera.worldToCameraTranslation;
    return {value[0], value[1], value[2]};
}

bool noisyNetworkTest()
{
    const std::vector<cv::Point3d> trueCentres{
        {0.0, 0.0, 0.0}, {1.2, 0.0, 0.0},
        {0.1, 1.0, 0.05}, {1.3, 1.1, -0.03}};
    std::vector<kestrel::SparseCameraPose> trueCameras;
    for (int index = 0; index < 4; ++index) {
        trueCameras.push_back({
            index, cv::Matx33d::eye(),
            cv::Vec3d(-trueCentres[index].x,
                      -trueCentres[index].y,
                      -trueCentres[index].z),
            index < 2 ? 0 : 60, 0.5, 0});
    }

    kestrel::SparseInitializationResult reconstruction;
    reconstruction.success = true;
    reconstruction.componentCount = 1;
    reconstruction.cameras = trueCameras;
    for (int index = 2; index < 4; ++index) {
        cv::Mat increment;
        cv::Rodrigues(cv::Vec3d(0.012 * index, -0.009 * index,
                                0.006 * index), increment);
        cv::Matx33d rotation;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                rotation(row, column) = increment.at<double>(row, column);
            }
        }
        reconstruction.cameras[index].worldToCameraRotation = rotation;
        reconstruction.cameras[index].worldToCameraTranslation +=
            cv::Vec3d(0.12 * index, -0.08 * index, 0.05 * index);
    }
    const double initialThirdCentreError = cv::norm(
        centre(reconstruction.cameras[2]) - trueCentres[2]);

    std::vector<kestrel::FeatureTrack> tracks;
    const kestrel::CameraCalibration cameraCalibration = calibration();
    for (int index = 0; index < 100; ++index) {
        const cv::Point3d truth(-2.2 + (index % 10) * 0.48,
                                -1.8 + ((index / 10) % 10) * 0.4,
                                7.0 + (index % 9) * 0.55);
        const cv::Point3d initial(
            truth.x + 0.08 * std::sin(index * 0.4),
            truth.y + 0.07 * std::cos(index * 0.3),
            truth.z + 0.1 * std::sin(index * 0.2));
        reconstruction.points.push_back(
            {index, initial, 2.0, 5.0, 0});
        kestrel::FeatureTrack track;
        track.id = index;
        for (int camera = 0; camera < 4; ++camera) {
            cv::Point2d measured = project(
                truth, trueCameras[camera], cameraCalibration);
            measured.x += 0.12 * std::sin(index * 0.7 + camera);
            measured.y += 0.12 * std::cos(index * 0.5 + camera);
            if (camera == 3 && index < 10) {
                measured.x += 35.0;
                measured.y -= 25.0;
            }
            track.observations.push_back(
                {camera, index * 10 + camera, measured});
        }
        tracks.push_back(std::move(track));
    }

    const std::vector<kestrel::CameraCalibration> calibrations(
        4, cameraCalibration);
    kestrel::BundleAdjustmentOptions options;
    options.maximumIterations = 20;
    const kestrel::BundleAdjustmentResult result =
        kestrel::SparseBundleAdjuster::optimize(
            &reconstruction, calibrations, tracks, {}, options);
    if (!expect(result.success, "The noisy camera network should optimize.")) {
        return false;
    }
    const auto &summary = result.components.front();
    const double finalThirdCentreError = cv::norm(
        centre(reconstruction.cameras[2]) - trueCentres[2]);
    return expect(summary.initialRmsPixels > 5.0,
                  "Perturbed initialization should have measurable error.")
           && expect(summary.finalRmsPixels < 0.5,
                     "Robust adjustment should reach subpixel RMS.")
           && expect(summary.finalRmsPixels < summary.initialRmsPixels * 0.1,
                     "Bundle adjustment should substantially reduce error.")
           && expect(summary.rejectedObservationCount >= 10,
                     "Injected feature outliers should be rejected.")
           && expect(finalThirdCentreError < initialThirdCentreError * 0.25,
                     "The free camera pose should move toward ground truth.")
           && expect(reconstruction.cameras[0].worldToCameraTranslation
                         == trueCameras[0].worldToCameraTranslation,
                     "The first gauge camera must remain fixed.");
}

bool gpsGaugeTest()
{
    const std::vector<cv::Point3d> trueCentres{
        {0.0, 0.0, 0.0}, {2.0, 0.0, 0.1},
        {0.0, 2.0, 0.2}, {2.0, 2.0, 0.5}};
    // ENU alignment and the fixed seed cameras define the global frame. This
    // offset models drift in later PnP cameras that GPS priors should resist.
    const cv::Vec3d frameOffset(0.55, -0.35, 0.15);
    kestrel::SparseInitializationResult reconstruction;
    reconstruction.success = true;
    reconstruction.componentCount = 1;
    std::vector<kestrel::SparseCameraPose> trueCameras;
    std::vector<kestrel::BundleAdjustmentPositionPrior> priors;
    for (int index = 0; index < 4; ++index) {
        trueCameras.push_back({
            index, cv::Matx33d::eye(),
            cv::Vec3d(-trueCentres[index].x,
                      -trueCentres[index].y,
                      -trueCentres[index].z),
            index < 2 ? 0 : 50, 0.2, 0});
        const cv::Point3d shifted = index < 2
            ? trueCentres[index]
            : cv::Point3d(trueCentres[index].x + frameOffset[0],
                          trueCentres[index].y + frameOffset[1],
                          trueCentres[index].z + frameOffset[2]);
        reconstruction.cameras.push_back({
            index, cv::Matx33d::eye(),
            cv::Vec3d(-shifted.x, -shifted.y, -shifted.z),
            index < 2 ? 0 : 50, 0.2, 0});
        priors.push_back({index, trueCentres[index], 0.5, true});
    }

    const kestrel::CameraCalibration cameraCalibration = calibration();
    std::vector<kestrel::FeatureTrack> tracks;
    for (int index = 0; index < 50; ++index) {
        const cv::Point3d truth(-1.5 + (index % 10) * 0.4,
                                -1.2 + (index / 10) * 0.55,
                                7.0 + (index % 6) * 0.4);
        reconstruction.points.push_back({
            index, truth,
            0.0, 5.0, 0});
        kestrel::FeatureTrack track;
        track.id = index;
        for (int camera = 0; camera < 4; ++camera) {
            track.observations.push_back({
                camera, index * 10 + camera,
                project(truth, trueCameras[camera], cameraCalibration)});
        }
        tracks.push_back(std::move(track));
    }

    kestrel::BundleAdjustmentOptions options;
    options.maximumIterations = 25;
    const auto result = kestrel::SparseBundleAdjuster::optimize(
        &reconstruction,
        std::vector<kestrel::CameraCalibration>(4, cameraCalibration),
        tracks, priors, options);
    const auto &summary = result.components.front();
    if (summary.finalGpsRmsMetres >= 0.05) {
        std::cerr << "GPS RMS initial=" << summary.initialGpsRmsMetres
                  << " final=" << summary.finalGpsRmsMetres
                  << " pixel final=" << summary.finalRmsPixels
                  << " iterations=" << summary.acceptedIterations
                  << " centre0=" << centre(reconstruction.cameras[0]) << '\n';
    }
    return expect(result.success,
                  "GPS priors should constrain the free similarity gauge.")
           && expect(summary.fixedCameraCount == 2
                         && summary.gpsPriorCount == 4,
                     "GPS priors should coexist with the explicit seed gauge.")
           && expect(summary.initialGpsRmsMetres > 0.4,
                     "The free cameras should retain measurable GPS drift.")
           && expect(summary.finalGpsRmsMetres < 0.05,
                     "Soft GPS priors should correct free-camera drift.")
           && expect(summary.finalRmsPixels < 0.01,
                     "Gauge correction must preserve image reprojection.")
           && expect(cv::norm(centre(reconstruction.cameras[0])
                              - trueCentres[0]) < 0.05,
                     "The optimized camera centre should return to ENU.")
           && expect(cv::norm(reconstruction.points[0].position
                              - cv::Point3d(-1.5, -1.2, 7.0)) < 0.1,
                     "Sparse points should follow the GPS-constrained frame.");
}

bool calibrationRefinementTest()
{
    kestrel::CameraCalibration truth = calibration();
    truth.quality = kestrel::CalibrationQuality::Approximate;
    truth.source = "synthetic approximate calibration";
    kestrel::CameraCalibration initial = truth;
    initial.intrinsic(0, 0) *= 1.06;
    initial.intrinsic(1, 1) *= 0.95;
    initial.intrinsic(0, 2) += 9.0;
    initial.intrinsic(1, 2) -= 7.0;
    initial.distortion[0] += 0.035;

    const std::vector<cv::Point3d> centres{
        {0.0, 0.0, 0.0}, {1.2, 0.0, 0.0}, {2.4, 0.1, 0.0},
        {0.1, 1.0, 0.0}, {1.3, 1.1, 0.0}, {2.5, 1.0, 0.0}};
    kestrel::SparseInitializationResult reconstruction;
    reconstruction.success = true;
    reconstruction.componentCount = 1;
    for (int index = 0; index < static_cast<int>(centres.size()); ++index) {
        reconstruction.cameras.push_back({
            index, cv::Matx33d::eye(),
            cv::Vec3d(-centres[index].x, -centres[index].y,
                      -centres[index].z),
            index < 2 ? 0 : 80, 0.2, 0});
    }

    std::vector<kestrel::FeatureTrack> tracks;
    for (int index = 0; index < 180; ++index) {
        const cv::Point3d point(-1.8 + (index % 15) * 0.42,
                                -1.5 + ((index / 15) % 12) * 0.30,
                                7.0 + (index % 8) * 0.45);
        reconstruction.points.push_back({index, point, 0.0, 5.0, 0});
        kestrel::FeatureTrack track;
        track.id = index;
        for (int camera = 0; camera < static_cast<int>(centres.size());
             ++camera) {
            track.observations.push_back({
                camera, index * 10 + camera,
                project(point, reconstruction.cameras[camera], truth)});
        }
        tracks.push_back(std::move(track));
    }

    std::vector<kestrel::CameraCalibration> calibrations(
        centres.size(), initial);
    kestrel::BundleAdjustmentOptions options;
    options.refineCameraCalibration = true;
    options.maximumIterations = 30;
    const auto result = kestrel::SparseBundleAdjuster::optimize(
        &reconstruction, &calibrations, tracks, {}, options);
    if (!result.success || result.calibrations.empty()) {
        return expect(false,
                      "The shared approximate calibration should refine.");
    }
    const auto &summary = result.components.front();
    const double initialFocalError =
        std::abs(initial.intrinsic(0, 0) - truth.intrinsic(0, 0));
    const double finalFocalError =
        std::abs(calibrations[0].intrinsic(0, 0)
                 - truth.intrinsic(0, 0));
    if (finalFocalError >= initialFocalError * 0.8) {
        std::cerr << "Calibration fx initial=" << initial.intrinsic(0, 0)
                  << " final=" << calibrations[0].intrinsic(0, 0)
                  << " truth=" << truth.intrinsic(0, 0)
                  << " RMS " << summary.initialRmsPixels << " -> "
                  << summary.finalRmsPixels << '\n';
    }
    return expect(summary.refinedCalibrationGroupCount == 1,
                  "Matching sensor models should refine as one group.")
           && expect(finalFocalError < initialFocalError * 0.8,
                     "Refinement should reduce focal-length error.")
           && expect(summary.finalRmsPixels < summary.initialRmsPixels * 0.2,
                     "Calibration refinement should reduce reprojection error.")
           && expect(summary.linearSolverIterations > 0,
                     "The sparse PCG camera solver should be exercised.");
}

bool sparseSolverNetworkTest()
{
    constexpr int cameraCount = 32;
    constexpr int pointCount = 192;
    const kestrel::CameraCalibration cameraCalibration = calibration();
    kestrel::SparseInitializationResult reconstruction;
    reconstruction.success = true;
    reconstruction.componentCount = 1;
    std::vector<kestrel::SparseCameraPose> truth;
    for (int camera = 0; camera < cameraCount; ++camera) {
        const cv::Point3d centre(camera * 0.22,
                                 0.15 * std::sin(camera * 0.3), 0.0);
        truth.push_back({camera, cv::Matx33d::eye(),
                         cv::Vec3d(-centre.x, -centre.y, -centre.z),
                         camera < 2 ? 0 : 60, 0.2, 0});
        reconstruction.cameras.push_back(truth.back());
        if (camera >= 2) {
            reconstruction.cameras.back().worldToCameraTranslation +=
                cv::Vec3d(0.025 * std::sin(camera),
                          0.018 * std::cos(camera * 0.7), 0.01);
        }
    }

    std::vector<kestrel::FeatureTrack> tracks;
    for (int index = 0; index < pointCount; ++index) {
        const int anchor = index % (cameraCount - 4);
        const cv::Point3d point(anchor * 0.22 - 0.4 + (index % 5) * 0.2,
                                -1.0 + ((index / 5) % 10) * 0.22,
                                7.0 + (index % 7) * 0.35);
        reconstruction.points.push_back({
            index,
            cv::Point3d(point.x + 0.02 * std::sin(index),
                        point.y + 0.02 * std::cos(index),
                        point.z + 0.03 * std::sin(index * 0.4)),
            0.0, 5.0, 0});
        kestrel::FeatureTrack track;
        track.id = index;
        for (int camera = anchor; camera < anchor + 5; ++camera) {
            track.observations.push_back({
                camera, index * 100 + camera,
                project(point, truth[camera], cameraCalibration)});
        }
        tracks.push_back(std::move(track));
    }
    kestrel::BundleAdjustmentOptions options;
    options.maximumIterations = 20;
    const auto result = kestrel::SparseBundleAdjuster::optimize(
        &reconstruction,
        std::vector<kestrel::CameraCalibration>(cameraCount,
                                                cameraCalibration),
        tracks, {}, options);
    const auto &summary = result.components.front();
    if (summary.finalRmsPixels >= summary.initialRmsPixels * 0.6) {
        std::cerr << "Sparse network RMS " << summary.initialRmsPixels
                  << " -> " << summary.finalRmsPixels
                  << ", PCG iterations=" << summary.linearSolverIterations
                  << ", accepted=" << summary.acceptedIterations << '\n';
    }
    return expect(result.success,
                  "The larger local-overlap network should optimize.")
           && expect(summary.cameraCount == cameraCount,
                     "Every camera should remain in the sparse system.")
           && expect(summary.linearSolverIterations > cameraCount,
                     "The iterative sparse camera solve should do real work.")
           && expect(summary.finalRmsPixels < summary.initialRmsPixels * 0.6,
                     "Sparse PCG adjustment should substantially improve the network.");
}

} // namespace

int main()
{
    if (noisyNetworkTest() && gpsGaugeTest()
        && calibrationRefinementTest() && sparseSolverNetworkTest()) {
        std::cout << "Bundle adjustment tests passed.\n";
        return 0;
    }
    return 1;
}
