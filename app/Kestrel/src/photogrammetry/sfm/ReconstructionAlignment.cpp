#include "photogrammetry/sfm/ReconstructionAlignment.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>

namespace kestrel {

namespace {

struct Correspondence
{
    cv::Point3d sparse;
    cv::Point3d enu;
};

cv::Point3d cameraCentre(const SparseCameraPose &camera)
{
    const cv::Vec3d centre = -camera.worldToCameraRotation.t()
                             * camera.worldToCameraTranslation;
    return {centre[0], centre[1], centre[2]};
}

cv::Point3d transformPoint(const cv::Point3d &point, double scale,
                           const cv::Matx33d &rotation,
                           const cv::Vec3d &translation)
{
    const cv::Vec3d transformed = scale * rotation
        * cv::Vec3d(point.x, point.y, point.z) + translation;
    return {transformed[0], transformed[1], transformed[2]};
}

double distance(const cv::Point3d &first, const cv::Point3d &second)
{
    return cv::norm(cv::Vec3d(first.x - second.x,
                              first.y - second.y,
                              first.z - second.z));
}

bool fitSimilarity(const std::vector<Correspondence> &correspondences,
                   const std::vector<int> &indices,
                   double *scale, cv::Matx33d *rotation,
                   cv::Vec3d *translation)
{
    if (indices.size() < 3) {
        return false;
    }
    cv::Vec3d sparseMean(0.0, 0.0, 0.0);
    cv::Vec3d enuMean(0.0, 0.0, 0.0);
    for (int index : indices) {
        sparseMean += cv::Vec3d(correspondences[index].sparse.x,
                                correspondences[index].sparse.y,
                                correspondences[index].sparse.z);
        enuMean += cv::Vec3d(correspondences[index].enu.x,
                             correspondences[index].enu.y,
                             correspondences[index].enu.z);
    }
    sparseMean /= static_cast<double>(indices.size());
    enuMean /= static_cast<double>(indices.size());

    cv::Matx33d covariance = cv::Matx33d::zeros();
    double sparseVariance = 0.0;
    for (int index : indices) {
        const cv::Vec3d sparse = cv::Vec3d(
            correspondences[index].sparse.x,
            correspondences[index].sparse.y,
            correspondences[index].sparse.z) - sparseMean;
        const cv::Vec3d enu = cv::Vec3d(
            correspondences[index].enu.x,
            correspondences[index].enu.y,
            correspondences[index].enu.z) - enuMean;
        covariance += enu * sparse.t();
        sparseVariance += sparse.dot(sparse);
    }
    covariance *= 1.0 / indices.size();
    sparseVariance /= indices.size();
    if (!(sparseVariance > 1e-12)) {
        return false;
    }

    cv::Mat singularValues;
    cv::Mat left;
    cv::Mat right;
    cv::SVD::compute(cv::Mat(covariance), singularValues, left, right,
                     cv::SVD::FULL_UV);
    if (singularValues.rows < 3
        || singularValues.at<double>(1) < 1e-10) {
        return false;
    }
    cv::Mat correction = cv::Mat::eye(3, 3, CV_64F);
    if (cv::determinant(left * right) < 0.0) {
        correction.at<double>(2, 2) = -1.0;
    }
    const cv::Mat fittedRotation = left * correction * right;
    double numerator = singularValues.at<double>(0)
                       + singularValues.at<double>(1);
    numerator += correction.at<double>(2, 2)
                 * singularValues.at<double>(2);
    *scale = numerator / sparseVariance;
    if (!std::isfinite(*scale) || !(*scale > 0.0)) {
        return false;
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            (*rotation)(row, column) =
                fittedRotation.at<double>(row, column);
        }
    }
    *translation = enuMean - *scale * *rotation * sparseMean;
    return true;
}

ComponentAlignment alignComponent(
    int componentId, const std::vector<Correspondence> &correspondences,
    const ReconstructionAlignmentOptions &options)
{
    ComponentAlignment result;
    result.componentId = componentId;
    result.priorCount = static_cast<int>(correspondences.size());
    if (result.priorCount < options.minimumPositionPriors) {
        result.message = "Too few GPS camera positions for similarity alignment.";
        return result;
    }

    std::mt19937 generator(0x4b455354u + componentId);
    std::uniform_int_distribution<int> distribution(
        0, static_cast<int>(correspondences.size()) - 1);
    std::vector<int> bestInliers;
    double bestSquaredError = 0.0;
    for (int iteration = 0; iteration < options.ransacIterations; ++iteration) {
        std::set<int> unique;
        while (unique.size() < 3) {
            unique.insert(distribution(generator));
        }
        const std::vector<int> sample(unique.begin(), unique.end());
        double scale = 1.0;
        cv::Matx33d rotation;
        cv::Vec3d translation;
        if (!fitSimilarity(correspondences, sample, &scale,
                           &rotation, &translation)) {
            continue;
        }
        std::vector<int> inliers;
        double squaredError = 0.0;
        for (int index = 0; index < static_cast<int>(correspondences.size());
             ++index) {
            const double error = distance(
                transformPoint(correspondences[index].sparse, scale,
                               rotation, translation),
                correspondences[index].enu);
            if (error <= options.ransacInlierThresholdMetres) {
                inliers.push_back(index);
                squaredError += error * error;
            }
        }
        if (inliers.size() > bestInliers.size()
            || (inliers.size() == bestInliers.size()
                && squaredError < bestSquaredError)) {
            bestInliers = std::move(inliers);
            bestSquaredError = squaredError;
        }
    }
    if (bestInliers.size()
        < static_cast<size_t>(options.minimumPositionPriors)
        || !fitSimilarity(correspondences, bestInliers, &result.scale,
                          &result.rotation, &result.translation)) {
        result.message = "GPS similarity RANSAC found no stable consensus.";
        return result;
    }

    double squaredError = 0.0;
    for (int index : bestInliers) {
        const double error = distance(
            transformPoint(correspondences[index].sparse, result.scale,
                           result.rotation, result.translation),
            correspondences[index].enu);
        squaredError += error * error;
    }
    result.inlierCount = static_cast<int>(bestInliers.size());
    result.rmsMetres = std::sqrt(squaredError / bestInliers.size());
    result.success = true;
    result.message = "Sparse component aligned to ENU.";
    return result;
}

} // namespace

