#include "photogrammetry/mvs/DenseReconstruction.h"
#include "photogrammetry/tiling/TileLayout.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <cstdint>
#include <limits>
#include <map>
#include <iterator>
#include <sstream>
#include <set>

namespace kestrel {

namespace {

constexpr float kInvalidDepth = std::numeric_limits<float>::quiet_NaN();

cv::Point3d cameraCentre(const SparseCameraPose &camera)
{
    const cv::Vec3d centre = -camera.worldToCameraRotation.t()
                             * camera.worldToCameraTranslation;
    return {centre[0], centre[1], centre[2]};
}

double triangulationAngleDegrees(const cv::Point3d &point,
                                 const cv::Point3d &firstCentre,
                                 const cv::Point3d &secondCentre)
{
    const cv::Vec3d first(firstCentre.x - point.x,
                          firstCentre.y - point.y,
                          firstCentre.z - point.z);
    const cv::Vec3d second(secondCentre.x - point.x,
                           secondCentre.y - point.y,
                           secondCentre.z - point.z);
    const double denominator = cv::norm(first) * cv::norm(second);
    if (!(denominator > 1e-12)) {
        return 0.0;
    }
    const double cosine = std::clamp(first.dot(second) / denominator,
                                     -1.0, 1.0);
    return std::acos(cosine) * 180.0 / CV_PI;
}

double median(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    double result = values[middle];
    if (values.size() % 2 == 0) {
        const auto lower = std::max_element(values.begin(),
                                            values.begin() + middle);
        result = 0.5 * (result + *lower);
    }
    return result;
}

cv::Matx33d scaledIntrinsic(const cv::Matx33d &intrinsic,
                            double scaleX, double scaleY)
{
    cv::Matx33d result = intrinsic;
    for (int column = 0; column < 3; ++column) {
        result(0, column) *= scaleX;
        result(1, column) *= scaleY;
    }
    return result;
}

bool toGrayFloat(const cv::Mat &source, cv::Mat *gray,
                 std::string *errorMessage)
{
    if (source.empty()) {
        if (errorMessage) {
            *errorMessage = "Dense MVS received an empty source image.";
        }
        return false;
    }
    cv::Mat singleChannel;
    if (source.channels() == 1) {
        singleChannel = source;
    } else if (source.channels() == 3) {
        cv::cvtColor(source, singleChannel, cv::COLOR_BGR2GRAY);
    } else if (source.channels() == 4) {
        cv::cvtColor(source, singleChannel, cv::COLOR_BGRA2GRAY);
    } else {
        if (errorMessage) {
            *errorMessage = "Dense MVS supports one-, three-, or four-channel images.";
        }
        return false;
    }
    double scale = 1.0;
    switch (singleChannel.depth()) {
    case CV_8U:
        scale = 1.0 / 255.0;
        break;
    case CV_16U:
        scale = 1.0 / 65535.0;
        break;
    case CV_32F:
    case CV_64F:
        break;
    default:
        if (errorMessage) {
            *errorMessage = "Dense MVS received an unsupported image depth.";
        }
        return false;
    }
    singleChannel.convertTo(*gray, CV_32F, scale);
    return true;
}

struct PreparedView
{
    int imageIndex = -1;
    cv::Mat image;
    cv::Matx33d intrinsic = cv::Matx33d::eye();
    SparseCameraPose pose;
    double imageScale = 1.0;
};

bool prepareView(const DenseMvsView &view, int maximumImageDimension,
                 PreparedView *prepared, std::string *errorMessage)
{
    if (!prepared || !view.calibration.isUsable()) {
        if (errorMessage) {
            *errorMessage = "Dense MVS requires a usable camera calibration.";
        }
        return false;
    }
    cv::Mat gray;
    if (!toGrayFloat(view.image, &gray, errorMessage)) {
        return false;
    }
    cv::Mat undistorted;
    cv::undistort(gray, undistorted,
                  cv::Mat(view.calibration.intrinsic, true),
                  cv::Mat(view.calibration.distortion, true));
    const int longest = std::max(undistorted.cols, undistorted.rows);
    const double scale = maximumImageDimension > 0
            && longest > maximumImageDimension
        ? static_cast<double>(maximumImageDimension) / longest : 1.0;
    cv::Mat resized;
    if (scale < 1.0) {
        cv::resize(undistorted, resized, {}, scale, scale, cv::INTER_AREA);
    } else {
        resized = undistorted;
    }
    prepared->imageIndex = view.imageIndex;
    prepared->image = resized;
    prepared->intrinsic = scaledIntrinsic(
        view.calibration.intrinsic,
        static_cast<double>(resized.cols) / gray.cols,
        static_cast<double>(resized.rows) / gray.rows);
    prepared->pose = view.pose;
    prepared->imageScale = static_cast<double>(resized.cols) / gray.cols;
    return true;
}

struct LevelView
{
    cv::Mat image;
    cv::Matx33d intrinsic = cv::Matx33d::eye();
    SparseCameraPose pose;
};

LevelView atLevel(const PreparedView &view, int level)
{
    const double nominalScale = 1.0 / (1 << level);
    const cv::Size size(std::max(1, cvRound(view.image.cols * nominalScale)),
                        std::max(1, cvRound(view.image.rows * nominalScale)));
    LevelView result;
    if (size == view.image.size()) {
        result.image = view.image;
    } else {
        cv::resize(view.image, result.image, size, 0.0, 0.0, cv::INTER_AREA);
    }
    result.intrinsic = scaledIntrinsic(
        view.intrinsic,
        static_cast<double>(size.width) / view.image.cols,
        static_cast<double>(size.height) / view.image.rows);
    result.pose = view.pose;
    return result;
}

cv::Matx33d referenceToSourceHomography(const LevelView &reference,
                                        const LevelView &source,
                                        double depth)
{
    const cv::Matx33d relativeRotation =
        source.pose.worldToCameraRotation
        * reference.pose.worldToCameraRotation.t();
    const cv::Vec3d relativeTranslation =
        source.pose.worldToCameraTranslation
        - relativeRotation * reference.pose.worldToCameraTranslation;
    cv::Matx33d planeTransform = relativeRotation;
    for (int row = 0; row < 3; ++row) {
        planeTransform(row, 2) += relativeTranslation[row] / depth;
    }
    return source.intrinsic * planeTransform * reference.intrinsic.inv();
}

std::vector<double> inverseDepthHypotheses(const DenseDepthRange &range,
                                           int count)
{
    std::vector<double> result;
    result.reserve(count);
    const double nearInverse = 1.0 / range.minimumDepth;
    const double farInverse = 1.0 / range.maximumDepth;
    for (int index = 0; index < count; ++index) {
        const double alpha = count == 1
            ? 0.5 : static_cast<double>(index) / (count - 1);
        result.push_back(1.0 / (nearInverse
                                + alpha * (farInverse - nearInverse)));
    }
    return result;
}

void setImageBorderInvalid(cv::Mat *mask, int radius)
{
    if (radius <= 0) {
        return;
    }
    mask->rowRange(0, std::min(radius, mask->rows)).setTo(0);
    mask->rowRange(std::max(0, mask->rows - radius), mask->rows).setTo(0);
    mask->colRange(0, std::min(radius, mask->cols)).setTo(0);
    mask->colRange(std::max(0, mask->cols - radius), mask->cols).setTo(0);
}

bool sweepLevel(const LevelView &reference,
                const std::vector<LevelView> &sources,
                const DenseDepthRange &range,
                const DenseMvsOptions &options,
                const cv::Mat &priorDepth,
                cv::Mat *depth, cv::Mat *confidence)
{
    if (sources.empty() || reference.image.empty()) {
        return false;
    }
    const cv::Size patchSize(options.patchRadius * 2 + 1,
                             options.patchRadius * 2 + 1);
    cv::Mat referenceMean;
    cv::Mat referenceSquaredMean;
    cv::boxFilter(reference.image, referenceMean, CV_32F, patchSize,
                  {-1, -1}, true, cv::BORDER_REFLECT101);
    cv::boxFilter(reference.image.mul(reference.image),
                  referenceSquaredMean, CV_32F, patchSize,
                  {-1, -1}, true, cv::BORDER_REFLECT101);
    cv::Mat referenceVariance =
        referenceSquaredMean - referenceMean.mul(referenceMean);
    cv::Mat referenceSupport(reference.image.size(), CV_8U, cv::Scalar(255));
    setImageBorderInvalid(&referenceSupport, options.patchRadius);

    cv::Mat best(reference.image.size(), CV_32F,
                 cv::Scalar(std::numeric_limits<float>::infinity()));
    cv::Mat second(reference.image.size(), CV_32F,
                   cv::Scalar(std::numeric_limits<float>::infinity()));
    *depth = cv::Mat(reference.image.size(), CV_32F,
                     cv::Scalar(kInvalidDepth));
    const std::vector<double> hypotheses = inverseDepthHypotheses(
        range, std::max(2, options.depthHypotheses));

    for (double candidateDepth : hypotheses) {
        cv::Mat accumulated = cv::Mat::zeros(reference.image.size(), CV_32F);
        cv::Mat support = cv::Mat::zeros(reference.image.size(), CV_16U);
        for (const LevelView &source : sources) {
            const cv::Matx33d homography = referenceToSourceHomography(
                reference, source, candidateDepth);
            cv::Mat warped;
            cv::warpPerspective(source.image, warped, cv::Mat(homography),
                                reference.image.size(),
                                cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,
                                cv::BORDER_CONSTANT, cv::Scalar(0));
            cv::Mat sourceMask(source.image.size(), CV_8U, cv::Scalar(255));
            cv::Mat warpedMask;
            cv::warpPerspective(sourceMask, warpedMask, cv::Mat(homography),
                                reference.image.size(),
                                cv::INTER_NEAREST | cv::WARP_INVERSE_MAP,
                                cv::BORDER_CONSTANT, cv::Scalar(0));
            if (options.patchRadius > 0) {
                const cv::Mat kernel = cv::getStructuringElement(
                    cv::MORPH_RECT, patchSize);
                cv::erode(warpedMask, warpedMask, kernel);
            }

            cv::Mat sourceMean;
            cv::Mat sourceSquaredMean;
            cv::Mat productMean;
            cv::boxFilter(warped, sourceMean, CV_32F, patchSize,
                          {-1, -1}, true, cv::BORDER_REFLECT101);
            cv::boxFilter(warped.mul(warped), sourceSquaredMean, CV_32F,
                          patchSize, {-1, -1}, true,
                          cv::BORDER_REFLECT101);
            cv::boxFilter(reference.image.mul(warped), productMean, CV_32F,
                          patchSize, {-1, -1}, true,
                          cv::BORDER_REFLECT101);
            const cv::Mat sourceVariance =
                sourceSquaredMean - sourceMean.mul(sourceMean);
            const cv::Mat covariance =
                productMean - referenceMean.mul(sourceMean);

            for (int row = 0; row < reference.image.rows; ++row) {
                const uchar *warpedValid = warpedMask.ptr<uchar>(row);
                const uchar *referenceValid = referenceSupport.ptr<uchar>(row);
                const float *referenceVar = referenceVariance.ptr<float>(row);
                const float *sourceVar = sourceVariance.ptr<float>(row);
                const float *covarianceValues = covariance.ptr<float>(row);
                float *costSum = accumulated.ptr<float>(row);
                ushort *viewCount = support.ptr<ushort>(row);
                for (int column = 0; column < reference.image.cols; ++column) {
                    if (!warpedValid[column] || !referenceValid[column]
                        || referenceVar[column] < options.minimumTextureVariance
                        || sourceVar[column] < options.minimumTextureVariance) {
                        continue;
                    }
                    const double denominator = std::sqrt(
                        std::max(1e-12, static_cast<double>(referenceVar[column])
                                           * sourceVar[column]));
                    const double ncc = std::clamp(
                        covarianceValues[column] / denominator, -1.0, 1.0);
                    costSum[column] += static_cast<float>(1.0 - ncc);
                    ++viewCount[column];
                }
            }
        }

        for (int row = 0; row < reference.image.rows; ++row) {
            const float *sum = accumulated.ptr<float>(row);
            const ushort *views = support.ptr<ushort>(row);
            const float *prior = priorDepth.empty()
                ? nullptr : priorDepth.ptr<float>(row);
            float *bestValues = best.ptr<float>(row);
            float *secondValues = second.ptr<float>(row);
            float *depthValues = depth->ptr<float>(row);
            for (int column = 0; column < reference.image.cols; ++column) {
                if (views[column] < options.minimumSupportingViews) {
                    continue;
                }
                if (prior && std::isfinite(prior[column])
                    && std::abs(candidateDepth - prior[column])
                           > options.refinementRelativeDepthWindow
                                 * prior[column]) {
                    continue;
                }
                const float cost = sum[column] / views[column];
                if (cost < bestValues[column]) {
                    secondValues[column] = bestValues[column];
                    bestValues[column] = cost;
                    depthValues[column] = static_cast<float>(candidateDepth);
                } else if (cost < secondValues[column]) {
                    secondValues[column] = cost;
                }
            }
        }
    }

    *confidence = cv::Mat::zeros(reference.image.size(), CV_32F);
    for (int row = 0; row < reference.image.rows; ++row) {
        const float *bestValues = best.ptr<float>(row);
        const float *secondValues = second.ptr<float>(row);
        const float *depthValues = depth->ptr<float>(row);
        float *confidenceValues = confidence->ptr<float>(row);
        for (int column = 0; column < reference.image.cols; ++column) {
            if (!std::isfinite(depthValues[column])
                || !std::isfinite(secondValues[column])) {
                continue;
            }
            const double uniqueness = std::max(
                0.0, static_cast<double>(secondValues[column]
                                         - bestValues[column]))
                / std::max(1e-6, static_cast<double>(secondValues[column]));
            const double agreement = std::clamp(
                1.0 - 0.5 * bestValues[column], 0.0, 1.0);
            confidenceValues[column] =
                static_cast<float>(uniqueness * agreement);
        }
    }
    return true;
}

cv::Rect scaledTileRect(const cv::Rect &fullResolutionRect,
                        const cv::Size &fullResolutionSize,
                        const cv::Size &levelSize)
{
    const double scaleX = static_cast<double>(levelSize.width)
        / fullResolutionSize.width;
    const double scaleY = static_cast<double>(levelSize.height)
        / fullResolutionSize.height;
    const int left = std::clamp(
        static_cast<int>(std::floor(fullResolutionRect.x * scaleX)),
        0, levelSize.width - 1);
    const int top = std::clamp(
        static_cast<int>(std::floor(fullResolutionRect.y * scaleY)),
        0, levelSize.height - 1);
    const int right = std::clamp(
        static_cast<int>(std::ceil(
            (fullResolutionRect.x + fullResolutionRect.width) * scaleX)),
        left + 1, levelSize.width);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(
            (fullResolutionRect.y + fullResolutionRect.height) * scaleY)),
        top + 1, levelSize.height);
    return {left, top, right - left, bottom - top};
}

