#include "photogrammetry/terrain/OrthophotoSeamBlender.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace kestrel {

namespace {

bool validOptions(const OrthophotoSeamBlendOptions &options)
{
    return options.maximumIterations >= 0
           && std::isfinite(options.qualityCostWeight)
           && options.qualityCostWeight >= 0.0
           && std::isfinite(options.smoothnessCost)
           && options.smoothnessCost >= 0.0
           && std::isfinite(options.photometricSeamCostWeight)
           && options.photometricSeamCostWeight >= 0.0
           && options.multibandLevels > 0;
}

bool validate(const std::vector<OrthophotoBlendLayer> &layers,
              const OrthophotoSeamBlendOptions &options,
              std::string *message)
{
    if (layers.empty() || layers.front().image.empty()
        || !validOptions(options)) {
        *message = "Seam blending requires layers and valid options.";
        return false;
    }
    const cv::Size size = layers.front().image.size();
    const int type = layers.front().image.type();
    const int channels = layers.front().image.channels();
    if (channels <= 0 || channels > CV_CN_MAX) {
        *message = "Seam blending requires a valid channel count.";
        return false;
    }
    for (const OrthophotoBlendLayer &layer : layers) {
        if (layer.image.empty() || layer.image.size() != size
            || layer.image.type() != type
            || layer.quality.type() != CV_32FC1
            || layer.quality.size() != size) {
            *message = "Seam layers require matching images and float quality maps.";
            return false;
        }
    }
    return true;
}

cv::Mat grayFloat(const cv::Mat &image)
{
    cv::Mat gray;
    if (image.channels() == 1) {
        gray = image;
    } else if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    }
    cv::Mat result;
    gray.convertTo(result, CV_32F);
    return result;
}

double seamMismatch(const std::vector<cv::Mat> &gray,
                    const std::vector<OrthophotoBlendLayer> &layers,
                    int candidate, int other, int row, int column,
                    int neighbourRow, int neighbourColumn)
{
    if (other < 0 || candidate == other) {
        return 0.0;
    }
    if (layers[other].quality.at<float>(row, column) <= 0.0f
        || layers[candidate].quality.at<float>(neighbourRow,
                                               neighbourColumn) <= 0.0f
        || layers[other].quality.at<float>(neighbourRow,
                                           neighbourColumn) <= 0.0f) {
        return 1.0;
    }
    const double candidateHere = gray[candidate].at<float>(row, column);
    const double otherHere = gray[other].at<float>(row, column);
    const double candidateThere = gray[candidate].at<float>(
        neighbourRow, neighbourColumn);
    const double otherThere = gray[other].at<float>(
        neighbourRow, neighbourColumn);
    const double scale = std::max({1.0, std::abs(candidateHere),
                                   std::abs(otherHere),
                                   std::abs(candidateThere),
                                   std::abs(otherThere)});
    return std::clamp(
        0.5 * (std::abs(candidateHere - otherHere)
               + std::abs(candidateThere - otherThere)) / scale,
        0.0, 2.0);
}

int pyramidLevelCount(cv::Size size, int requested)
{
    int levels = 1;
    while (levels < requested && size.width >= 2 && size.height >= 2) {
        size = {(size.width + 1) / 2, (size.height + 1) / 2};
        ++levels;
    }
    return levels;
}

std::vector<cv::Mat> gaussianPyramid(const cv::Mat &base, int levels)
{
    std::vector<cv::Mat> result{base};
    for (int level = 1; level < levels; ++level) {
        cv::Mat down;
        cv::pyrDown(result.back(), down);
        result.push_back(std::move(down));
    }
    return result;
}

std::vector<cv::Mat> laplacianPyramid(const cv::Mat &base, int levels)
{
    const std::vector<cv::Mat> gaussian = gaussianPyramid(base, levels);
    std::vector<cv::Mat> result(levels);
    for (int level = 0; level < levels - 1; ++level) {
        cv::Mat expanded;
        cv::pyrUp(gaussian[level + 1], expanded, gaussian[level].size());
        result[level] = gaussian[level] - expanded;
    }
    result.back() = gaussian.back();
    return result;
}

