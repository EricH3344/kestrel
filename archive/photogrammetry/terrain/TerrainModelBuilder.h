#ifndef TERRAINMODELBUILDER_H
#define TERRAINMODELBUILDER_H

#include "photogrammetry/terrain/TerrainOrthorectifier.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace kestrel {

struct TerrainPoint
{
    cv::Point3d position;
};

struct DsmGridDefinition
{
    cv::Size size;
    cv::Point2d origin; // Centre of cell (0, 0).
    cv::Vec2d spacing{1.0, -1.0};
    int minimumSamplesPerCell = 1;
};

class TerrainModelBuilder
{
public:
    // Rasterizes the top surface of a dense point cloud. Each valid DSM cell
    // contains its highest finite sample; empty/undersampled cells remain NaN.
    // sampleCounts is CV_32SC1 and can be retained as a confidence measure.
    static bool rasterizeDsm(const std::vector<TerrainPoint> &points,
                             const DsmGridDefinition &definition,
                             TerrainGrid *terrain,
                             cv::Mat *sampleCounts,
                             std::string *errorMessage = nullptr);

    // Fills only invalid components that are enclosed by valid terrain and no
    // larger than maximumHoleCells. Border-connected and large gaps remain
    // invalid. filledMask is optional and marks synthesized cells with 255.
    static bool fillBoundedHoles(TerrainGrid *terrain,
                                 int maximumHoleCells,
                                 cv::Mat *filledMask = nullptr,
                                 std::string *errorMessage = nullptr);
};

} // namespace kestrel

#endif // TERRAINMODELBUILDER_H
