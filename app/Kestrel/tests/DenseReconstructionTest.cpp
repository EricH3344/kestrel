#include "photogrammetry/mvs/DenseReconstruction.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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

kestrel::CameraCalibration calibration()
{
    kestrel::CameraCalibration value;
    value.imageSize = {96, 72};
    value.intrinsic = cv::Matx33d(120.0, 0.0, 47.5,
                                  0.0, 120.0, 35.5,
                                  0.0, 0.0, 1.0);
    value.quality = kestrel::CalibrationQuality::Approximate;
    value.source = "synthetic";
    return value;
}

kestrel::SparseCameraPose camera(int imageIndex,
                                 const cv::Point3d &centre)
{
    return {imageIndex, cv::Matx33d::eye(),
            cv::Vec3d(-centre.x, -centre.y, -centre.z),
            50, 0.1, 0};
}

cv::Matx33d homography(const kestrel::DenseMvsView &reference,
                       const kestrel::DenseMvsView &source,
                       double depth)
{
    const cv::Matx33d relativeRotation =
        source.pose.worldToCameraRotation
        * reference.pose.worldToCameraRotation.t();
    const cv::Vec3d relativeTranslation =
        source.pose.worldToCameraTranslation
        - relativeRotation * reference.pose.worldToCameraTranslation;
    cv::Matx33d plane = relativeRotation;
    for (int row = 0; row < 3; ++row) {
        plane(row, 2) += relativeTranslation[row] / depth;
    }
    return source.calibration.intrinsic * plane
           * reference.calibration.intrinsic.inv();
}

kestrel::DenseReconstructionInput syntheticScene()
{
    constexpr double planeDepth = 8.0;
    kestrel::DenseReconstructionInput input;
    const auto model = calibration();
    input.views = {
        {0, {}, model, camera(0, {0.0, 0.0, 0.0})},
        {1, {}, model, camera(1, {0.40, 0.0, 0.0})},
        {2, {}, model, camera(2, {-0.35, 0.05, 0.0})},
        {3, {}, model, camera(3, {10.0, 0.0, 0.0})}};

    cv::RNG random(1337);
    cv::Mat texture(model.imageSize, CV_32F);
    random.fill(texture, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(texture, texture, {3, 3}, 0.7);
    input.views[0].image = texture;
    for (int index = 1; index < static_cast<int>(input.views.size()); ++index) {
        cv::warpPerspective(texture, input.views[index].image,
                            cv::Mat(homography(input.views[0],
                                               input.views[index],
                                               planeDepth)),
                            model.imageSize, cv::INTER_LINEAR,
                            cv::BORDER_CONSTANT, cv::Scalar(0));
    }

    int trackId = 0;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 10; ++column) {
            const cv::Point3d point(-2.6 + column * 0.58,
                                    -1.9 + row * 0.54,
                                    planeDepth);
            input.sparsePoints.push_back({trackId, point, 0.1, 3.0, 0});
            kestrel::FeatureTrack track;
            track.id = trackId;
            for (int image = 0; image < 4; ++image) {
                track.observations.push_back({image, trackId * 10 + image,
                                              {0.0, 0.0}});
            }
            input.tracks.push_back(std::move(track));
            ++trackId;
        }
    }
    return input;
}

bool neighbourAndRangeTest()
{
    const auto input = syntheticScene();
    kestrel::DenseMvsOptions options;
    options.minimumSharedSparsePoints = 20;
    options.minimumTriangulationAngleDegrees = 0.2;
    options.maximumTriangulationAngleDegrees = 20.0;
    options.maximumNeighbours = 2;
    const auto neighbours =
        kestrel::PlaneSweepMvsBackend::selectNeighbours(input, options);
    const auto range = kestrel::PlaneSweepMvsBackend::estimateDepthRange(
        input.views[0], input.sparsePoints, input.tracks, options);
    return expect(neighbours.size() == 4,
                  "Neighbour selection should return one list per view.")
           && expect(neighbours[0].size() == 2,
                     "The reference should retain its two useful baselines.")
           && expect(neighbours[0][0].imageIndex != 3
                         && neighbours[0][1].imageIndex != 3,
                     "An excessive-baseline camera should be rejected.")
           && expect(neighbours[0][0].sharedSparsePointCount == 80,
                     "Shared sparse support should be retained.")
           && expect(range.valid && range.supportingPointCount == 80,
                     "Sparse observations should define a depth range.")
           && expect(range.minimumDepth < 8.0
                         && range.maximumDepth > 8.0,
                     "The robust depth interval should contain the plane.");
}

