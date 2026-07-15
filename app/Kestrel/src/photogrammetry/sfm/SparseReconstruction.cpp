#include "photogrammetry/sfm/SparseReconstruction.h"

#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace kestrel {

namespace {

constexpr double kRadiansToDegrees = 57.2957795130823208768;

struct SharedTrack
{
    int trackId = -1;
    FeatureObservation first;
    FeatureObservation second;
};

struct PairCandidate
{
    int firstImage = -1;
    int secondImage = -1;
    std::vector<SharedTrack> sharedTracks;
};

bool finitePoint(const cv::Point3d &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y)
           && std::isfinite(point.z);
}

cv::Point2d projectPoint(const CameraCalibration &calibration,
                         const cv::Matx33d &rotation,
                         const cv::Vec3d &translation,
                         const cv::Point3d &worldPoint)
{
    const cv::Vec3d cameraPoint = rotation * cv::Vec3d(
        worldPoint.x, worldPoint.y, worldPoint.z) + translation;
    if (!(cameraPoint[2] > 0.0) || !std::isfinite(cameraPoint[2])) {
        const double invalid = std::numeric_limits<double>::quiet_NaN();
        return {invalid, invalid};
    }
    const double x = cameraPoint[0] / cameraPoint[2];
    const double y = cameraPoint[1] / cameraPoint[2];
    const double r2 = x * x + y * y;
    const double r4 = r2 * r2;
    const double r6 = r4 * r2;
    const double k1 = calibration.distortion[0];
    const double k2 = calibration.distortion[1];
    const double p1 = calibration.distortion[2];
    const double p2 = calibration.distortion[3];
    const double k3 = calibration.distortion[4];
    const double radial = 1.0 + k1 * r2 + k2 * r4 + k3 * r6;
    const double distortedX = x * radial + 2.0 * p1 * x * y
                              + p2 * (r2 + 2.0 * x * x);
    const double distortedY = y * radial + p1 * (r2 + 2.0 * y * y)
                              + 2.0 * p2 * x * y;
    return {
        calibration.intrinsic(0, 0) * distortedX
            + calibration.intrinsic(0, 1) * distortedY
            + calibration.intrinsic(0, 2),
        calibration.intrinsic(1, 1) * distortedY
            + calibration.intrinsic(1, 2)};
}

double pointDistance(const cv::Point2d &first, const cv::Point2d &second)
{
    const double x = first.x - second.x;
    const double y = first.y - second.y;
    return std::sqrt(x * x + y * y);
}

double triangulationAngle(const cv::Point3d &point,
                          const cv::Point3d &firstCentre,
                          const cv::Point3d &secondCentre)
{
    cv::Vec3d firstRay(point.x - firstCentre.x,
                       point.y - firstCentre.y,
                       point.z - firstCentre.z);
    cv::Vec3d secondRay(point.x - secondCentre.x,
                        point.y - secondCentre.y,
                        point.z - secondCentre.z);
    const double firstLength = cv::norm(firstRay);
    const double secondLength = cv::norm(secondRay);
    if (!(firstLength > 0.0) || !(secondLength > 0.0)) {
        return 0.0;
    }
    firstRay /= firstLength;
    secondRay /= secondLength;
    const double cosine = std::clamp(firstRay.dot(secondRay), -1.0, 1.0);
    return std::acos(cosine) * kRadiansToDegrees;
}