cv::Mat multibandBlend(const std::vector<OrthophotoBlendLayer> &layers,
                       const cv::Mat &labels, const cv::Mat &validity,
                       int requestedLevels)
{
    const int channels = layers.front().image.channels();
    const int levels = pyramidLevelCount(labels.size(), requestedLevels);
    std::vector<std::vector<cv::Mat>> layerPyramids;
    std::vector<std::vector<cv::Mat>> maskPyramids;
    std::vector<std::vector<cv::Mat>> availabilityPyramids;
    layerPyramids.reserve(layers.size());
    maskPyramids.reserve(layers.size());
    availabilityPyramids.reserve(layers.size());
    for (int layerIndex = 0;
         layerIndex < static_cast<int>(layers.size()); ++layerIndex) {
        cv::Mat floatImage;
        layers[layerIndex].image.convertTo(floatImage, CV_32F);
        layerPyramids.push_back(laplacianPyramid(floatImage, levels));
        cv::Mat hardMask;
        cv::compare(labels, layerIndex, hardMask, cv::CMP_EQ);
        hardMask.convertTo(hardMask, CV_32F, 1.0 / 255.0);
        maskPyramids.push_back(gaussianPyramid(hardMask, levels));
        cv::Mat available = layers[layerIndex].quality > 0.0f;
        available.convertTo(available, CV_32F, 1.0 / 255.0);
        availabilityPyramids.push_back(
            gaussianPyramid(available, levels));
    }

    std::vector<cv::Mat> blendedPyramid(levels);
    for (int level = 0; level < levels; ++level) {
        const cv::Size size = layerPyramids.front()[level].size();
        cv::Mat denominator = cv::Mat::zeros(size, CV_32F);
        std::vector<cv::Mat> accumulated(channels);
        for (cv::Mat &channel : accumulated) {
            channel = cv::Mat::zeros(size, CV_32F);
        }
        for (int layerIndex = 0;
             layerIndex < static_cast<int>(layers.size()); ++layerIndex) {
            const cv::Mat weight = maskPyramids[layerIndex][level].mul(
                availabilityPyramids[layerIndex][level]);
            std::vector<cv::Mat> imageChannels;
            cv::split(layerPyramids[layerIndex][level], imageChannels);
            for (int channel = 0; channel < channels; ++channel) {
                accumulated[channel] += imageChannels[channel].mul(weight);
            }
            denominator += weight;
        }
        cv::Mat usable = denominator > 1e-8f;
        denominator.setTo(1.0f, usable == 0);
        for (cv::Mat &channel : accumulated) {
            cv::divide(channel, denominator, channel);
            channel.setTo(0.0f, usable == 0);
        }
        cv::merge(accumulated, blendedPyramid[level]);
    }

    cv::Mat blended = blendedPyramid.back();
    for (int level = levels - 2; level >= 0; --level) {
        cv::Mat expanded;
        cv::pyrUp(blended, expanded, blendedPyramid[level].size());
        blended = expanded + blendedPyramid[level];
    }
    // cv::Mat::setTo only accepts a four-element scalar, while a physical
    // multispectral capture can contain more channels. Masked copy works for
    // arbitrary channel counts and leaves invalid spectral pixels at zero.
    cv::Mat masked = cv::Mat::zeros(blended.size(), blended.type());
    blended.copyTo(masked, validity);
    blended = std::move(masked);
    cv::Mat output;
    blended.convertTo(output, layers.front().image.depth());
    return output;
}

void buildSeamProducts(const std::vector<OrthophotoBlendLayer> &layers,
                       const cv::Mat &labels,
                       OrthophotoSeamBlendResult *result)
{
    result->seamMask = cv::Mat::zeros(labels.size(), CV_8U);
    result->primarySourceImageIndex = cv::Mat(
        labels.size(), CV_32S, cv::Scalar(-1));
    constexpr int rowOffsets[] = {-1, 1, 0, 0};
    constexpr int columnOffsets[] = {0, 0, -1, 1};
    for (int row = 0; row < labels.rows; ++row) {
        for (int column = 0; column < labels.cols; ++column) {
            const int label = labels.at<int>(row, column);
            if (label < 0) {
                continue;
            }
            result->primarySourceImageIndex.at<int>(row, column) =
                layers[label].imageIndex;
            bool seam = false;
            for (int direction = 0; direction < 4; ++direction) {
                const int neighbourRow = row + rowOffsets[direction];
                const int neighbourColumn = column + columnOffsets[direction];
                if (neighbourRow >= 0 && neighbourColumn >= 0
                    && neighbourRow < labels.rows
                    && neighbourColumn < labels.cols) {
                    const int other = labels.at<int>(neighbourRow,
                                                     neighbourColumn);
                    seam = seam || (other >= 0 && other != label);
                }
            }
            if (seam) {
                result->seamMask.at<uchar>(row, column) = 255;
            }
        }
    }
    result->seamPixelCount = static_cast<size_t>(
        cv::countNonZero(result->seamMask));
}

} // namespace