LevelView croppedLevel(const LevelView &view, const cv::Rect &rect)
{
    LevelView result = view;
    result.image = view.image(rect);
    result.intrinsic(0, 2) -= rect.x;
    result.intrinsic(1, 2) -= rect.y;
    return result;
}

std::uint64_t imageFingerprint(const cv::Mat &image)
{
    constexpr std::uint64_t offset = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    const size_t bytesPerRow = static_cast<size_t>(image.cols)
        * image.elemSize();
    for (int row = 0; row < image.rows; ++row) {
        const uchar *values = image.ptr<uchar>(row);
        for (size_t index = 0; index < bytesPerRow; ++index) {
            hash ^= values[index];
            hash *= prime;
        }
    }
    return hash;
}

void appendPreparedViewSignature(std::ostringstream *stream,
                                 const PreparedView &view)
{
    *stream << view.imageIndex << ':' << view.image.cols << 'x'
            << view.image.rows << ':' << std::hex
            << imageFingerprint(view.image) << std::dec << ':';
    for (double value : view.intrinsic.val) {
        *stream << value << ',';
    }
    for (double value : view.pose.worldToCameraRotation.val) {
        *stream << value << ',';
    }
    for (double value : view.pose.worldToCameraTranslation.val) {
        *stream << value << ',';
    }
}

