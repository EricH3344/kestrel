#include "photogrammetry/mvs/DensePointCloudBackend.h"

#include "photogrammetry/mvs/DenseReconstruction.h"

#include <utility>

namespace kestrel {

DensePointCloudReconstructionResult PlaneSweepPointCloudBackend::reconstruct(
    const DenseReconstructionInput &input,
    const DensePointCloudReconstructionOptions &options) const
{
    DensePointCloudReconstructionResult result;
    result.backendName = "Kestrel plane sweep";

    const PlaneSweepMvsBackend backend;
    const DenseReconstructionResult dense = backend.reconstruct(
        input, options.planeSweep);
    if (!dense.success) {
        result.message = dense.message;
        return result;
    }

    DepthMapFusionResult fusion;
    if (options.planeSweep.streamConsistencyFromCheckpoints) {
        fusion = DepthMapFusion::fuseStreaming(
            input.views, dense.depthMaps.size(),
            [&dense, &options](size_t index, DenseDepthMap *map,
                               std::string *errorMessage) {
                return PlaneSweepMvsBackend::loadConsistentDepthMap(
                    dense.depthMaps, index, options.planeSweep, map,
                    errorMessage);
            },
            options.planeSweepFusion);
    } else {
        fusion = DepthMapFusion::fuse(
            input.views, dense.depthMaps, options.planeSweepFusion);
    }
    if (!fusion.success) {
        result.message = fusion.message;
        return result;
    }

    result.success = true;
    result.points = std::move(fusion.points);
    result.message = "Kestrel plane-sweep dense reconstruction produced "
        + std::to_string(result.points.size()) + " fused points.";
    return result;
}

std::unique_ptr<DensePointCloudBackend>
DensePointCloudReconstructor::createBackend(DensePointCloudBackendKind backend)
{
    switch (backend) {
    case DensePointCloudBackendKind::OpenMvs:
        return std::make_unique<OpenMvsPointCloudBackend>();
    case DensePointCloudBackendKind::PlaneSweep:
    case DensePointCloudBackendKind::PreferOpenMvs:
        return std::make_unique<PlaneSweepPointCloudBackend>();
    }
    return {};
}

DensePointCloudReconstructionResult DensePointCloudReconstructor::reconstruct(
    const DenseReconstructionInput &input,
    DensePointCloudBackendKind backend,
    const DensePointCloudReconstructionOptions &options)
{
    if (backend != DensePointCloudBackendKind::PreferOpenMvs) {
        std::unique_ptr<DensePointCloudBackend> selected = createBackend(backend);
        return selected->reconstruct(input, options);
    }

    std::string availabilityReason;
    if (OpenMvsPointCloudBackend::isAvailable(options,
                                               &availabilityReason)) {
        const OpenMvsPointCloudBackend openMvs;
        DensePointCloudReconstructionResult result = openMvs.reconstruct(
            input, options);
        if (result.success || !options.allowPlaneSweepFallback) {
            return result;
        }
        availabilityReason = result.message;
    } else if (!options.allowPlaneSweepFallback) {
        DensePointCloudReconstructionResult result;
        result.backendName = "OpenMVS";
        result.message = availabilityReason;
        return result;
    }

    const PlaneSweepPointCloudBackend planeSweep;
    DensePointCloudReconstructionResult fallback = planeSweep.reconstruct(
        input, options);
    fallback.usedFallback = true;
    if (fallback.success) {
        fallback.message = "OpenMVS was unavailable or failed ("
            + availabilityReason + "); " + fallback.message;
    } else {
        fallback.message = "OpenMVS was unavailable or failed ("
            + availabilityReason + "); plane-sweep fallback also failed: "
            + fallback.message;
    }
    return fallback;
}

} // namespace kestrel