OrthophotoSeamBlendResult OrthophotoSeamBlender::blend(
    const std::vector<OrthophotoBlendLayer> &layers,
    const OrthophotoSeamBlendOptions &options)
{
    OrthophotoSeamBlendResult result;
    if (!validate(layers, options, &result.message)) {
        return result;
    }
    const int channelCount = layers.front().image.channels();
    if (channelCount != 1 && channelCount != 3 && channelCount != 4) {
        result.message = "Photometric seam optimization supports one, three, or four reference channels.";
        return result;
    }
    const cv::Size size = layers.front().image.size();
    result.validityMask = cv::Mat::zeros(size, CV_8U);
    cv::Mat labels(size, CV_32S, cv::Scalar(-1));
    std::vector<cv::Mat> gray;
    gray.reserve(layers.size());
    for (const OrthophotoBlendLayer &layer : layers) {
        gray.push_back(grayFloat(layer.image));
    }

    for (int row = 0; row < size.height; ++row) {
        for (int column = 0; column < size.width; ++column) {
            int best = -1;
            float bestQuality = 0.0f;
            for (int layerIndex = 0;
                 layerIndex < static_cast<int>(layers.size()); ++layerIndex) {
                const float quality = layers[layerIndex].quality.at<float>(
                    row, column);
                if (quality > bestQuality
                    || (quality == bestQuality && quality > 0.0f
                        && (best < 0 || layers[layerIndex].imageIndex
                                         < layers[best].imageIndex))) {
                    best = layerIndex;
                    bestQuality = quality;
                }
            }
            if (best >= 0) {
                labels.at<int>(row, column) = best;
                result.validityMask.at<uchar>(row, column) = 255;
            }
        }
    }
    if (cv::countNonZero(result.validityMask) == 0) {
        result.message = "No seam layer has positive quality.";
        return result;
    }

    constexpr int rowOffsets[] = {-1, 1, 0, 0};
    constexpr int columnOffsets[] = {0, 0, -1, 1};
    for (int iteration = 0; iteration < options.maximumIterations;
         ++iteration) {
        size_t changes = 0;
        for (int parity = 0; parity < 2; ++parity) {
            for (int row = 0; row < size.height; ++row) {
                for (int column = 0; column < size.width; ++column) {
                    if ((row + column) % 2 != parity
                        || !result.validityMask.at<uchar>(row, column)) {
                        continue;
                    }
                    double maximumQuality = 0.0;
                    for (const OrthophotoBlendLayer &layer : layers) {
                        maximumQuality = std::max(
                            maximumQuality,
                            static_cast<double>(layer.quality.at<float>(
                                row, column)));
                    }
                    int bestLabel = labels.at<int>(row, column);
                    double bestCost = std::numeric_limits<double>::infinity();
                    for (int candidate = 0;
                         candidate < static_cast<int>(layers.size());
                         ++candidate) {
                        const double quality = layers[candidate].quality.at<float>(
                            row, column);
                        if (!(quality > 0.0)) {
                            continue;
                        }
                        double cost = options.qualityCostWeight
                            * -std::log(std::max(1e-9, quality / maximumQuality));
                        for (int direction = 0; direction < 4; ++direction) {
                            const int neighbourRow = row + rowOffsets[direction];
                            const int neighbourColumn = column
                                + columnOffsets[direction];
                            if (neighbourRow < 0 || neighbourColumn < 0
                                || neighbourRow >= size.height
                                || neighbourColumn >= size.width) {
                                continue;
                            }
                            const int other = labels.at<int>(
                                neighbourRow, neighbourColumn);
                            if (other >= 0 && other != candidate) {
                                cost += options.smoothnessCost
                                    * (1.0
                                       + options.photometricSeamCostWeight
                                         * seamMismatch(
                                             gray, layers, candidate, other,
                                             row, column, neighbourRow,
                                             neighbourColumn));
                            }
                        }
                        if (cost < bestCost - 1e-12
                            || (std::abs(cost - bestCost) <= 1e-12
                                && layers[candidate].imageIndex
                                   < layers[bestLabel].imageIndex)) {
                            bestCost = cost;
                            bestLabel = candidate;
                        }
                    }
                    int &label = labels.at<int>(row, column);
                    if (bestLabel != label) {
                        label = bestLabel;
                        ++changes;
                    }
                }
            }
        }
        ++result.optimizationIterations;
        result.labelChangeCount += changes;
        if (changes == 0) {
            break;
        }
    }

    buildSeamProducts(layers, labels, &result);
    result.image = multibandBlend(
        layers, labels, result.validityMask, options.multibandLevels);
    result.success = !result.image.empty();
    result.message = result.success
        ? "Orthophoto seams optimized and blended across frequency bands."
        : "Orthophoto multiband blending failed.";
    return result;
}

