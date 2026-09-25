#include "photogrammetry/sfm/BundleAdjustment.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace kestrel {

namespace {

struct Observation
{
    int cameraIndex = -1;
    int pointIndex = -1;
    int trackId = -1;
    cv::Point2d measured;
    bool active = true;
};

struct LinearizedObservation
{
    cv::Vec2d residual;
    cv::Matx<double, 2, 6> cameraJacobian;
    cv::Matx<double, 2, 3> pointJacobian;
    cv::Matx<double, 2, 9> calibrationJacobian;
    bool valid = false;
};

struct CrossBlock
{
    int variableCamera = -1;
    cv::Mat value;
};

struct PointNormalBlock
{
    cv::Mat hessian = cv::Mat::zeros(3, 3, CV_64F);
    cv::Mat gradient = cv::Mat::zeros(3, 1, CV_64F);
    cv::Mat inverse;
    std::vector<CrossBlock> crosses;
};

struct ComponentProblem
{
    int componentId = -1;
    std::vector<int> cameraIndices;
    std::vector<int> pointIndices;
    std::vector<int> variableCameraIndices;
    std::map<int, int> variableCameraLookup;
    std::map<int, int> localPointLookup;
    std::vector<Observation> observations;
    struct PositionPrior {
        int cameraIndex = -1;
        cv::Point3d positionMetres;
        double standardDeviationMetres = 1.0;
    };
    std::vector<PositionPrior> positionPriors;
    struct CalibrationGroup {
        std::vector<int> imageIndices;
        CameraCalibration prior;
        int observationCount = 0;
        bool enabled = false;
    };
    std::vector<CalibrationGroup> calibrationGroups;
};

bool finitePoint(const cv::Vec3d &point)
{
    return std::isfinite(point[0]) && std::isfinite(point[1])
           && std::isfinite(point[2]);
}

LinearizedObservation linearize(
    const SparseCameraPose &camera, const SparsePoint &point,
    const CameraCalibration &calibration, const cv::Point2d &measured)
{
    LinearizedObservation result;
    const cv::Vec3d world(point.position.x, point.position.y,
                          point.position.z);
    const cv::Vec3d cameraPoint = camera.worldToCameraRotation * world
                                  + camera.worldToCameraTranslation;
    if (!(cameraPoint[2] > 1e-9) || !finitePoint(cameraPoint)) {
        return result;
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
    const double radialX = 2.0 * k1 * x + 4.0 * k2 * r2 * x
                           + 6.0 * k3 * r4 * x;
    const double radialY = 2.0 * k1 * y + 4.0 * k2 * r2 * y
                           + 6.0 * k3 * r4 * y;
    const double distortedX = x * radial + 2.0 * p1 * x * y
                              + p2 * (r2 + 2.0 * x * x);
    const double distortedY = y * radial + p1 * (r2 + 2.0 * y * y)
                              + 2.0 * p2 * x * y;
    const double fx = calibration.intrinsic(0, 0);
    const double skew = calibration.intrinsic(0, 1);
    const double fy = calibration.intrinsic(1, 1);
    const cv::Point2d projected(
        fx * distortedX + skew * distortedY + calibration.intrinsic(0, 2),
        fy * distortedY + calibration.intrinsic(1, 2));
    result.residual = {projected.x - measured.x,
                       projected.y - measured.y};

    // Calibration parameter order: fx, fy, cx, cy, k1, k2, p1, p2, k3.
    result.calibrationJacobian(0, 0) = distortedX;
    result.calibrationJacobian(1, 1) = distortedY;
    result.calibrationJacobian(0, 2) = 1.0;
    result.calibrationJacobian(1, 3) = 1.0;
    const double radialPixelX = fx * x + skew * y;
    const double radialPixelY = fy * y;
    result.calibrationJacobian(0, 4) = radialPixelX * r2;
    result.calibrationJacobian(1, 4) = radialPixelY * r2;
    result.calibrationJacobian(0, 5) = radialPixelX * r4;
    result.calibrationJacobian(1, 5) = radialPixelY * r4;
    result.calibrationJacobian(0, 6) =
        fx * (2.0 * x * y) + skew * (r2 + 2.0 * y * y);
    result.calibrationJacobian(1, 6) =
        fy * (r2 + 2.0 * y * y);
    result.calibrationJacobian(0, 7) =
        fx * (r2 + 2.0 * x * x) + skew * (2.0 * x * y);
    result.calibrationJacobian(1, 7) = fy * (2.0 * x * y);
    result.calibrationJacobian(0, 8) = radialPixelX * r6;
    result.calibrationJacobian(1, 8) = radialPixelY * r6;

    const double dxdX = radial + x * radialX + 2.0 * p1 * y
                        + 6.0 * p2 * x;
    const double dxdY = x * radialY + 2.0 * p1 * x + 2.0 * p2 * y;
    const double dydX = y * radialX + 2.0 * p1 * x + 2.0 * p2 * y;
    const double dydY = radial + y * radialY + 6.0 * p1 * y
                        + 2.0 * p2 * x;
    const cv::Matx22d pixelFromNormalized(
        fx * dxdX + skew * dydX, fx * dxdY + skew * dydY,
        fy * dydX, fy * dydY);
    const double inverseZ = 1.0 / cameraPoint[2];
    const cv::Matx<double, 2, 3> normalizedFromCamera(
        inverseZ, 0.0, -x * inverseZ,
        0.0, inverseZ, -y * inverseZ);
    const cv::Matx<double, 2, 3> pixelFromCamera =
        pixelFromNormalized * normalizedFromCamera;
    result.pointJacobian = pixelFromCamera
                           * camera.worldToCameraRotation;

    const cv::Matx33d negativeSkew(
        0.0, cameraPoint[2], -cameraPoint[1],
        -cameraPoint[2], 0.0, cameraPoint[0],
        cameraPoint[1], -cameraPoint[0], 0.0);
    const cv::Matx<double, 2, 3> rotationJacobian =
        pixelFromCamera * negativeSkew;
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.cameraJacobian(row, column) =
                rotationJacobian(row, column);
            result.cameraJacobian(row, column + 3) =
                pixelFromCamera(row, column);
        }
    }
    result.valid = std::isfinite(result.residual[0])
                   && std::isfinite(result.residual[1]);
    return result;
}

