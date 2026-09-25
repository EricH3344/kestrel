#ifndef BUNDLEADJUSTMENT_H
#define BUNDLEADJUSTMENT_H

#include "photogrammetry/camera/CameraCalibration.h"
#include "photogrammetry/sfm/FeatureTracks.h"
#include "photogrammetry/sfm/SparseReconstruction.h"

#include <string>
#include <vector>

namespace kestrel {

struct BundleAdjustmentOptions
{
    int maximumIterations = 15;
    int outlierRefinementIterations = 8;
    double huberThresholdPixels = 2.0;
    double outlierThresholdPixels = 5.0;
    double initialDamping = 1e-3;
    double convergenceTolerance = 1e-7;
    double gpsHuberThresholdStandardDeviations = 2.5;
    bool refineCameraCalibration = false;
    int minimumCalibrationGroupCameras = 3;
    int minimumCalibrationGroupObservations = 100;
    int maximumLinearSolverIterations = 300;
    double linearSolverTolerance = 1e-8;
};

struct BundleAdjustmentPositionPrior
{
    int imageIndex = -1;
    cv::Point3d positionMetres;
    double standardDeviationMetres = 3.0;
    bool valid = false;
};

struct BundleAdjustmentComponentSummary
{
    int componentId = -1;
    bool success = false;
    std::string message;
    int cameraCount = 0;
    int fixedCameraCount = 0;
    int pointCount = 0;
    int observationCount = 0;
    int rejectedObservationCount = 0;
    int gpsPriorCount = 0;
    int refinedCalibrationGroupCount = 0;
    int acceptedIterations = 0;
    int linearSolverIterations = 0;
    double initialRmsPixels = 0.0;
    double finalRmsPixels = 0.0;
    double initialGpsRmsMetres = 0.0;
    double finalGpsRmsMetres = 0.0;
};

struct BundleAdjustmentCalibrationSummary
{
    int componentId = -1;
    std::vector<int> imageIndices;
    bool refined = false;
    CameraCalibration initial;
    CameraCalibration final;
};

struct BundleAdjustmentResult
{
    bool success = false;
    int optimizedComponentCount = 0;
    int rejectedObservationCount = 0;
    std::vector<BundleAdjustmentComponentSummary> components;
    std::vector<BundleAdjustmentCalibrationSummary> calibrations;
    struct RejectedObservation {
        int componentId = -1;
        int imageIndex = -1;
        int trackId = -1;
        double errorPixels = 0.0;
    };
    std::vector<RejectedObservation> rejectedObservations;
};

class SparseBundleAdjuster
{
public:
    // Jointly optimizes camera extrinsics and sparse points. Two seed camera
    // poses per component remain fixed to remove the similarity gauge. Point
    // blocks are eliminated with a Schur complement and the reduced block-
    // sparse camera system is solved by block-Jacobi preconditioned CG.
    // When enabled, matching sensor calibrations are refined as shared models
    // with quality-dependent priors and written back through calibrations.
    static BundleAdjustmentResult optimize(
        SparseInitializationResult *reconstruction,
        std::vector<CameraCalibration> *calibrations,
        const std::vector<FeatureTrack> &tracks,
        const std::vector<BundleAdjustmentPositionPrior> &positionPriors = {},
        const BundleAdjustmentOptions &options = {});

    // Read-only convenience overload. Calibration refinement, if requested,
    // is performed on an internal copy and reported in the result.
    static BundleAdjustmentResult optimize(
        SparseInitializationResult *reconstruction,
        const std::vector<CameraCalibration> &calibrations,
        const std::vector<FeatureTrack> &tracks,
        const std::vector<BundleAdjustmentPositionPrior> &positionPriors = {},
        const BundleAdjustmentOptions &options = {});
};

} // namespace kestrel

#endif // BUNDLEADJUSTMENT_H
