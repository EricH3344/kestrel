#ifndef TILELAYOUT_H
#define TILELAYOUT_H

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace kestrel {

struct ProcessingTile
{
    int index = -1;
    cv::Rect core; // Non-overlapping ownership region in canvas coordinates.
    cv::Rect expanded; // Core plus clipped processing halo.

    cv::Rect coreInExpanded() const
    {
        return {core.x - expanded.x, core.y - expanded.y,
                core.width, core.height};
    }
};

class TileLayout
{
public:
    // Builds deterministic row-major tiles. Core rectangles partition the
    // canvas exactly once; expanded rectangles provide context for local
    // filters, pyramids, seam optimization, and blending.
    static bool build(const cv::Size &canvasSize,
                      const cv::Size &maximumCoreSize,
                      int haloPixels,
                      std::vector<ProcessingTile> *tiles,
                      std::string *errorMessage = nullptr);
};

} // namespace kestrel

#endif // TILELAYOUT_H
