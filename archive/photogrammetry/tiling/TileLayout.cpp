#include "photogrammetry/tiling/TileLayout.h"

#include <algorithm>

namespace kestrel {

bool TileLayout::build(const cv::Size &canvasSize,
                       const cv::Size &maximumCoreSize,
                       int haloPixels,
                       std::vector<ProcessingTile> *tiles,
                       std::string *errorMessage)
{
    if (!tiles || canvasSize.width <= 0 || canvasSize.height <= 0
        || maximumCoreSize.width <= 0 || maximumCoreSize.height <= 0
        || haloPixels < 0) {
        if (errorMessage) {
            *errorMessage = "Tile layout requires positive canvas/core sizes and a non-negative halo.";
        }
        return false;
    }
    tiles->clear();
    const cv::Rect canvas(0, 0, canvasSize.width, canvasSize.height);
    int index = 0;
    for (int y = 0; y < canvasSize.height; y += maximumCoreSize.height) {
        const int height = std::min(maximumCoreSize.height,
                                    canvasSize.height - y);
        for (int x = 0; x < canvasSize.width; x += maximumCoreSize.width) {
            const int width = std::min(maximumCoreSize.width,
                                       canvasSize.width - x);
            const cv::Rect core(x, y, width, height);
            const cv::Rect requested(
                x - haloPixels, y - haloPixels,
                width + 2 * haloPixels, height + 2 * haloPixels);
            tiles->push_back({index++, core, requested & canvas});
        }
    }
    return !tiles->empty();
}

} // namespace kestrel
