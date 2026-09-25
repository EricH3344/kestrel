#ifndef FEATURETRACKS_H
#define FEATURETRACKS_H

#include <opencv2/core.hpp>

#include <vector>

namespace kestrel {

struct FeatureObservation
{
    int imageIndex = -1;
    int featureIndex = -1;
    cv::Point2d imagePoint;
};

struct PairwiseFeatureMatch
{
    FeatureObservation first;
    FeatureObservation second;
};

struct FeatureTrack
{
    int id = -1;
    std::vector<FeatureObservation> observations;
};

struct FeatureTrackBuildResult
{
    std::vector<FeatureTrack> tracks;
    int acceptedMatches = 0;
    int redundantMatches = 0;
    int rejectedConflicts = 0;
    int ignoredInvalidMatches = 0;
};

class FeatureTrackBuilder
{
public:
    // A valid track contains at most one feature from each image. Pairwise
    // matches that would merge two different features from the same image are
    // rejected instead of contaminating the full connected component.
    static FeatureTrackBuildResult build(
        const std::vector<PairwiseFeatureMatch> &matches,
        int minimumObservations = 3);
};

} // namespace kestrel

#endif // FEATURETRACKS_H