double robustCost(double error, double threshold)
{
    return error <= threshold
        ? 0.5 * error * error
        : threshold * (error - 0.5 * threshold);
}

struct Evaluation
{
    double cost = 0.0;
    double rms = 0.0;
    int count = 0;
    double gpsRmsMetres = 0.0;
    int gpsCount = 0;
};

cv::Point3d cameraCentre(const SparseCameraPose &camera)
{
    const cv::Vec3d centre = -camera.worldToCameraRotation.t()
                             * camera.worldToCameraTranslation;
    return {centre[0], centre[1], centre[2]};
}

using CalibrationVector = cv::Vec<double, 9>;

CalibrationVector calibrationParameters(const CameraCalibration &calibration)
{
    return {calibration.intrinsic(0, 0), calibration.intrinsic(1, 1),
            calibration.intrinsic(0, 2), calibration.intrinsic(1, 2),
            calibration.distortion[0], calibration.distortion[1],
            calibration.distortion[2], calibration.distortion[3],
            calibration.distortion[4]};
}

void setCalibrationParameters(const CalibrationVector &parameters,
                              CameraCalibration *calibration)
{
    calibration->intrinsic(0, 0) = parameters[0];
    calibration->intrinsic(1, 1) = parameters[1];
    calibration->intrinsic(0, 2) = parameters[2];
    calibration->intrinsic(1, 2) = parameters[3];
    for (int index = 0; index < 5; ++index) {
        calibration->distortion[index] = parameters[index + 4];
    }
}

CalibrationVector calibrationPriorSigmas(
    const CameraCalibration &calibration)
{
    const double width = std::max(1, calibration.imageSize.width);
    const double height = std::max(1, calibration.imageSize.height);
    const double fx = calibration.intrinsic(0, 0);
    const double fy = calibration.intrinsic(1, 1);
    if (calibration.quality == CalibrationQuality::MetadataCalibrated) {
        return {std::max(1.0, 0.01 * fx), std::max(1.0, 0.01 * fy),
                std::max(1.5, 0.0025 * width),
                std::max(1.5, 0.0025 * height),
                0.01, 0.02, 0.001, 0.001, 0.03};
    }
    return {std::max(2.0, 0.10 * fx), std::max(2.0, 0.10 * fy),
            std::max(5.0, 0.03 * width),
            std::max(5.0, 0.03 * height),
            0.10, 0.20, 0.01, 0.01, 0.30};
}

bool validCalibrationCandidate(const CalibrationVector &parameters,
                               const CameraCalibration &prior)
{
    for (double value : parameters.val) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    const CalibrationVector initial = calibrationParameters(prior);
    const CalibrationVector sigma = calibrationPriorSigmas(prior);
    if (parameters[0] <= 0.0 || parameters[1] <= 0.0
        || parameters[2] < 0.0 || parameters[3] < 0.0
        || parameters[2] > prior.imageSize.width
        || parameters[3] > prior.imageSize.height
        || std::abs(parameters[4]) > 1.5
        || std::abs(parameters[5]) > 1.5
        || std::abs(parameters[6]) > 0.15
        || std::abs(parameters[7]) > 0.15
        || std::abs(parameters[8]) > 1.5) {
        return false;
    }
    for (int index = 0; index < 9; ++index) {
        if (std::abs(parameters[index] - initial[index])
            > 5.0 * sigma[index]) {
            return false;
        }
    }
    return true;
}

bool equivalentCalibration(const CameraCalibration &first,
                           const CameraCalibration &second)
{
    if (first.imageSize != second.imageSize || first.quality != second.quality
        || first.source != second.source) {
        return false;
    }
    const CalibrationVector a = calibrationParameters(first);
    const CalibrationVector b = calibrationParameters(second);
    for (int index = 0; index < 9; ++index) {
        const double scale = std::max({1.0, std::abs(a[index]),
                                       std::abs(b[index])});
        if (std::abs(a[index] - b[index]) > 1e-6 * scale) {
            return false;
        }
    }
    return true;
}