std::vector<PairCandidate> buildCandidates(
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks)
{
    std::map<std::pair<int, int>, std::vector<SharedTrack>> grouped;
    for (const FeatureTrack &track : tracks) {
        for (size_t first = 0; first < track.observations.size(); ++first) {
            const FeatureObservation &a = track.observations[first];
            if (a.imageIndex < 0
                || a.imageIndex >= static_cast<int>(calibrations.size())
                || !calibrations[a.imageIndex].isUsable()) {
                continue;
            }
            for (size_t second = first + 1;
                 second < track.observations.size(); ++second) {
                const FeatureObservation &b = track.observations[second];
                if (b.imageIndex < 0
                    || b.imageIndex >= static_cast<int>(calibrations.size())
                    || !calibrations[b.imageIndex].isUsable()
                    || a.imageIndex == b.imageIndex) {
                    continue;
                }
                if (a.imageIndex < b.imageIndex) {
                    grouped[{a.imageIndex, b.imageIndex}].push_back(
                        {track.id, a, b});
                } else {
                    grouped[{b.imageIndex, a.imageIndex}].push_back(
                        {track.id, b, a});
                }
            }
        }
    }

    std::vector<PairCandidate> candidates;
    candidates.reserve(grouped.size());
    for (auto &[images, sharedTracks] : grouped) {
        candidates.push_back(
            {images.first, images.second, std::move(sharedTracks)});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const PairCandidate &first, const PairCandidate &second) {
                  if (first.sharedTracks.size() != second.sharedTracks.size()) {
                      return first.sharedTracks.size() > second.sharedTracks.size();
                  }
                  return std::pair(first.firstImage, first.secondImage)
                         < std::pair(second.firstImage, second.secondImage);
              });
    return candidates;
}