std::string checkpointBaseSignature(
    const PreparedView &reference,
    const std::vector<PreparedView> &neighbours,
    const DenseDepthRange &range,
    const DenseMvsOptions &options)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << "kestrel-dense-tile-v1:"
           << range.minimumDepth << ':' << range.maximumDepth << ':'
           << options.pyramidLevels << ':' << options.depthHypotheses << ':'
           << options.patchRadius << ':' << options.minimumSupportingViews
           << ':' << options.minimumTextureVariance << ':'
           << options.refinementRelativeDepthWindow << ':';
    appendPreparedViewSignature(&stream, reference);
    for (const PreparedView &neighbour : neighbours) {
        appendPreparedViewSignature(&stream, neighbour);
    }
    return stream.str();
}

std::string tileCheckpointSignature(const std::string &base,
                                    const ProcessingTile &tile)
{
    return base + ':' + std::to_string(tile.core.x) + ':'
        + std::to_string(tile.core.y) + ':'
        + std::to_string(tile.core.width) + ':'
        + std::to_string(tile.core.height) + ':'
        + std::to_string(tile.expanded.x) + ':'
        + std::to_string(tile.expanded.y) + ':'
        + std::to_string(tile.expanded.width) + ':'
        + std::to_string(tile.expanded.height);
}

bool loadCheckpoint(const std::filesystem::path &path,
                    const std::string &signature,
                    const cv::Size &expectedSize,
                    cv::Mat *depth, cv::Mat *confidence)
{
    std::error_code fileError;
    if (!std::filesystem::exists(path, fileError) || fileError
        || std::filesystem::file_size(path, fileError) == 0 || fileError) {
        return false;
    }
    try {
        cv::FileStorage storage(path.string(), cv::FileStorage::READ);
        if (!storage.isOpened()) {
            return false;
        }
        std::string storedSignature;
        storage["signature"] >> storedSignature;
        storage["depth"] >> *depth;
        storage["confidence"] >> *confidence;
        return storedSignature == signature
               && depth->type() == CV_32FC1
               && confidence->type() == CV_32FC1
               && depth->size() == expectedSize
               && confidence->size() == expectedSize;
    } catch (const cv::Exception &) {
        return false;
    }
}

bool saveCheckpoint(const std::filesystem::path &path,
                    const std::string &signature,
                    const cv::Mat &depth, const cv::Mat &confidence,
                    std::string *errorMessage)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (errorMessage) {
            *errorMessage = "Could not create dense-MVS checkpoint directory: "
                + error.message();
        }
        return false;
    }
    const std::filesystem::path temporary =
        path.string() + ".tmp.yml.gz";
    std::filesystem::remove(temporary, error);
    try {
        cv::FileStorage storage(temporary.string(), cv::FileStorage::WRITE);
        if (!storage.isOpened()) {
            if (errorMessage) {
                *errorMessage = "Could not write dense-MVS checkpoint "
                    + path.string();
            }
            return false;
        }
        storage << "signature" << signature
                << "depth" << depth
                << "confidence" << confidence;
        storage.release();
    } catch (const cv::Exception &exception) {
        std::filesystem::remove(temporary, error);
        if (errorMessage) {
            *errorMessage = "Could not encode dense-MVS checkpoint: "
                + std::string(exception.what());
        }
        return false;
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        if (errorMessage) {
            *errorMessage = "Could not finalize dense-MVS checkpoint: "
                + error.message();
        }
        return false;
    }
    return true;
}

std::filesystem::path checkpointPath(const DenseMvsOptions &options,
                                     int imageIndex,
                                     const ProcessingTile &tile)
{
    const std::string filename = "tile_" + std::to_string(tile.core.x)
        + "_" + std::to_string(tile.core.y) + "_"
        + std::to_string(tile.core.width) + "x"
        + std::to_string(tile.core.height) + ".yml.gz";
    return std::filesystem::path(options.checkpointDirectory)
        / ("image_" + std::to_string(imageIndex)) / filename;
}

bool mapTiles(const cv::Size &size, const DenseMvsOptions &options,
              int halo, std::vector<ProcessingTile> *tiles,
              std::string *errorMessage)
{
    const int maximumCore = options.tileSizePixels > 0
        ? options.tileSizePixels : std::max(size.width, size.height);
    return TileLayout::build(size, {maximumCore, maximumCore}, halo,
                             tiles, errorMessage);
}

bool loadRawDepthMap(const DenseDepthMap &descriptor,
                     const DenseMvsOptions &options,
                     DenseDepthMap *depthMap,
                     std::string *errorMessage)
{
    if (!depthMap || descriptor.depthMapSize.width <= 0
        || descriptor.depthMapSize.height <= 0
        || descriptor.rawCheckpointSignature.empty()
        || options.checkpointDirectory.empty()) {
        if (errorMessage) {
            *errorMessage = "Raw depth-map checkpoint metadata is incomplete.";
        }
        return false;
    }
    std::vector<ProcessingTile> tiles;
    if (!mapTiles(descriptor.depthMapSize, options,
                  descriptor.tileHaloPixels, &tiles, errorMessage)) {
        return false;
    }
    *depthMap = descriptor;
    depthMap->depth = cv::Mat(descriptor.depthMapSize, CV_32F,
                             cv::Scalar(kInvalidDepth));
    depthMap->confidence = cv::Mat::zeros(
        descriptor.depthMapSize, CV_32F);
    depthMap->validityMask = cv::Mat::zeros(
        descriptor.depthMapSize, CV_8U);
    for (const ProcessingTile &tile : tiles) {
        cv::Mat coreDepth;
        cv::Mat coreConfidence;
        const std::filesystem::path path = checkpointPath(
            options, descriptor.imageIndex, tile);
        const std::string signature = tileCheckpointSignature(
            descriptor.rawCheckpointSignature, tile);
        if (!loadCheckpoint(path, signature, tile.core.size(),
                            &coreDepth, &coreConfidence)) {
            if (errorMessage) {
                *errorMessage = "Could not load valid raw dense-MVS tile "
                    + path.string();
            }
            return false;
        }
        coreDepth.copyTo(depthMap->depth(tile.core));
        coreConfidence.copyTo(depthMap->confidence(tile.core));
    }
    for (int row = 0; row < depthMap->depth.rows; ++row) {
        const float *depth = depthMap->depth.ptr<float>(row);
        uchar *valid = depthMap->validityMask.ptr<uchar>(row);
        for (int column = 0; column < depthMap->depth.cols; ++column) {
            valid[column] = std::isfinite(depth[column]) ? 255 : 0;
        }
    }
    return true;
}

std::string compactSignature(const std::string &value)
{
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t first = 1469598103934665603ULL;
    std::uint64_t second = 1099511628211ULL;
    for (unsigned char byte : value) {
        first = (first ^ byte) * prime;
        // A second independent traversal state makes accidental acceptance of
        // a changed multi-map dependency substantially less likely while
        // keeping OpenCV YAML signature fields small.
        second = (second ^ static_cast<unsigned char>(byte + 0x9dU))
            * (prime + 0x13ULL);
    }
    std::ostringstream stream;
    stream << "fnv-pair-" << std::hex << first << '-' << second
           << '-' << std::dec << value.size();
    return stream.str();
}