Evaluation evaluate(const ComponentProblem &problem,
                    const SparseInitializationResult &reconstruction,
                    const std::vector<CameraCalibration> &calibrations,
                    const BundleAdjustmentOptions &options)
{
    Evaluation result;
    double squaredError = 0.0;
    for (const Observation &observation : problem.observations) {
        if (!observation.active) {
            continue;
        }
        const SparseCameraPose &camera =
            reconstruction.cameras[observation.cameraIndex];
        const LinearizedObservation linearized = linearize(
            camera, reconstruction.points[observation.pointIndex],
            calibrations[camera.imageIndex], observation.measured);
        if (!linearized.valid) {
            continue;
        }
        const double error = cv::norm(linearized.residual);
        result.cost += robustCost(error, options.huberThresholdPixels);
        squaredError += error * error;
        ++result.count;
    }
    result.rms = result.count > 0
        ? std::sqrt(squaredError / result.count) : 0.0;
    double gpsSquaredError = 0.0;
    for (const ComponentProblem::PositionPrior &prior
         : problem.positionPriors) {
        const cv::Point3d centre = cameraCentre(
            reconstruction.cameras[prior.cameraIndex]);
        const cv::Vec3d residual(
            centre.x - prior.positionMetres.x,
            centre.y - prior.positionMetres.y,
            centre.z - prior.positionMetres.z);
        const double errorMetres = cv::norm(residual);
        const double normalizedError = errorMetres
            / prior.standardDeviationMetres;
        result.cost += robustCost(
            normalizedError,
            options.gpsHuberThresholdStandardDeviations);
        gpsSquaredError += errorMetres * errorMetres;
        ++result.gpsCount;
    }
    result.gpsRmsMetres = result.gpsCount > 0
        ? std::sqrt(gpsSquaredError / result.gpsCount) : 0.0;
    for (const ComponentProblem::CalibrationGroup &group
         : problem.calibrationGroups) {
        if (!group.enabled || group.imageIndices.empty()) {
            continue;
        }
        const CalibrationVector parameters = calibrationParameters(
            calibrations[group.imageIndices.front()]);
        const CalibrationVector prior = calibrationParameters(group.prior);
        const CalibrationVector sigma = calibrationPriorSigmas(group.prior);
        for (int index = 0; index < 9; ++index) {
            const double normalized =
                (parameters[index] - prior[index]) / sigma[index];
            result.cost += 0.5 * normalized * normalized;
        }
    }
    return result;
}

struct BlockSparseCameraSystem
{
    std::vector<std::map<int, cv::Mat>> rows;
};

void addCameraBlock(BlockSparseCameraSystem *system, int row, int column,
                    const cv::Mat &block, double sign = 1.0)
{
    auto existing = system->rows[row].find(column);
    if (existing == system->rows[row].end()) {
        existing = system->rows[row]
                       .emplace(column, cv::Mat::zeros(6, 6, CV_64F))
                       .first;
    }
    existing->second += sign * block;
}

cv::Mat multiply(const BlockSparseCameraSystem &system,
                 const cv::Mat &vector)
{
    cv::Mat result = cv::Mat::zeros(vector.rows, 1, CV_64F);
    for (int row = 0; row < static_cast<int>(system.rows.size()); ++row) {
        cv::Mat output = result.rowRange(row * 6, row * 6 + 6);
        for (const auto &[column, block] : system.rows[row]) {
            output += block * vector.rowRange(column * 6,
                                              column * 6 + 6);
        }
    }
    return result;
}

double dot(const cv::Mat &first, const cv::Mat &second)
{
    return first.dot(second);
}

bool solveBlockPcg(const BlockSparseCameraSystem &system,
                   const cv::Mat &rightHandSide,
                   const BundleAdjustmentOptions &options,
                   cv::Mat *solution, int *iterations)
{
    const int blockCount = static_cast<int>(system.rows.size());
    *solution = cv::Mat::zeros(rightHandSide.rows, 1, CV_64F);
    *iterations = 0;
    if (blockCount == 0) {
        return true;
    }
    std::vector<cv::Mat> inverseDiagonal;
    inverseDiagonal.reserve(blockCount);
    for (int block = 0; block < blockCount; ++block) {
        const auto diagonal = system.rows[block].find(block);
        if (diagonal == system.rows[block].end()) {
            return false;
        }
        cv::Mat inverse;
        if (!cv::invert(diagonal->second, inverse, cv::DECOMP_CHOLESKY)
            && !cv::invert(diagonal->second, inverse, cv::DECOMP_SVD)) {
            return false;
        }
        inverseDiagonal.push_back(std::move(inverse));
    }
    auto precondition = [&inverseDiagonal](const cv::Mat &input) {
        cv::Mat output = cv::Mat::zeros(input.rows, 1, CV_64F);
        for (int block = 0;
             block < static_cast<int>(inverseDiagonal.size()); ++block) {
            cv::Mat value = inverseDiagonal[block]
                * input.rowRange(block * 6, block * 6 + 6);
            value.copyTo(output.rowRange(block * 6, block * 6 + 6));
        }
        return output;
    };

    cv::Mat residual = rightHandSide.clone();
    cv::Mat z = precondition(residual);
    cv::Mat direction = z.clone();
    double residualDotPreconditioned = dot(residual, z);
    if (!std::isfinite(residualDotPreconditioned)
        || residualDotPreconditioned < 0.0) {
        return false;
    }
    const double tolerance = options.linearSolverTolerance
        * std::max(1.0, cv::norm(rightHandSide));
    if (cv::norm(residual) <= tolerance) {
        return true;
    }
    const int maximumIterations = std::max(
        options.maximumLinearSolverIterations, blockCount * 6);
    for (int iteration = 0; iteration < maximumIterations; ++iteration) {
        const cv::Mat product = multiply(system, direction);
        const double curvature = dot(direction, product);
        if (!(curvature > 1e-20) || !std::isfinite(curvature)) {
            return false;
        }
        const double alpha = residualDotPreconditioned / curvature;
        *solution += alpha * direction;
        residual -= alpha * product;
        *iterations = iteration + 1;
        if (cv::norm(residual) <= tolerance) {
            return true;
        }
        z = precondition(residual);
        const double nextResidualDotPreconditioned = dot(residual, z);
        if (!std::isfinite(nextResidualDotPreconditioned)
            || nextResidualDotPreconditioned < 0.0) {
            return false;
        }
        const double beta = nextResidualDotPreconditioned
            / std::max(1e-30, residualDotPreconditioned);
        direction = z + beta * direction;
        residualDotPreconditioned = nextResidualDotPreconditioned;
    }
    return cv::norm(residual) <= tolerance * 10.0;
}

