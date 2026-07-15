#include "photogrammetry/mosaic/StitchingController.h"
#include "photogrammetry/camera/CameraCalibration.h"
#include "photogrammetry/geo/GeoCoordinates.h"
#include "photogrammetry/io/CaptureGrouping.h"
#include "photogrammetry/io/TiffImageMetadata.h"
#include "photogrammetry/sfm/FeatureTracks.h"
#include "photogrammetry/sfm/BundleAdjustment.h"
#include "photogrammetry/sfm/ReconstructionAlignment.h"
#include "photogrammetry/sfm/SparseReconstruction.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMetaObject>
#include <QSaveFile>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/flann.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace {

constexpr int kMatchDimension = 1400;
constexpr int kSpatialNeighbours = 10;
constexpr int kSequenceFallbackNeighbours = 2;
constexpr int kMinimumInliers = 20;
constexpr int kBundleMatchesPerPair = 300;
constexpr int kTrackMatchesPerPair = 500;
constexpr int kBundleMaximumIterations = 8;
constexpr double kBundleHuberThreshold = 5.0;
constexpr double kRatioTest = 0.74;
constexpr qint64 kMaximumCanvasPixels = 50000000;

struct Capture
{
    QString name;
    QString stableId;
    QMap<int, QString> bandFiles;
    QMap<int, kestrel::TiffImageMetadata> bandMetadata;
    QMap<int, kestrel::CameraCalibration> bandCalibrations;
    kestrel::TiffImageMetadata metadata;
    QStringList metadataWarnings;
    QStringList calibrationWarnings;
    cv::Size fullSize;
    double matchScale = 1.0;
    double brightness = 0.0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    cv::Point3d gpsEnu;
    bool hasGps = false;
    bool hasGpsAltitude = false;
    cv::Mat matchImage;
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
};

struct CandidatePair
{
    int first = -1;
    int second = -1;
};

cv::Mat readImage(const QString &filePath, int flags)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray encoded = file.readAll();
    if (encoded.isEmpty()) {
        return {};
    }
    const cv::Mat buffer(1, encoded.size(), CV_8U,
                         const_cast<char *>(encoded.constData()));
    return cv::imdecode(buffer, flags);
}

bool writeImage(const QString &filePath, const cv::Mat &image)
{
    const std::string extension = "." + QFileInfo(filePath).suffix().toStdString();
    std::vector<uchar> encoded;
    if (!cv::imencode(extension, image, encoded)) {
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(reinterpret_cast<const char *>(encoded.data()),
                      static_cast<qint64>(encoded.size())) != static_cast<qint64>(encoded.size())) {
        return false;
    }
    return file.commit();
}

struct PairTransform
{
    int first = -1;
    int second = -1;
    int inliers = 0;
    int fundamentalInliers = 0;
    cv::Mat homography; // Maps first image coordinates into second.
    cv::Mat fundamental;
    std::vector<cv::Point2f> firstInlierPoints;
    std::vector<cv::Point2f> secondInlierPoints;
    std::vector<kestrel::PairwiseFeatureMatch> featureMatches;
};

struct BundleAdjustmentSummary
{
    bool success = false;
    int iterations = 0;
    size_t observations = 0;
    double initialRms = 0.0;
    double finalRms = 0.0;
};

const char *calibrationQualityName(kestrel::CalibrationQuality quality)
{
    switch (quality) {
    case kestrel::CalibrationQuality::MetadataCalibrated:
        return "metadata_calibrated";
    case kestrel::CalibrationQuality::Approximate:
        return "approximate";
    case kestrel::CalibrationQuality::Invalid:
        return "invalid";
    }
    return "invalid";
}

cv::Mat singleChannel(const cv::Mat &input)
{
    if (input.channels() == 1) {
        return input;
    }
    cv::Mat gray;
    cv::cvtColor(input, gray, input.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
    return gray;
}

cv::Mat normalizeBand(const cv::Mat &input)
{
    cv::Mat band = singleChannel(input);
    cv::Mat floatBand;
    band.convertTo(floatBand, CV_32F);

    // Percentile contrast resists a few hot/dead pixels without changing the
    // spatial information used by registration.
    std::vector<float> samples;
    const int stride = std::max(1, static_cast<int>(std::sqrt(
        static_cast<double>(floatBand.total()) / 200000.0)));
    samples.reserve(floatBand.total() / (stride * stride) + 1);
    for (int y = 0; y < floatBand.rows; y += stride) {
        const float *row = floatBand.ptr<float>(y);
        for (int x = 0; x < floatBand.cols; x += stride) {
            const float value = row[x];
            if (std::isfinite(value)) {
                samples.push_back(value);
            }
        }
    }
    if (samples.empty()) {
        return {};
    }

    const auto percentile = [&samples](double fraction) {
        const size_t index = std::min(samples.size() - 1,
                                      static_cast<size_t>(fraction * samples.size()));
        std::nth_element(samples.begin(), samples.begin() + index, samples.end());
        return samples[index];
    };
    const float low = percentile(0.01);
    const float high = percentile(0.99);
    if (!(high > low)) {
        return {};
    }

    cv::Mat output;
    floatBand = (floatBand - low) * (255.0f / (high - low));
    cv::min(floatBand, 255.0, floatBand);
    cv::max(floatBand, 0.0, floatBand);
    floatBand.convertTo(output, CV_8U);
    return output;
}

cv::Mat alignBand(const cv::Mat &moving, const cv::Mat &reference)
{
    if (moving.empty() || reference.empty() || moving.size() != reference.size()) {
        return moving;
    }

    const double scale = std::min(1.0, 900.0 / std::max(reference.cols, reference.rows));
    cv::Mat movingSmall;
    cv::Mat referenceSmall;
    cv::resize(moving, movingSmall, {}, scale, scale, cv::INTER_AREA);
    cv::resize(reference, referenceSmall, {}, scale, scale, cv::INTER_AREA);
    movingSmall.convertTo(movingSmall, CV_32F, 1.0 / 255.0);
    referenceSmall.convertTo(referenceSmall, CV_32F, 1.0 / 255.0);

    cv::Mat smallWarp = cv::Mat::eye(3, 3, CV_32F);
    try {
        cv::findTransformECC(referenceSmall, movingSmall, smallWarp,
                             cv::MOTION_HOMOGRAPHY,
                             cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS,
                                              70, 1e-5),
                             cv::noArray(), 5);
    } catch (const cv::Exception &) {
        // Different spectral bands can occasionally have too little common
        // texture for ECC. The capture remains usable, but uncorrected.
        return moving;
    }

    cv::Mat warp64;
    smallWarp.convertTo(warp64, CV_64F);
    const cv::Mat scaleMatrix = (cv::Mat_<double>(3, 3) <<
        scale, 0, 0,
        0, scale, 0,
        0, 0, 1);
    const cv::Mat fullWarp = scaleMatrix.inv() * warp64 * scaleMatrix;
    cv::Mat aligned;
    cv::warpPerspective(moving, aligned, fullWarp, reference.size(),
                        cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,
                        cv::BORDER_REFLECT);
    return aligned;
}

cv::Mat loadCaptureImage(const Capture &capture, QString *errorMessage)
{
    if (capture.bandFiles.contains(1) && capture.bandFiles.contains(2)
        && capture.bandFiles.contains(3)) {
        cv::Mat blue = normalizeBand(readImage(capture.bandFiles.value(1), cv::IMREAD_UNCHANGED));
        cv::Mat green = normalizeBand(readImage(capture.bandFiles.value(2), cv::IMREAD_UNCHANGED));
        cv::Mat red = normalizeBand(readImage(capture.bandFiles.value(3), cv::IMREAD_UNCHANGED));
        if (blue.empty() || green.empty() || red.empty()) {
            *errorMessage = "Could not decode the Blue, Green, and Red bands for " + capture.name + ".";
            return {};
        }
        blue = alignBand(blue, green);
        red = alignBand(red, green);
        cv::Mat rgb;
        cv::merge(std::vector<cv::Mat>{blue, green, red}, rgb); // OpenCV BGR order.
        return rgb;
    }

    const QString filePath = capture.bandFiles.first();
    cv::Mat image = readImage(filePath, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        *errorMessage = "Could not decode " + QFileInfo(filePath).fileName() + ".";
        return {};
    }
    if (image.channels() == 3 || image.channels() == 4) {
        if (image.channels() == 4) {
            cv::cvtColor(image, image, cv::COLOR_BGRA2BGR);
        }
        if (image.depth() != CV_8U) {
            std::vector<cv::Mat> channels;
            cv::split(image, channels);
            for (cv::Mat &channel : channels) {
                channel = normalizeBand(channel);
            }
            cv::merge(channels, image);
        }
        return image;
    }
    cv::Mat gray = normalizeBand(image);
    if (gray.empty()) {
        *errorMessage = "Could not normalize " + QFileInfo(filePath).fileName() + ".";
        return {};
    }
    cv::cvtColor(gray, image, cv::COLOR_GRAY2BGR);
    return image;
}