SparseInitializationResult evaluateCandidate(
    const PairCandidate &candidate,
    const std::vector<CameraCalibration> &calibrations,
    const SparseInitializationOptions &options)
{
    SparseInitializationResult result;
    result.firstImageIndex = candidate.firstImage;
    result.secondImageIndex = candidate.secondImage;
    result.sharedTrackCount = static_cast<int>(candidate.sharedTracks.size());
    if (result.sharedTrackCount < options.minimumSharedTracks) {
        result.message = "The pair has too few shared tracks.";
        return result;
    }

    const CameraCalibration &firstCalibration =
        calibrations[candidate.firstImage];
    const CameraCalibration &secondCalibration =
        calibrations[candidate.secondImage];
    std::vector<cv::Point2d> firstPixels;
    std::vector<cv::Point2d> secondPixels;
    firstPixels.reserve(candidate.sharedTracks.size());
    secondPixels.reserve(candidate.sharedTracks.size());
    for (const SharedTrack &track : candidate.sharedTracks) {
        firstPixels.push_back(track.first.imagePoint);
        secondPixels.push_back(track.second.imagePoint);
    }

    std::vector<cv::Point2d> firstNormalized;
    std::vector<cv::Point2d> secondNormalized;
    const cv::Mat firstIntrinsic(firstCalibration.intrinsic, true);
    const cv::Mat secondIntrinsic(secondCalibration.intrinsic, true);
    const cv::Mat firstDistortion(firstCalibration.distortion, true);
    const cv::Mat secondDistortion(secondCalibration.distortion, true);
    cv::undistortPoints(firstPixels, firstNormalized,
                        firstIntrinsic, firstDistortion);
    cv::undistortPoints(secondPixels, secondNormalized,
                        secondIntrinsic, secondDistortion);

    const double meanFocal = 0.25
        * (firstCalibration.intrinsic(0, 0)
           + firstCalibration.intrinsic(1, 1)
           + secondCalibration.intrinsic(0, 0)
           + secondCalibration.intrinsic(1, 1));
    if (!(meanFocal > 0.0)) {
        result.message = "The pair has invalid focal lengths.";
        return result;
    }
    cv::Mat essentialMask;
    cv::Mat essential = cv::findEssentialMat(
        firstNormalized, secondNormalized, 1.0, cv::Point2d(0.0, 0.0),
        cv::RANSAC, options.ransacConfidence,
        options.ransacThresholdPixels / meanFocal, essentialMask);
    if (essential.empty() || essential.cols != 3 || essential.rows < 3) {
        result.message = "Essential-matrix estimation failed.";
        return result;
    }
    if (essential.rows != 3) {
        essential = essential.rowRange(0, 3).clone();
    }

    cv::Mat rotation;
    cv::Mat translation;
    const int recovered = cv::recoverPose(
        essential, firstNormalized, secondNormalized,
        rotation, translation, 1.0, cv::Point2d(0.0, 0.0), essentialMask);
    result.essentialInlierCount = recovered;
    if (recovered < options.minimumTriangulatedPoints
        || rotation.rows != 3 || translation.total() != 3) {
        result.message = "Pose recovery retained too few cheirality inliers.";
        return result;
    }

    cv::Matx33d secondRotation;
    cv::Vec3d secondTranslation;
    for (int row = 0; row < 3; ++row) {
        secondTranslation[row] = translation.at<double>(row);
        for (int column = 0; column < 3; ++column) {
            secondRotation(row, column) = rotation.at<double>(row, column);
        }
    }
    const cv::Matx33d firstRotation = cv::Matx33d::eye();
    const cv::Vec3d firstTranslation(0.0, 0.0, 0.0);
    const cv::Point3d firstCentre(0.0, 0.0, 0.0);
    const cv::Vec3d secondCentreVector =
        -secondRotation.t() * secondTranslation;
    const cv::Point3d secondCentre(secondCentreVector[0],
                                   secondCentreVector[1],
                                   secondCentreVector[2]);

    cv::Mat firstProjection = cv::Mat::zeros(3, 4, CV_64F);
    cv::Mat secondProjection = cv::Mat::zeros(3, 4, CV_64F);
    cv::Mat(firstRotation, false).copyTo(firstProjection.colRange(0, 3));
    cv::Mat(secondRotation, false).copyTo(secondProjection.colRange(0, 3));
    cv::Mat(firstTranslation, false).copyTo(firstProjection.col(3));
    cv::Mat(secondTranslation, false).copyTo(secondProjection.col(3));

    std::vector<cv::Point2d> inlierFirst;
    std::vector<cv::Point2d> inlierSecond;
    std::vector<int> inlierIndices;
    for (int index = 0; index < static_cast<int>(candidate.sharedTracks.size());
         ++index) {
        if (essentialMask.at<uchar>(index)) {
            inlierFirst.push_back(firstNormalized[index]);
            inlierSecond.push_back(secondNormalized[index]);
            inlierIndices.push_back(index);
        }
    }
    cv::Mat homogeneous;
    cv::triangulatePoints(firstProjection, secondProjection,
                          inlierFirst, inlierSecond, homogeneous);

    std::vector<double> acceptedAngles;
    for (int column = 0; column < homogeneous.cols; ++column) {
        const double w = homogeneous.at<double>(3, column);
        if (!std::isfinite(w) || std::abs(w) < 1e-12) {
            continue;
        }
        const cv::Point3d point(homogeneous.at<double>(0, column) / w,
                                homogeneous.at<double>(1, column) / w,
                                homogeneous.at<double>(2, column) / w);
        if (!finitePoint(point) || !(point.z > 0.0)) {
            continue;
        }
        const cv::Vec3d secondCameraPoint = secondRotation
            * cv::Vec3d(point.x, point.y, point.z) + secondTranslation;
        if (!(secondCameraPoint[2] > 0.0)) {
            continue;
        }
        const double angle = triangulationAngle(point, firstCentre,
                                                secondCentre);
        if (angle < options.minimumTriangulationAngleDegrees) {
            continue;
        }
        const int originalIndex = inlierIndices[column];
        const cv::Point2d firstReprojected = projectPoint(
            firstCalibration, firstRotation, firstTranslation, point);
        const cv::Point2d secondReprojected = projectPoint(
            secondCalibration, secondRotation, secondTranslation, point);
        const double firstError = pointDistance(
            firstReprojected, firstPixels[originalIndex]);
        const double secondError = pointDistance(
            secondReprojected, secondPixels[originalIndex]);
        if (!std::isfinite(firstError) || !std::isfinite(secondError)
            || std::max(firstError, secondError)
                   > options.maximumReprojectionErrorPixels) {
            continue;
        }
        const double rms = std::sqrt(
            (firstError * firstError + secondError * secondError) * 0.5);
        result.points.push_back({
            candidate.sharedTracks[originalIndex].trackId, point, rms, angle});
        acceptedAngles.push_back(angle);
    }

    if (result.points.size()
        < static_cast<size_t>(options.minimumTriangulatedPoints)) {
        result.message = "Too few points survived triangulation checks.";
        result.points.clear();
        return result;
    }
    const size_t medianIndex = acceptedAngles.size() / 2;
    std::nth_element(acceptedAngles.begin(),
                     acceptedAngles.begin() + medianIndex,
                     acceptedAngles.end());
    result.medianTriangulationAngleDegrees = acceptedAngles[medianIndex];
    result.cameras = {
        {candidate.firstImage, firstRotation, firstTranslation},
        {candidate.secondImage, secondRotation, secondTranslation}};
    result.success = true;
    result.message = "Initial calibrated pair reconstructed successfully.";
    return result;
}