std::string consistencyBaseSignature(
    const std::vector<DenseDepthMap> &descriptors,
    size_t referenceIndex,
    const DenseMvsOptions &options)
{
    if (referenceIndex >= descriptors.size()) {
        return {};
    }
    std::map<int, const DenseDepthMap *> byImage;
    for (const DenseDepthMap &descriptor : descriptors) {
        byImage[descriptor.imageIndex] = &descriptor;
    }
    const DenseDepthMap &reference = descriptors[referenceIndex];
    std::ostringstream stream;
    stream << std::setprecision(17) << "kestrel-consistency-tile-v1:"
           << options.minimumConsistencyChecks << ':'
           << options.minimumConsistentViews << ':'
           << options.consistencyRelativeDepthTolerance << ':'
           << options.consistencyAbsoluteDepthTolerance << ':'
           << options.maximumRoundTripErrorPixels << ':'
           << options.minimumConfidence << ':'
           << reference.imageIndex << ':'
           << reference.rawCheckpointSignature << ':';
    for (int neighbourImage : reference.neighbourImageIndices) {
        stream << neighbourImage << '=';
        const auto neighbour = byImage.find(neighbourImage);
        if (neighbour != byImage.end()) {
            stream << neighbour->second->rawCheckpointSignature;
        } else {
            stream << "missing";
        }
        stream << ':';
    }
    return compactSignature(stream.str());
}

std::filesystem::path consistencyCheckpointPath(
    const DenseMvsOptions &options, int imageIndex,
    const ProcessingTile &tile)
{
    return std::filesystem::path(options.checkpointDirectory) / "consistent"
        / ("image_" + std::to_string(imageIndex))
        / ("tile_" + std::to_string(tile.core.x) + "_"
           + std::to_string(tile.core.y) + "_"
           + std::to_string(tile.core.width) + "x"
           + std::to_string(tile.core.height) + ".yml.gz");
}

bool loadConsistencyCheckpoint(
    const std::filesystem::path &path, const std::string &signature,
    const cv::Size &expectedSize, DenseDepthMap *tile)
{
    std::error_code fileError;
    if (!std::filesystem::exists(path, fileError) || fileError
        || std::filesystem::file_size(path, fileError) == 0 || fileError) {
        return false;
    }
    if (!tile) {
        return false;
    }
    try {
        cv::FileStorage storage(path.string(), cv::FileStorage::READ);
        if (!storage.isOpened()) {
            return false;
        }
        std::string storedSignature;
        storage["signature"] >> storedSignature;
        storage["depth"] >> tile->depth;
        storage["confidence"] >> tile->confidence;
        storage["validity"] >> tile->validityMask;
        storage["consistent_views"] >> tile->consistentViewCount;
        storage["occluded_views"] >> tile->occludedViewCount;
        return storedSignature == signature
            && tile->depth.type() == CV_32FC1
            && tile->confidence.type() == CV_32FC1
            && tile->validityMask.type() == CV_8UC1
            && tile->consistentViewCount.type() == CV_8UC1
            && tile->occludedViewCount.type() == CV_8UC1
            && tile->depth.size() == expectedSize
            && tile->confidence.size() == expectedSize
            && tile->validityMask.size() == expectedSize
            && tile->consistentViewCount.size() == expectedSize
            && tile->occludedViewCount.size() == expectedSize;
    } catch (const cv::Exception &) {
        return false;
    }
}

bool saveConsistencyCheckpoint(
    const std::filesystem::path &path, const std::string &signature,
    const DenseDepthMap &tile, std::string *errorMessage)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (errorMessage) {
            *errorMessage = "Could not create consistency checkpoint directory: "
                + error.message();
        }
        return false;
    }
    const std::filesystem::path temporary =
        path.string() + ".tmp.yml.gz";
    std::filesystem::remove(temporary, error);
    try {
        cv::FileStorage storage(temporary.string(), cv::FileStorage::WRITE);
        if (!storage.isOpened()) {
            if (errorMessage) {
                *errorMessage = "Could not write consistency checkpoint "
                    + path.string();
            }
            return false;
        }
        storage << "signature" << signature
                << "depth" << tile.depth
                << "confidence" << tile.confidence
                << "validity" << tile.validityMask
                << "consistent_views" << tile.consistentViewCount
                << "occluded_views" << tile.occludedViewCount;
        storage.release();
    } catch (const cv::Exception &exception) {
        std::filesystem::remove(temporary, error);
        if (errorMessage) {
            *errorMessage = "Could not encode consistency checkpoint: "
                + std::string(exception.what());
        }
        return false;
    }
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        if (errorMessage) {
            *errorMessage = "Could not finalize consistency checkpoint: "
                + error.message();
        }
        return false;
    }
    return true;
}

} // namespace

std::vector<std::vector<DenseMvsNeighbour>>
PlaneSweepMvsBackend::selectNeighbours(
    const DenseReconstructionInput &input, const DenseMvsOptions &options)
{
    std::vector<std::vector<DenseMvsNeighbour>> result(input.views.size());
    std::map<int, int> viewByImage;
    for (int index = 0; index < static_cast<int>(input.views.size()); ++index) {
        viewByImage[input.views[index].imageIndex] = index;
    }
    std::map<int, const SparsePoint *> pointByTrack;
    for (const SparsePoint &point : input.sparsePoints) {
        pointByTrack[point.trackId] = &point;
    }
    std::map<std::pair<int, int>, std::vector<double>> pairAngles;
    for (const FeatureTrack &track : input.tracks) {
        const auto point = pointByTrack.find(track.id);
        if (point == pointByTrack.end()) {
            continue;
        }
        std::set<int> trackViews;
        for (const FeatureObservation &observation : track.observations) {
            const auto view = viewByImage.find(observation.imageIndex);
            if (view != viewByImage.end()
                && input.views[view->second].pose.componentId
                       == point->second->componentId) {
                trackViews.insert(view->second);
            }
        }
        for (auto first = trackViews.begin(); first != trackViews.end(); ++first) {
            for (auto second = std::next(first); second != trackViews.end();
                 ++second) {
                const cv::Point3d firstCentre = cameraCentre(
                    input.views[*first].pose);
                const cv::Point3d secondCentre = cameraCentre(
                    input.views[*second].pose);
                pairAngles[{*first, *second}].push_back(
                    triangulationAngleDegrees(point->second->position,
                                              firstCentre, secondCentre));
            }
        }
    }

    for (auto &[pair, angles] : pairAngles) {
        const int sharedPointCount = static_cast<int>(angles.size());
        if (sharedPointCount < options.minimumSharedSparsePoints) {
            continue;
        }
        const double angle = median(angles);
        if (angle < options.minimumTriangulationAngleDegrees
            || angle > options.maximumTriangulationAngleDegrees) {
            continue;
        }
        const double preferred = std::max(
            0.1, options.preferredTriangulationAngleDegrees);
        const double angleQuality = std::exp(
            -std::abs(std::log(std::max(0.1, angle) / preferred)));
        const double score = sharedPointCount * angleQuality;
        const int first = pair.first;
        const int second = pair.second;
        result[first].push_back({
            second, input.views[second].imageIndex,
            sharedPointCount, angle, score});
        result[second].push_back({
            first, input.views[first].imageIndex,
            sharedPointCount, angle, score});
    }
    for (auto &neighbours : result) {
        std::sort(neighbours.begin(), neighbours.end(),
                  [](const DenseMvsNeighbour &first,
                     const DenseMvsNeighbour &second) {
                      if (first.score != second.score) {
                          return first.score > second.score;
                      }
                      return first.imageIndex < second.imageIndex;
                  });
        if (static_cast<int>(neighbours.size()) > options.maximumNeighbours) {
            neighbours.resize(options.maximumNeighbours);
        }
    }
    return result;
}