ReconstructionAlignmentResult ReconstructionAligner::alignToEnu(
    SparseInitializationResult *reconstruction,
    const std::vector<CameraPositionPrior> &priors,
    const ReconstructionAlignmentOptions &options)
{
    ReconstructionAlignmentResult result;
    if (!reconstruction || !reconstruction->success) {
        return result;
    }
    std::map<int, CameraPositionPrior> priorsByImage;
    for (const CameraPositionPrior &prior : priors) {
        if (prior.valid && prior.imageIndex >= 0) {
            priorsByImage[prior.imageIndex] = prior;
        }
    }
    for (int componentId = 0;
         componentId < reconstruction->componentCount; ++componentId) {
        std::vector<Correspondence> correspondences;
        for (const SparseCameraPose &camera : reconstruction->cameras) {
            const auto prior = priorsByImage.find(camera.imageIndex);
            if (camera.componentId == componentId
                && prior != priorsByImage.end()) {
                correspondences.push_back(
                    {cameraCentre(camera), prior->second.enuMetres});
            }
        }
        ComponentAlignment alignment = alignComponent(
            componentId, correspondences, options);
        if (alignment.success) {
            ++result.alignedComponentCount;
            for (SparseCameraPose &camera : reconstruction->cameras) {
                if (camera.componentId != componentId) {
                    continue;
                }
                const cv::Point3d alignedCentre = transformPoint(
                    cameraCentre(camera), alignment.scale,
                    alignment.rotation, alignment.translation);
                camera.worldToCameraRotation =
                    camera.worldToCameraRotation * alignment.rotation.t();
                camera.worldToCameraTranslation =
                    -camera.worldToCameraRotation
                    * cv::Vec3d(alignedCentre.x, alignedCentre.y,
                                alignedCentre.z);
            }
            for (SparsePoint &point : reconstruction->points) {
                if (point.componentId == componentId) {
                    point.position = transformPoint(
                        point.position, alignment.scale,
                        alignment.rotation, alignment.translation);
                }
            }
        }
        result.components.push_back(std::move(alignment));
    }
    return result;
}

} // namespace kestrel