const SparseCameraPose *findCamera(const std::vector<SparseCameraPose> &cameras,
                                   int imageIndex)
{
    const auto camera = std::find_if(
        cameras.begin(), cameras.end(),
        [imageIndex](const SparseCameraPose &value) {
            return value.imageIndex == imageIndex;
        });
    return camera == cameras.end() ? nullptr : &*camera;
}

cv::Point2d normalizedObservation(const FeatureObservation &observation,
                                  const CameraCalibration &calibration)
{
    std::vector<cv::Point2d> source{observation.imagePoint};
    std::vector<cv::Point2d> normalized;
    cv::undistortPoints(source, normalized,
                        cv::Mat(calibration.intrinsic, true),
                        cv::Mat(calibration.distortion, true));
    return normalized.front();
}

bool triangulateTrack(const FeatureTrack &track,
                      const std::vector<CameraCalibration> &calibrations,
                      const std::vector<SparseCameraPose> &cameras,
                      const SparseInitializationOptions &options,
                      SparsePoint *result)
{
    struct View
    {
        const FeatureObservation *observation = nullptr;
        const CameraCalibration *calibration = nullptr;
        const SparseCameraPose *camera = nullptr;
        cv::Point2d normalized;
        cv::Point3d centre;
    };
    std::vector<View> views;
    for (const FeatureObservation &observation : track.observations) {
        if (observation.imageIndex < 0
            || observation.imageIndex >= static_cast<int>(calibrations.size())) {
            continue;
        }
        const SparseCameraPose *camera = findCamera(cameras,
                                                    observation.imageIndex);
        if (!camera || !calibrations[observation.imageIndex].isUsable()) {
            continue;
        }
        const cv::Vec3d centreVector =
            -camera->worldToCameraRotation.t()
            * camera->worldToCameraTranslation;
        views.push_back({
            &observation,
            &calibrations[observation.imageIndex],
            camera,
            normalizedObservation(observation,
                                  calibrations[observation.imageIndex]),
            {centreVector[0], centreVector[1], centreVector[2]}});
    }
    if (views.size() < 2) {
        return false;
    }

    cv::Mat system(static_cast<int>(views.size() * 2), 4, CV_64F);
    for (int viewIndex = 0; viewIndex < static_cast<int>(views.size());
         ++viewIndex) {
        const View &view = views[viewIndex];
        const cv::Matx33d &rotation = view.camera->worldToCameraRotation;
        const cv::Vec3d &translation = view.camera->worldToCameraTranslation;
        for (int column = 0; column < 4; ++column) {
            const double row0 = column < 3 ? rotation(0, column)
                                            : translation[0];
            const double row1 = column < 3 ? rotation(1, column)
                                            : translation[1];
            const double row2 = column < 3 ? rotation(2, column)
                                            : translation[2];
            system.at<double>(viewIndex * 2, column) =
                view.normalized.x * row2 - row0;
            system.at<double>(viewIndex * 2 + 1, column) =
                view.normalized.y * row2 - row1;
        }
    }
    cv::Mat singularValues;
    cv::Mat left;
    cv::Mat right;
    cv::SVD::compute(system, singularValues, left, right,
                     cv::SVD::FULL_UV);
    if (right.rows != 4 || right.cols != 4) {
        return false;
    }
    const double homogeneousScale = right.at<double>(3, 3);
    if (!std::isfinite(homogeneousScale)
        || std::abs(homogeneousScale) < 1e-12) {
        return false;
    }
    const cv::Point3d point(right.at<double>(3, 0) / homogeneousScale,
                            right.at<double>(3, 1) / homogeneousScale,
                            right.at<double>(3, 2) / homogeneousScale);
    if (!finitePoint(point)) {
        return false;
    }

    double squaredError = 0.0;
    double maximumAngle = 0.0;
    for (size_t index = 0; index < views.size(); ++index) {
        const View &view = views[index];
        const cv::Vec3d cameraPoint = view.camera->worldToCameraRotation
            * cv::Vec3d(point.x, point.y, point.z)
            + view.camera->worldToCameraTranslation;
        if (!(cameraPoint[2] > 0.0)) {
            return false;
        }
        const cv::Point2d reprojected = projectPoint(
            *view.calibration, view.camera->worldToCameraRotation,
            view.camera->worldToCameraTranslation, point);
        const double error = pointDistance(reprojected,
                                           view.observation->imagePoint);
        if (!std::isfinite(error)
            || error > options.maximumReprojectionErrorPixels) {
            return false;
        }
        squaredError += error * error;
        for (size_t other = index + 1; other < views.size(); ++other) {
            maximumAngle = std::max(
                maximumAngle,
                triangulationAngle(point, view.centre,
                                   views[other].centre));
        }
    }
    if (maximumAngle < options.minimumTriangulationAngleDegrees) {
        return false;
    }
    *result = {track.id, point,
               std::sqrt(squaredError / views.size()), maximumAngle};
    return true;
}