bool buildAndSolveStep(
    const ComponentProblem &problem,
    const SparseInitializationResult &reconstruction,
    const std::vector<CameraCalibration> &calibrations,
    const BundleAdjustmentOptions &options, double damping,
    std::vector<cv::Vec<double, 6>> *cameraDeltas,
    std::vector<cv::Vec3d> *pointDeltas, int *linearSolverIterations)
{
    const int variableCameraCount =
        static_cast<int>(problem.variableCameraIndices.size());
    std::vector<cv::Mat> cameraHessians(
        variableCameraCount, cv::Mat::zeros(6, 6, CV_64F));
    std::vector<cv::Mat> cameraGradients(
        variableCameraCount, cv::Mat::zeros(6, 1, CV_64F));
    std::vector<PointNormalBlock> pointBlocks(problem.pointIndices.size());

    for (const Observation &observation : problem.observations) {
        if (!observation.active) {
            continue;
        }
        const SparseCameraPose &camera =
            reconstruction.cameras[observation.cameraIndex];
        const LinearizedObservation value = linearize(
            camera, reconstruction.points[observation.pointIndex],
            calibrations[camera.imageIndex], observation.measured);
        if (!value.valid) {
            continue;
        }
        const double error = cv::norm(value.residual);
        const double weight = error <= options.huberThresholdPixels
            || error <= 1e-12
            ? 1.0 : options.huberThresholdPixels / error;
        const cv::Mat residual(2, 1, CV_64F,
                               const_cast<double *>(value.residual.val));
        const cv::Mat pointJacobian(2, 3, CV_64F,
            const_cast<double *>(value.pointJacobian.val));
        const int localPoint = problem.localPointLookup.at(
            observation.pointIndex);
        PointNormalBlock &pointBlock = pointBlocks[localPoint];
        pointBlock.hessian += weight * pointJacobian.t() * pointJacobian;
        pointBlock.gradient += weight * pointJacobian.t() * residual;

        const auto variable = problem.variableCameraLookup.find(
            observation.cameraIndex);
        if (variable == problem.variableCameraLookup.end()) {
            continue;
        }
        const cv::Mat cameraJacobian(2, 6, CV_64F,
            const_cast<double *>(value.cameraJacobian.val));
        cameraHessians[variable->second] +=
            weight * cameraJacobian.t() * cameraJacobian;
        cameraGradients[variable->second] +=
            weight * cameraJacobian.t() * residual;
        pointBlock.crosses.push_back({
            variable->second,
            weight * cameraJacobian.t() * pointJacobian});
    }

    for (const ComponentProblem::PositionPrior &prior
         : problem.positionPriors) {
        const auto variable = problem.variableCameraLookup.find(
            prior.cameraIndex);
        if (variable == problem.variableCameraLookup.end()) {
            continue;
        }
        const SparseCameraPose &camera =
            reconstruction.cameras[prior.cameraIndex];
        const cv::Point3d centre = cameraCentre(camera);
        const cv::Vec3d residualMetres(
            centre.x - prior.positionMetres.x,
            centre.y - prior.positionMetres.y,
            centre.z - prior.positionMetres.z);
        const double normalizedNorm = cv::norm(residualMetres)
            / prior.standardDeviationMetres;
        const double robustWeight = normalizedNorm
                <= options.gpsHuberThresholdStandardDeviations
            || normalizedNorm <= 1e-12
            ? 1.0
            : options.gpsHuberThresholdStandardDeviations
                  / normalizedNorm;
        const double inverseSigma = 1.0 / prior.standardDeviationMetres;
        cv::Mat jacobian = cv::Mat::zeros(3, 6, CV_64F);
        // With the left-multiplicative SE(3) update used here, a pure
        // rotation increment leaves the camera centre unchanged. Translation
        // changes it by -R^T * delta_translation.
        const cv::Matx33d centreFromTranslation =
            -camera.worldToCameraRotation.t() * inverseSigma;
        cv::Mat(centreFromTranslation, false).copyTo(
            jacobian.colRange(3, 6));
        const cv::Mat residual = (cv::Mat_<double>(3, 1)
            << residualMetres[0] * inverseSigma,
               residualMetres[1] * inverseSigma,
               residualMetres[2] * inverseSigma);
        cameraHessians[variable->second] +=
            robustWeight * jacobian.t() * jacobian;
        cameraGradients[variable->second] +=
            robustWeight * jacobian.t() * residual;
    }

    for (cv::Mat &hessian : cameraHessians) {
        for (int diagonal = 0; diagonal < 6; ++diagonal) {
            hessian.at<double>(diagonal, diagonal) +=
                damping * (hessian.at<double>(diagonal, diagonal) + 1.0);
        }
    }
    for (PointNormalBlock &block : pointBlocks) {
        for (int diagonal = 0; diagonal < 3; ++diagonal) {
            block.hessian.at<double>(diagonal, diagonal) +=
                damping * (block.hessian.at<double>(diagonal, diagonal) + 1.0);
        }
        if (!cv::invert(block.hessian, block.inverse, cv::DECOMP_CHOLESKY)) {
            if (!cv::invert(block.hessian, block.inverse, cv::DECOMP_SVD)) {
                return false;
            }
        }
    }

    BlockSparseCameraSystem schur;
    schur.rows.resize(variableCameraCount);
    cv::Mat gradient = cv::Mat::zeros(variableCameraCount * 6, 1, CV_64F);
    for (int camera = 0; camera < variableCameraCount; ++camera) {
        addCameraBlock(&schur, camera, camera, cameraHessians[camera]);
        cameraGradients[camera].copyTo(
            gradient.rowRange(camera * 6, camera * 6 + 6));
    }
    for (const PointNormalBlock &point : pointBlocks) {
        for (const CrossBlock &first : point.crosses) {
            const cv::Mat left = first.value * point.inverse;
            gradient.rowRange(first.variableCamera * 6,
                              first.variableCamera * 6 + 6)
                -= left * point.gradient;
            for (const CrossBlock &second : point.crosses) {
                addCameraBlock(&schur, first.variableCamera,
                               second.variableCamera,
                               left * second.value.t(), -1.0);
            }
        }
    }

    cv::Mat stackedCameraDelta;
    if (variableCameraCount > 0) {
        int iterations = 0;
        if (!solveBlockPcg(schur, -gradient, options,
                           &stackedCameraDelta, &iterations)) {
            return false;
        }
        *linearSolverIterations += iterations;
    }
    cameraDeltas->assign(variableCameraCount, {});
    for (int camera = 0; camera < variableCameraCount; ++camera) {
        for (int value = 0; value < 6; ++value) {
            (*cameraDeltas)[camera][value] =
                stackedCameraDelta.at<double>(camera * 6 + value);
        }
    }

    pointDeltas->assign(pointBlocks.size(), {});
    for (int pointIndex = 0;
         pointIndex < static_cast<int>(pointBlocks.size()); ++pointIndex) {
        const PointNormalBlock &point = pointBlocks[pointIndex];
        cv::Mat rightHandSide = point.gradient.clone();
        for (const CrossBlock &cross : point.crosses) {
            const cv::Vec<double, 6> &delta =
                (*cameraDeltas)[cross.variableCamera];
            const cv::Mat deltaMatrix(6, 1, CV_64F,
                                      const_cast<double *>(delta.val));
            rightHandSide += cross.value.t() * deltaMatrix;
        }
        const cv::Mat delta = -point.inverse * rightHandSide;
        (*pointDeltas)[pointIndex] = {
            delta.at<double>(0), delta.at<double>(1), delta.at<double>(2)};
    }
    return true;
}

