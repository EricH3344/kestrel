#ifndef DENSERECONSTRUCTION_H
#define DENSERECONSTRUCTION_H

#include "photogrammetry/camera/CameraCalibration.h"
#include "photogrammetry/sfm/FeatureTracks.h"
#include "photogrammetry/sfm/SparseReconstruction.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace kestrel {

struct DenseMvsOptions
{
    int maximumNeighbours = 5;
    int minimumSharedSparsePoints = 12;
    double minimumTriangulationAngleDegrees = 0.5;
    double maximumTriangulationAngleDegrees = 35.0;
    double preferredTriangulationAngleDegrees = 8.0;
    int minimumDepthRangePoints = 10;
    double depthRangeQuantile = 0.05;
    double depthRangePaddingFraction = 0.15;
    int maximumImageDimension = 1024;
    int pyramidLevels = 3;
    int depthHypotheses = 64;
    int patchRadius = 2;
    int minimumSupportingViews = 2;
    double minimumTextureVariance = 1e-5;
    double refinementRelativeDepthWindow = 0.08;
    double minimumConfidence = 0.02;
    int minimumConsistencyChecks = 1;
    int minimumConsistentViews = 1;
    double consistencyRelativeDepthTolerance = 0.02;
    double consistencyAbsoluteDepthTolerance = 0.05;
    double maximumRoundTripErrorPixels = 1.5;
    int tileSizePixels = 0; // Zero processes each reference view as one tile.
    int tileHaloPixels = 0; // Zero derives a safe halo from patch/pyramid support.
    std::string checkpointDirectory; // Empty disables persistent tile checkpoints.
    bool resumeFromCheckpoints = true;
    // Releases raw maps after plane sweep, reloads only the reference and its
    // neighbours for consistency, and stores consistent tiles for downstream
    // one-map-at-a-time consumers. Requires checkpointDirectory.
    bool streamConsistencyFromCheckpoints = false;
};

struct DenseMvsView
{
    int imageIndex = -1;
    cv::Mat image;
    CameraCalibration calibration;
    SparseCameraPose pose;
};

struct DenseMvsNeighbour
{
    int viewIndex = -1;
    int imageIndex = -1;
    int sharedSparsePointCount = 0;
    double medianTriangulationAngleDegrees = 0.0;
    double score = 0.0;
};

struct DenseDepthRange
{
    bool valid = false;
    double minimumDepth = 0.0;
    double maximumDepth = 0.0;
    int supportingPointCount = 0;
};

struct DenseDepthMap
{
    int imageIndex = -1;
    cv::Mat depth; // CV_32FC1, depth along the reference camera Z axis.
    cv::Mat confidence; // CV_32FC1 in [0, 1].
    cv::Mat validityMask; // CV_8UC1; valid pixels are 255.
    cv::Mat consistentViewCount; // CV_8UC1.
    cv::Mat occludedViewCount; // CV_8UC1.
    cv::Size depthMapSize; // Retained when streamed matrices are released.
    double imageScale = 1.0;
    DenseDepthRange depthRange;
    std::vector<int> neighbourImageIndices;
    int processedTileCount = 0;
    int resumedTileCount = 0;
    int tileHaloPixels = 0;
    size_t rawValidPixelCount = 0;
    size_t consistentValidPixelCount = 0;
    int processedConsistencyTileCount = 0;
    int resumedConsistencyTileCount = 0;
    // Opaque input fingerprint used to validate raw and consistent tiles.
    std::string rawCheckpointSignature;
};

struct DenseReconstructionInput
{
    std::vector<DenseMvsView> views;
    std::vector<SparsePoint> sparsePoints;
    std::vector<FeatureTrack> tracks;
};

struct DenseReconstructionResult
{
    bool success = false;
    std::string message;
    size_t rawValidPixelCount = 0;
    size_t consistentPixelCount = 0;
    size_t processedTileCount = 0;
    size_t resumedTileCount = 0;
    size_t processedConsistencyTileCount = 0;
    size_t resumedConsistencyTileCount = 0;
    std::vector<DenseDepthMap> depthMaps;
};

class DenseReconstructionBackend
{
public:
    virtual ~DenseReconstructionBackend() = default;
    virtual DenseReconstructionResult reconstruct(
        const DenseReconstructionInput &input,
        const DenseMvsOptions &options = {}) const = 0;
};

class PlaneSweepMvsBackend final : public DenseReconstructionBackend
{
public:
    DenseReconstructionResult reconstruct(
        const DenseReconstructionInput &input,
        const DenseMvsOptions &options = {}) const override;

    static std::vector<std::vector<DenseMvsNeighbour>> selectNeighbours(
        const DenseReconstructionInput &input,
        const DenseMvsOptions &options = {});

    static DenseDepthRange estimateDepthRange(
        const DenseMvsView &reference,
        const std::vector<SparsePoint> &sparsePoints,
        const std::vector<FeatureTrack> &tracks,
        const DenseMvsOptions &options = {});

    static int recommendedTileHaloPixels(
        const DenseMvsOptions &options = {});

    static bool computeDepthMap(
        const DenseMvsView &reference,
        const std::vector<const DenseMvsView *> &neighbours,
        const DenseDepthRange &depthRange,
        const DenseMvsOptions &options,
        DenseDepthMap *depthMap,
        std::string *errorMessage = nullptr);

    // Cross-projects every depth into available neighbouring depth maps.
    // Closer source depths are recorded as occlusions instead of penalizing a
    // valid reference surface; other samples must pass depth and round-trip
    // reprojection checks before they contribute confidence.
    static bool applyMultiViewConsistency(
        const std::vector<DenseMvsView> &views,
        std::vector<DenseDepthMap> *depthMaps,
        const DenseMvsOptions &options = {},
        std::string *errorMessage = nullptr);

    // Reassembles one consistency-filtered map from signature-validated tile
    // checkpoints. This is the loader used by streaming fusion/export stages.
    static bool loadConsistentDepthMap(
        const std::vector<DenseDepthMap> &depthMapDescriptors,
        size_t depthMapIndex,
        const DenseMvsOptions &options,
        DenseDepthMap *depthMap,
        std::string *errorMessage = nullptr);
};

} // namespace kestrel

#endif // DENSERECONSTRUCTION_H
