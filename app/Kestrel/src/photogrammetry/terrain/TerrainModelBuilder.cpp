#include "photogrammetry/terrain/TerrainModelBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace kestrel {

namespace {

void setError(std::string *errorMessage, const std::string &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

} // namespace

bool TerrainModelBuilder::rasterizeDsm(
    const std::vector<TerrainPoint> &points,
    const DsmGridDefinition &definition,
    TerrainGrid *terrain,
    cv::Mat *sampleCounts,
    std::string *errorMessage)
{
    if (!terrain || !sampleCounts) {
        setError(errorMessage, "Terrain and sample-count outputs are required.");
        return false;
    }
    if (definition.size.width <= 0 || definition.size.height <= 0
        || !std::isfinite(definition.origin.x)
        || !std::isfinite(definition.origin.y)
        || !std::isfinite(definition.spacing[0])
        || !std::isfinite(definition.spacing[1])
        || std::abs(definition.spacing[0]) < 1e-12
        || std::abs(definition.spacing[1]) < 1e-12
        || definition.minimumSamplesPerCell <= 0) {
        setError(errorMessage, "The DSM grid definition is invalid.");
        return false;
    }

    terrain->origin = definition.origin;
    terrain->spacing = definition.spacing;
    terrain->elevation = cv::Mat(
        definition.size, CV_32FC1,
        cv::Scalar(std::numeric_limits<float>::quiet_NaN()));
    terrain->validityMask = cv::Mat::zeros(definition.size, CV_8U);
    *sampleCounts = cv::Mat::zeros(definition.size, CV_32SC1);

    for (const TerrainPoint &point : points) {
        if (!std::isfinite(point.position.x) || !std::isfinite(point.position.y)
            || !std::isfinite(point.position.z)) {
            continue;
        }
        const long long column = std::llround(
            (point.position.x - definition.origin.x) / definition.spacing[0]);
        const long long row = std::llround(
            (point.position.y - definition.origin.y) / definition.spacing[1]);
        if (column < 0 || row < 0 || column >= definition.size.width
            || row >= definition.size.height) {
            continue;
        }

        int &count = sampleCounts->at<int>(static_cast<int>(row),
                                           static_cast<int>(column));
        float &height = terrain->elevation.at<float>(static_cast<int>(row),
                                                     static_cast<int>(column));
        ++count;
        if (!std::isfinite(height) || point.position.z > height) {
            height = static_cast<float>(point.position.z);
        }
    }

    for (int row = 0; row < definition.size.height; ++row) {
        float *heightRow = terrain->elevation.ptr<float>(row);
        uchar *validityRow = terrain->validityMask.ptr<uchar>(row);
        const int *countRow = sampleCounts->ptr<int>(row);
        for (int column = 0; column < definition.size.width; ++column) {
            if (countRow[column] >= definition.minimumSamplesPerCell) {
                validityRow[column] = 255;
            } else {
                heightRow[column] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool TerrainModelBuilder::fillBoundedHoles(TerrainGrid *terrain,
                                           int maximumHoleCells,
                                           cv::Mat *filledMask,
                                           std::string *errorMessage)
{
    if (!terrain || terrain->elevation.empty()
        || terrain->elevation.type() != CV_32FC1 || maximumHoleCells <= 0) {
        setError(errorMessage, "A float DSM and positive hole-size limit are required.");
        return false;
    }
    if (!terrain->validityMask.empty()
        && (terrain->validityMask.type() != CV_8UC1
            || terrain->validityMask.size() != terrain->elevation.size())) {
        setError(errorMessage, "The DSM validity mask has invalid type or dimensions.");
        return false;
    }
    if (terrain->validityMask.empty()) {
        terrain->validityMask = cv::Mat::zeros(terrain->elevation.size(), CV_8U);
        for (int row = 0; row < terrain->elevation.rows; ++row) {
            const float *heights = terrain->elevation.ptr<float>(row);
            uchar *validity = terrain->validityMask.ptr<uchar>(row);
            for (int column = 0; column < terrain->elevation.cols; ++column) {
                validity[column] = std::isfinite(heights[column]) ? 255 : 0;
            }
        }
    }
    cv::Mat synthesized = cv::Mat::zeros(terrain->elevation.size(), CV_8U);
    cv::Mat visited = cv::Mat::zeros(terrain->elevation.size(), CV_8U);
    constexpr int rowOffsets[] = {-1, 1, 0, 0};
    constexpr int columnOffsets[] = {0, 0, -1, 1};

    for (int startRow = 0; startRow < terrain->elevation.rows; ++startRow) {
        for (int startColumn = 0; startColumn < terrain->elevation.cols;
             ++startColumn) {
            if (terrain->validityMask.at<uchar>(startRow, startColumn) != 0
                || visited.at<uchar>(startRow, startColumn) != 0) {
                continue;
            }

            std::vector<cv::Point> component;
            std::queue<cv::Point> pending;
            pending.emplace(startColumn, startRow);
            visited.at<uchar>(startRow, startColumn) = 255;
            bool touchesBorder = false;
            while (!pending.empty()) {
                const cv::Point cell = pending.front();
                pending.pop();
                component.push_back(cell);
                touchesBorder = touchesBorder || cell.x == 0 || cell.y == 0
                    || cell.x == terrain->elevation.cols - 1
                    || cell.y == terrain->elevation.rows - 1;
                for (int direction = 0; direction < 4; ++direction) {
                    const int row = cell.y + rowOffsets[direction];
                    const int column = cell.x + columnOffsets[direction];
                    if (row < 0 || column < 0 || row >= terrain->elevation.rows
                        || column >= terrain->elevation.cols
                        || terrain->validityMask.at<uchar>(row, column) != 0
                        || visited.at<uchar>(row, column) != 0) {
                        continue;
                    }
                    visited.at<uchar>(row, column) = 255;
                    pending.emplace(column, row);
                }
            }
            if (touchesBorder
                || component.size() > static_cast<size_t>(maximumHoleCells)) {
                continue;
            }

            double boundarySum = 0.0;
            int boundaryCount = 0;
            for (const cv::Point &cell : component) {
                for (int direction = 0; direction < 4; ++direction) {
                    const int row = cell.y + rowOffsets[direction];
                    const int column = cell.x + columnOffsets[direction];
                    if (row < 0 || column < 0 || row >= terrain->elevation.rows
                        || column >= terrain->elevation.cols
                        || terrain->validityMask.at<uchar>(row, column) == 0) {
                        continue;
                    }
                    const float height = terrain->elevation.at<float>(row, column);
                    if (std::isfinite(height)) {
                        boundarySum += height;
                        ++boundaryCount;
                    }
                }
            }
            if (boundaryCount == 0) {
                continue;
            }

            const float initialHeight = static_cast<float>(boundarySum / boundaryCount);
            cv::Mat componentMask = cv::Mat::zeros(terrain->elevation.size(), CV_8U);
            for (const cv::Point &cell : component) {
                terrain->elevation.at<float>(cell.y, cell.x) = initialHeight;
                componentMask.at<uchar>(cell.y, cell.x) = 255;
            }

            std::vector<float> nextHeights(component.size(), initialHeight);
            for (int iteration = 0; iteration < 100; ++iteration) {
                double maximumChange = 0.0;
                for (size_t index = 0; index < component.size(); ++index) {
                    const cv::Point cell = component[index];
                    double sum = 0.0;
                    int count = 0;
                    for (int direction = 0; direction < 4; ++direction) {
                        const int row = cell.y + rowOffsets[direction];
                        const int column = cell.x + columnOffsets[direction];
                        if (row < 0 || column < 0 || row >= terrain->elevation.rows
                            || column >= terrain->elevation.cols) {
                            continue;
                        }
                        const bool usable = terrain->validityMask.at<uchar>(row, column)
                                                != 0
                                            || componentMask.at<uchar>(row, column) != 0;
                        const float height = terrain->elevation.at<float>(row, column);
                        if (usable && std::isfinite(height)) {
                            sum += height;
                            ++count;
                        }
                    }
                    if (count > 0) {
                        nextHeights[index] = static_cast<float>(sum / count);
                        maximumChange = std::max(
                            maximumChange,
                            std::abs(static_cast<double>(nextHeights[index])
                                     - terrain->elevation.at<float>(cell.y, cell.x)));
                    }
                }
                for (size_t index = 0; index < component.size(); ++index) {
                    const cv::Point cell = component[index];
                    terrain->elevation.at<float>(cell.y, cell.x) = nextHeights[index];
                }
                if (maximumChange < 1e-4) {
                    break;
                }
            }
            for (const cv::Point &cell : component) {
                terrain->validityMask.at<uchar>(cell.y, cell.x) = 255;
                synthesized.at<uchar>(cell.y, cell.x) = 255;
            }
        }
    }

    if (filledMask) {
        *filledMask = synthesized;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace kestrel