DenseDepthRange PlaneSweepMvsBackend::estimateDepthRange(
    const DenseMvsView &reference,
    const std::vector<SparsePoint> &sparsePoints,
    const std::vector<FeatureTrack> &tracks,
    const DenseMvsOptions &options)
{
    std::set<int> visibleTracks;
    for (const FeatureTrack &track : tracks) {
        for (const FeatureObservation &observation : track.observations) {
            if (observation.imageIndex == reference.imageIndex) {
                visibleTracks.insert(track.id);
                break;
            }
        }
    }
    std::vector<double> depths;
    for (const SparsePoint &point : sparsePoints) {
        if (point.componentId != reference.pose.componentId
            || visibleTracks.count(point.trackId) == 0) {
            continue;
        }
        const cv::Vec3d cameraPoint =
            reference.pose.worldToCameraRotation
                * cv::Vec3d(point.position.x, point.position.y,
                            point.position.z)
            + reference.pose.worldToCameraTranslation;
        if (cameraPoint[2] > 1e-6 && std::isfinite(cameraPoint[2])) {
            depths.push_back(cameraPoint[2]);
        }
    }
    DenseDepthRange result;
    result.supportingPointCount = static_cast<int>(depths.size());
    if (result.supportingPointCount < options.minimumDepthRangePoints) {
        return result;
    }
    std::sort(depths.begin(), depths.end());
    const double quantile = std::clamp(options.depthRangeQuantile, 0.0, 0.45);
    const size_t lowerIndex = static_cast<size_t>(
        std::floor(quantile * (depths.size() - 1)));
    const size_t upperIndex = static_cast<size_t>(
        std::ceil((1.0 - quantile) * (depths.size() - 1)));
    const double lower = depths[lowerIndex];
    const double upper = depths[upperIndex];
    const double span = upper - lower;
    const double centre = 0.5 * (lower + upper);
    const double padding = std::max(
        options.depthRangePaddingFraction * span, 0.03 * centre);
    result.minimumDepth = std::max(1e-4, lower - padding);
    result.maximumDepth = upper + padding;
    result.valid = result.maximumDepth > result.minimumDepth;
    return result;
}

int PlaneSweepMvsBackend::recommendedTileHaloPixels(
    const DenseMvsOptions &options)
{
    const int levels = std::clamp(options.pyramidLevels, 1, 16);
    const int coarsestScale = 1 << (levels - 1);
    // boxFilter support is patchRadius at the coarsest level. Two additional
    // pixels per level cover pyrDown's five-tap resampling support.
    return std::max(1, (std::max(0, options.patchRadius) + 2)
                           * coarsestScale);
}

bool PlaneSweepMvsBackend::computeDepthMap(
    const DenseMvsView &reference,
    const std::vector<const DenseMvsView *> &neighbours,
    const DenseDepthRange &depthRange,
    const DenseMvsOptions &options,
    DenseDepthMap *depthMap, std::string *errorMessage)
{
    if (!depthMap || !depthRange.valid
        || depthRange.minimumDepth <= 0.0
        || depthRange.maximumDepth <= depthRange.minimumDepth
        || options.pyramidLevels < 1 || options.pyramidLevels > 16
        || options.depthHypotheses < 2 || options.patchRadius < 0
        || options.minimumSupportingViews < 1
        || options.tileSizePixels < 0 || options.tileHaloPixels < 0) {
        if (errorMessage) {
            *errorMessage = "Dense MVS received an invalid depth range or options.";
        }
        return false;
    }
    if (static_cast<int>(neighbours.size())
        < options.minimumSupportingViews) {
        if (errorMessage) {
            *errorMessage = "Dense MVS has too few neighbouring views.";
        }
        return false;
    }
    PreparedView preparedReference;
    if (!prepareView(reference, options.maximumImageDimension,
                     &preparedReference, errorMessage)) {
        return false;
    }
    std::vector<PreparedView> preparedNeighbours;
    for (const DenseMvsView *neighbour : neighbours) {
        if (!neighbour) {
            continue;
        }
        PreparedView prepared;
        if (!prepareView(*neighbour, options.maximumImageDimension,
                         &prepared, errorMessage)) {
            return false;
        }
        preparedNeighbours.push_back(std::move(prepared));
    }
    if (static_cast<int>(preparedNeighbours.size())
        < options.minimumSupportingViews) {
        return false;
    }

    const int levelCount = std::max(1, options.pyramidLevels);
    const int minimumHalo = recommendedTileHaloPixels(options);
    const int tileHalo = options.tileHaloPixels > 0
        ? options.tileHaloPixels : minimumHalo;
    if (options.tileSizePixels < 0 || options.tileHaloPixels < 0
        || (options.tileSizePixels > 0 && tileHalo < minimumHalo)) {
        if (errorMessage) {
            *errorMessage = "Dense-MVS tiling requires non-negative sizes and a halo large enough for pyramid/patch support.";
        }
        return false;
    }

    std::vector<LevelView> referencePyramid(levelCount);
    std::vector<std::vector<LevelView>> neighbourPyramids(
        preparedNeighbours.size(), std::vector<LevelView>(levelCount));
    for (int level = 0; level < levelCount; ++level) {
        referencePyramid[level] = atLevel(preparedReference, level);
        for (int neighbour = 0;
             neighbour < static_cast<int>(preparedNeighbours.size());
             ++neighbour) {
            neighbourPyramids[neighbour][level] = atLevel(
                preparedNeighbours[neighbour], level);
        }
    }

    const int maximumCore = options.tileSizePixels > 0
        ? options.tileSizePixels
        : std::max(preparedReference.image.cols,
                   preparedReference.image.rows);
    std::vector<ProcessingTile> tiles;
    if (!TileLayout::build(preparedReference.image.size(),
                           {maximumCore, maximumCore}, tileHalo,
                           &tiles, errorMessage)) {
        return false;
    }

    cv::Mat depth(preparedReference.image.size(), CV_32F,
                  cv::Scalar(kInvalidDepth));
    cv::Mat confidence = cv::Mat::zeros(
        preparedReference.image.size(), CV_32F);
    int resumedTiles = 0;
    const bool checkpointEnabled = !options.checkpointDirectory.empty();
    const std::string checkpointBase = checkpointEnabled
        ? checkpointBaseSignature(preparedReference, preparedNeighbours,
                                  depthRange, options)
        : std::string();
    for (const ProcessingTile &tile : tiles) {
        const std::string signature = checkpointEnabled
            ? tileCheckpointSignature(checkpointBase, tile) : std::string();
        cv::Mat coreDepth;
        cv::Mat coreConfidence;
        const std::filesystem::path path = checkpointEnabled
            ? checkpointPath(options, reference.imageIndex, tile)
            : std::filesystem::path();
        if (checkpointEnabled && options.resumeFromCheckpoints
            && std::filesystem::exists(path)
            && loadCheckpoint(path, signature, tile.core.size(),
                              &coreDepth, &coreConfidence)) {
            ++resumedTiles;
        } else {
            cv::Mat priorDepth;
            cv::Mat tileDepth;
            cv::Mat tileConfidence;
            for (int level = levelCount - 1; level >= 0; --level) {
                const cv::Rect levelRect = scaledTileRect(
                    tile.expanded, preparedReference.image.size(),
                    referencePyramid[level].image.size());
                const LevelView referenceLevel = croppedLevel(
                    referencePyramid[level], levelRect);
                std::vector<LevelView> sourceLevels;
                sourceLevels.reserve(preparedNeighbours.size());
                for (int neighbour = 0;
                     neighbour < static_cast<int>(preparedNeighbours.size());
                     ++neighbour) {
                    sourceLevels.push_back(
                        neighbourPyramids[neighbour][level]);
                }
                cv::Mat scaledPrior;
                if (!priorDepth.empty()) {
                    cv::resize(priorDepth, scaledPrior,
                               referenceLevel.image.size(),
                               0.0, 0.0, cv::INTER_NEAREST);
                }
                if (!sweepLevel(referenceLevel, sourceLevels, depthRange,
                                options, scaledPrior,
                                &tileDepth, &tileConfidence)) {
                    if (errorMessage) {
                        *errorMessage = "Dense MVS plane sweep failed in tile "
                            + std::to_string(tile.index) + ".";
                    }
                    return false;
                }
                priorDepth = tileDepth;
            }
            const cv::Rect coreInExpanded = tile.coreInExpanded();
            coreDepth = tileDepth(coreInExpanded).clone();
            coreConfidence = tileConfidence(coreInExpanded).clone();
            if (checkpointEnabled
                && !saveCheckpoint(path, signature, coreDepth,
                                   coreConfidence, errorMessage)) {
                return false;
            }
        }
        coreDepth.copyTo(depth(tile.core));
        coreConfidence.copyTo(confidence(tile.core));
    }

    depthMap->imageIndex = reference.imageIndex;
    depthMap->depth = depth;
    depthMap->confidence = confidence;
    depthMap->depthMapSize = depth.size();
    depthMap->validityMask = cv::Mat::zeros(depth.size(), CV_8U);
    for (int row = 0; row < depth.rows; ++row) {
        const float *depthValues = depth.ptr<float>(row);
        uchar *valid = depthMap->validityMask.ptr<uchar>(row);
        for (int column = 0; column < depth.cols; ++column) {
            // Preserve every finite raw hypothesis for cross-view geometry.
            // The confidence threshold is applied after consistency so a
            // weak but geometrically useful source map is not discarded too
            // early.
            if (std::isfinite(depthValues[column])) {
                valid[column] = 255;
            }
        }
    }
    depthMap->imageScale = preparedReference.imageScale;
    depthMap->depthRange = depthRange;
    depthMap->processedTileCount = static_cast<int>(tiles.size());
    depthMap->resumedTileCount = resumedTiles;
    depthMap->tileHaloPixels = tileHalo;
    depthMap->rawCheckpointSignature = checkpointBase;
    depthMap->neighbourImageIndices.clear();
    for (const PreparedView &neighbour : preparedNeighbours) {
        depthMap->neighbourImageIndices.push_back(neighbour.imageIndex);
    }
    depthMap->rawValidPixelCount = static_cast<size_t>(
        cv::countNonZero(depthMap->validityMask));
    return true;
}