std::vector<SparsePoint> retriangulateTracks(
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks,
    const std::vector<SparseCameraPose> &cameras,
    const SparseInitializationOptions &options)
{
    std::vector<SparsePoint> points;
    points.reserve(tracks.size());
    for (const FeatureTrack &track : tracks) {
        SparsePoint point;
        if (triangulateTrack(track, calibrations, cameras, options, &point)) {
            points.push_back(point);
        }
    }
    return points;
}

struct PnpCandidate
{
    bool success = false;
    SparseCameraPose camera;
};

PnpCandidate estimateCameraPose(
    int imageIndex,
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks,
    const std::vector<SparsePoint> &points,
    const SparseInitializationOptions &options)
{
    PnpCandidate result;
    result.camera.imageIndex = imageIndex;
    if (imageIndex < 0
        || imageIndex >= static_cast<int>(calibrations.size())
        || !calibrations[imageIndex].isUsable()) {
        return result;
    }

    std::map<int, cv::Point3d> pointsByTrack;
    for (const SparsePoint &point : points) {
        pointsByTrack.insert({point.trackId, point.position});
    }
    std::vector<cv::Point3d> objectPoints;
    std::vector<cv::Point2d> imagePoints;
    for (const FeatureTrack &track : tracks) {
        const auto point = pointsByTrack.find(track.id);
        if (point == pointsByTrack.end()) {
            continue;
        }
        const auto observation = std::find_if(
            track.observations.begin(), track.observations.end(),
            [imageIndex](const FeatureObservation &value) {
                return value.imageIndex == imageIndex;
            });
        if (observation != track.observations.end()) {
            objectPoints.push_back(point->second);
            imagePoints.push_back(observation->imagePoint);
        }
    }
    if (objectPoints.size()
        < static_cast<size_t>(options.minimumPnpCorrespondences)) {
        return result;
    }

    cv::Mat rotationVector;
    cv::Mat translation;
    cv::Mat inliers;
    try {
        const bool solved = cv::solvePnPRansac(
            objectPoints, imagePoints,
            cv::Mat(calibrations[imageIndex].intrinsic, true),
            cv::Mat(calibrations[imageIndex].distortion, true),
            rotationVector, translation, false,
            options.pnpRansacIterations,
            options.pnpRansacThresholdPixels,
            options.pnpRansacConfidence, inliers, cv::SOLVEPNP_EPNP);
        if (!solved || inliers.rows < options.minimumPnpInliers) {
            return result;
        }

        std::vector<cv::Point3d> inlierObjects;
        std::vector<cv::Point2d> inlierImages;
        inlierObjects.reserve(inliers.rows);
        inlierImages.reserve(inliers.rows);
        for (int row = 0; row < inliers.rows; ++row) {
            const int index = inliers.at<int>(row);
            inlierObjects.push_back(objectPoints[index]);
            inlierImages.push_back(imagePoints[index]);
        }
        cv::solvePnP(inlierObjects, inlierImages,
                     cv::Mat(calibrations[imageIndex].intrinsic, true),
                     cv::Mat(calibrations[imageIndex].distortion, true),
                     rotationVector, translation, true,
                     cv::SOLVEPNP_ITERATIVE);
    } catch (const cv::Exception &) {
        return result;
    }

    cv::Mat rotation;
    cv::Rodrigues(rotationVector, rotation);
    if (!cv::checkRange(rotation) || !cv::checkRange(translation)) {
        return result;
    }
    for (int row = 0; row < 3; ++row) {
        result.camera.worldToCameraTranslation[row] =
            translation.at<double>(row);
        for (int column = 0; column < 3; ++column) {
            result.camera.worldToCameraRotation(row, column) =
                rotation.at<double>(row, column);
        }
    }

    double squaredError = 0.0;
    int positiveDepthCount = 0;
    for (int row = 0; row < inliers.rows; ++row) {
        const int index = inliers.at<int>(row);
        const cv::Point3d &point = objectPoints[index];
        const cv::Vec3d cameraPoint = result.camera.worldToCameraRotation
            * cv::Vec3d(point.x, point.y, point.z)
            + result.camera.worldToCameraTranslation;
        positiveDepthCount += cameraPoint[2] > 0.0 ? 1 : 0;
        const double error = pointDistance(
            projectPoint(calibrations[imageIndex],
                         result.camera.worldToCameraRotation,
                         result.camera.worldToCameraTranslation, point),
            imagePoints[index]);
        squaredError += error * error;
    }
    if (positiveDepthCount < options.minimumPnpInliers
        || positiveDepthCount * 10 < inliers.rows * 9) {
        return result;
    }
    result.camera.pnpInliers = inliers.rows;
    result.camera.pnpReprojectionRmsPixels =
        std::sqrt(squaredError / inliers.rows);
    result.success = std::isfinite(
        result.camera.pnpReprojectionRmsPixels);
    return result;
}