cv::Mat loadRegistrationImage(const Capture &capture, QString *errorMessage)
{
    // A single spectral band contains all spatial information needed for
    // capture-to-capture registration. Aligning RGB bands here would repeat
    // the expensive ECC work later performed for the final mosaic.
    if (capture.bandFiles.contains(2)) {
        cv::Mat green = normalizeBand(
            readImage(capture.bandFiles.value(2), cv::IMREAD_UNCHANGED));
        if (green.empty()) {
            *errorMessage = "Could not decode the Green band for " + capture.name + ".";
        }
        return green;
    }

    cv::Mat image = readImage(capture.bandFiles.first(), cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        *errorMessage = "Could not decode "
                        + QFileInfo(capture.bandFiles.first()).fileName() + ".";
        return {};
    }
    if (image.depth() != CV_8U || image.channels() != 1) {
        image = normalizeBand(image);
    }
    return singleChannel(image);
}

double locationDistanceSquared(const Capture &first, const Capture &second)
{
    const double x = second.gpsEnu.x - first.gpsEnu.x;
    const double y = second.gpsEnu.y - first.gpsEnu.y;
    return x * x + y * y;
}

std::vector<CandidatePair> candidatePairs(const std::vector<Capture> &captures)
{
    std::set<std::pair<int, int>> uniquePairs;
    const int captureCount = static_cast<int>(captures.size());

    // Always retain a small sequence fallback for TIFFs without GPS and for
    // cameras whose GPS momentarily drops out during a flight.
    for (int first = 0; first < captureCount; ++first) {
        const int last = std::min(captureCount,
                                  first + kSequenceFallbackNeighbours + 1);
        for (int second = first + 1; second < last; ++second) {
            uniquePairs.emplace(first, second);
        }
    }

    for (int first = 0; first < captureCount; ++first) {
        if (!captures[first].hasGps) {
            continue;
        }
        std::vector<std::pair<double, int>> distances;
        distances.reserve(captures.size());
        for (int second = 0; second < captureCount; ++second) {
            if (second != first && captures[second].hasGps) {
                distances.emplace_back(locationDistanceSquared(captures[first], captures[second]),
                                       second);
            }
        }
        const int neighbourCount = std::min(kSpatialNeighbours,
                                            static_cast<int>(distances.size()));
        std::partial_sort(distances.begin(), distances.begin() + neighbourCount,
                          distances.end());
        for (int index = 0; index < neighbourCount; ++index) {
            uniquePairs.emplace(std::min(first, distances[index].second),
                                std::max(first, distances[index].second));
        }
    }

    std::vector<CandidatePair> result;
    result.reserve(uniquePairs.size());
    for (const auto &[first, second] : uniquePairs) {
        result.push_back({first, second});
    }
    return result;
}

std::vector<Capture> discoverCaptures(const QString &rawImagesPath,
                                      int *metadataFileCount)
{
    QStringList files;
    QDirIterator iterator(rawImagesPath, {"*.tif", "*.tiff", "*.TIF", "*.TIFF"},
                         QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        files.append(iterator.next());
    }

    const kestrel::CaptureGroupingResult grouped =
        kestrel::CaptureGrouping::groupFiles(files);
    if (metadataFileCount) {
        *metadataFileCount = grouped.metadataFileCount;
    }
    std::vector<Capture> captures;
    captures.reserve(grouped.captures.size());
    for (const kestrel::GroupedCapture &group : grouped.captures) {
        Capture capture;
        capture.name = group.displayName;
        capture.stableId = group.stableId;
        capture.metadataWarnings = group.warnings;
        for (auto band = group.bands.cbegin(); band != group.bands.cend(); ++band) {
            capture.bandFiles.insert(band.key(), band.value().filePath);
            capture.bandMetadata.insert(band.key(), band.value().metadata);
            capture.bandCalibrations.insert(
                band.key(), kestrel::CameraCalibrationFactory::fromMetadata(
                                band.value().metadata));
        }
        captures.push_back(std::move(capture));
    }
    return captures;
}

bool estimatePair(const Capture &first, const Capture &second, PairTransform *result)
{
    if (first.descriptors.empty() || second.descriptors.empty()) {
        return false;
    }
    cv::FlannBasedMatcher matcher(
        cv::makePtr<cv::flann::KDTreeIndexParams>(5),
        cv::makePtr<cv::flann::SearchParams>(64));
    std::vector<std::vector<cv::DMatch>> neighbours;
    matcher.knnMatch(first.descriptors, second.descriptors, neighbours, 2);

    std::vector<cv::DMatch> goodMatches;
    goodMatches.reserve(neighbours.size());
    for (const auto &pair : neighbours) {
        if (pair.size() == 2 && pair[0].distance < kRatioTest * pair[1].distance) {
            goodMatches.push_back(pair[0]);
        }
    }
    std::sort(goodMatches.begin(), goodMatches.end(),
              [](const cv::DMatch &left, const cv::DMatch &right) {
                  return left.distance < right.distance;
              });

    // A feature in the second image may be the nearest neighbour of several
    // descriptors. Keep only its strongest assignment before robust geometry
    // so one keypoint cannot create multiple identities in a track.
    std::set<int> usedSecondFeatures;
    std::vector<cv::Point2f> firstPoints;
    std::vector<cv::Point2f> secondPoints;
    std::vector<int> firstFeatureIndices;
    std::vector<int> secondFeatureIndices;
    for (const cv::DMatch &match : goodMatches) {
        if (usedSecondFeatures.insert(match.trainIdx).second) {
            firstPoints.push_back(first.keypoints[match.queryIdx].pt);
            secondPoints.push_back(second.keypoints[match.trainIdx].pt);
            firstFeatureIndices.push_back(match.queryIdx);
            secondFeatureIndices.push_back(match.trainIdx);
        }
    }
    if (firstPoints.size() < static_cast<size_t>(kMinimumInliers)) {
        return false;
    }

    cv::Mat fundamentalMask;
    cv::Mat fundamental = cv::findFundamentalMat(
        firstPoints, secondPoints, cv::FM_RANSAC, 1.5, 0.999,
        fundamentalMask);
    if (fundamental.rows != 3 || fundamental.cols != 3
        || !cv::checkRange(fundamental)) {
        return false;
    }
    const int fundamentalInliers = cv::countNonZero(fundamentalMask);
    if (fundamentalInliers < kMinimumInliers) {
        return false;
    }

    cv::Mat inlierMask;
    cv::Mat homography = cv::findHomography(firstPoints, secondPoints, cv::RANSAC, 3.0,
                                            inlierMask, 3000, 0.995);
    if (homography.empty()) {
        return false;
    }
    const int inliers = cv::countNonZero(inlierMask);
    if (inliers < kMinimumInliers) {
        return false;
    }
    homography /= homography.at<double>(2, 2);
    if (!cv::checkRange(homography)) {
        return false;
    }
    result->inliers = inliers;
    result->fundamentalInliers = fundamentalInliers;
    result->homography = homography;
    const cv::Mat firstToMatching = (cv::Mat_<double>(3, 3) <<
        first.matchScale, 0.0, 0.0,
        0.0, first.matchScale, 0.0,
        0.0, 0.0, 1.0);
    const cv::Mat secondToMatching = (cv::Mat_<double>(3, 3) <<
        second.matchScale, 0.0, 0.0,
        0.0, second.matchScale, 0.0,
        0.0, 0.0, 1.0);
    result->fundamental = secondToMatching.t() * fundamental
                          * firstToMatching;

    std::vector<int> fundamentalIndices;
    fundamentalIndices.reserve(fundamentalInliers);
    for (int index = 0; index < static_cast<int>(firstPoints.size()); ++index) {
        if (fundamentalMask.at<uchar>(index)) {
            fundamentalIndices.push_back(index);
        }
    }
    const size_t trackMatchCount = std::min(
        fundamentalIndices.size(), static_cast<size_t>(kTrackMatchesPerPair));
    result->featureMatches.reserve(trackMatchCount);
    for (size_t retained = 0; retained < trackMatchCount; ++retained) {
        const size_t sampled = retained * fundamentalIndices.size()
                               / trackMatchCount;
        const int index = fundamentalIndices[sampled];
        result->featureMatches.push_back({
            {result->first, firstFeatureIndices[index],
             firstPoints[index] * (1.0 / first.matchScale)},
            {result->second, secondFeatureIndices[index],
             secondPoints[index] * (1.0 / second.matchScale)}});
    }

    std::vector<int> inlierIndices;
    inlierIndices.reserve(inliers);
    for (int index = 0; index < inlierMask.rows; ++index) {
        if (inlierMask.at<uchar>(index, 0)) {
            inlierIndices.push_back(index);
        }
    }
    const size_t retainedCount = std::min(
        inlierIndices.size(), static_cast<size_t>(kBundleMatchesPerPair));
    result->firstInlierPoints.reserve(retainedCount);
    result->secondInlierPoints.reserve(retainedCount);
    for (size_t retained = 0; retained < retainedCount; ++retained) {
        const size_t sampled = retained * inlierIndices.size() / retainedCount;
        const int index = inlierIndices[sampled];
        result->firstInlierPoints.push_back(firstPoints[index]);
        result->secondInlierPoints.push_back(secondPoints[index]);
    }
    return true;
}

