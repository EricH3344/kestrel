#include "photogrammetry/mvs/DensePointCloudFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>

namespace kestrel {

namespace {

constexpr double kMadScale = 1.4826;

struct CellKey
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    bool operator<(const CellKey &other) const
    {
        if (x != other.x) {
            return x < other.x;
        }
        if (y != other.y) {
            return y < other.y;
        }
        return z < other.z;
    }
};

bool finitePoint(const FusedDensePoint &point)
{
    return std::isfinite(point.position.x)
           && std::isfinite(point.position.y)
           && std::isfinite(point.position.z)
           && std::isfinite(point.confidence);
}

double median(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    double result = values[middle];
    if (values.size() % 2 == 0) {
        result = 0.5 * (result + *std::max_element(
            values.begin(), values.begin() + middle));
    }
    return result;
}

double medianAbsoluteDeviation(const std::vector<double> &values,
                               double centre)
{
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values) {
        deviations.push_back(std::abs(value - centre));
    }
    return median(std::move(deviations));
}

CellKey cellFor(const cv::Point3d &point, double cellSize,
                bool includeElevation)
{
    return {
        static_cast<std::int64_t>(std::floor(point.x / cellSize)),
        static_cast<std::int64_t>(std::floor(point.y / cellSize)),
        includeElevation
            ? static_cast<std::int64_t>(std::floor(point.z / cellSize)) : 0};
}

using SpatialIndex = std::map<CellKey, std::vector<int>>;

SpatialIndex buildIndex(const std::vector<FusedDensePoint> &points,
                        const std::vector<bool> &active,
                        double cellSize, bool includeElevation)
{
    SpatialIndex index;
    for (int pointIndex = 0;
         pointIndex < static_cast<int>(points.size()); ++pointIndex) {
        if (active[pointIndex]) {
            index[cellFor(points[pointIndex].position, cellSize,
                          includeElevation)].push_back(pointIndex);
        }
    }
    return index;
}

std::vector<int> neighbours(const std::vector<FusedDensePoint> &points,
                            const SpatialIndex &index, int pointIndex,
                            double radius, bool includeElevation)
{
    std::vector<int> result;
    const cv::Point3d &centre = points[pointIndex].position;
    const CellKey cell = cellFor(centre, radius, includeElevation);
    const int zStart = includeElevation ? -1 : 0;
    const int zEnd = includeElevation ? 1 : 0;
    const double squaredRadius = radius * radius;
    for (int zOffset = zStart; zOffset <= zEnd; ++zOffset) {
        for (int yOffset = -1; yOffset <= 1; ++yOffset) {
            for (int xOffset = -1; xOffset <= 1; ++xOffset) {
                const auto found = index.find({
                    cell.x + xOffset, cell.y + yOffset,
                    cell.z + zOffset});
                if (found == index.end()) {
                    continue;
                }
                for (int candidate : found->second) {
                    if (candidate == pointIndex) {
                        continue;
                    }
                    const cv::Point3d &value = points[candidate].position;
                    const double dx = value.x - centre.x;
                    const double dy = value.y - centre.y;
                    const double dz = includeElevation
                        ? value.z - centre.z : 0.0;
                    if (dx * dx + dy * dy + dz * dz <= squaredRadius) {
                        result.push_back(candidate);
                    }
                }
            }
        }
    }
    return result;
}

bool validOptions(const DensePointCloudFilterOptions &options)
{
    return std::isfinite(options.minimumConfidence)
           && options.minimumConfidence >= 0.0f
           && options.minimumConfidence <= 1.0f
           && std::isfinite(options.spatialSearchRadiusMetres)
           && options.spatialSearchRadiusMetres > 0.0
           && options.minimumSpatialNeighbours > 0
           && options.nearestNeighbourCount > 0
           && std::isfinite(options.spatialMadMultiplier)
           && options.spatialMadMultiplier >= 0.0
           && std::isfinite(options.maximumMeanNeighbourDistanceMetres)
           && options.maximumMeanNeighbourDistanceMetres >= 0.0
           && std::isfinite(options.elevationSearchRadiusMetres)
           && options.elevationSearchRadiusMetres > 0.0
           && options.minimumElevationNeighbours > 0
           && std::isfinite(options.elevationMadMultiplier)
           && options.elevationMadMultiplier >= 0.0
           && std::isfinite(options.minimumElevationToleranceMetres)
           && options.minimumElevationToleranceMetres >= 0.0;
}

} // namespace