void growConnectedComponent(
    SparseInitializationResult *result,
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks,
    const SparseInitializationOptions &options)
{
    while (result->cameras.size() < calibrations.size()) {
        PnpCandidate best;
        for (int imageIndex = 0;
             imageIndex < static_cast<int>(calibrations.size()); ++imageIndex) {
            if (findCamera(result->cameras, imageIndex)
                || !calibrations[imageIndex].isUsable()) {
                continue;
            }
            ++result->pnpAttempts;
            PnpCandidate candidate = estimateCameraPose(
                imageIndex, calibrations, tracks, result->points, options);
            if (candidate.success
                && (!best.success
                    || candidate.camera.pnpInliers > best.camera.pnpInliers
                    || (candidate.camera.pnpInliers == best.camera.pnpInliers
                        && candidate.camera.pnpReprojectionRmsPixels
                           < best.camera.pnpReprojectionRmsPixels))) {
                best = candidate;
            }
        }
        if (!best.success) {
            break;
        }
        result->cameras.push_back(best.camera);
        ++result->pnpRegisteredCameraCount;
        result->points = retriangulateTracks(calibrations, tracks,
                                             result->cameras, options);
    }
}

} // namespace

SparseInitializationResult SparseReconstructor::initialize(
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks,
    const SparseInitializationOptions &options)
{
    if (calibrations.size() < 2) {
        return {false, "At least two camera calibrations are required."};
    }
    std::vector<PairCandidate> candidates = buildCandidates(calibrations,
                                                            tracks);
    if (candidates.empty()) {
        return {false, "No calibrated image pair shares feature tracks."};
    }

    const size_t candidateLimit = std::min(
        candidates.size(),
        static_cast<size_t>(std::max(1, options.maximumCandidatePairs)));
    SparseInitializationResult best;
    double bestScore = -1.0;
    for (size_t index = 0; index < candidateLimit; ++index) {
        if (candidates[index].sharedTracks.size()
            < static_cast<size_t>(options.minimumSharedTracks)) {
            break;
        }
        SparseInitializationResult evaluated = evaluateCandidate(
            candidates[index], calibrations, options);
        if (!evaluated.success) {
            if (best.message.empty()) {
                best = std::move(evaluated);
            }
            continue;
        }
        const double score = evaluated.points.size()
            * std::min(10.0, evaluated.medianTriangulationAngleDegrees);
        if (score > bestScore) {
            bestScore = score;
            best = std::move(evaluated);
        }
    }
    if (bestScore < 0.0) {
        if (best.message.empty()) {
            best.message = "No candidate pair passed sparse initialization checks.";
        }
        return best;
    }
    return best;
}

