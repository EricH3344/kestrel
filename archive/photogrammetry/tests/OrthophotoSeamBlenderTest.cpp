#include "photogrammetry/terrain/OrthophotoSeamBlender.h"

#include <algorithm>
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

bool crossingQualityTest()
{
    constexpr int width = 64;
    constexpr int height = 16;
    cv::Mat firstImage(height, width, CV_32F, cv::Scalar(50.0f));
    cv::Mat secondImage(height, width, CV_32F, cv::Scalar(150.0f));
    cv::Mat firstQuality(height, width, CV_32F);
    cv::Mat secondQuality(height, width, CV_32F);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const float alpha = static_cast<float>(column) / (width - 1);
            firstQuality.at<float>(row, column) = std::max(0.05f, 1.0f - alpha);
            secondQuality.at<float>(row, column) = std::max(0.05f, alpha);
        }
    }
    kestrel::OrthophotoSeamBlendOptions options;
    options.smoothnessCost = 0.20;
    options.multibandLevels = 5;
    const auto result = kestrel::OrthophotoSeamBlender::blend(
        {{10, firstImage, firstQuality}, {20, secondImage, secondQuality}},
        options);
    const int row = height / 2;
    int changes = 0;
    for (int column = 1; column < width; ++column) {
        changes += result.success
            && result.primarySourceImageIndex.at<int>(row, column)
                   != result.primarySourceImageIndex.at<int>(row, column - 1);
    }
    return expect(result.success, "Crossing quality layers should blend.")
           && expect(cv::countNonZero(result.validityMask) == width * height,
                     "Positive-quality layers should cover the full output.")
           && expect(result.primarySourceImageIndex.at<int>(row, 0) == 10
                         && result.primarySourceImageIndex.at<int>(row, width - 1)
                                == 20,
                     "Quality should select opposite sources at the image edges.")
           && expect(changes == 1 && result.seamPixelCount > 0,
                     "The optimized labels should form one coherent boundary.")
           && expect(result.image.at<float>(row, 0) < 80.0f
                         && result.image.at<float>(row, width - 1) > 120.0f,
                     "The pyramid blend should preserve content away from the seam.");
}

bool deterministicTieTest()
{
    const cv::Size size(12, 8);
    cv::Mat quality(size, CV_32F, cv::Scalar(1.0f));
    const auto result = kestrel::OrthophotoSeamBlender::blend(
        {{20, cv::Mat(size, CV_8U, cv::Scalar(220)), quality},
         {10, cv::Mat(size, CV_8U, cv::Scalar(30)), quality}});
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(result.primarySourceImageIndex, &minimum, &maximum);
    return expect(result.success && minimum == 10.0 && maximum == 10.0,
                  "Equal quality should deterministically choose image index 10.")
           && expect(result.seamPixelCount == 0,
                     "A single selected source should have no seam.")
           && expect(result.image.type() == CV_8U
                         && result.image.at<uchar>(0, 0) == 30,
                     "Blending should preserve the input depth and selected value.");
}

bool fixedPhysicalCaptureLabelsTest()
{
    const cv::Size size(4, 2);
    cv::Mat firstQuality(size, CV_32F, cv::Scalar(1.0f));
    cv::Mat secondQuality(size, CV_32F, cv::Scalar(1.0f));
    secondQuality.at<float>(0, 3) = 0.0f;
    cv::Mat requested(size, CV_32S, cv::Scalar(10));
    requested.colRange(2, 4).setTo(20);
    const auto result =
        kestrel::OrthophotoSeamBlender::blendWithPrimarySources(
            {{10, cv::Mat(size, CV_8UC3, cv::Scalar(10, 20, 30)),
              firstQuality},
             {20, cv::Mat(size, CV_8UC3, cv::Scalar(100, 110, 120)),
              secondQuality}},
            requested, 1);
    return expect(result.success,
                  "A valid fixed physical-capture map should blend.")
           && expect(result.primarySourceImageIndex.at<int>(0, 2) == 20
                         && result.primarySourceImageIndex.at<int>(0, 3) == 10,
                     "Fixed labels should change only when their capture is unavailable.")
           && expect(result.unavailableSourceFallbackCount == 1,
                     "Unavailable fixed labels should report exact fallback count.")
           && expect(result.image.at<cv::Vec3b>(0, 2)
                         == cv::Vec3b(100, 110, 120)
                         && result.image.at<cv::Vec3b>(0, 3)
                                == cv::Vec3b(10, 20, 30),
                     "Fixed-label output should use the selected physical capture.");
}

bool validationTest()
{
    kestrel::OrthophotoSeamBlendOptions invalidOptions;
    invalidOptions.multibandLevels = 0;
    const auto empty = kestrel::OrthophotoSeamBlender::blend({});
    const auto invalid = kestrel::OrthophotoSeamBlender::blend(
        {{0, cv::Mat::ones(2, 2, CV_32F),
          cv::Mat::ones(2, 2, CV_32F)}}, invalidOptions);
    return expect(!empty.success && !invalid.success,
                  "Missing layers and invalid options should fail safely.");
}

} // namespace

int main()
{
    if (crossingQualityTest() && deterministicTieTest()
        && fixedPhysicalCaptureLabelsTest() && validationTest()) {
        std::cout << "Orthophoto seam blending tests passed.\n";
        return 0;
    }
    return 1;
}