DensePointCloudFilterResult DensePointCloudFilter::filter(
    const std::vector<FusedDensePoint> &points,
    const DensePointCloudFilterOptions &options)
{
    DensePointCloudFilterResult result;
    if (points.empty()) {
        result.message = "Dense point filtering requires input points.";
        return result;
    }
    if (!validOptions(options)) {
        result.message = "Dense point-filter options are invalid.";
        return result;
    }

    std::vector<bool> active(points.size(), true);
    for (size_t index = 0; index < points.size(); ++index) {
        if (!finitePoint(points[index])) {
            active[index] = false;
            ++result.invalidPointCount;
        } else if (points[index].confidence < options.minimumConfidence) {
            active[index] = false;
            ++result.lowConfidencePointCount;
        }
    }

    const SpatialIndex spatialIndex = buildIndex(
        points, active, options.spatialSearchRadiusMetres, true);
    std::vector<double> meanDistances(
        points.size(), std::numeric_limits<double>::quiet_NaN());
    std::vector<double> scorePopulation;
    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
        if (!active[index]) {
            continue;
        }
        const std::vector<int> nearby = neighbours(
            points, spatialIndex, index,
            options.spatialSearchRadiusMetres, true);
        if (static_cast<int>(nearby.size())
            < options.minimumSpatialNeighbours) {
            active[index] = false;
            ++result.isolatedPointCount;
            continue;
        }
        std::vector<double> distances;
        distances.reserve(nearby.size());
        for (int neighbour : nearby) {
            distances.push_back(cv::norm(
                points[neighbour].position - points[index].position));
        }
        const int retained = std::min(
            options.nearestNeighbourCount,
            static_cast<int>(distances.size()));
        std::nth_element(distances.begin(), distances.begin() + retained - 1,
                         distances.end());
        const double mean = std::accumulate(
            distances.begin(), distances.begin() + retained, 0.0) / retained;
        meanDistances[index] = mean;
        scorePopulation.push_back(mean);
    }

    if (!scorePopulation.empty()) {
        const double centre = median(scorePopulation);
        const double mad = medianAbsoluteDeviation(scorePopulation, centre);
        // Perfect grids can have a near-zero MAD while boundary points still
        // have slightly larger valid neighbour distances. Retain a modest
        // scale floor relative to the population median so the robust test
        // targets genuinely sparse clusters instead of trimming every edge.
        const double robustScale = std::max(
            kMadScale * mad, std::max(1e-9, 0.10 * centre));
        double threshold = centre
                           + options.spatialMadMultiplier * robustScale;
        if (options.maximumMeanNeighbourDistanceMetres > 0.0) {
            threshold = std::min(
                threshold, options.maximumMeanNeighbourDistanceMetres);
        }
        result.medianMeanNeighbourDistanceMetres = centre;
        result.spatialRejectionThresholdMetres = threshold;
        for (size_t index = 0; index < points.size(); ++index) {
            if (active[index] && meanDistances[index] > threshold) {
                active[index] = false;
                ++result.statisticalOutlierCount;
            }
        }
    }

    const SpatialIndex elevationIndex = buildIndex(
        points, active, options.elevationSearchRadiusMetres, false);
    std::vector<bool> elevationRejected(points.size(), false);
    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
        if (!active[index]) {
            continue;
        }
        const std::vector<int> nearby = neighbours(
            points, elevationIndex, index,
            options.elevationSearchRadiusMetres, false);
        if (static_cast<int>(nearby.size())
            < options.minimumElevationNeighbours) {
            continue;
        }
        std::vector<double> heights;
        heights.reserve(nearby.size());
        for (int neighbour : nearby) {
            heights.push_back(points[neighbour].position.z);
        }
        const double localMedian = median(heights);
        const double localMad = medianAbsoluteDeviation(heights, localMedian);
        const double tolerance = std::max(
            options.minimumElevationToleranceMetres,
            options.elevationMadMultiplier * kMadScale
                * std::max(localMad, 1e-9));
        if (std::abs(points[index].position.z - localMedian) > tolerance) {
            elevationRejected[index] = true;
        }
    }
    for (size_t index = 0; index < points.size(); ++index) {
        if (elevationRejected[index]) {
            active[index] = false;
            ++result.elevationOutlierCount;
        }
        if (active[index]) {
            result.points.push_back(points[index]);
        }
    }

    result.success = !result.points.empty();
    result.message = result.success
        ? "Dense point cloud passed spatial and local-elevation filtering."
        : "Dense point filtering rejected every input point.";
    return result;
}

} // namespace kestrel