bool planeSweepTest()
{
    const auto input = syntheticScene();
    kestrel::DenseMvsOptions options;
    options.maximumImageDimension = 96;
    options.pyramidLevels = 2;
    options.depthHypotheses = 64;
    options.patchRadius = 2;
    options.minimumSupportingViews = 2;
    options.minimumConfidence = 0.005;
    kestrel::DenseDepthRange range;
    range.valid = true;
    range.minimumDepth = 7.0;
    range.maximumDepth = 9.0;
    range.supportingPointCount = 80;
    std::vector<const kestrel::DenseMvsView *> neighbours{
        &input.views[1], &input.views[2]};
    kestrel::DenseDepthMap map;
    std::string error;
    if (!kestrel::PlaneSweepMvsBackend::computeDepthMap(
            input.views[0], neighbours, range, options, &map, &error)) {
        std::cerr << "Dense MVS error: " << error << '\n';
        return false;
    }
    std::vector<float> validDepths;
    double confidenceSum = 0.0;
    for (int row = 8; row < map.depth.rows - 8; ++row) {
        for (int column = 8; column < map.depth.cols - 8; ++column) {
            const float value = map.depth.at<float>(row, column);
            if (map.validityMask.at<uchar>(row, column)
                && std::isfinite(value)) {
                validDepths.push_back(value);
                confidenceSum += map.confidence.at<float>(row, column);
            }
        }
    }
    if (validDepths.empty()) {
        return expect(false, "Plane sweep should recover valid central pixels.");
    }
    const size_t middle = validDepths.size() / 2;
    std::nth_element(validDepths.begin(), validDepths.begin() + middle,
                     validDepths.end());
    const double recovered = validDepths[middle];
    const int centralPixels = (map.depth.rows - 16) * (map.depth.cols - 16);
    return expect(map.depth.type() == CV_32FC1
                      && map.confidence.type() == CV_32FC1
                      && map.validityMask.type() == CV_8UC1,
                  "Depth artifacts should use the documented matrix types.")
           && expect(validDepths.size()
                         > static_cast<size_t>(centralPixels * 0.55),
                     "The textured plane should have useful depth coverage.")
           && expect(std::abs(recovered - 8.0) < 0.12,
                     "Plane sweep should recover the known plane depth.")
           && expect(confidenceSum / validDepths.size() > 0.005,
                     "Recovered depths should carry positive confidence.");
}