namespace {

cv::Matx33d depthMapIntrinsic(const DenseMvsView &view,
                              const DenseDepthMap &depthMap)
{
    const cv::Size sourceSize = !view.image.empty()
        ? view.image.size() : view.calibration.imageSize;
    return scaledIntrinsic(
        view.calibration.intrinsic,
        static_cast<double>(depthMap.depth.cols) / sourceSize.width,
        static_cast<double>(depthMap.depth.rows) / sourceSize.height);
}

bool worldFromDepthPixel(const DenseMvsView &view,
                         const DenseDepthMap &depthMap,
                         double imageX, double imageY, double depth,
                         cv::Point3d *world)
{
    if (!world || !(depth > 0.0) || !std::isfinite(depth)) {
        return false;
    }
    const cv::Matx33d intrinsic = depthMapIntrinsic(view, depthMap);
    const cv::Vec3d ray = intrinsic.inv()
        * cv::Vec3d(imageX, imageY, 1.0);
    const cv::Vec3d cameraPoint = ray * depth;
    const cv::Vec3d value = view.pose.worldToCameraRotation.t()
        * (cameraPoint - view.pose.worldToCameraTranslation);
    if (!std::isfinite(value[0]) || !std::isfinite(value[1])
        || !std::isfinite(value[2])) {
        return false;
    }
    *world = {value[0], value[1], value[2]};
    return true;
}

bool projectToDepthMap(const DenseMvsView &view,
                       const DenseDepthMap &depthMap,
                       const cv::Point3d &world,
                       cv::Point2d *imagePoint, double *cameraDepth)
{
    const cv::Vec3d cameraPoint = view.pose.worldToCameraRotation
        * cv::Vec3d(world.x, world.y, world.z)
        + view.pose.worldToCameraTranslation;
    if (!(cameraPoint[2] > 1e-9) || !std::isfinite(cameraPoint[2])) {
        return false;
    }
    const cv::Vec3d projected = depthMapIntrinsic(view, depthMap)
                               * cameraPoint;
    const double x = projected[0] / projected[2];
    const double y = projected[1] / projected[2];
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    if (imagePoint) {
        *imagePoint = {x, y};
    }
    if (cameraDepth) {
        *cameraDepth = cameraPoint[2];
    }
    return true;
}

bool sampleValidDepth(const DenseDepthMap &map, double x, double y,
                      double *depth)
{
    if (!depth || x < 0.0 || y < 0.0
        || x >= map.depth.cols - 1 || y >= map.depth.rows - 1
        || map.depth.type() != CV_32FC1
        || map.validityMask.type() != CV_8UC1) {
        return false;
    }
    const int left = static_cast<int>(std::floor(x));
    const int top = static_cast<int>(std::floor(y));
    const double alphaX = x - left;
    const double alphaY = y - top;
    double value = 0.0;
    for (int rowOffset = 0; rowOffset <= 1; ++rowOffset) {
        for (int columnOffset = 0; columnOffset <= 1; ++columnOffset) {
            const int row = top + rowOffset;
            const int column = left + columnOffset;
            if (!map.validityMask.at<uchar>(row, column)) {
                return false;
            }
            const float sample = map.depth.at<float>(row, column);
            if (!(sample > 0.0f) || !std::isfinite(sample)) {
                return false;
            }
            const double weight = (columnOffset ? alphaX : 1.0 - alphaX)
                * (rowOffset ? alphaY : 1.0 - alphaY);
            value += weight * sample;
        }
    }
    *depth = value;
    return true;
}

bool filterDepthMap(
    const DenseDepthMap &referenceMap,
    const std::map<int, const DenseMvsView *> &viewByImage,
    const std::map<int, const DenseDepthMap *> &mapByImage,
    const DenseMvsOptions &options,
    DenseDepthMap *output)
{
    const auto referenceViewEntry = viewByImage.find(referenceMap.imageIndex);
    if (!output || referenceViewEntry == viewByImage.end()
        || referenceMap.depth.type() != CV_32FC1
        || referenceMap.confidence.type() != CV_32FC1
        || referenceMap.validityMask.type() != CV_8UC1) {
        return false;
    }
    const DenseMvsView &referenceView = *referenceViewEntry->second;
    *output = referenceMap;
    // MatExpr assignment may reuse a shared cv::Mat allocation. Release the
    // copied headers first so zero-initializing outputs cannot erase the raw
    // reference matrices they were copied from.
    output->validityMask.release();
    output->confidence.release();
    output->consistentViewCount.release();
    output->occludedViewCount.release();
    output->validityMask = cv::Mat::zeros(referenceMap.depth.size(), CV_8U);
    output->confidence = cv::Mat::zeros(referenceMap.depth.size(), CV_32F);
    output->consistentViewCount = cv::Mat::zeros(
        referenceMap.depth.size(), CV_8U);
    output->occludedViewCount = cv::Mat::zeros(
        referenceMap.depth.size(), CV_8U);

    for (int row = 0; row < referenceMap.depth.rows; ++row) {
        const float *referenceDepth = referenceMap.depth.ptr<float>(row);
        const float *referenceConfidence =
            referenceMap.confidence.ptr<float>(row);
        const uchar *referenceValid =
            referenceMap.validityMask.ptr<uchar>(row);
        uchar *filtered = output->validityMask.ptr<uchar>(row);
        float *confidence = output->confidence.ptr<float>(row);
        uchar *consistentOutput =
            output->consistentViewCount.ptr<uchar>(row);
        uchar *occludedOutput = output->occludedViewCount.ptr<uchar>(row);
        for (int column = 0; column < referenceMap.depth.cols; ++column) {
            if (!referenceValid[column]
                || !(referenceDepth[column] > 0.0f)
                || !std::isfinite(referenceDepth[column])) {
                continue;
            }
            cv::Point3d world;
            if (!worldFromDepthPixel(referenceView, referenceMap,
                                     column, row,
                                     referenceDepth[column], &world)) {
                continue;
            }
            int checks = 0;
            int consistent = 0;
            int occluded = 0;
            for (int neighbourImage : referenceMap.neighbourImageIndices) {
                const auto sourceViewEntry = viewByImage.find(neighbourImage);
                const auto sourceMapEntry = mapByImage.find(neighbourImage);
                if (sourceViewEntry == viewByImage.end()
                    || sourceMapEntry == mapByImage.end()) {
                    continue;
                }
                const DenseMvsView &sourceView = *sourceViewEntry->second;
                const DenseDepthMap &sourceMap = *sourceMapEntry->second;
                cv::Point2d sourcePixel;
                double predictedSourceDepth = 0.0;
                if (!projectToDepthMap(sourceView, sourceMap, world,
                                       &sourcePixel,
                                       &predictedSourceDepth)) {
                    continue;
                }
                double sampledSourceDepth = 0.0;
                if (!sampleValidDepth(sourceMap, sourcePixel.x,
                                      sourcePixel.y,
                                      &sampledSourceDepth)) {
                    continue;
                }
                const double tolerance =
                    options.consistencyAbsoluteDepthTolerance
                    + options.consistencyRelativeDepthTolerance
                          * predictedSourceDepth;
                if (sampledSourceDepth < predictedSourceDepth - tolerance) {
                    ++occluded;
                    continue;
                }
                ++checks;
                if (std::abs(sampledSourceDepth - predictedSourceDepth)
                    > tolerance) {
                    continue;
                }
                cv::Point3d returnedWorld;
                if (!worldFromDepthPixel(sourceView, sourceMap,
                                         sourcePixel.x, sourcePixel.y,
                                         sampledSourceDepth,
                                         &returnedWorld)) {
                    continue;
                }
                cv::Point2d returnedReferencePixel;
                if (!projectToDepthMap(referenceView, referenceMap,
                                       returnedWorld,
                                       &returnedReferencePixel,
                                       nullptr)) {
                    continue;
                }
                if (cv::norm(returnedReferencePixel
                             - cv::Point2d(column, row))
                    <= options.maximumRoundTripErrorPixels) {
                    ++consistent;
                }
            }
            consistentOutput[column] = static_cast<uchar>(
                std::min(255, consistent));
            occludedOutput[column] = static_cast<uchar>(
                std::min(255, occluded));
            if (checks < options.minimumConsistencyChecks
                || consistent < options.minimumConsistentViews) {
                continue;
            }
            const double consistencyRatio =
                static_cast<double>(consistent) / checks;
            confidence[column] = static_cast<float>(
                std::clamp(referenceConfidence[column] * consistencyRatio,
                           0.0, 1.0));
            if (confidence[column] >= options.minimumConfidence) {
                filtered[column] = 255;
            }
        }
    }
    output->consistentValidPixelCount = static_cast<size_t>(
        cv::countNonZero(output->validityMask));
    return true;
}

} // namespace