bool solveGlobalTransforms(int captureCount, const std::vector<PairTransform> &pairs,
                           std::vector<cv::Mat> *globalTransforms, int *rootIndex,
                           QString *errorMessage)
{
    globalTransforms->assign(captureCount, cv::Mat());
    if (captureCount == 1) {
        *rootIndex = 0;
        (*globalTransforms)[0] = cv::Mat::eye(3, 3, CV_64F);
        return true;
    }

    std::vector<int> degree(captureCount, 0);
    std::vector<std::vector<int>> adjacency(captureCount);
    for (size_t i = 0; i < pairs.size(); ++i) {
        degree[pairs[i].first] += pairs[i].inliers;
        degree[pairs[i].second] += pairs[i].inliers;
        adjacency[pairs[i].first].push_back(static_cast<int>(i));
        adjacency[pairs[i].second].push_back(static_cast<int>(i));
    }
    const int root = static_cast<int>(std::distance(
        degree.begin(), std::max_element(degree.begin(), degree.end())));
    *rootIndex = root;
    (*globalTransforms)[root] = cv::Mat::eye(3, 3, CV_64F);

    struct Candidate {
        int score;
        int pairIndex;
        bool operator<(const Candidate &other) const { return score < other.score; }
    };
    std::priority_queue<Candidate> queue;
    std::vector<bool> reached(captureCount, false);
    reached[root] = true;
    int reachedCount = 1;
    for (int edge : adjacency[root]) {
        queue.push({pairs[edge].inliers, edge});
    }

    while (!queue.empty() && reachedCount < captureCount) {
        const PairTransform &pair = pairs[queue.top().pairIndex];
        queue.pop();
        if (reached[pair.first] == reached[pair.second]) {
            continue;
        }
        const int discovered = reached[pair.first] ? pair.second : pair.first;
        if (reached[pair.first]) {
            cv::Mat inverse;
            if (!cv::invert(pair.homography, inverse, cv::DECOMP_SVD)) {
                continue;
            }
            (*globalTransforms)[pair.second] = (*globalTransforms)[pair.first] * inverse;
        } else {
            (*globalTransforms)[pair.first] = (*globalTransforms)[pair.second] * pair.homography;
        }
        reached[discovered] = true;
        ++reachedCount;
        for (int edge : adjacency[discovered]) {
            queue.push({pairs[edge].inliers, edge});
        }
    }

    if (reachedCount != captureCount) {
        *errorMessage = QString("Only %1 of %2 captures could be connected. "
                                "The flight needs more overlap or fewer blurred images.")
                            .arg(reachedCount).arg(captureCount);
        return false;
    }
    return true;
}

bool projectPoint(const cv::Mat &homography, const cv::Point2f &point,
                  cv::Point2d *projected, cv::Matx<double, 2, 8> *jacobian = nullptr)
{
    const double x = point.x;
    const double y = point.y;
    const double denominator = homography.at<double>(2, 0) * x
                               + homography.at<double>(2, 1) * y
                               + homography.at<double>(2, 2);
    if (!std::isfinite(denominator) || std::abs(denominator) < 1e-9) {
        return false;
    }

    const double inverseDenominator = 1.0 / denominator;
    projected->x = (homography.at<double>(0, 0) * x
                    + homography.at<double>(0, 1) * y
                    + homography.at<double>(0, 2)) * inverseDenominator;
    projected->y = (homography.at<double>(1, 0) * x
                    + homography.at<double>(1, 1) * y
                    + homography.at<double>(1, 2)) * inverseDenominator;
    if (!std::isfinite(projected->x) || !std::isfinite(projected->y)) {
        return false;
    }

    if (jacobian) {
        *jacobian = cv::Matx<double, 2, 8>::zeros();
        (*jacobian)(0, 0) = x * inverseDenominator;
        (*jacobian)(0, 1) = y * inverseDenominator;
        (*jacobian)(0, 2) = inverseDenominator;
        (*jacobian)(0, 6) = -projected->x * x * inverseDenominator;
        (*jacobian)(0, 7) = -projected->x * y * inverseDenominator;
        (*jacobian)(1, 3) = x * inverseDenominator;
        (*jacobian)(1, 4) = y * inverseDenominator;
        (*jacobian)(1, 5) = inverseDenominator;
        (*jacobian)(1, 6) = -projected->y * x * inverseDenominator;
        (*jacobian)(1, 7) = -projected->y * y * inverseDenominator;
    }
    return true;
}

double huberLoss(double error)
{
    if (error <= kBundleHuberThreshold) {
        return 0.5 * error * error;
    }
    return kBundleHuberThreshold * (error - 0.5 * kBundleHuberThreshold);
}

struct BundleMetrics
{
    double cost = 0.0;
    double squaredError = 0.0;
    size_t observations = 0;
};

BundleMetrics bundleMetrics(const std::vector<PairTransform> &pairs,
                            const std::vector<cv::Mat> &transforms)
{
    BundleMetrics metrics;
    for (const PairTransform &pair : pairs) {
        for (size_t index = 0; index < pair.firstInlierPoints.size(); ++index) {
            cv::Point2d firstPoint;
            cv::Point2d secondPoint;
            if (!projectPoint(transforms[pair.first], pair.firstInlierPoints[index],
                              &firstPoint)
                || !projectPoint(transforms[pair.second], pair.secondInlierPoints[index],
                                 &secondPoint)) {
                continue;
            }
            const double error = cv::norm(firstPoint - secondPoint);
            metrics.cost += huberLoss(error);
            metrics.squaredError += error * error;
            ++metrics.observations;
        }
    }
    return metrics;
}

double cameraBundleCost(int cameraIndex, const cv::Mat &candidate,
                        const std::vector<PairTransform> &pairs,
                        const std::vector<std::vector<int>> &adjacency,
                        const std::vector<cv::Mat> &transforms)
{
    double cost = 0.0;
    size_t observations = 0;
    for (int pairIndex : adjacency[cameraIndex]) {
        const PairTransform &pair = pairs[pairIndex];
        const bool isFirst = pair.first == cameraIndex;
        const int neighbourIndex = isFirst ? pair.second : pair.first;
        const std::vector<cv::Point2f> &cameraPoints = isFirst
            ? pair.firstInlierPoints : pair.secondInlierPoints;
        const std::vector<cv::Point2f> &neighbourPoints = isFirst
            ? pair.secondInlierPoints : pair.firstInlierPoints;
        for (size_t index = 0; index < cameraPoints.size(); ++index) {
            cv::Point2d cameraPoint;
            cv::Point2d neighbourPoint;
            if (!projectPoint(candidate, cameraPoints[index], &cameraPoint)
                || !projectPoint(transforms[neighbourIndex], neighbourPoints[index],
                                 &neighbourPoint)) {
                continue;
            }
            cost += huberLoss(cv::norm(cameraPoint - neighbourPoint));
            ++observations;
        }
    }
    return observations >= 4 ? cost : std::numeric_limits<double>::infinity();
}