bool tiledPlaneSweepAndResumeTest()
{
    const auto input = syntheticScene();
    kestrel::DenseMvsOptions options;
    options.maximumImageDimension = 96;
    options.pyramidLevels = 2;
    options.depthHypotheses = 32;
    options.patchRadius = 2;
    options.minimumSupportingViews = 2;
    kestrel::DenseDepthRange range;
    range.valid = true;
    range.minimumDepth = 7.0;
    range.maximumDepth = 9.0;
    range.supportingPointCount = 80;
    std::vector<const kestrel::DenseMvsView *> neighbours{
        &input.views[1], &input.views[2]};
    kestrel::DenseDepthMap untiled;
    std::string error;
    if (!kestrel::PlaneSweepMvsBackend::computeDepthMap(
            input.views[0], neighbours, range, options, &untiled, &error)) {
        std::cerr << "Untiled MVS error: " << error << '\n';
        return false;
    }

    options.tileSizePixels = 32;
    kestrel::DenseDepthMap tiled;
    if (!kestrel::PlaneSweepMvsBackend::computeDepthMap(
            input.views[0], neighbours, range, options, &tiled, &error)) {
        std::cerr << "Tiled MVS error: " << error << '\n';
        return false;
    }
    cv::Mat comparable = untiled.validityMask & tiled.validityMask;
    double maximumDepthDifference = 0.0;
    double maximumConfidenceDifference = 0.0;
    cv::minMaxLoc(cv::abs(untiled.depth - tiled.depth), nullptr,
                  &maximumDepthDifference, nullptr, nullptr, comparable);
    cv::minMaxLoc(cv::abs(untiled.confidence - tiled.confidence), nullptr,
                  &maximumConfidenceDifference, nullptr, nullptr, comparable);
    if (!expect(cv::countNonZero(untiled.validityMask != tiled.validityMask) == 0,
                "Tiled MVS should preserve the untiled validity mask.")
        || !expect(maximumDepthDifference < 1e-5
                       && maximumConfidenceDifference < 1e-5,
                   "Tiled MVS should match untiled depths and confidence.")) {
        std::cerr << "Tile differences: depth=" << maximumDepthDifference
                  << ", confidence=" << maximumConfidenceDifference << '\n';
        return false;
    }

    const std::filesystem::path checkpointDirectory =
        std::filesystem::current_path() / "dense_mvs_checkpoint_test";
    std::error_code filesystemError;
    std::filesystem::remove_all(checkpointDirectory, filesystemError);
    options.checkpointDirectory = checkpointDirectory.string();
    kestrel::DenseDepthMap firstRun;
    kestrel::DenseDepthMap resumed;
    const bool wrote = kestrel::PlaneSweepMvsBackend::computeDepthMap(
        input.views[0], neighbours, range, options, &firstRun, &error);
    const bool reused = wrote
        && kestrel::PlaneSweepMvsBackend::computeDepthMap(
            input.views[0], neighbours, range, options, &resumed, &error);
    auto changedOptions = options;
    ++changedOptions.depthHypotheses;
    kestrel::DenseDepthMap recomputed;
    const bool rejectedStale = reused
        && kestrel::PlaneSweepMvsBackend::computeDepthMap(
            input.views[0], neighbours, range, changedOptions,
            &recomputed, &error);
    std::filesystem::remove_all(checkpointDirectory, filesystemError);
    auto invalidHalo = options;
    invalidHalo.checkpointDirectory.clear();
    invalidHalo.tileHaloPixels = 1;
    kestrel::DenseDepthMap invalid;
    const bool acceptedInvalidHalo =
        kestrel::PlaneSweepMvsBackend::computeDepthMap(
            input.views[0], neighbours, range, invalidHalo, &invalid, &error);
    return expect(tiled.processedTileCount == 9
                      && tiled.resumedTileCount == 0,
                  "A 96-by-72 image should use nine 32-pixel core tiles.")
           && expect(wrote && firstRun.resumedTileCount == 0,
                     "The first checkpointed run should compute every tile.")
           && expect(reused
                         && resumed.resumedTileCount
                                == resumed.processedTileCount,
                     "A matching second run should resume every tile.")
           && expect(rejectedStale && recomputed.resumedTileCount == 0,
                     "Changed algorithm settings must invalidate stale tile checkpoints.")
           && expect(!acceptedInvalidHalo,
                     "A halo below pyramid support must be rejected.");
}

std::vector<kestrel::DenseDepthMap> constantPlaneMaps(double depth)
{
    std::vector<kestrel::DenseDepthMap> maps;
    for (int image = 0; image < 3; ++image) {
        kestrel::DenseDepthMap map;
        map.imageIndex = image;
        map.depth = cv::Mat(72, 96, CV_32F, cv::Scalar(depth)).clone();
        map.confidence = cv::Mat(72, 96, CV_32F, cv::Scalar(0.8)).clone();
        map.validityMask = cv::Mat(72, 96, CV_8U, cv::Scalar(255)).clone();
        for (int neighbour = 0; neighbour < 3; ++neighbour) {
            if (neighbour != image) {
                map.neighbourImageIndices.push_back(neighbour);
            }
        }
        maps.push_back(std::move(map));
    }
    return maps;
}

