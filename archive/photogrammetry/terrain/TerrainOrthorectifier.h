#ifndef TERRAINORTHORECTIFIER_H
#define TERRAINORTHORECTIFIER_H

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <string>

namespace kestrel {

// A regularly spaced elevation surface in a local metric coordinate system.
// origin is the world position of elevation(0, 0), and spacing may use a
// negative Y component for north-up raster conventions.
struct TerrainGrid
{
    cv::Mat elevation; // CV_32FC1; non-finite cells are treated as missing.
    cv::Mat validityMask; // Optional CV_8UC1; zero cells are missing.
    cv::Point2d origin;
    cv::Vec2d spacing{1.0, 1.0};

    bool sample(double worldX, double worldY, double *height) const;
};

// Calibrated pinhole camera with Brown-Conrady lens distortion. World points
// are transformed with cameraPoint = worldToCameraRotation * worldPoint
// + worldToCameraTranslation.
struct PinholeCamera
{
    cv::Matx33d intrinsic = cv::Matx33d::eye();
    cv::Matx33d worldToCameraRotation = cv::Matx33d::eye();
    cv::Vec3d worldToCameraTranslation{0.0, 0.0, 0.0};
    cv::Vec<double, 5> distortion{0.0, 0.0, 0.0, 0.0, 0.0}; // k1,k2,p1,p2,k3

    bool project(const cv::Point3d &worldPoint, cv::Point2d *imagePoint) const;
};

// origin is the world coordinate of the centre of output pixel (0, 0).
struct OrthophotoGrid
{
    cv::Size size;
    cv::Point2d origin;
    cv::Vec2d pixelSize{1.0, 1.0};
};

class TerrainOrthorectifier
{
public:
    // Builds a north-up output grid that covers every terrain sample plus an
    // optional metric padding. The ground sample distance is metres per pixel.
    static bool createOutputGrid(const TerrainGrid &terrain,
                                 double groundSampleDistance,
                                 double padding,
                                 OrthophotoGrid *outputGrid,
                                 std::string *errorMessage = nullptr);

    // Inverse-projects each orthophoto pixel through the DSM and camera model,
    // then resamples the source once. validityMask is 255 only where both DSM
    // elevation and source-image projection are valid.
    static bool orthorectify(const cv::Mat &source,
                             const PinholeCamera &camera,
                             const TerrainGrid &terrain,
                             const OrthophotoGrid &outputGrid,
                             cv::Mat *orthophoto,
                             cv::Mat *validityMask,
                             std::string *errorMessage = nullptr,
                             int interpolation = cv::INTER_LANCZOS4);
};

} // namespace kestrel

#endif // TERRAINORTHORECTIFIER_H