bool refineCameraTransform(int cameraIndex,
                           const std::vector<PairTransform> &pairs,
                           const std::vector<std::vector<int>> &adjacency,
                           std::vector<cv::Mat> *transforms)
{
    cv::Matx<double, 8, 8> normal = cv::Matx<double, 8, 8>::zeros();
    cv::Vec<double, 8> gradient = cv::Vec<double, 8>::all(0.0);
    size_t observations = 0;

    for (int pairIndex : adjacency[cameraIndex]) {
        const PairTransform &pair = pairs[pairIndex];
        const bool isFirst = pair.first == cameraIndex;
        const int neighbourIndex = isFirst ? pair.second : pair.first;
        const std::vector<cv::Point2f> &cameraPoints = isFirst
            ? pair.firstInlierPoints : pair.secondInlierPoints;
        const std::vector<cv::Point2f> &neighbourPoints = isFirst
            ? pair.secondInlierPoints : pair.firstInlierPoints;

        for (size_t index = 0; index < cameraPoints.size(); ++index) {
            cv::Point2d cameraPoint;
            cv::Point2d neighbourPoint;
            cv::Matx<double, 2, 8> jacobian;
            if (!projectPoint((*transforms)[cameraIndex], cameraPoints[index],
                              &cameraPoint, &jacobian)
                || !projectPoint((*transforms)[neighbourIndex], neighbourPoints[index],
                                 &neighbourPoint)) {
                continue;
            }

            const cv::Point2d residual = cameraPoint - neighbourPoint;
            const double error = cv::norm(residual);
            const double weight = error > kBundleHuberThreshold
                ? kBundleHuberThreshold / std::max(error, 1e-12) : 1.0;
            for (int row = 0; row < 8; ++row) {
                gradient[row] += weight * (jacobian(0, row) * residual.x
                                           + jacobian(1, row) * residual.y);
                for (int column = row; column < 8; ++column) {
                    normal(row, column) += weight
                        * (jacobian(0, row) * jacobian(0, column)
                           + jacobian(1, row) * jacobian(1, column));
                }
            }
            ++observations;
        }
    }
    if (observations < 4) {
        return false;
    }

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < row; ++column) {
            normal(row, column) = normal(column, row);
        }
        normal(row, row) += 1e-6 * std::max(1.0, normal(row, row));
    }

    cv::Mat normalMatrix(8, 8, CV_64F, normal.val);
    cv::Mat gradientMatrix(8, 1, CV_64F, gradient.val);
    cv::Mat delta;
    if (!cv::solve(normalMatrix, -gradientMatrix, delta, cv::DECOMP_CHOLESKY)
        && !cv::solve(normalMatrix, -gradientMatrix, delta, cv::DECOMP_SVD)) {
        return false;
    }

    const cv::Mat current = (*transforms)[cameraIndex].clone();
    const double currentCost = cameraBundleCost(cameraIndex, current, pairs,
                                                adjacency, *transforms);
    double stepScale = 1.0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        cv::Mat candidate = current.clone();
        candidate.at<double>(0, 0) += stepScale * delta.at<double>(0);
        candidate.at<double>(0, 1) += stepScale * delta.at<double>(1);
        candidate.at<double>(0, 2) += stepScale * delta.at<double>(2);
        candidate.at<double>(1, 0) += stepScale * delta.at<double>(3);
        candidate.at<double>(1, 1) += stepScale * delta.at<double>(4);
        candidate.at<double>(1, 2) += stepScale * delta.at<double>(5);
        candidate.at<double>(2, 0) += stepScale * delta.at<double>(6);
        candidate.at<double>(2, 1) += stepScale * delta.at<double>(7);
        candidate.at<double>(2, 2) = 1.0;
        if (cv::checkRange(candidate)
            && cameraBundleCost(cameraIndex, candidate, pairs, adjacency,
                                *transforms) < currentCost) {
            (*transforms)[cameraIndex] = candidate;
            return true;
        }
        stepScale *= 0.5;
    }
    return false;
}

BundleAdjustmentSummary adjustGlobalTransforms(
    int rootIndex, const std::vector<PairTransform> &pairs,
    std::vector<cv::Mat> *transforms)
{
    BundleAdjustmentSummary summary;
    if (transforms->size() < 2 || pairs.empty()) {
        return summary;
    }

    const std::vector<cv::Mat> initialTransforms = *transforms;
    std::vector<std::vector<int>> adjacency(transforms->size());
    for (size_t index = 0; index < pairs.size(); ++index) {
        adjacency[pairs[index].first].push_back(static_cast<int>(index));
        adjacency[pairs[index].second].push_back(static_cast<int>(index));
    }

    std::vector<int> distance(transforms->size(), std::numeric_limits<int>::max());
    std::queue<int> queue;
    distance[rootIndex] = 0;
    queue.push(rootIndex);
    while (!queue.empty()) {
        const int camera = queue.front();
        queue.pop();
        for (int pairIndex : adjacency[camera]) {
            const PairTransform &pair = pairs[pairIndex];
            const int neighbour = pair.first == camera ? pair.second : pair.first;
            if (distance[neighbour] == std::numeric_limits<int>::max()) {
                distance[neighbour] = distance[camera] + 1;
                queue.push(neighbour);
            }
        }
    }
    std::vector<int> updateOrder(transforms->size());
    std::iota(updateOrder.begin(), updateOrder.end(), 0);
    std::stable_sort(updateOrder.begin(), updateOrder.end(),
                     [&distance](int first, int second) {
                         return distance[first] < distance[second];
                     });

    const BundleMetrics initialMetrics = bundleMetrics(pairs, *transforms);
    if (initialMetrics.observations == 0 || !std::isfinite(initialMetrics.cost)) {
        return summary;
    }
    summary.observations = initialMetrics.observations;
    summary.initialRms = std::sqrt(initialMetrics.squaredError
                                   / initialMetrics.observations);
    double previousCost = initialMetrics.cost;

    for (int iteration = 0; iteration < kBundleMaximumIterations; ++iteration) {
        for (int camera : updateOrder) {
            if (camera != rootIndex) {
                refineCameraTransform(camera, pairs, adjacency, transforms);
            }
        }
        for (auto it = updateOrder.rbegin(); it != updateOrder.rend(); ++it) {
            if (*it != rootIndex) {
                refineCameraTransform(*it, pairs, adjacency, transforms);
            }
        }

        const BundleMetrics metrics = bundleMetrics(pairs, *transforms);
        if (!std::isfinite(metrics.cost) || metrics.cost > previousCost) {
            *transforms = initialTransforms;
            return BundleAdjustmentSummary{};
        }
        summary.iterations = iteration + 1;
        const double relativeImprovement = (previousCost - metrics.cost)
                                           / std::max(previousCost, 1e-12);
        previousCost = metrics.cost;
        if (relativeImprovement < 1e-4) {
            break;
        }
    }

    const BundleMetrics finalMetrics = bundleMetrics(pairs, *transforms);
    summary.finalRms = std::sqrt(finalMetrics.squaredError
                                 / std::max<size_t>(1, finalMetrics.observations));
    summary.success = finalMetrics.cost <= initialMetrics.cost
                      && summary.finalRms <= summary.initialRms
                      && cv::checkRange((*transforms)[rootIndex]);
    if (!summary.success) {
        *transforms = initialTransforms;
    }
    return summary;
}

} // namespace

