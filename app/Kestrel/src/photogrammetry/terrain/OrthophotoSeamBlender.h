#ifndef ORTHOPHOTOSEAMBLENDER_H
#define ORTHOPHOTOSEAMBLENDER_H

#include <opencv2/core.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace kestrel {

struct OrthophotoBlendLayer
{
    int imageIndex = -1;
    cv::Mat image;
    cv::Mat quality; // CV_32FC1; positive values mark available pixels.
};

struct OrthophotoSeamBlendOptions
{
    int maximumIterations = 6;
    double qualityCostWeight = 1.0;
    double smoothnessCost = 0.35;
    double photometricSeamCostWeight = 0.75;
    int multibandLevels = 5; // Includes the full-resolution level.
};

struct OrthophotoSeamBlendResult
{
    bool success = false;
    std::string message;
    cv::Mat image;
    cv::Mat validityMask; // CV_8UC1.
    cv::Mat primarySourceImageIndex; // CV_32SC1; -1 means invalid.
    cv::Mat seamMask; // CV_8UC1; source-label boundaries are 255.
    int optimizationIterations = 0;
    size_t labelChangeCount = 0;
    size_t seamPixelCount = 0;
    size_t unavailableSourceFallbackCount = 0;
};

class OrthophotoSeamBlender
{
public:
    // Uses deterministic iterated conditional modes to minimize source
    // quality loss and label discontinuities. Boundary penalties rise where
    // candidate images disagree, pushing seams toward visually compatible
    // regions. The resulting hard labels drive a Laplacian-pyramid blend.
    static OrthophotoSeamBlendResult blend(
        const std::vector<OrthophotoBlendLayer> &layers,
        const OrthophotoSeamBlendOptions &options = {});

    // Blends layers using an existing CV_32SC1 map of imageIndex values. This
    // lets independently projected spectral bands share the exact source
    // ownership selected from a reference band. If the requested layer is not
    // available at a pixel, the best available layer is used and reported.
    static OrthophotoSeamBlendResult blendWithPrimarySources(
        const std::vector<OrthophotoBlendLayer> &layers,
        const cv::Mat &primarySourceImageIndex,
        int multibandLevels = 5);
};

} // namespace kestrel

#endif // ORTHOPHOTOSEAMBLENDER_H