bool PlaneSweepMvsBackend::applyMultiViewConsistency(
    const std::vector<DenseMvsView> &views,
    std::vector<DenseDepthMap> *depthMaps,
    const DenseMvsOptions &options,
    std::string *errorMessage)
{
    if (!depthMaps || depthMaps->empty()) {
        if (errorMessage) {
            *errorMessage = "Dense MVS consistency received no depth maps.";
        }
        return false;
    }
    std::map<int, const DenseMvsView *> viewByImage;
    for (const DenseMvsView &view : views) {
        viewByImage[view.imageIndex] = &view;
    }
    const std::vector<DenseDepthMap> original = *depthMaps;
    std::map<int, const DenseDepthMap *> mapByImage;
    for (const DenseDepthMap &map : original) {
        mapByImage[map.imageIndex] = &map;
    }

    bool processedAny = false;
    for (size_t mapIndex = 0; mapIndex < depthMaps->size(); ++mapIndex) {
        DenseDepthMap filtered;
        if (filterDepthMap(original[mapIndex], viewByImage, mapByImage,
                           options, &filtered)) {
            (*depthMaps)[mapIndex] = std::move(filtered);
            processedAny = true;
        }
    }
    if (!processedAny && errorMessage) {
        *errorMessage = "Dense MVS consistency found no matching camera/map pairs.";
    }
    return processedAny;
}

