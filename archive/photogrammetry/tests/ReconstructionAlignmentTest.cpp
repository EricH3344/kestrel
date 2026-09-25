#include "photogrammetry/sfm/ReconstructionAlignment.h"

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

cv::Point3d transform(const cv::Point3d &point, double scale,
                      const cv::Matx33d &rotation,
                      const cv::Vec3d &translation)
{
    const cv::Vec3d value = scale * rotation
        * cv::Vec3d(point.x, point.y, point.z) + translation;
    return {value[0], value[1], value[2]};
}

cv::Point3d centre(const kestrel::SparseCameraPose &camera)
{
    const cv::Vec3d value = -camera.worldToCameraRotation.t()
                             * camera.worldToCameraTranslation;
    return {value[0], value[1], value[2]};
}

bool robustAlignmentTest()
{
    kestrel::SparseInitializationResult reconstruction;
    reconstruction.success = true;
    reconstruction.componentCount = 2;
    const std::vector<cv::Point3d> sparseCentres{
        {0.0, 0.0, 1.0}, {2.0, 0.0, 1.2}, {0.0, 3.0, 0.8},
        {2.0, 3.0, 1.1}, {4.0, 1.0, 1.4}, {1.0, 5.0, 0.9},
        {5.0, 4.0, 1.3}, {3.0, 6.0, 1.0}};
    for (int index = 0; index < static_cast<int>(sparseCentres.size());
         ++index) {
        reconstruction.cameras.push_back(
            {index, cv::Matx33d::eye(),
             cv::Vec3d(-sparseCentres[index].x,
                       -sparseCentres[index].y,
                       -sparseCentres[index].z)});
    }
    reconstruction.cameras.push_back(
        {8, cv::Matx33d::eye(), cv::Vec3d(-1.0, 0.0, -1.0), 0, 0.0, 1});
    reconstruction.cameras.push_back(
        {9, cv::Matx33d::eye(), cv::Vec3d(-2.0, 0.0, -1.0), 0, 0.0, 1});
    reconstruction.points.push_back(
        {42, {1.5, 2.5, 0.2}, 0.1, 5.0, 0});

    const double scale = 2.75;
    const double angle = 28.0 * CV_PI / 180.0;
    const cv::Matx33d rotation(
        std::cos(angle), -std::sin(angle), 0.0,
        std::sin(angle), std::cos(angle), 0.0,
        0.0, 0.0, 1.0);
    const cv::Vec3d translation(150.0, -80.0, 12.0);
    std::vector<kestrel::CameraPositionPrior> priors;
    for (int index = 0; index < static_cast<int>(sparseCentres.size());
         ++index) {
        cv::Point3d enu = transform(sparseCentres[index], scale,
                                    rotation, translation);
        enu.x += 0.05 * std::sin(index);
        enu.y += 0.05 * std::cos(index);
        if (index == 7) {
            enu.x += 60.0;
            enu.y -= 45.0;
        }
        priors.push_back({index, enu, true});
    }
    priors.push_back({8, {10.0, 20.0, 3.0}, true});
    priors.push_back({9, {12.0, 20.0, 3.0}, true});

    kestrel::ReconstructionAlignmentOptions options;
    options.ransacInlierThresholdMetres = 1.0;
    const auto result = kestrel::ReconstructionAligner::alignToEnu(
        &reconstruction, priors, options);
    const cv::Point3d expectedFirst = transform(
        sparseCentres[0], scale, rotation, translation);
    const cv::Point3d expectedPoint = transform(
        {1.5, 2.5, 0.2}, scale, rotation, translation);
    const cv::Vec3d originalCameraPoint =
        cv::Vec3d(1.5 - sparseCentres[0].x,
                  2.5 - sparseCentres[0].y,
                  0.2 - sparseCentres[0].z);
    const cv::Vec3d alignedCameraPoint =
        reconstruction.cameras[0].worldToCameraRotation
            * cv::Vec3d(reconstruction.points[0].position.x,
                        reconstruction.points[0].position.y,
                        reconstruction.points[0].position.z)
        + reconstruction.cameras[0].worldToCameraTranslation;
    return expect(result.alignedComponentCount == 1,
                  "Only the sufficiently constrained component should align.")
           && expect(result.components.size() == 2
                         && result.components[0].success,
                     "The primary component should have an alignment result.")
           && expect(result.components[0].inlierCount == 7,
                     "Similarity RANSAC should reject the GPS outlier.")
           && expect(std::abs(result.components[0].scale - scale) < 0.03,
                     "Recovered ENU scale should match ground truth.")
           && expect(cv::norm(centre(reconstruction.cameras[0])
                              - expectedFirst) < 0.15,
                     "Aligned camera centres should be expressed in ENU metres.")
           && expect(cv::norm(reconstruction.points[0].position
                              - expectedPoint) < 0.15,
                     "Sparse points should receive the same similarity transform.")
           && expect(std::abs(originalCameraPoint[0] / originalCameraPoint[2]
                              - alignedCameraPoint[0] / alignedCameraPoint[2])
                         < 1e-10,
                     "ENU alignment must preserve camera projection geometry.")
           && expect(!result.components[1].success,
                     "Two GPS priors must not define a 3D similarity safely.");
}

} // namespace

int main()
{
    if (robustAlignmentTest()) {
        std::cout << "Reconstruction alignment tests passed.\n";
        return 0;
    }
    return 1;
}