SparseInitializationResult SparseReconstructor::reconstruct(
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks,
    const SparseInitializationOptions &options)
{
    SparseInitializationResult result = initialize(calibrations, tracks,
                                                    options);
    if (!result.success) {
        return result;
    }

    result.componentCount = 1;
    growConnectedComponent(&result, calibrations, tracks, options);

    while (true) {
        std::vector<CameraCalibration> remainingCalibrations = calibrations;
        for (const SparseCameraPose &camera : result.cameras) {
            remainingCalibrations[camera.imageIndex].quality =
                CalibrationQuality::Invalid;
        }
        SparseInitializationResult component = initialize(
            remainingCalibrations, tracks, options);
        if (!component.success) {
            break;
        }
        growConnectedComponent(&component, remainingCalibrations,
                               tracks, options);
        const int componentId = result.componentCount++;
        for (SparseCameraPose &camera : component.cameras) {
            camera.componentId = componentId;
            result.cameras.push_back(camera);
        }
        for (SparsePoint &point : component.points) {
            point.componentId = componentId;
            result.points.push_back(point);
        }
        result.pnpAttempts += component.pnpAttempts;
        result.pnpRegisteredCameraCount +=
            component.pnpRegisteredCameraCount;
    }

    for (int imageIndex = 0;
         imageIndex < static_cast<int>(calibrations.size()); ++imageIndex) {
        if (!findCamera(result.cameras, imageIndex)) {
            result.unregisteredImageIndices.push_back(imageIndex);
        }
    }
    if (!result.unregisteredImageIndices.empty()) {
        result.message = "Sparse reconstruction completed with unregistered cameras.";
    } else if (result.componentCount > 1) {
        result.message = "All cameras reconstructed in disconnected components.";
    } else {
        result.message = "All cameras registered in one sparse component.";
    }
    return result;
}

} // namespace kestrel
