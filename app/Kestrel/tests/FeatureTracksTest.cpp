#include "photogrammetry/sfm/FeatureTracks.h"

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

kestrel::FeatureObservation observation(int image, int feature, double x)
{
    return {image, feature, cv::Point2d(x, image * 10.0)};
}

bool multiViewTrackTest()
{
    const std::vector<kestrel::PairwiseFeatureMatch> matches{
        {observation(0, 10, 1.0), observation(1, 20, 2.0)},
        {observation(1, 20, 2.0), observation(2, 30, 3.0)},
        {observation(2, 30, 3.0), observation(3, 40, 4.0)},
        {observation(0, 10, 1.0), observation(1, 20, 2.0)}, // redundant
        {observation(0, 99, 9.0), observation(3, 40, 4.0)}, // conflict
        {observation(4, 50, 5.0), observation(5, 60, 6.0)}}; // only two views
    const kestrel::FeatureTrackBuildResult result =
        kestrel::FeatureTrackBuilder::build(matches, 3);
    return expect(result.tracks.size() == 1,
                  "Only the four-view component should become a track.")
           && expect(result.tracks[0].observations.size() == 4,
                     "The persistent track should span four images.")
           && expect(result.redundantMatches == 1,
                     "Repeated pairwise matches should be counted once.")
           && expect(result.rejectedConflicts == 1,
                     "A second feature from the same image must be rejected.")
           && expect(result.tracks[0].observations[0].featureIndex == 10,
                     "Track observations should retain feature identities.");
}

bool invalidMatchTest()
{
    const std::vector<kestrel::PairwiseFeatureMatch> matches{
        {observation(0, 1, 1.0), observation(0, 2, 2.0)},
        {observation(-1, 1, 1.0), observation(2, 2, 2.0)}};
    const auto result = kestrel::FeatureTrackBuilder::build(matches);
    return expect(result.tracks.empty(), "Invalid matches must not form tracks.")
           && expect(result.ignoredInvalidMatches == 2,
                     "Invalid match diagnostics should be retained.");
}

} // namespace

int main()
{
    const bool success = multiViewTrackTest() && invalidMatchTest();
    if (success) {
        std::cout << "Multi-view feature-track tests passed.\n";
        return 0;
    }
    return 1;
}