bool consistencyAndOcclusionTest()
{
    const auto input = syntheticScene();
    std::vector<kestrel::DenseMvsView> views(
        input.views.begin(), input.views.begin() + 3);
    kestrel::DenseMvsOptions options;
    options.minimumConfidence = 0.1;
    options.minimumConsistencyChecks = 1;
    options.minimumConsistentViews = 1;

    auto consistent = constantPlaneMaps(8.0);
    std::string error;
    if (!kestrel::PlaneSweepMvsBackend::applyMultiViewConsistency(
            views, &consistent, options, &error)) {
        std::cerr << "Consistency error: " << error << '\n';
        return false;
    }
    const cv::Point centre(48, 36);
    if (!expect(consistent[0].validityMask.at<uchar>(centre) == 255
                    && consistent[0].consistentViewCount.at<uchar>(centre) == 2,
                "A shared plane should pass both neighbouring depth checks.")) {
        return false;
    }

    auto occluded = constantPlaneMaps(8.0);
    occluded[1].depth.setTo(6.0f);
    if (!kestrel::PlaneSweepMvsBackend::applyMultiViewConsistency(
            views, &occluded, options, &error)) {
        return false;
    }
    if (!expect(occluded[0].validityMask.at<uchar>(centre) == 255
                    && occluded[0].occludedViewCount.at<uchar>(centre) == 1
                    && occluded[0].consistentViewCount.at<uchar>(centre) == 1,
                "A closer source surface should count as occlusion, not disagreement.")) {
        return false;
    }

    auto contradictory = constantPlaneMaps(8.0);
    contradictory[1].depth.setTo(10.0f);
    contradictory[2].depth.setTo(10.0f);
    if (!kestrel::PlaneSweepMvsBackend::applyMultiViewConsistency(
            views, &contradictory, options, &error)) {
        return false;
    }
    return expect(contradictory[0].validityMask.at<uchar>(centre) == 0
                      && contradictory[0].consistentViewCount.at<uchar>(centre) == 0,
                  "Contradictory farther depths should invalidate the reference depth.");
}

bool backendContractTest()
{
    const auto input = syntheticScene();
    kestrel::DenseMvsOptions options;
    options.minimumSharedSparsePoints = 20;
    options.minimumTriangulationAngleDegrees = 0.2;
    options.maximumTriangulationAngleDegrees = 20.0;
    options.maximumNeighbours = 2;
    options.maximumImageDimension = 96;
    options.pyramidLevels = 2;
    options.depthHypotheses = 64;
    options.minimumSupportingViews = 2;
    options.minimumConfidence = 0.005;
    const kestrel::PlaneSweepMvsBackend backend;
    const auto result = backend.reconstruct(input, options);
    if (!result.success) {
        std::cerr << "Backend result: " << result.message
                  << ", raw=" << result.rawValidPixelCount
                  << ", consistent=" << result.consistentPixelCount
                  << ", maps=" << result.depthMaps.size() << '\n';
    }
    return expect(result.success,
                  "The backend interface should produce depth artifacts.")
           && expect(result.depthMaps.size() == 3,
                     "Only the three mutually supported views should process.")
           && expect(result.depthMaps.front().neighbourImageIndices.size() == 2,
                     "Depth artifacts should retain source-view provenance.");
}