OrthophotoSeamBlendResult OrthophotoSeamBlender::blendWithPrimarySources(
    const std::vector<OrthophotoBlendLayer> &layers,
    const cv::Mat &primarySourceImageIndex,
    int multibandLevels)
{
    OrthophotoSeamBlendResult result;
    OrthophotoSeamBlendOptions validationOptions;
    validationOptions.multibandLevels = multibandLevels;
    if (!validate(layers, validationOptions, &result.message)
        || primarySourceImageIndex.type() != CV_32SC1
        || primarySourceImageIndex.size() != layers.front().image.size()) {
        if (result.message.empty()) {
            result.message = "Fixed-label blending requires a matching CV_32SC1 source map.";
        }
        return result;
    }

    std::map<int, int> layerForImageIndex;
    for (int index = 0; index < static_cast<int>(layers.size()); ++index) {
        if (!layerForImageIndex.emplace(layers[index].imageIndex, index).second) {
            result.message = "Fixed-label blend image indices must be unique.";
            return result;
        }
    }

    cv::Mat labels(primarySourceImageIndex.size(), CV_32S, cv::Scalar(-1));
    result.validityMask = cv::Mat::zeros(primarySourceImageIndex.size(), CV_8U);
    for (int row = 0; row < labels.rows; ++row) {
        for (int column = 0; column < labels.cols; ++column) {
            const int requestedImageIndex =
                primarySourceImageIndex.at<int>(row, column);
            int selected = -1;
            const auto requested = layerForImageIndex.find(requestedImageIndex);
            if (requested != layerForImageIndex.end()
                && layers[requested->second].quality.at<float>(row, column)
                       > 0.0f) {
                selected = requested->second;
            } else if (requestedImageIndex >= 0) {
                float bestQuality = 0.0f;
                for (int index = 0;
                     index < static_cast<int>(layers.size()); ++index) {
                    const float quality = layers[index].quality.at<float>(
                        row, column);
                    if (quality > bestQuality
                        || (quality == bestQuality && quality > 0.0f
                            && (selected < 0
                                || layers[index].imageIndex
                                   < layers[selected].imageIndex))) {
                        selected = index;
                        bestQuality = quality;
                    }
                }
                if (selected >= 0) {
                    ++result.unavailableSourceFallbackCount;
                }
            }
            if (selected >= 0) {
                labels.at<int>(row, column) = selected;
                result.validityMask.at<uchar>(row, column) = 255;
            }
        }
    }
    if (cv::countNonZero(result.validityMask) == 0) {
        result.message = "No fixed source label has usable layer coverage.";
        return result;
    }

    buildSeamProducts(layers, labels, &result);
    result.image = multibandBlend(
        layers, labels, result.validityMask, multibandLevels);
    result.success = !result.image.empty();
    result.message = result.success
        ? "Orthophoto layers blended with shared reference-band source labels."
        : "Fixed-label orthophoto blending failed.";
    return result;
}

} // namespace kestrel