bool buildCalibrationStep(
    const ComponentProblem &problem,
    const SparseInitializationResult &reconstruction,
    const std::vector<CameraCalibration> &calibrations,
    const BundleAdjustmentOptions &options, double damping,
    std::vector<CalibrationVector> *deltas)
{
    deltas->assign(problem.calibrationGroups.size(), CalibrationVector{});
    for (int groupIndex = 0;
         groupIndex < static_cast<int>(problem.calibrationGroups.size());
         ++groupIndex) {
        const ComponentProblem::CalibrationGroup &group =
            problem.calibrationGroups[groupIndex];
        if (!group.enabled || group.imageIndices.empty()) {
            continue;
        }
        const std::set<int> images(group.imageIndices.begin(),
                                   group.imageIndices.end());
        cv::Mat hessian = cv::Mat::zeros(9, 9, CV_64F);
        cv::Mat gradient = cv::Mat::zeros(9, 1, CV_64F);
        const CameraCalibration &current =
            calibrations[group.imageIndices.front()];
        const CalibrationVector scale = calibrationPriorSigmas(group.prior);
        for (const Observation &observation : problem.observations) {
            const SparseCameraPose &camera =
                reconstruction.cameras[observation.cameraIndex];
            if (!observation.active
                || images.count(camera.imageIndex) == 0) {
                continue;
            }
            const LinearizedObservation value = linearize(
                camera, reconstruction.points[observation.pointIndex],
                calibrations[camera.imageIndex], observation.measured);
            if (!value.valid) {
                continue;
            }
            const double error = cv::norm(value.residual);
            const double weight = error <= options.huberThresholdPixels
                    || error <= 1e-12
                ? 1.0 : options.huberThresholdPixels / error;
            cv::Mat jacobian(2, 9, CV_64F);
            for (int row = 0; row < 2; ++row) {
                for (int column = 0; column < 9; ++column) {
                    jacobian.at<double>(row, column) =
                        value.calibrationJacobian(row, column)
                        * scale[column];
                }
            }
            const cv::Mat residual(2, 1, CV_64F,
                                   const_cast<double *>(value.residual.val));
            hessian += weight * jacobian.t() * jacobian;
            gradient += weight * jacobian.t() * residual;
        }
        const CalibrationVector currentParameters =
            calibrationParameters(current);
        const CalibrationVector priorParameters =
            calibrationParameters(group.prior);
        for (int parameter = 0; parameter < 9; ++parameter) {
            // The normalized update uses the prior sigma as its scale, so the
            // Gaussian prior contributes an identity Hessian.
            hessian.at<double>(parameter, parameter) += 1.0;
            gradient.at<double>(parameter) +=
                (currentParameters[parameter] - priorParameters[parameter])
                / scale[parameter];
            hessian.at<double>(parameter, parameter) +=
                damping * (hessian.at<double>(parameter, parameter) + 1.0);
        }
        cv::Mat normalizedDelta;
        if (!cv::solve(hessian, -gradient, normalizedDelta,
                       cv::DECOMP_CHOLESKY)
            && !cv::solve(hessian, -gradient, normalizedDelta,
                          cv::DECOMP_SVD)) {
            return false;
        }
        CalibrationVector actualDelta;
        CalibrationVector candidate = currentParameters;
        for (int parameter = 0; parameter < 9; ++parameter) {
            actualDelta[parameter] =
                scale[parameter] * normalizedDelta.at<double>(parameter);
            candidate[parameter] += actualDelta[parameter];
        }
        if (!validCalibrationCandidate(candidate, group.prior)) {
            return false;
        }
        (*deltas)[groupIndex] = actualDelta;
    }
    return true;
}