bool PlaneSweepMvsBackend::loadConsistentDepthMap(
    const std::vector<DenseDepthMap> &depthMapDescriptors,
    size_t depthMapIndex,
    const DenseMvsOptions &options,
    DenseDepthMap *depthMap,
    std::string *errorMessage)
{
    if (!depthMap || depthMapIndex >= depthMapDescriptors.size()
        || options.checkpointDirectory.empty()) {
        if (errorMessage) {
            *errorMessage = "Consistency checkpoint request is invalid.";
        }
        return false;
    }
    const DenseDepthMap &descriptor = depthMapDescriptors[depthMapIndex];
    if (descriptor.depthMapSize.width <= 0
        || descriptor.depthMapSize.height <= 0) {
        if (errorMessage) {
            *errorMessage = "Consistency checkpoint has no depth-map geometry.";
        }
        return false;
    }
    std::vector<ProcessingTile> tiles;
    if (!mapTiles(descriptor.depthMapSize, options, 0, &tiles,
                  errorMessage)) {
        return false;
    }
    const std::string base = consistencyBaseSignature(
        depthMapDescriptors, depthMapIndex, options);
    *depthMap = descriptor;
    depthMap->depth = cv::Mat(descriptor.depthMapSize, CV_32F,
                             cv::Scalar(kInvalidDepth));
    depthMap->confidence = cv::Mat::zeros(
        descriptor.depthMapSize, CV_32F);
    depthMap->validityMask = cv::Mat::zeros(
        descriptor.depthMapSize, CV_8U);
    depthMap->consistentViewCount = cv::Mat::zeros(
        descriptor.depthMapSize, CV_8U);
    depthMap->occludedViewCount = cv::Mat::zeros(
        descriptor.depthMapSize, CV_8U);
    for (const ProcessingTile &tile : tiles) {
        DenseDepthMap checkpoint;
        const std::filesystem::path path = consistencyCheckpointPath(
            options, descriptor.imageIndex, tile);
        const std::string signature = tileCheckpointSignature(base, tile);
        if (!loadConsistencyCheckpoint(path, signature, tile.core.size(),
                                       &checkpoint)) {
            if (errorMessage) {
                *errorMessage = "Could not load valid consistency tile "
                    + path.string();
            }
            return false;
        }
        checkpoint.depth.copyTo(depthMap->depth(tile.core));
        checkpoint.confidence.copyTo(depthMap->confidence(tile.core));
        checkpoint.validityMask.copyTo(depthMap->validityMask(tile.core));
        checkpoint.consistentViewCount.copyTo(
            depthMap->consistentViewCount(tile.core));
        checkpoint.occludedViewCount.copyTo(
            depthMap->occludedViewCount(tile.core));
    }
    depthMap->processedConsistencyTileCount = static_cast<int>(tiles.size());
    depthMap->resumedConsistencyTileCount = static_cast<int>(tiles.size());
    depthMap->consistentValidPixelCount = static_cast<size_t>(
        cv::countNonZero(depthMap->validityMask));
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

namespace {

bool applyCheckpointBackedConsistency(
    const std::vector<DenseMvsView> &views,
    std::vector<DenseDepthMap> *descriptors,
    const DenseMvsOptions &options,
    std::string *errorMessage)
{
    if (!descriptors || descriptors->empty()
        || options.checkpointDirectory.empty()) {
        if (errorMessage) {
            *errorMessage = "Streamed consistency requires depth-map checkpoints.";
        }
        return false;
    }
    std::map<int, const DenseMvsView *> viewByImage;
    for (const DenseMvsView &view : views) {
        viewByImage[view.imageIndex] = &view;
    }
    std::map<int, size_t> descriptorByImage;
    for (size_t index = 0; index < descriptors->size(); ++index) {
        descriptorByImage[(*descriptors)[index].imageIndex] = index;
    }

    bool processedAny = false;
    for (size_t referenceIndex = 0;
         referenceIndex < descriptors->size(); ++referenceIndex) {
        DenseDepthMap resumed;
        if (options.resumeFromCheckpoints
            && PlaneSweepMvsBackend::loadConsistentDepthMap(
                *descriptors, referenceIndex, options, &resumed, nullptr)) {
            DenseDepthMap &descriptor = (*descriptors)[referenceIndex];
            descriptor.consistentValidPixelCount =
                resumed.consistentValidPixelCount;
            descriptor.processedConsistencyTileCount =
                resumed.processedConsistencyTileCount;
            descriptor.resumedConsistencyTileCount =
                resumed.resumedConsistencyTileCount;
            processedAny = true;
            continue;
        }

        DenseDepthMap reference;
        if (!loadRawDepthMap((*descriptors)[referenceIndex], options,
                             &reference, errorMessage)) {
            return false;
        }
        std::vector<DenseDepthMap> neighbours;
        neighbours.reserve(reference.neighbourImageIndices.size());
        for (int neighbourImage : reference.neighbourImageIndices) {
            const auto descriptorEntry = descriptorByImage.find(neighbourImage);
            if (descriptorEntry == descriptorByImage.end()) {
                continue;
            }
            DenseDepthMap neighbour;
            if (!loadRawDepthMap((*descriptors)[descriptorEntry->second],
                                 options, &neighbour, errorMessage)) {
                return false;
            }
            neighbours.push_back(std::move(neighbour));
        }
        std::map<int, const DenseDepthMap *> mapByImage;
        mapByImage[reference.imageIndex] = &reference;
        for (const DenseDepthMap &neighbour : neighbours) {
            mapByImage[neighbour.imageIndex] = &neighbour;
        }
        DenseDepthMap filtered;
        if (!filterDepthMap(reference, viewByImage, mapByImage,
                            options, &filtered)) {
            if (errorMessage) {
                *errorMessage = "Could not filter checkpointed depth map "
                    + std::to_string(reference.imageIndex) + ".";
            }
            return false;
        }
        std::vector<ProcessingTile> tiles;
        if (!mapTiles(filtered.depthMapSize, options, 0, &tiles,
                      errorMessage)) {
            return false;
        }
        const std::string base = consistencyBaseSignature(
            *descriptors, referenceIndex, options);
        for (const ProcessingTile &tile : tiles) {
            DenseDepthMap checkpoint;
            checkpoint.depth = filtered.depth(tile.core).clone();
            checkpoint.confidence = filtered.confidence(tile.core).clone();
            checkpoint.validityMask =
                filtered.validityMask(tile.core).clone();
            checkpoint.consistentViewCount =
                filtered.consistentViewCount(tile.core).clone();
            checkpoint.occludedViewCount =
                filtered.occludedViewCount(tile.core).clone();
            if (!saveConsistencyCheckpoint(
                    consistencyCheckpointPath(
                        options, filtered.imageIndex, tile),
                    tileCheckpointSignature(base, tile), checkpoint,
                    errorMessage)) {
                return false;
            }
        }
        DenseDepthMap &descriptor = (*descriptors)[referenceIndex];
        descriptor.consistentValidPixelCount =
            filtered.consistentValidPixelCount;
        descriptor.processedConsistencyTileCount =
            static_cast<int>(tiles.size());
        descriptor.resumedConsistencyTileCount = 0;
        processedAny = true;
    }
    return processedAny;
}

void releaseDepthPixels(DenseDepthMap *map)
{
    map->depth.release();
    map->confidence.release();
    map->validityMask.release();
    map->consistentViewCount.release();
    map->occludedViewCount.release();
}

} // namespace

DenseReconstructionResult PlaneSweepMvsBackend::reconstruct(
    const DenseReconstructionInput &input,
    const DenseMvsOptions &options) const
{
    DenseReconstructionResult result;
    if (input.views.size() < 2 || input.sparsePoints.empty()
        || input.tracks.empty()) {
        result.message = "Dense MVS input lacks views or sparse support.";
        return result;
    }
    if (options.streamConsistencyFromCheckpoints
        && options.checkpointDirectory.empty()) {
        result.message = "Checkpoint-backed consistency requires a checkpoint directory.";
        return result;
    }
    const auto selected = selectNeighbours(input, options);
    for (int referenceIndex = 0;
         referenceIndex < static_cast<int>(input.views.size());
         ++referenceIndex) {
        const DenseDepthRange range = estimateDepthRange(
            input.views[referenceIndex], input.sparsePoints,
            input.tracks, options);
        if (!range.valid
            || static_cast<int>(selected[referenceIndex].size())
                   < options.minimumSupportingViews) {
            continue;
        }
        std::vector<const DenseMvsView *> neighbours;
        for (const DenseMvsNeighbour &selection : selected[referenceIndex]) {
            neighbours.push_back(&input.views[selection.viewIndex]);
        }
        DenseDepthMap map;
        std::string error;
        if (computeDepthMap(input.views[referenceIndex], neighbours,
                            range, options, &map, &error)) {
            result.depthMaps.push_back(std::move(map));
            if (options.streamConsistencyFromCheckpoints) {
                releaseDepthPixels(&result.depthMaps.back());
            }
        }
    }
    for (const DenseDepthMap &map : result.depthMaps) {
        result.rawValidPixelCount += map.rawValidPixelCount;
        result.processedTileCount += static_cast<size_t>(
            map.processedTileCount);
        result.resumedTileCount += static_cast<size_t>(
            map.resumedTileCount);
    }
    std::string consistencyError;
    const bool consistencySucceeded = result.depthMaps.empty() ? false
        : options.streamConsistencyFromCheckpoints
            ? applyCheckpointBackedConsistency(
                  input.views, &result.depthMaps, options,
                  &consistencyError)
            : applyMultiViewConsistency(
                  input.views, &result.depthMaps, options,
                  &consistencyError);
    if (!result.depthMaps.empty() && !consistencySucceeded) {
        result.message = consistencyError;
        return result;
    }
    for (const DenseDepthMap &map : result.depthMaps) {
        result.consistentPixelCount += map.consistentValidPixelCount;
        result.processedConsistencyTileCount += static_cast<size_t>(
            map.processedConsistencyTileCount);
        result.resumedConsistencyTileCount += static_cast<size_t>(
            map.resumedConsistencyTileCount);
    }
    result.success = result.consistentPixelCount > 0;
    if (result.success) {
        result.message = options.streamConsistencyFromCheckpoints
            ? "Dense MVS produced checkpoint-backed, streamed multi-view-consistent depth maps."
            : options.tileSizePixels > 0
            ? "Dense MVS produced tiled, resumable, multi-view-consistent plane-sweep depth maps."
            : "Dense MVS produced multi-view-consistent plane-sweep depth maps.";
    } else {
        result.message = "Dense MVS could not produce a consistent depth map.";
    }
    return result;
}

} // namespace kestrel
