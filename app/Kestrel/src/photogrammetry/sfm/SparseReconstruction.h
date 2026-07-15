#ifndef SPARSERECONSTRUCTION_H
#define SPARSERECONSTRUCTION_H

#include "photogrammetry/camera/CameraCalibration.h"
#include "photogrammetry/sfm/FeatureTracks.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace kestrel {

struct SparseCameraPose
{
    int imageIndex = -1;
    cv::Matx33d worldToCameraRotation = cv::Matx33d::eye();
    cv::Vec3d worldToCameraTranslation{0.0, 0.0, 0.0};
    int pnpInliers = 0;
    double pnpReprojectionRmsPixels = 0.0;
    int componentId = 0;
};

struct SparsePoint
{
    int trackId = -1;
    cv::Point3d position;
    double reprojectionRmsPixels = 0.0;
    double triangulationAngleDegrees = 0.0;
    int componentId = 0;
};

struct SparseInitializationOptions
{
    int minimumSharedTracks = 20;
    int minimumTriangulatedPoints = 15;
    int maximumCandidatePairs = 30;
    double ransacThresholdPixels = 1.5;
    double ransacConfidence = 0.999;
    double maximumReprojectionErrorPixels = 4.0;
    double minimumTriangulationAngleDegrees = 0.5;
    int minimumPnpCorrespondences = 12;
    int minimumPnpInliers = 10;
    int pnpRansacIterations = 300;
    double pnpRansacConfidence = 0.999;
    double pnpRansacThresholdPixels = 4.0;
};

struct SparseInitializationResult
{
    bool success = false;
    std::string message;
    int firstImageIndex = -1;
    int secondImageIndex = -1;
    int sharedTrackCount = 0;
    int essentialInlierCount = 0;
    double medianTriangulationAngleDegrees = 0.0;
    int pnpAttempts = 0;
    int pnpRegisteredCameraCount = 0;
    int componentCount = 0;
    std::vector<SparseCameraPose> cameras;
    std::vector<SparsePoint> points;
    std::vector<int> unregisteredImageIndices;
};

class SparseReconstructor
{
public:
    // Initializes a two-camera reconstruction in an arbitrary metric scale.
    // Camera zero defines the world frame and the recovered translation has
    // unit length. GPS alignment and absolute scale are intentionally deferred.
    static SparseInitializationResult initialize(
        const std::vector<CameraCalibration> &calibrations,
        const std::vector<FeatureTrack> &tracks,
        const SparseInitializationOptions &options = {});

    // Runs two-view initialization and then incrementally registers additional
    // cameras with PnP RANSAC. Tracks are retriangulated after every accepted
    // camera so later cameras can use newly created 3D correspondences.
    static SparseInitializationResult reconstruct(
        const std::vector<CameraCalibration> &calibrations,
        const std::vector<FeatureTrack> &tracks,
        const SparseInitializationOptions &options = {});
};

} // namespace kestrel

#endif // SPARSERECONSTRUCTION_H
