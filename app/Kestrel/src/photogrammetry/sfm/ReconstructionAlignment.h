#ifndef RECONSTRUCTIONALIGNMENT_H
#define RECONSTRUCTIONALIGNMENT_H

#include "photogrammetry/sfm/SparseReconstruction.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace kestrel {

struct CameraPositionPrior
{
    int imageIndex = -1;
    cv::Point3d enuMetres;
    bool valid = false;
};

struct ComponentAlignment
{
    int componentId = -1;
    bool success = false;
    std::string message;
    int priorCount = 0;
    int inlierCount = 0;
    double scale = 1.0;
    cv::Matx33d rotation = cv::Matx33d::eye();
    cv::Vec3d translation{0.0, 0.0, 0.0};
    double rmsMetres = 0.0;
};

struct ReconstructionAlignmentOptions
{
    int minimumPositionPriors = 3;
    int ransacIterations = 600;
    double ransacInlierThresholdMetres = 5.0;
};

struct ReconstructionAlignmentResult
{
    int alignedComponentCount = 0;
    std::vector<ComponentAlignment> components;
};

class ReconstructionAligner
{
public:
    // Estimates ENU = scale * rotation * sparse + translation independently
    // for every component with enough camera/GPS correspondences. Successful
    // components are transformed in place; unsupported components remain in
    // their explicit arbitrary frames.
    static ReconstructionAlignmentResult alignToEnu(
        SparseInitializationResult *reconstruction,
        const std::vector<CameraPositionPrior> &priors,
        const ReconstructionAlignmentOptions &options = {});
};

} // namespace kestrel

#endif // RECONSTRUCTIONALIGNMENT_H