StitchingController::StitchingController(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<MosaicResult>::finished, this, [this] {
        const MosaicResult result = m_watcher.result();
        if (result.success) {
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
        emit stitchingFailed(projectPath, "Another stitching job is already running.");
        return;
    }
    emit stitchingStarted(projectPath);
    m_watcher.setFuture(QtConcurrent::run([this, projectPath] {
        try {
            return buildMosaic(projectPath);
        } catch (const cv::Exception &exception) {
            MosaicResult result;
            result.projectPath = projectPath;
            result.errorMessage = "Mosaic processing failed: "
                                  + QString::fromUtf8(exception.what());
            return result;
        } catch (const std::exception &exception) {
            MosaicResult result;
            result.projectPath = projectPath;
            result.errorMessage = "Mosaic processing failed: "
                                  + QString::fromUtf8(exception.what());
            return result;
        }
    }));
}

void StitchingController::postStatus(const QString &message)
{
    QMetaObject::invokeMethod(this, [this, message] {
        emit stitchingStatusChanged(message);
    }, Qt::QueuedConnection);
}

StitchingController::MosaicResult StitchingController::buildMosaic(const QString &projectPath)
{
    MosaicResult result;
    result.projectPath = projectPath;

    const QString rawImagesPath = QDir(projectPath).filePath("raw_images");
    if (!QDir(rawImagesPath).exists()) {
        result.errorMessage = "The project has no raw_images directory.";
        return result;
    }

    postStatus("Grouping spectral bands into camera captures...");
    int metadataFileCount = 0;
    std::vector<Capture> captures = discoverCaptures(rawImagesPath,
                                                     &metadataFileCount);
    if (captures.empty()) {
        result.errorMessage = "No TIFF files were found in raw_images.";
        return result;
    }

    int gpsCaptureCount = 0;
    int altitudeCaptureCount = 0;
    int metadataWarningCount = 0;
    int calibratedBandCount = 0;
    int approximateBandCount = 0;
    QString firstMetadataWarning;
    bool hasGpsFrame = false;
    kestrel::GeoCoordinate gpsFrameOrigin;
    for (Capture &capture : captures) {
        for (auto band = capture.bandCalibrations.cbegin();
             band != capture.bandCalibrations.cend(); ++band) {
            if (band.value().quality
                == kestrel::CalibrationQuality::MetadataCalibrated) {
                ++calibratedBandCount;
            } else if (band.value().quality
                       == kestrel::CalibrationQuality::Approximate) {
                ++approximateBandCount;
            }
            for (const QString &warning : band.value().warnings) {
                capture.calibrationWarnings.append(
                    QString("band %1: %2").arg(band.key()).arg(warning));
            }
        }

        if (!capture.bandMetadata.isEmpty()) {
            capture.metadata = capture.bandMetadata.contains(1)
                ? capture.bandMetadata.value(1) : capture.bandMetadata.first();
        }
        capture.hasGps = capture.metadata.hasGps;
        capture.hasGpsAltitude = capture.metadata.hasGpsAltitude;
        capture.latitude = capture.metadata.gps.latitudeDegrees;
        capture.longitude = capture.metadata.gps.longitudeDegrees;
        capture.altitude = capture.metadata.gps.altitudeMetres;
        gpsCaptureCount += capture.hasGps ? 1 : 0;
        altitudeCaptureCount += capture.hasGpsAltitude ? 1 : 0;
        metadataWarningCount += capture.metadataWarnings.size();
        if (firstMetadataWarning.isEmpty() && !capture.metadataWarnings.isEmpty()) {
            firstMetadataWarning = capture.name + ": "
                                   + capture.metadataWarnings.first();
        }
    }

    if (metadataWarningCount == 0) {
        postStatus(QString("Grouped %1 physical captures and validated metadata for "
                           "%2 TIFFs (%3 calibrated bands, %4 approximate).")
                       .arg(captures.size()).arg(metadataFileCount)
                       .arg(calibratedBandCount).arg(approximateBandCount));
    } else {
        postStatus(QString("Metadata validation found %1 warning(s). First: %2")
                       .arg(metadataWarningCount).arg(firstMetadataWarning));
    }

    auto referenceCapture = std::find_if(
        captures.begin(), captures.end(),
        [](const Capture &capture) { return capture.hasGps && capture.hasGpsAltitude; });
    if (referenceCapture == captures.end()) {
        referenceCapture = std::find_if(
            captures.begin(), captures.end(),
            [](const Capture &capture) { return capture.hasGps; });
    }
    if (referenceCapture != captures.end()) {
        const double referenceAltitude = referenceCapture->hasGpsAltitude
                                             ? referenceCapture->altitude : 0.0;
        gpsFrameOrigin = {referenceCapture->latitude, referenceCapture->longitude,
                          referenceAltitude};
        const kestrel::LocalTangentPlane gpsFrame(gpsFrameOrigin);
        hasGpsFrame = gpsFrame.isValid();
        for (Capture &capture : captures) {
            if (!capture.hasGps) {
                continue;
            }
            const kestrel::GeoCoordinate coordinate{
                capture.latitude, capture.longitude,
                capture.hasGpsAltitude ? capture.altitude : referenceAltitude};
            if (!gpsFrame.toEnu(coordinate, &capture.gpsEnu)) {
                capture.hasGps = false;
                --gpsCaptureCount;
            }
        }
    }

    const cv::Ptr<cv::SIFT> featureDetector = cv::SIFT::create(4000);
    for (size_t i = 0; i < captures.size(); ++i) {
        postStatus(QString("Preparing capture %1 of %2...").arg(i + 1).arg(captures.size()));
        QString loadError;
        cv::Mat registrationImage = loadRegistrationImage(captures[i], &loadError);
        if (registrationImage.empty()) {
            result.errorMessage = loadError;
            return result;
        }
        captures[i].fullSize = registrationImage.size();

        captures[i].matchScale = std::min(1.0, static_cast<double>(kMatchDimension)
                                                  / std::max(registrationImage.cols,
                                                             registrationImage.rows));
        cv::resize(registrationImage, captures[i].matchImage, {}, captures[i].matchScale,
                   captures[i].matchScale,
                   captures[i].matchScale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
        captures[i].brightness = cv::mean(captures[i].matchImage)[0];
        featureDetector->detectAndCompute(captures[i].matchImage, cv::noArray(),
                                          captures[i].keypoints,
                                          captures[i].descriptors);
    }

    const std::vector<CandidatePair> candidates = candidatePairs(captures);
    postStatus(QString("Matching %1 likely overlaps using GPS from %2 of %3 captures "
                       "(%4 with altitude)...")
                   .arg(candidates.size()).arg(gpsCaptureCount).arg(captures.size())
                   .arg(altitudeCaptureCount));
    std::vector<PairTransform> pairTransforms;
    pairTransforms.reserve(candidates.size());
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (index == 0 || (index + 1) % 10 == 0 || index + 1 == candidates.size()) {
            postStatus(QString("Matching likely overlap %1 of %2...")
                           .arg(index + 1).arg(candidates.size()));
        }
        PairTransform pair;
        pair.first = candidates[index].first;
        pair.second = candidates[index].second;
        if (estimatePair(captures[pair.first], captures[pair.second], &pair)) {
            pairTransforms.push_back(std::move(pair));
        }
    }

    std::vector<kestrel::PairwiseFeatureMatch> allFeatureMatches;
    for (const PairTransform &pair : pairTransforms) {
        allFeatureMatches.insert(allFeatureMatches.end(),
                                 pair.featureMatches.begin(),
                                 pair.featureMatches.end());
    }
    const kestrel::FeatureTrackBuildResult featureTracks =
        kestrel::FeatureTrackBuilder::build(allFeatureMatches, 3);
    postStatus(QString("Built %1 persistent multi-view tracks from %2 "
                       "geometrically verified pair matches (%3 conflicts rejected).")
                   .arg(featureTracks.tracks.size())
                   .arg(allFeatureMatches.size())
                   .arg(featureTracks.rejectedConflicts));

    std::vector<kestrel::CameraCalibration> registrationCalibrations;
    registrationCalibrations.reserve(captures.size());
    for (const Capture &capture : captures) {
        // Registration uses the green band when present, so sparse geometry
        // must use that sensor's intrinsics as well.
        if (capture.bandCalibrations.contains(2)) {
            registrationCalibrations.push_back(
                capture.bandCalibrations.value(2));
        } else if (!capture.bandCalibrations.isEmpty()) {
            registrationCalibrations.push_back(
                capture.bandCalibrations.first());
        } else {
            registrationCalibrations.emplace_back();
        }
    }
    kestrel::SparseInitializationResult sparseInitialization =
        kestrel::SparseReconstructor::reconstruct(
            registrationCalibrations, featureTracks.tracks);
    kestrel::ReconstructionAlignmentResult reconstructionAlignment;
    if (sparseInitialization.success && hasGpsFrame) {
        std::vector<kestrel::CameraPositionPrior> positionPriors;
        positionPriors.reserve(captures.size());
        for (int imageIndex = 0;
             imageIndex < static_cast<int>(captures.size()); ++imageIndex) {
            positionPriors.push_back({
                imageIndex, captures[imageIndex].gpsEnu,
                captures[imageIndex].hasGps});
        }
        reconstructionAlignment = kestrel::ReconstructionAligner::alignToEnu(
            &sparseInitialization, positionPriors);
    }
    std::set<int> alignedComponentIds;
    for (const kestrel::ComponentAlignment &alignment
         : reconstructionAlignment.components) {
        if (alignment.success) {
            alignedComponentIds.insert(alignment.componentId);
        }
    }
    std::vector<kestrel::BundleAdjustmentPositionPrior> bundlePositionPriors;
    for (const kestrel::SparseCameraPose &camera : sparseInitialization.cameras) {
        if (camera.imageIndex >= 0
            && camera.imageIndex < static_cast<int>(captures.size())
            && captures[camera.imageIndex].hasGps
            && alignedComponentIds.count(camera.componentId) > 0) {
            bundlePositionPriors.push_back({
                camera.imageIndex, captures[camera.imageIndex].gpsEnu,
                3.0, true});
        }
    }
    kestrel::BundleAdjustmentResult sparseBundleAdjustment;
    if (sparseInitialization.success) {
        kestrel::BundleAdjustmentOptions bundleOptions;
        bundleOptions.refineCameraCalibration = true;
        sparseBundleAdjustment = kestrel::SparseBundleAdjuster::optimize(
            &sparseInitialization, &registrationCalibrations,
            featureTracks.tracks, bundlePositionPriors, bundleOptions);
        if (sparseBundleAdjustment.success) {
            for (int imageIndex = 0;
                 imageIndex < static_cast<int>(captures.size()); ++imageIndex) {
                if (captures[imageIndex].bandCalibrations.contains(2)) {
                    captures[imageIndex].bandCalibrations[2] =
                        registrationCalibrations[imageIndex];
                } else if (!captures[imageIndex].bandCalibrations.isEmpty()) {
                    captures[imageIndex].bandCalibrations[
                        captures[imageIndex].bandCalibrations.firstKey()] =
                            registrationCalibrations[imageIndex];
                }
            }
        }
    }
    if (sparseInitialization.success) {
        postStatus(QString("Sparse reconstruction placed %1 of %2 captures in %3 "
                           "component(s) and triangulated %4 points from seed "
                           "captures %5 and %6.")
                       .arg(sparseInitialization.cameras.size())
                       .arg(captures.size())
                       .arg(sparseInitialization.componentCount)
                       .arg(sparseInitialization.points.size())
                       .arg(sparseInitialization.firstImageIndex + 1)
                       .arg(sparseInitialization.secondImageIndex + 1));
        for (const kestrel::BundleAdjustmentComponentSummary &summary
             : sparseBundleAdjustment.components) {
            if (summary.success) {
                postStatus(QString("3D bundle adjustment component %1 reduced RMS "
                                   "from %2 to %3 pixels, GPS RMS from %4 to %5 "
                                   "metres, refined %6 calibration group(s), "
                                   "and rejected %7 observations.")
                               .arg(summary.componentId)
                               .arg(summary.initialRmsPixels, 0, 'f', 2)
                               .arg(summary.finalRmsPixels, 0, 'f', 2)
                               .arg(summary.initialGpsRmsMetres, 0, 'f', 2)
                               .arg(summary.finalGpsRmsMetres, 0, 'f', 2)
                               .arg(summary.refinedCalibrationGroupCount)
                               .arg(summary.rejectedObservationCount));
            }
        }
        if (reconstructionAlignment.alignedComponentCount > 0) {
            postStatus(QString("Aligned %1 of %2 sparse component(s) to the GPS ENU frame.")
                           .arg(reconstructionAlignment.alignedComponentCount)
                           .arg(sparseInitialization.componentCount));
        }
    } else {
        postStatus(QString("Sparse initialization deferred: %1 The planar preview "
                           "will still be generated.")
                       .arg(QString::fromStdString(
                           sparseInitialization.message)));
    }

    std::vector<cv::Mat> matchTransforms;
    int rootIndex = 0;
    QString solveError;
    if (!solveGlobalTransforms(static_cast<int>(captures.size()), pairTransforms,
                               &matchTransforms, &rootIndex, &solveError)) {
        result.errorMessage = solveError;
        return result;
    }

    postStatus("Refining preview transforms with planar global adjustment...");
    const BundleAdjustmentSummary bundleAdjustment = adjustGlobalTransforms(
        rootIndex, pairTransforms, &matchTransforms);
    if (bundleAdjustment.success) {
        postStatus(QString("Global adjustment reduced match RMS from %1 to %2 pixels "
                           "across %3 observations.")
                       .arg(bundleAdjustment.initialRms, 0, 'f', 2)
                       .arg(bundleAdjustment.finalRms, 0, 'f', 2)
                       .arg(bundleAdjustment.observations));
    } else {
        postStatus("Global adjustment was not stable; using the connected-graph transforms.");
    }

    // Feature matching is intentionally downsampled, but the solved transforms
    // are converted back to source-image coordinates for the final mosaic.
    std::vector<cv::Mat> globalTransforms(captures.size());
    const double rootScale = captures[rootIndex].matchScale;
    const cv::Mat matchToRootFull = (cv::Mat_<double>(3, 3) <<
        1.0 / rootScale, 0, 0,
        0, 1.0 / rootScale, 0,
        0, 0, 1);
    for (size_t i = 0; i < captures.size(); ++i) {
        const cv::Mat sourceToMatch = (cv::Mat_<double>(3, 3) <<
            captures[i].matchScale, 0, 0,
            0, captures[i].matchScale, 0,
            0, 0, 1);
        globalTransforms[i] = matchToRootFull * matchTransforms[i] * sourceToMatch;
        captures[i].matchImage.release();
        captures[i].descriptors.release();
        captures[i].keypoints.clear();
    }
    postStatus("Calculating mosaic bounds...");
    double minimumX = std::numeric_limits<double>::max();
    double minimumY = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    double maximumY = std::numeric_limits<double>::lowest();
    for (size_t i = 0; i < captures.size(); ++i) {
        const cv::Size size = captures[i].fullSize;
        std::vector<cv::Point2f> corners{{0, 0}, {static_cast<float>(size.width), 0},
                                        {static_cast<float>(size.width), static_cast<float>(size.height)},
                                        {0, static_cast<float>(size.height)}};
        std::vector<cv::Point2f> transformed;
        cv::perspectiveTransform(corners, transformed, globalTransforms[i]);
        for (const cv::Point2f &point : transformed) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                result.errorMessage = "A solved image transform contains invalid coordinates.";
                return result;
            }
            minimumX = std::min(minimumX, static_cast<double>(point.x));
            minimumY = std::min(minimumY, static_cast<double>(point.y));
            maximumX = std::max(maximumX, static_cast<double>(point.x));
            maximumY = std::max(maximumY, static_cast<double>(point.y));
        }
    }

    const double unscaledWidth = maximumX - minimumX;
    const double unscaledHeight = maximumY - minimumY;
    if (!(unscaledWidth > 0.0) || !(unscaledHeight > 0.0)) {
        result.errorMessage = "The solved mosaic canvas is invalid. "
                              "This usually indicates a bad image match.";
        return result;
    }

    const double unscaledPixels = unscaledWidth * unscaledHeight;
    const double outputScale = unscaledPixels > kMaximumCanvasPixels
        ? std::sqrt(static_cast<double>(kMaximumCanvasPixels) / unscaledPixels)
        : 1.0;
    if (outputScale < 1.0) {
        postStatus(QString("Scaling the mosaic to %1% to fit available memory...")
                       .arg(qRound(outputScale * 100.0)));
        const cv::Mat canvasScale = (cv::Mat_<double>(3, 3) <<
            outputScale, 0, 0,
            0, outputScale, 0,
            0, 0, 1);
        for (cv::Mat &transform : globalTransforms) {
            transform = canvasScale * transform;
        }
        minimumX *= outputScale;
        minimumY *= outputScale;
        maximumX *= outputScale;
        maximumY *= outputScale;
    }

    constexpr int margin = 20;
    const int canvasWidth = static_cast<int>(std::ceil(maximumX - minimumX)) + margin * 2;
    const int canvasHeight = static_cast<int>(std::ceil(maximumY - minimumY)) + margin * 2;
    if (canvasWidth <= 0 || canvasHeight <= 0
        || static_cast<qint64>(canvasWidth) * canvasHeight
               > kMaximumCanvasPixels + 2LL * margin * (canvasWidth + canvasHeight)) {
        result.errorMessage = "The solved mosaic canvas is too large or invalid.";
        return result;
    }

    const cv::Mat translation = (cv::Mat_<double>(3, 3) <<
        1, 0, -minimumX + margin,
        0, 1, -minimumY + margin,
        0, 0, 1);
    cv::Mat accumulated = cv::Mat::zeros(canvasHeight, canvasWidth, CV_32FC3);
    cv::Mat accumulatedWeight = cv::Mat::zeros(canvasHeight, canvasWidth, CV_32F);

    std::vector<double> brightness;
    brightness.reserve(captures.size());
    for (const Capture &capture : captures) {
        brightness.push_back(capture.brightness);
    }
    std::vector<double> sortedBrightness = brightness;
    std::sort(sortedBrightness.begin(), sortedBrightness.end());
    const double targetBrightness = sortedBrightness[sortedBrightness.size() / 2];

    for (size_t i = 0; i < captures.size(); ++i) {
        postStatus(QString("Blending capture %1 of %2...").arg(i + 1).arg(captures.size()));
        QString loadError;
        cv::Mat sourceImage = loadCaptureImage(captures[i], &loadError);
        if (sourceImage.empty() || sourceImage.size() != captures[i].fullSize) {
            result.errorMessage = loadError.isEmpty()
                ? "A source image changed while the mosaic was being built."
                : loadError;
            return result;
        }
        cv::Mat floatImage;
        const double gain = std::clamp(targetBrightness / std::max(1.0, brightness[i]), 0.7, 1.4);
        sourceImage.convertTo(floatImage, CV_32FC3, gain);

        cv::Mat sourceMask = cv::Mat::zeros(sourceImage.size(), CV_8U);
        cv::rectangle(sourceMask, cv::Rect(2, 2, sourceMask.cols - 4, sourceMask.rows - 4),
                      cv::Scalar(255), cv::FILLED);
        cv::Mat sourceWeight;
        cv::distanceTransform(sourceMask, sourceWeight, cv::DIST_L2, 3);
        double maximumWeight = 0;
        cv::minMaxLoc(sourceWeight, nullptr, &maximumWeight);
        sourceWeight /= std::max(1.0, maximumWeight);

        const cv::Mat transform = translation * globalTransforms[i];
        cv::Mat warpedImage;
        cv::Mat warpedWeight;
        cv::warpPerspective(floatImage, warpedImage, transform,
                            cv::Size(canvasWidth, canvasHeight), cv::INTER_LINEAR,
                            cv::BORDER_CONSTANT);
        cv::warpPerspective(sourceWeight, warpedWeight, transform,
                            cv::Size(canvasWidth, canvasHeight), cv::INTER_LINEAR,
                            cv::BORDER_CONSTANT);
        for (int y = 0; y < canvasHeight; ++y) {
            cv::Vec3f *accumulatedRow = accumulated.ptr<cv::Vec3f>(y);
            const cv::Vec3f *imageRow = warpedImage.ptr<cv::Vec3f>(y);
            const float *weightRow = warpedWeight.ptr<float>(y);
            float *accumulatedWeightRow = accumulatedWeight.ptr<float>(y);
            for (int x = 0; x < canvasWidth; ++x) {
                const float weight = weightRow[x];
                accumulatedRow[x] += imageRow[x] * weight;
                accumulatedWeightRow[x] += weight;
            }
        }
    }

    for (int y = 0; y < canvasHeight; ++y) {
        cv::Vec3f *accumulatedRow = accumulated.ptr<cv::Vec3f>(y);
        const float *weightRow = accumulatedWeight.ptr<float>(y);
        for (int x = 0; x < canvasWidth; ++x) {
            accumulatedRow[x] /= std::max(weightRow[x], 1e-6f);
        }
    }
    cv::Mat mosaic;
    accumulated.convertTo(mosaic, CV_8UC3);

    postStatus("Writing mosaic products...");
    const QString outputPath = QDir(projectPath).filePath("processed_images/kestrel_mosaic");
    QDir outputDirectory(outputPath);
    if (outputDirectory.exists() && !outputDirectory.removeRecursively()) {
        result.errorMessage = "Could not replace the previous Kestrel mosaic output.";
        return result;
    }
    if (!QDir().mkpath(outputPath)) {
        result.errorMessage = "Could not create the mosaic output directory.";
        return result;
    }

    const QString previewPath = QDir(outputPath).filePath("field_mosaic.png");
    const QString tiffPath = QDir(outputPath).filePath("field_mosaic.tif");
    if (!writeImage(previewPath, mosaic) || !writeImage(tiffPath, mosaic)) {
        result.errorMessage = "OpenCV could not write the mosaic output.";
        return result;
    }

    const QString transformsPath = QDir(outputPath).filePath("transforms.yml");
    cv::FileStorage transforms("transforms.yml",
                               cv::FileStorage::WRITE | cv::FileStorage::MEMORY);
    transforms << "planar_preview_adjustment" << "{"
               << "applied" << bundleAdjustment.success
               << "iterations" << bundleAdjustment.iterations
               << "observations" << static_cast<int>(bundleAdjustment.observations)
               << "initial_rms_pixels" << bundleAdjustment.initialRms
               << "final_rms_pixels" << bundleAdjustment.finalRms << "}";
    transforms << "coordinate_frame" << "{"
               << "type" << (hasGpsFrame ? "WGS84_ENU" : "none")
               << "has_origin" << hasGpsFrame;
    if (hasGpsFrame) {
        transforms << "latitude_degrees" << gpsFrameOrigin.latitudeDegrees
                   << "longitude_degrees" << gpsFrameOrigin.longitudeDegrees
                   << "altitude_metres" << gpsFrameOrigin.altitudeMetres;
    }
    transforms << "}";
    transforms << "sparse_reconstruction" << "{"
               << "success" << sparseInitialization.success
               << "message" << sparseInitialization.message
               << "coordinate_status"
               << (sparseInitialization.componentCount > 0
                           && reconstructionAlignment.alignedComponentCount
                                  == sparseInitialization.componentCount
                       ? "ENU_metres"
                       : (reconstructionAlignment.alignedComponentCount > 0
                              ? "mixed_ENU_and_arbitrary_components"
                              : "arbitrary_unit_components"))
               << "gps_aligned_components"
               << reconstructionAlignment.alignedComponentCount
               << "first_capture_index"
               << sparseInitialization.firstImageIndex
               << "second_capture_index"
               << sparseInitialization.secondImageIndex
               << "shared_tracks" << sparseInitialization.sharedTrackCount
               << "essential_inliers"
               << sparseInitialization.essentialInlierCount
               << "pnp_attempts" << sparseInitialization.pnpAttempts
               << "pnp_registered_cameras"
               << sparseInitialization.pnpRegisteredCameraCount
               << "component_count" << sparseInitialization.componentCount
               << "seed_median_triangulation_angle_degrees"
               << sparseInitialization.medianTriangulationAngleDegrees
               << "cameras" << "[";
    for (const kestrel::SparseCameraPose &camera
         : sparseInitialization.cameras) {
        const cv::Mat rotation(camera.worldToCameraRotation, true);
        transforms << "{"
                   << "capture_index" << camera.imageIndex
                   << "component_id" << camera.componentId
                   << "world_to_camera_rotation" << rotation
                   << "world_to_camera_translation" << "["
                   << camera.worldToCameraTranslation[0]
                   << camera.worldToCameraTranslation[1]
                   << camera.worldToCameraTranslation[2]
                   << "]"
                   << "pnp_inliers" << camera.pnpInliers
                   << "pnp_reprojection_rms_pixels"
                   << camera.pnpReprojectionRmsPixels
                   << "}";
    }
    transforms << "]" << "sparse_bundle_adjustment" << "{"
               << "success" << sparseBundleAdjustment.success
               << "optimized_component_count"
               << sparseBundleAdjustment.optimizedComponentCount
               << "rejected_observation_count"
               << sparseBundleAdjustment.rejectedObservationCount
               << "components" << "[";
    for (const kestrel::BundleAdjustmentComponentSummary &summary
         : sparseBundleAdjustment.components) {
        transforms << "{"
                   << "component_id" << summary.componentId
                   << "success" << summary.success
                   << "message" << summary.message
                   << "camera_count" << summary.cameraCount
                   << "fixed_camera_count" << summary.fixedCameraCount
                   << "point_count" << summary.pointCount
                   << "observation_count" << summary.observationCount
                   << "gps_prior_count" << summary.gpsPriorCount
                   << "refined_calibration_group_count"
                   << summary.refinedCalibrationGroupCount
                   << "rejected_observation_count"
                   << summary.rejectedObservationCount
                   << "accepted_iterations" << summary.acceptedIterations
                   << "linear_solver" << "block_jacobi_pcg"
                   << "linear_solver_iterations"
                   << summary.linearSolverIterations
                   << "initial_rms_pixels" << summary.initialRmsPixels
                   << "final_rms_pixels" << summary.finalRmsPixels
                   << "initial_gps_rms_metres"
                   << summary.initialGpsRmsMetres
                   << "final_gps_rms_metres" << summary.finalGpsRmsMetres
                   << "}";
    }
    transforms << "]" << "refined_calibrations" << "[";
    for (const kestrel::BundleAdjustmentCalibrationSummary &summary
         : sparseBundleAdjustment.calibrations) {
        const cv::Mat initialIntrinsic(summary.initial.intrinsic, true);
        const cv::Mat finalIntrinsic(summary.final.intrinsic, true);
        transforms << "{"
                   << "component_id" << summary.componentId
                   << "refined" << summary.refined
                   << "capture_indices" << "[";
        for (int imageIndex : summary.imageIndices) {
            transforms << imageIndex;
        }
        transforms << "]"
                   << "initial_intrinsic_matrix" << initialIntrinsic
                   << "final_intrinsic_matrix" << finalIntrinsic
                   << "initial_distortion_opencv" << "[";
        for (double value : summary.initial.distortion.val) {
            transforms << value;
        }
        transforms << "]" << "final_distortion_opencv" << "[";
        for (double value : summary.final.distortion.val) {
            transforms << value;
        }
        transforms << "]" << "}";
    }
    transforms << "]" << "rejected_observations" << "[";
    for (const auto &observation
         : sparseBundleAdjustment.rejectedObservations) {
        transforms << "{"
                   << "component_id" << observation.componentId
                   << "capture_index" << observation.imageIndex
                   << "track_id" << observation.trackId
                   << "error_pixels" << observation.errorPixels
                   << "}";
    }
    transforms << "]" << "}" << "points" << "[";
    for (const kestrel::SparsePoint &point : sparseInitialization.points) {
        transforms << "{"
                   << "track_id" << point.trackId
                   << "component_id" << point.componentId
                   << "x" << point.position.x
                   << "y" << point.position.y
                   << "z" << point.position.z
                   << "reprojection_rms_pixels"
                   << point.reprojectionRmsPixels
                   << "triangulation_angle_degrees"
                   << point.triangulationAngleDegrees
                   << "}";
    }
    transforms << "]" << "component_alignments" << "[";
    for (const kestrel::ComponentAlignment &alignment
         : reconstructionAlignment.components) {
        const cv::Mat rotation(alignment.rotation, true);
        transforms << "{"
                   << "component_id" << alignment.componentId
                   << "success" << alignment.success
                   << "message" << alignment.message
                   << "prior_count" << alignment.priorCount
                   << "inlier_count" << alignment.inlierCount
                   << "scale" << alignment.scale
                   << "rotation" << rotation
                   << "translation" << "["
                   << alignment.translation[0]
                   << alignment.translation[1]
                   << alignment.translation[2]
                   << "]"
                   << "rms_metres" << alignment.rmsMetres
                   << "}";
    }
    transforms << "]" << "unregistered_capture_indices" << "[";
    for (int imageIndex : sparseInitialization.unregisteredImageIndices) {
        transforms << imageIndex;
    }
    transforms << "]" << "}";
    transforms << "feature_track_summary" << "{"
               << "coordinate_space" << "source_image_pixels"
               << "track_count" << static_cast<int>(featureTracks.tracks.size())
               << "accepted_pair_matches" << featureTracks.acceptedMatches
               << "redundant_pair_matches" << featureTracks.redundantMatches
               << "rejected_conflicts" << featureTracks.rejectedConflicts
               << "ignored_invalid_matches" << featureTracks.ignoredInvalidMatches
               << "}";
    transforms << "pairwise_models" << "[";
    for (const PairTransform &pair : pairTransforms) {
        transforms << "{"
                   << "first_capture_index" << pair.first
                   << "second_capture_index" << pair.second
                   << "homography_inliers" << pair.inliers
                   << "fundamental_inliers" << pair.fundamentalInliers
                   << "fundamental_matrix" << pair.fundamental
                   << "matches" << "[";
        for (const kestrel::PairwiseFeatureMatch &match : pair.featureMatches) {
            transforms << "{"
                       << "first_feature_index" << match.first.featureIndex
                       << "first_x" << match.first.imagePoint.x
                       << "first_y" << match.first.imagePoint.y
                       << "second_feature_index" << match.second.featureIndex
                       << "second_x" << match.second.imagePoint.x
                       << "second_y" << match.second.imagePoint.y
                       << "}";
        }
        transforms << "]" << "}";
    }
    transforms << "]";
    transforms << "feature_tracks" << "[";
    for (const kestrel::FeatureTrack &track : featureTracks.tracks) {
        transforms << "{" << "id" << track.id << "observations" << "[";
        for (const kestrel::FeatureObservation &observation : track.observations) {
            transforms << "{"
                       << "capture_index" << observation.imageIndex
                       << "feature_index" << observation.featureIndex
                       << "x" << observation.imagePoint.x
                       << "y" << observation.imagePoint.y
                       << "}";
        }
        transforms << "]" << "}";
    }
    transforms << "]";
    transforms << "captures" << "[";
    for (size_t i = 0; i < captures.size(); ++i) {
        transforms << "{" << "name" << captures[i].name.toStdString()
                   << "stable_id" << captures[i].stableId.toStdString()
                   << "homography" << (translation * globalTransforms[i])
                   << "has_gps" << captures[i].hasGps;
        if (captures[i].hasGps) {
            transforms << "gps" << "{"
                       << "latitude_degrees" << captures[i].latitude
                       << "longitude_degrees" << captures[i].longitude
                       << "has_altitude" << captures[i].hasGpsAltitude
                       << "altitude_metres" << captures[i].altitude
                       << "east_metres" << captures[i].gpsEnu.x
                       << "north_metres" << captures[i].gpsEnu.y
                       << "up_metres" << captures[i].gpsEnu.z << "}";
        }
        transforms << "metadata_warnings" << "[";
        for (const QString &warning : captures[i].metadataWarnings) {
            transforms << warning.toStdString();
        }
        transforms << "]" << "calibration_warnings" << "[";
        for (const QString &warning : captures[i].calibrationWarnings) {
            transforms << warning.toStdString();
        }
        transforms << "]" << "bands" << "[";
        for (auto band = captures[i].bandMetadata.cbegin();
             band != captures[i].bandMetadata.cend(); ++band) {
            const kestrel::TiffImageMetadata &metadata = band.value();
            const kestrel::CameraCalibration calibration =
                captures[i].bandCalibrations.value(band.key());
            const cv::Mat intrinsic(calibration.intrinsic, true);
            const cv::Mat rigRotation(calibration.rigPose.rotationToReference,
                                      true);
            transforms << "{"
                       << "band_index" << band.key()
                       << "band_name" << metadata.bandName.toStdString()
                       << "capture_id" << metadata.captureId.toStdString()
                       << "rig_camera_index" << metadata.rigCameraIndex
                       << "camera_make" << metadata.cameraMake.toStdString()
                       << "camera_model" << metadata.cameraModel.toStdString()
                       << "camera_serial" << metadata.cameraSerial.toStdString()
                       << "capture_time" << metadata.captureTime.toStdString()
                       << "image_width" << metadata.imageWidth
                       << "image_height" << metadata.imageHeight
                       << "orientation" << metadata.orientation
                       << "bits_per_sample" << metadata.bitsPerSample
                       << "exposure_time_seconds"
                       << metadata.exposureTimeSeconds
                       << "iso_speed" << metadata.isoSpeed
                       << "band_sensitivity" << metadata.bandSensitivity
                       << "focal_length_mm" << metadata.focalLengthMillimetres
                       << "focal_plane_pixels_per_mm_x"
                       << metadata.focalPlanePixelsPerMillimetreX
                       << "focal_plane_pixels_per_mm_y"
                       << metadata.focalPlanePixelsPerMillimetreY
                       << "calibrated_focal_length_mm"
                       << metadata.calibratedFocalLengthMillimetres
                       << "central_wavelength_nm"
                       << metadata.centralWavelengthNanometres
                       << "calibration_quality"
                       << calibrationQualityName(calibration.quality)
                       << "calibration_source"
                       << calibration.source.toStdString()
                       << "intrinsic_matrix" << intrinsic
                       << "sensor_width_mm"
                       << calibration.sensorSizeMillimetres.width
                       << "sensor_height_mm"
                       << calibration.sensorSizeMillimetres.height
                       << "rig_has_rotation"
                       << calibration.rigPose.hasRotation
                       << "rig_has_translation"
                       << calibration.rigPose.hasTranslation
                       << "rig_rotation_to_reference" << rigRotation
                       << "rig_translation_to_reference" << "["
                       << calibration.rigPose.translationToReference[0]
                       << calibration.rigPose.translationToReference[1]
                       << calibration.rigPose.translationToReference[2]
                       << "]";
            transforms << "distortion_opencv" << "[";
            for (double value : calibration.distortion.val) {
                transforms << value;
            }
            transforms << "]" << "calibration_warnings" << "[";
            for (const QString &warning : calibration.warnings) {
                transforms << warning.toStdString();
            }
            transforms << "]";
            transforms << "black_levels" << "[";
            for (double value : metadata.blackLevels) {
                transforms << value;
            }
            transforms << "]" << "vignetting_center" << "[";
            for (double value : metadata.vignettingCenter) {
                transforms << value;
            }
            transforms << "]" << "vignetting_polynomial" << "[";
            for (double value : metadata.vignettingPolynomial) {
                transforms << value;
            }
            transforms << "]" << "radiometric_calibration" << "[";
            for (double value : metadata.radiometricCalibration) {
                transforms << value;
            }
            transforms << "]";
            transforms << "principal_point_mm" << "[";
            for (double value : metadata.principalPointMillimetres) {
                transforms << value;
            }
            transforms << "]" << "perspective_distortion" << "[";
            for (double value : metadata.perspectiveDistortion) {
                transforms << value;
            }
            transforms << "]" << "rig_relatives" << "[";
            for (double value : metadata.rigRelatives) {
                transforms << value;
            }
            transforms << "]" << "}";
        }
        transforms << "]";
        transforms << "}";
    }
    transforms << "]";
    const std::string transformsText = transforms.releaseAndGetString();
    QSaveFile transformsFile(transformsPath);
    if (!transformsFile.open(QIODevice::WriteOnly)
        || transformsFile.write(transformsText.data(),
                                static_cast<qint64>(transformsText.size()))
               != static_cast<qint64>(transformsText.size())
        || !transformsFile.commit()) {
        result.errorMessage = "Could not write the mosaic transform metadata.";
        return result;
    }

    result.success = true;
    result.previewUrl = QUrl::fromLocalFile(previewPath);
    return result;
}
