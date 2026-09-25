#ifndef DENSEPOINTCLOUDFILTER_H
#define DENSEPOINTCLOUDFILTER_H

#include "photogrammetry/mvs/DepthMapFusion.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kestrel {

struct DensePointCloudFilterOptions
{
    float minimumConfidence = 0.0f;
    double spatialSearchRadiusMetres = 0.30;
    int minimumSpatialNeighbours = 5;
    int nearestNeighbourCount = 12;
    double spatialMadMultiplier = 3.5;
    // Zero disables this optional absolute cap; the MAD threshold still
    // applies and adapts to the point-cloud resolution.
    double maximumMeanNeighbourDistanceMetres = 0.0;
    double elevationSearchRadiusMetres = 0.40;
    int minimumElevationNeighbours = 5;
    double elevationMadMultiplier = 4.0;
    double minimumElevationToleranceMetres = 0.20;
};

struct DensePointCloudFilterResult
{
    bool success = false;
    std::string message;
    size_t invalidPointCount = 0;
    size_t lowConfidencePointCount = 0;
    size_t isolatedPointCount = 0;
    size_t statisticalOutlierCount = 0;
    size_t elevationOutlierCount = 0;
    double medianMeanNeighbourDistanceMetres = 0.0;
    double spatialRejectionThresholdMetres = 0.0;
    std::vector<FusedDensePoint> points;
};

class DensePointCloudFilter
{
public:
    // Removes points without local 3D support, rejects globally unusual mean
    // neighbour distances with a robust MAD threshold, and then rejects local
    // elevation spikes in XY neighbourhoods. Surviving point records are
    // copied intact, including camera/source-pixel provenance.
    static DensePointCloudFilterResult filter(
        const std::vector<FusedDensePoint> &points,
        const DensePointCloudFilterOptions &options = {});
};

} // namespace kestrel

#endif // DENSEPOINTCLOUDFILTER_H