void applyCalibrationStep(
    const ComponentProblem &problem,
    const std::vector<CalibrationVector> &deltas,
    std::vector<CameraCalibration> *calibrations)
{
    for (int groupIndex = 0;
         groupIndex < static_cast<int>(problem.calibrationGroups.size());
         ++groupIndex) {
        const ComponentProblem::CalibrationGroup &group =
            problem.calibrationGroups[groupIndex];
        if (!group.enabled || group.imageIndices.empty()) {
            continue;
        }
        CalibrationVector parameters = calibrationParameters(
            (*calibrations)[group.imageIndices.front()]);
        parameters += deltas[groupIndex];
        for (int imageIndex : group.imageIndices) {
            setCalibrationParameters(parameters,
                                     &(*calibrations)[imageIndex]);
        }
    }
}

void applyStep(const ComponentProblem &problem,
               const std::vector<cv::Vec<double, 6>> &cameraDeltas,
               const std::vector<cv::Vec3d> &pointDeltas,
               SparseInitializationResult *reconstruction)
{
    for (int variable = 0;
         variable < static_cast<int>(problem.variableCameraIndices.size());
         ++variable) {
        SparseCameraPose &camera = reconstruction->cameras[
            problem.variableCameraIndices[variable]];
        const cv::Vec<double, 6> &delta = cameraDeltas[variable];
        cv::Mat rotationIncrement;
        cv::Rodrigues(cv::Vec3d(delta[0], delta[1], delta[2]),
                      rotationIncrement);
        cv::Matx33d increment;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                increment(row, column) =
                    rotationIncrement.at<double>(row, column);
            }
        }
        camera.worldToCameraRotation = increment
                                       * camera.worldToCameraRotation;
        camera.worldToCameraTranslation = increment
            * camera.worldToCameraTranslation
            + cv::Vec3d(delta[3], delta[4], delta[5]);
    }
    for (int localPoint = 0;
         localPoint < static_cast<int>(problem.pointIndices.size());
         ++localPoint) {
        SparsePoint &point = reconstruction->points[
            problem.pointIndices[localPoint]];
        point.position += cv::Point3d(pointDeltas[localPoint][0],
                                      pointDeltas[localPoint][1],
                                      pointDeltas[localPoint][2]);
    }
}

int optimizePass(ComponentProblem *problem,
                 SparseInitializationResult *reconstruction,
                 std::vector<CameraCalibration> *calibrations,
                 const BundleAdjustmentOptions &options, int iterations,
                 int *linearSolverIterations)
{
    double damping = options.initialDamping;
    Evaluation current = evaluate(*problem, *reconstruction,
                                  *calibrations, options);
    int acceptedIterations = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        std::vector<cv::Vec<double, 6>> cameraDeltas;
        std::vector<cv::Vec3d> pointDeltas;
        if (!buildAndSolveStep(*problem, *reconstruction, *calibrations,
                               options, damping, &cameraDeltas,
                               &pointDeltas, linearSolverIterations)) {
            damping *= 10.0;
            continue;
        }
        const std::vector<SparseCameraPose> camerasBefore =
            reconstruction->cameras;
        const std::vector<SparsePoint> pointsBefore = reconstruction->points;
        const std::vector<CameraCalibration> calibrationsBefore =
            *calibrations;
        applyStep(*problem, cameraDeltas, pointDeltas, reconstruction);
        std::vector<CalibrationVector> calibrationDeltas;
        if (!buildCalibrationStep(*problem, *reconstruction, *calibrations,
                                  options, damping, &calibrationDeltas)) {
            reconstruction->cameras = camerasBefore;
            reconstruction->points = pointsBefore;
            *calibrations = calibrationsBefore;
            damping = std::min(1e12, damping * 10.0);
            continue;
        }
        applyCalibrationStep(*problem, calibrationDeltas, calibrations);
        const Evaluation candidate = evaluate(
            *problem, *reconstruction, *calibrations, options);
        if (candidate.count == current.count
            && candidate.cost < current.cost) {
            const double relativeImprovement =
                (current.cost - candidate.cost)
                / std::max(1.0, current.cost);
            current = candidate;
            damping = std::max(1e-12, damping * 0.3);
            ++acceptedIterations;
            if (relativeImprovement < options.convergenceTolerance) {
                break;
            }
        } else {
            reconstruction->cameras = camerasBefore;
            reconstruction->points = pointsBefore;
            *calibrations = calibrationsBefore;
            damping = std::min(1e12, damping * 10.0);
        }
    }
    return acceptedIterations;
}

