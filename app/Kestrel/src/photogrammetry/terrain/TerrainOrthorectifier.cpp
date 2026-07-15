#include "photogrammetry/terrain/TerrainOrthorectifier.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace kestrel {

namespace {

bool finiteMatrix(const cv::Matx33d &matrix)
{
    for (double value : matrix.val) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

void setError(std::string *errorMessage, const std::string &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

} // namespace

bool TerrainGrid::sample(double worldX, double worldY, double *height) const
{
    if (!height || elevation.empty() || elevation.type() != CV_32FC1
        || !std::isfinite(worldX) || !std::isfinite(worldY)
        || !std::isfinite(spacing[0]) || !std::isfinite(spacing[1])
        || std::abs(spacing[0]) < 1e-12 || std::abs(spacing[1]) < 1e-12) {
        return false;
    }

    const double gridX = (worldX - origin.x) / spacing[0];
    const double gridY = (worldY - origin.y) / spacing[1];
    if (gridX < 0.0 || gridY < 0.0
        || gridX > elevation.cols - 1.0 || gridY > elevation.rows - 1.0) {
        return false;
    }

    const int x0 = std::clamp(static_cast<int>(std::floor(gridX)), 0,
                              elevation.cols - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(gridY)), 0,
                              elevation.rows - 1);
    const int x1 = std::min(x0 + 1, elevation.cols - 1);
    const int y1 = std::min(y0 + 1, elevation.rows - 1);
    const float h00 = elevation.at<float>(y0, x0);
    const float h10 = elevation.at<float>(y0, x1);
    const float h01 = elevation.at<float>(y1, x0);
    const float h11 = elevation.at<float>(y1, x1);
    if (!validityMask.empty()
        && (validityMask.type() != CV_8UC1
            || validityMask.size() != elevation.size())) {
        return false;
    }

    const double fractionX = gridX - x0;
    const double fractionY = gridY - y0;
    const double weights[] = {
        (1.0 - fractionX) * (1.0 - fractionY),
        fractionX * (1.0 - fractionY),
        (1.0 - fractionX) * fractionY,
        fractionX * fractionY};
    const float samples[] = {h00, h10, h01, h11};
    const cv::Point cells[] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    double value = 0.0;
    for (int index = 0; index < 4; ++index) {
        if (weights[index] <= 1e-12) {
            continue;
        }
        if (!std::isfinite(samples[index])
            || (!validityMask.empty()
                && validityMask.at<uchar>(cells[index]) == 0)) {
            return false;
        }
        value += weights[index] * samples[index];
    }
    *height = value;
    return std::isfinite(*height);
}

bool PinholeCamera::project(const cv::Point3d &worldPoint,
                            cv::Point2d *imagePoint) const
{
    if (!imagePoint || !finiteMatrix(intrinsic)
        || !finiteMatrix(worldToCameraRotation)) {
        return false;
    }
    for (int index = 0; index < 3; ++index) {
        if (!std::isfinite(worldToCameraTranslation[index])) {
            return false;
        }
    }
    for (int index = 0; index < 5; ++index) {
        if (!std::isfinite(distortion[index])) {
            return false;
        }
    }

    const cv::Vec3d cameraPoint = worldToCameraRotation
        * cv::Vec3d(worldPoint.x, worldPoint.y, worldPoint.z)
        + worldToCameraTranslation;
    if (!std::isfinite(cameraPoint[2]) || cameraPoint[2] <= 1e-9) {
        return false;
    }

    const double x = cameraPoint[0] / cameraPoint[2];
    const double y = cameraPoint[1] / cameraPoint[2];
    const double radiusSquared = x * x + y * y;
    const double radial = 1.0 + distortion[0] * radiusSquared
                          + distortion[1] * radiusSquared * radiusSquared
                          + distortion[4] * radiusSquared * radiusSquared
                            * radiusSquared;
    const double distortedX = x * radial + 2.0 * distortion[2] * x * y
                              + distortion[3] * (radiusSquared + 2.0 * x * x);
    const double distortedY = y * radial + distortion[2]
                              * (radiusSquared + 2.0 * y * y)
                              + 2.0 * distortion[3] * x * y;

    imagePoint->x = intrinsic(0, 0) * distortedX
                    + intrinsic(0, 1) * distortedY + intrinsic(0, 2);
    imagePoint->y = intrinsic(1, 0) * distortedX
                    + intrinsic(1, 1) * distortedY + intrinsic(1, 2);
    return std::isfinite(imagePoint->x) && std::isfinite(imagePoint->y);
}

bool TerrainOrthorectifier::createOutputGrid(const TerrainGrid &terrain,
                                             double groundSampleDistance,
                                             double padding,
                                             OrthophotoGrid *outputGrid,
                                             std::string *errorMessage)
{
    if (!outputGrid) {
        setError(errorMessage, "An orthophoto output grid is required.");
        return false;
    }
    if (terrain.elevation.empty() || terrain.elevation.type() != CV_32FC1
        || !std::isfinite(terrain.origin.x) || !std::isfinite(terrain.origin.y)
        || !std::isfinite(terrain.spacing[0])
        || !std::isfinite(terrain.spacing[1])
        || std::abs(terrain.spacing[0]) < 1e-12
        || std::abs(terrain.spacing[1]) < 1e-12) {
        setError(errorMessage, "The terrain grid has invalid geometry.");
        return false;
    }
    if (!std::isfinite(groundSampleDistance) || groundSampleDistance <= 0.0
        || !std::isfinite(padding) || padding < 0.0) {
        setError(errorMessage,
                 "Ground sample distance must be positive and padding non-negative.");
        return false;
    }

    const double oppositeX = terrain.origin.x
        + (terrain.elevation.cols - 1) * terrain.spacing[0];
    const double oppositeY = terrain.origin.y
        + (terrain.elevation.rows - 1) * terrain.spacing[1];
    const double minimumX = std::min(terrain.origin.x, oppositeX) - padding;
    const double maximumX = std::max(terrain.origin.x, oppositeX) + padding;
    const double minimumY = std::min(terrain.origin.y, oppositeY) - padding;
    const double maximumY = std::max(terrain.origin.y, oppositeY) + padding;
    const double width = std::ceil((maximumX - minimumX)
                                   / groundSampleDistance) + 1.0;
    const double height = std::ceil((maximumY - minimumY)
                                    / groundSampleDistance) + 1.0;
    if (!std::isfinite(width) || !std::isfinite(height)
        || width > std::numeric_limits<int>::max()
        || height > std::numeric_limits<int>::max()) {
        setError(errorMessage, "The requested orthophoto grid is too large.");
        return false;
    }

    outputGrid->size = cv::Size(static_cast<int>(width),
                                static_cast<int>(height));
    outputGrid->origin = cv::Point2d(minimumX, maximumY);
    outputGrid->pixelSize = cv::Vec2d(groundSampleDistance,
                                      -groundSampleDistance);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool TerrainOrthorectifier::orthorectify(const cv::Mat &source,
                                          const PinholeCamera &camera,
                                          const TerrainGrid &terrain,
                                          const OrthophotoGrid &outputGrid,
                                          cv::Mat *orthophoto,
                                          cv::Mat *validityMask,
                                          std::string *errorMessage,
                                          int interpolation)
{
    if (!orthophoto || !validityMask) {
        setError(errorMessage, "Output image and validity mask are required.");
        return false;
    }
    if (source.empty()) {
        setError(errorMessage, "The source image is empty.");
        return false;
    }
    if (terrain.elevation.empty() || terrain.elevation.type() != CV_32FC1) {
        setError(errorMessage, "The terrain grid must be a CV_32FC1 elevation raster.");
        return false;
    }
    if (outputGrid.size.width <= 0 || outputGrid.size.height <= 0
        || !std::isfinite(outputGrid.pixelSize[0])
        || !std::isfinite(outputGrid.pixelSize[1])
        || std::abs(outputGrid.pixelSize[0]) < 1e-12
        || std::abs(outputGrid.pixelSize[1]) < 1e-12) {
        setError(errorMessage, "The orthophoto grid has invalid size or pixel spacing.");
        return false;
    }

    cv::Mat mapX(outputGrid.size, CV_32F, cv::Scalar(-1.0f));
    cv::Mat mapY(outputGrid.size, CV_32F, cv::Scalar(-1.0f));
    *validityMask = cv::Mat::zeros(outputGrid.size, CV_8U);
    for (int row = 0; row < outputGrid.size.height; ++row) {
        float *mapXRow = mapX.ptr<float>(row);
        float *mapYRow = mapY.ptr<float>(row);
        uchar *maskRow = validityMask->ptr<uchar>(row);
        const double worldY = outputGrid.origin.y + row * outputGrid.pixelSize[1];
        for (int column = 0; column < outputGrid.size.width; ++column) {
            const double worldX = outputGrid.origin.x
                                  + column * outputGrid.pixelSize[0];
            double elevation = 0.0;
            cv::Point2d imagePoint;
            if (!terrain.sample(worldX, worldY, &elevation)
                || !camera.project(cv::Point3d(worldX, worldY, elevation),
                                   &imagePoint)
                || imagePoint.x < 0.0 || imagePoint.y < 0.0
                || imagePoint.x > source.cols - 1.0
                || imagePoint.y > source.rows - 1.0) {
                continue;
            }
            mapXRow[column] = static_cast<float>(imagePoint.x);
            mapYRow[column] = static_cast<float>(imagePoint.y);
            maskRow[column] = 255;
        }
    }

    cv::remap(source, *orthophoto, mapX, mapY, interpolation,
              cv::BORDER_CONSTANT, cv::Scalar::all(0));
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace kestrel
