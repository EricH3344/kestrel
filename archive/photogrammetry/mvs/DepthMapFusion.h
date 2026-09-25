#ifndef DEPTHMAPFUSION_H
#define DEPTHMAPFUSION_H

#include "photogrammetry/mvs/DenseReconstruction.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace kestrel {

struct DensePointSource
{
    int imageIndex = -1;
    cv::Point2f depthPixel;
    float confidence = 0.0f;
};

struct FusedDensePoint
{
    cv::Point3d position;
    float confidence = 0.0f;
    int observationCount = 0; // Distinct reference cameras.
    int sampleCount = 0; // Depth pixels accumulated across those cameras.
    std::vector<DensePointSource> sources;
};

struct DepthMapFusionOptions
{
    double voxelSizeMetres = 0.05;
    int minimumObservations = 2;
    float minimumInputConfidence = 0.0f;
    int pixelStride = 1;
    int maximumProvenanceViews = 8;
};

struct DepthMapFusionResult
{
    bool success = false;
    std::string message;
    size_t validInputSampleCount = 0;
    size_t rejectedInputSampleCount = 0;
    size_t candidateVoxelCount = 0;
    size_t underSupportedVoxelCount = 0;
    std::vector<FusedDensePoint> points;
};

class DepthMapFusion
{
public:
    using DepthMapLoader = std::function<bool(
        size_t index, DenseDepthMap *depthMap, std::string *errorMessage)>;

    // Back-projects valid depth pixels into the reconstruction's world frame,
    // then deterministically merges samples in metric voxels. Each reference
    // camera contributes at most one weighted observation to a voxel so a
    // high-resolution view cannot overwhelm independent camera support.
    static DepthMapFusionResult fuse(
        const std::vector<DenseMvsView> &views,
        const std::vector<DenseDepthMap> &depthMaps,
        const DepthMapFusionOptions &options = {});

    // Consumes one depth map at a time. The loader may read a consistency
    // checkpoint into depthMap and release it after this call returns, keeping
    // fusion independent from the resident representation used by MVS.
    static DepthMapFusionResult fuseStreaming(
        const std::vector<DenseMvsView> &views,
        size_t depthMapCount,
        const DepthMapLoader &loader,
        const DepthMapFusionOptions &options = {});
};

} // namespace kestrel

#endif // DEPTHMAPFUSION_H
