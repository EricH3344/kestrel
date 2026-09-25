#ifndef DENSEPOINTCLOUDBACKEND_H
#define DENSEPOINTCLOUDBACKEND_H

#include "photogrammetry/mvs/DepthMapFusion.h"

#include <memory>
#include <string>
#include <vector>

namespace kestrel {

enum class DensePointCloudBackendKind
{
    PlaneSweep,
    OpenMvs,
    PreferOpenMvs
};

struct DensePointCloudReconstructionOptions
{
    DenseMvsOptions planeSweep;
    DepthMapFusionOptions planeSweepFusion;

    // OpenMVS writes restartable scene/depth artifacts here. The directory is
    // required for the OpenMVS backend and should be project-specific.
    std::string workspaceDirectory;
    // Empty enables discovery beside Kestrel, through KESTREL_OPENMVS_BIN,
    // through the build-time bundled location, and finally on PATH.
    std::string openMvsExecutableDirectory;
    int openMvsResolutionLevel = 0;
    int openMvsMaximumResolution = 8192;
    int openMvsMinimumResolution = 320;
    int openMvsNumberViews = 8;
    int openMvsMinimumViewsFuse = 3;
    int openMvsGeometricIterations = 2;
    int openMvsMaximumThreads = 0;
    // Zero permits a full-flight process to run without an artificial limit.
    int openMvsProcessTimeoutMilliseconds = 0;
    bool allowPlaneSweepFallback = true;
};

struct DensePointCloudReconstructionResult
{
    bool success = false;
    bool usedFallback = false;
    std::string backendName;
    std::string message;
    std::string processLog;
    std::string workspaceDirectory;
    std::string densePointCloudPath;
    std::vector<FusedDensePoint> points;
};

class DensePointCloudBackend
{
public:
    virtual ~DensePointCloudBackend() = default;
    virtual DensePointCloudReconstructionResult reconstruct(
        const DenseReconstructionInput &input,
        const DensePointCloudReconstructionOptions &options = {}) const = 0;
};

// Adapter that deliberately retains Kestrel's in-house plane-sweep depth-map
// estimator and existing confidence-aware fusion behind the same point-cloud
// boundary used by OpenMVS.
class PlaneSweepPointCloudBackend final : public DensePointCloudBackend
{
public:
    DensePointCloudReconstructionResult reconstruct(
        const DenseReconstructionInput &input,
        const DensePointCloudReconstructionOptions &options = {}) const override;
};

class OpenMvsPointCloudBackend final : public DensePointCloudBackend
{
public:
    DensePointCloudReconstructionResult reconstruct(
        const DenseReconstructionInput &input,
        const DensePointCloudReconstructionOptions &options = {}) const override;

    static bool isAvailable(
        const DensePointCloudReconstructionOptions &options = {},
        std::string *reason = nullptr);

    // Public for deterministic format-contract tests. Production callers
    // normally use reconstruct().
    static bool exportColmapProject(
        const DenseReconstructionInput &input,
        const std::string &workspaceDirectory,
        std::string *errorMessage = nullptr);
    static bool loadDensePointCloud(
        const std::string &filePath,
        const std::vector<DenseMvsView> &views,
        std::vector<FusedDensePoint> *points,
        std::string *errorMessage = nullptr);
};

class DensePointCloudReconstructor
{
public:
    static DensePointCloudReconstructionResult reconstruct(
        const DenseReconstructionInput &input,
        DensePointCloudBackendKind backend,
        const DensePointCloudReconstructionOptions &options = {});

    static std::unique_ptr<DensePointCloudBackend> createBackend(
        DensePointCloudBackendKind backend);
};

} // namespace kestrel

#endif // DENSEPOINTCLOUDBACKEND_H