bool streamedCheckpointConsistencyTest()
{
    const auto input = syntheticScene();
    kestrel::DenseMvsOptions options;
    options.minimumSharedSparsePoints = 20;
    options.minimumTriangulationAngleDegrees = 0.2;
    options.maximumTriangulationAngleDegrees = 20.0;
    options.maximumNeighbours = 2;
    options.maximumImageDimension = 96;
    options.pyramidLevels = 2;
    options.depthHypotheses = 64;
    options.minimumSupportingViews = 2;
    options.minimumConfidence = 0.005;
    options.tileSizePixels = 32;
    const kestrel::PlaneSweepMvsBackend backend;
    const auto resident = backend.reconstruct(input, options);
    if (!expect(resident.success, "Resident consistency baseline should succeed.")) {
        return false;
    }

    const std::filesystem::path checkpointDirectory =
        std::filesystem::current_path()
        / "dense_streamed_consistency_checkpoint_test";
    std::error_code filesystemError;
    std::filesystem::remove_all(checkpointDirectory, filesystemError);
    auto streamedOptions = options;
    streamedOptions.checkpointDirectory = checkpointDirectory.string();
    streamedOptions.streamConsistencyFromCheckpoints = true;
    const auto streamed = backend.reconstruct(input, streamedOptions);
    if (!expect(streamed.success
                    && streamed.depthMaps.size() == resident.depthMaps.size(),
                "Checkpoint-backed consistency should produce every resident map.")) {
        std::filesystem::remove_all(checkpointDirectory, filesystemError);
        return false;
    }
    for (size_t index = 0; index < streamed.depthMaps.size(); ++index) {
        if (!expect(streamed.depthMaps[index].depth.empty()
                        && streamed.depthMaps[index].validityMask.empty(),
                    "Streamed reconstruction should retain descriptors, not resident pixels.")) {
            std::filesystem::remove_all(checkpointDirectory, filesystemError);
            return false;
        }
        kestrel::DenseDepthMap loaded;
        std::string error;
        if (!expect(kestrel::PlaneSweepMvsBackend::loadConsistentDepthMap(
                        streamed.depthMaps, index, streamedOptions,
                        &loaded, &error),
                    error.c_str())) {
            std::filesystem::remove_all(checkpointDirectory, filesystemError);
            return false;
        }
        const auto residentMap = std::find_if(
            resident.depthMaps.begin(), resident.depthMaps.end(),
            [&loaded](const kestrel::DenseDepthMap &map) {
                return map.imageIndex == loaded.imageIndex;
            });
        if (!expect(residentMap != resident.depthMaps.end()
                        && cv::countNonZero(
                            residentMap->validityMask
                            != loaded.validityMask) == 0
                        && cv::norm(residentMap->depth, loaded.depth,
                                    cv::NORM_INF) == 0.0
                        && cv::norm(residentMap->confidence,
                                    loaded.confidence,
                                    cv::NORM_INF) == 0.0
                        && cv::countNonZero(
                            residentMap->consistentViewCount
                            != loaded.consistentViewCount) == 0
                        && cv::countNonZero(
                            residentMap->occludedViewCount
                            != loaded.occludedViewCount) == 0,
                    "Loaded consistency tiles must exactly reproduce resident output.")) {
            std::filesystem::remove_all(checkpointDirectory, filesystemError);
            return false;
        }
    }

    const std::filesystem::path corruptTile = checkpointDirectory
        / "consistent" / "image_0" / "tile_0_0_32x32.yml.gz";
    {
        std::ofstream truncate(corruptTile,
                               std::ios::binary | std::ios::trunc);
    }
    const auto recovered = backend.reconstruct(input, streamedOptions);
    const auto resumed = backend.reconstruct(input, streamedOptions);
    auto changedOptions = streamedOptions;
    changedOptions.minimumConfidence *= 2.0;
    const auto staleRejected = backend.reconstruct(input, changedOptions);
    std::filesystem::remove_all(checkpointDirectory, filesystemError);
    return expect(recovered.success
                      && recovered.resumedTileCount
                            == recovered.processedTileCount
                      && recovered.resumedConsistencyTileCount > 0
                      && recovered.resumedConsistencyTileCount
                            < recovered.processedConsistencyTileCount,
                  "A corrupt consistency tile should be safely recomputed while other maps resume.")
           && expect(resumed.success
                      && resumed.resumedTileCount
                            == resumed.processedTileCount
                      && resumed.resumedConsistencyTileCount
                            == resumed.processedConsistencyTileCount,
                  "A repeated streamed run should resume raw and consistency tiles.")
           && expect(staleRejected.success
                         && staleRejected.resumedTileCount
                                == staleRejected.processedTileCount
                         && staleRejected.resumedConsistencyTileCount == 0,
                     "Changed consistency settings must reuse raw tiles but reject stale filtered tiles.");
}

} // namespace

int main()
{
    if (neighbourAndRangeTest() && planeSweepTest()
        && tiledPlaneSweepAndResumeTest() && consistencyAndOcclusionTest()
        && backendContractTest() && streamedCheckpointConsistencyTest()) {
        std::cout << "Dense reconstruction tests passed.\n";
        return 0;
    }
    return 1;
}