ComponentProblem buildProblem(
    int componentId, const SparseInitializationResult &reconstruction,
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks,
    const std::vector<BundleAdjustmentPositionPrior> &positionPriors,
    const BundleAdjustmentOptions &options)
{
    ComponentProblem problem;
    problem.componentId = componentId;
    std::map<int, int> cameraByImage;
    for (int index = 0;
         index < static_cast<int>(reconstruction.cameras.size()); ++index) {
        const SparseCameraPose &camera = reconstruction.cameras[index];
        if (camera.componentId == componentId
            && camera.imageIndex >= 0
            && camera.imageIndex < static_cast<int>(calibrations.size())
            && calibrations[camera.imageIndex].isUsable()) {
            cameraByImage[camera.imageIndex] = index;
            problem.cameraIndices.push_back(index);
        }
    }
    std::sort(problem.cameraIndices.begin(), problem.cameraIndices.end(),
              [&reconstruction](int first, int second) {
                  const bool firstSeed =
                      reconstruction.cameras[first].pnpInliers == 0;
                  const bool secondSeed =
                      reconstruction.cameras[second].pnpInliers == 0;
                  if (firstSeed != secondSeed) {
                      return firstSeed;
                  }
                  return reconstruction.cameras[first].imageIndex
                         < reconstruction.cameras[second].imageIndex;
              });
    for (const BundleAdjustmentPositionPrior &prior : positionPriors) {
        const auto camera = cameraByImage.find(prior.imageIndex);
        if (prior.valid && camera != cameraByImage.end()
            && std::isfinite(prior.positionMetres.x)
            && std::isfinite(prior.positionMetres.y)
            && std::isfinite(prior.positionMetres.z)
            && prior.standardDeviationMetres > 0.0) {
            problem.positionPriors.push_back({
                camera->second, prior.positionMetres,
                prior.standardDeviationMetres});
        }
    }
    const int fixedCameraCount = std::min(
        2, static_cast<int>(problem.cameraIndices.size()));
    for (int index = fixedCameraCount;
         index < static_cast<int>(problem.cameraIndices.size()); ++index) {
        const int globalIndex = problem.cameraIndices[index];
        problem.variableCameraLookup[globalIndex] =
            static_cast<int>(problem.variableCameraIndices.size());
        problem.variableCameraIndices.push_back(globalIndex);
    }

    std::map<int, const FeatureTrack *> tracksById;
    for (const FeatureTrack &track : tracks) {
        tracksById[track.id] = &track;
    }
    for (int pointIndex = 0;
         pointIndex < static_cast<int>(reconstruction.points.size());
         ++pointIndex) {
        const SparsePoint &point = reconstruction.points[pointIndex];
        if (point.componentId != componentId) {
            continue;
        }
        const auto track = tracksById.find(point.trackId);
        if (track == tracksById.end()) {
            continue;
        }
        std::vector<Observation> pointObservations;
        for (const FeatureObservation &feature : track->second->observations) {
            const auto camera = cameraByImage.find(feature.imageIndex);
            if (camera != cameraByImage.end()) {
                pointObservations.push_back(
                    {camera->second, pointIndex, point.trackId,
                     feature.imagePoint, true});
            }
        }
        if (pointObservations.size() < 2) {
            continue;
        }
        problem.localPointLookup[pointIndex] =
            static_cast<int>(problem.pointIndices.size());
        problem.pointIndices.push_back(pointIndex);
        problem.observations.insert(problem.observations.end(),
                                    pointObservations.begin(),
                                    pointObservations.end());
    }
    if (options.refineCameraCalibration) {
        std::map<int, int> groupByImage;
        for (int cameraIndex : problem.cameraIndices) {
            const int imageIndex =
                reconstruction.cameras[cameraIndex].imageIndex;
            const CameraCalibration &calibration = calibrations[imageIndex];
            int groupIndex = -1;
            for (int candidate = 0;
                 candidate < static_cast<int>(problem.calibrationGroups.size());
                 ++candidate) {
                if (equivalentCalibration(
                        problem.calibrationGroups[candidate].prior,
                        calibration)) {
                    groupIndex = candidate;
                    break;
                }
            }
            if (groupIndex < 0) {
                groupIndex = static_cast<int>(problem.calibrationGroups.size());
                ComponentProblem::CalibrationGroup group;
                group.prior = calibration;
                problem.calibrationGroups.push_back(std::move(group));
            }
            problem.calibrationGroups[groupIndex].imageIndices.push_back(
                imageIndex);
            groupByImage[imageIndex] = groupIndex;
        }
        for (const Observation &observation : problem.observations) {
            const int imageIndex =
                reconstruction.cameras[observation.cameraIndex].imageIndex;
            const auto group = groupByImage.find(imageIndex);
            if (group != groupByImage.end()) {
                ++problem.calibrationGroups[group->second].observationCount;
            }
        }
        for (ComponentProblem::CalibrationGroup &group
             : problem.calibrationGroups) {
            group.enabled =
                static_cast<int>(group.imageIndices.size())
                    >= options.minimumCalibrationGroupCameras
                && group.observationCount
                    >= options.minimumCalibrationGroupObservations;
        }
    }
    return problem;
}

