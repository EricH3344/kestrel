#include "photogrammetry/mvs/DensePointCloudFilter.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

kestrel::FusedDensePoint point(double x, double y, double z,
                               float confidence = 0.8f)
{
    kestrel::FusedDensePoint value;
    value.position = {x, y, z};
    value.confidence = confidence;
    value.observationCount = 3;
    value.sampleCount = 6;
    value.sources = {{0, {10.0f, 20.0f}, confidence},
                     {1, {11.0f, 21.0f}, confidence}};
    return value;
}

bool outlierRemovalTest()
{
    std::vector<kestrel::FusedDensePoint> input;
    for (int row = 0; row < 11; ++row) {
        for (int column = 0; column < 11; ++column) {
            const double x = column * 0.1;
            const double y = row * 0.1;
            const double z = 0.05 * x - 0.03 * y;
            input.push_back(point(x, y, z));
        }
    }
    // This point still has nearby 3D support but violates its local terrain.
    input[5 * 11 + 5].position.z += 0.30;
    input.push_back(point(10.0, 10.0, 5.0));
    input.push_back(point(20.0, 20.0, 0.0, 0.01f));

    kestrel::DensePointCloudFilterOptions options;
    options.minimumConfidence = 0.05f;
    options.spatialSearchRadiusMetres = 0.40;
    options.minimumSpatialNeighbours = 4;
    options.nearestNeighbourCount = 8;
    options.spatialMadMultiplier = 100.0;
    options.maximumMeanNeighbourDistanceMetres = 0.50;
    options.elevationSearchRadiusMetres = 0.30;
    options.minimumElevationNeighbours = 4;
    options.elevationMadMultiplier = 4.0;
    options.minimumElevationToleranceMetres = 0.08;
    const auto result = kestrel::DensePointCloudFilter::filter(input, options);
    if (!result.success || result.points.size() != 120) {
        std::cerr << "Filter result: kept=" << result.points.size()
                  << ", low-confidence=" << result.lowConfidencePointCount
                  << ", isolated=" << result.isolatedPointCount
                  << ", statistical=" << result.statisticalOutlierCount
                  << ", elevation=" << result.elevationOutlierCount
                  << ", threshold=" << result.spatialRejectionThresholdMetres
                  << '\n';
    }
    return expect(result.success && result.points.size() == 120,
                  "Filtering should retain the tilted plane without its spike.")
           && expect(result.lowConfidencePointCount == 1,
                     "The confidence gate should be reported separately.")
           && expect(result.isolatedPointCount == 1,
                     "The distant point should be rejected as isolated.")
           && expect(result.elevationOutlierCount == 1,
                     "The locally unsupported height spike should be rejected.")
           && expect(result.points.front().sources.size() == 2,
                     "Filtering must preserve source-camera provenance.");
}

bool statisticalDistanceTest()
{
    std::vector<kestrel::FusedDensePoint> input;
    for (int index = 0; index < 20; ++index) {
        input.push_back(point((index % 5) * 0.05,
                              (index / 5) * 0.05, 0.0));
    }
    // A sparse satellite cluster has enough neighbours to avoid the simple
    // isolation count, but its mean neighbour distance is anomalous.
    for (int index = 0; index < 5; ++index) {
        input.push_back(point(1.0 + index * 0.15, 0.0, 0.0));
    }
    kestrel::DensePointCloudFilterOptions options;
    options.spatialSearchRadiusMetres = 0.50;
    options.minimumSpatialNeighbours = 2;
    options.nearestNeighbourCount = 4;
    options.spatialMadMultiplier = 3.0;
    options.maximumMeanNeighbourDistanceMetres = 0.20;
    options.elevationSearchRadiusMetres = 0.30;
    options.minimumElevationNeighbours = 2;
    const auto result = kestrel::DensePointCloudFilter::filter(input, options);
    return expect(result.success && result.statisticalOutlierCount > 0,
                  "Robust mean-distance filtering should reject sparse clusters.");
}

bool validationTest()
{
    std::vector<kestrel::FusedDensePoint> input{point(0.0, 0.0, 0.0)};
    kestrel::DensePointCloudFilterOptions options;
    options.spatialSearchRadiusMetres = 0.0;
    const auto invalid = kestrel::DensePointCloudFilter::filter(input, options);
    const auto empty = kestrel::DensePointCloudFilter::filter({}, {});
    return expect(!invalid.success && !empty.success,
                  "Empty inputs and invalid filter options should fail safely.");
}

} // namespace

int main()
{
    if (outlierRemovalTest() && statisticalDistanceTest()
        && validationTest()) {
        std::cout << "Dense point-cloud filter tests passed.\n";
        return 0;
    }
    return 1;
}