BundleAdjustmentComponentSummary optimizeComponent(
    ComponentProblem *problem, SparseInitializationResult *reconstruction,
    std::vector<CameraCalibration> *calibrations,
    const BundleAdjustmentOptions &options,
    std::vector<BundleAdjustmentResult::RejectedObservation> *rejected,
    std::vector<BundleAdjustmentCalibrationSummary> *calibrationSummaries)
{
    BundleAdjustmentComponentSummary summary;
    summary.componentId = problem->componentId;
    summary.cameraCount = static_cast<int>(problem->cameraIndices.size());
    summary.fixedCameraCount = summary.cameraCount
        - static_cast<int>(problem->variableCameraIndices.size());
    summary.gpsPriorCount = static_cast<int>(problem->positionPriors.size());
    summary.pointCount = static_cast<int>(problem->pointIndices.size());
    summary.observationCount = static_cast<int>(problem->observations.size());
    if (summary.cameraCount < 2 || summary.pointCount == 0
        || summary.observationCount < 4) {
        summary.message = "Component has insufficient bundle-adjustment geometry.";
        return summary;
    }
    const Evaluation initial = evaluate(*problem, *reconstruction,
                                        *calibrations, options);
    summary.initialRmsPixels = initial.rms;
    summary.initialGpsRmsMetres = initial.gpsRmsMetres;
    summary.acceptedIterations += optimizePass(
        problem, reconstruction, calibrations, options,
        options.maximumIterations, &summary.linearSolverIterations);

    std::map<int, int> remainingByPoint;
    for (const Observation &observation : problem->observations) {
        if (observation.active) {
            ++remainingByPoint[observation.pointIndex];
        }
    }
    for (Observation &observation : problem->observations) {
        const SparseCameraPose &camera =
            reconstruction->cameras[observation.cameraIndex];
        const LinearizedObservation value = linearize(
            camera, reconstruction->points[observation.pointIndex],
            (*calibrations)[camera.imageIndex], observation.measured);
        if (value.valid
            && cv::norm(value.residual) > options.outlierThresholdPixels
            && remainingByPoint[observation.pointIndex] > 2) {
            rejected->push_back({
                problem->componentId, camera.imageIndex,
                observation.trackId, cv::norm(value.residual)});
            observation.active = false;
            --remainingByPoint[observation.pointIndex];
            ++summary.rejectedObservationCount;
        }
    }
    if (summary.rejectedObservationCount > 0) {
        summary.acceptedIterations += optimizePass(
            problem, reconstruction, calibrations, options,
            options.outlierRefinementIterations,
            &summary.linearSolverIterations);
    }
    const Evaluation final = evaluate(*problem, *reconstruction,
                                      *calibrations, options);
    summary.finalRmsPixels = final.rms;
    summary.finalGpsRmsMetres = final.gpsRmsMetres;
    std::map<int, std::pair<double, int>> pointErrors;
    for (const Observation &observation : problem->observations) {
        if (!observation.active) {
            continue;
        }
        const SparseCameraPose &camera =
            reconstruction->cameras[observation.cameraIndex];
        const LinearizedObservation value = linearize(
            camera, reconstruction->points[observation.pointIndex],
            (*calibrations)[camera.imageIndex], observation.measured);
        if (value.valid) {
            auto &error = pointErrors[observation.pointIndex];
            error.first += value.residual.dot(value.residual);
            ++error.second;
        }
    }
    for (const auto &[pointIndex, error] : pointErrors) {
        reconstruction->points[pointIndex].reprojectionRmsPixels =
            std::sqrt(error.first / error.second);
    }
    summary.success = final.count > 0
                      && std::isfinite(final.rms)
                      && final.cost <= initial.cost;
    summary.message = summary.success
        ? "Bundle adjustment converged."
        : "Bundle adjustment did not improve this component.";
    for (const ComponentProblem::CalibrationGroup &group
         : problem->calibrationGroups) {
        if (!group.enabled || group.imageIndices.empty()) {
            continue;
        }
        BundleAdjustmentCalibrationSummary calibrationSummary;
        calibrationSummary.componentId = problem->componentId;
        calibrationSummary.imageIndices = group.imageIndices;
        calibrationSummary.initial = group.prior;
        calibrationSummary.final =
            (*calibrations)[group.imageIndices.front()];
        const CalibrationVector before =
            calibrationParameters(calibrationSummary.initial);
        const CalibrationVector after =
            calibrationParameters(calibrationSummary.final);
        calibrationSummary.refined = cv::norm(before - after) > 1e-10;
        if (calibrationSummary.refined) {
            ++summary.refinedCalibrationGroupCount;
        }
        calibrationSummaries->push_back(std::move(calibrationSummary));
    }
    return summary;
}

} // namespace

BundleAdjustmentResult SparseBundleAdjuster::optimize(
    SparseInitializationResult *reconstruction,
    std::vector<CameraCalibration> *calibrations,
    const std::vector<FeatureTrack> &tracks,
    const std::vector<BundleAdjustmentPositionPrior> &positionPriors,
    const BundleAdjustmentOptions &options)
{
    BundleAdjustmentResult result;
    if (!reconstruction || !reconstruction->success || !calibrations) {
        return result;
    }
    for (int componentId = 0;
         componentId < reconstruction->componentCount; ++componentId) {
        ComponentProblem problem = buildProblem(
            componentId, *reconstruction, *calibrations, tracks,
            positionPriors, options);
        BundleAdjustmentComponentSummary summary = optimizeComponent(
            &problem, reconstruction, calibrations, options,
            &result.rejectedObservations, &result.calibrations);
        if (summary.success) {
            ++result.optimizedComponentCount;
        }
        result.rejectedObservationCount +=
            summary.rejectedObservationCount;
        result.components.push_back(std::move(summary));
    }
    result.success = result.optimizedComponentCount > 0;
    return result;
}

BundleAdjustmentResult SparseBundleAdjuster::optimize(
    SparseInitializationResult *reconstruction,
    const std::vector<CameraCalibration> &calibrations,
    const std::vector<FeatureTrack> &tracks,
    const std::vector<BundleAdjustmentPositionPrior> &positionPriors,
    const BundleAdjustmentOptions &options)
{
    std::vector<CameraCalibration> mutableCalibrations = calibrations;
    return optimize(reconstruction, &mutableCalibrations, tracks,
                    positionPriors, options);
}

} // namespace kestrel
