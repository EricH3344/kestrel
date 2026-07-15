#include "photogrammetry/sfm/FeatureTracks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>

namespace kestrel {

namespace {

uint64_t observationKey(int imageIndex, int featureIndex)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(imageIndex)) << 32)
           | static_cast<uint32_t>(featureIndex);
}

struct DisjointTracks
{
    std::vector<int> parent;
    std::vector<int> sizes;
    std::vector<FeatureObservation> observations;
    std::vector<std::map<int, int>> imageFeatures;
    std::unordered_map<uint64_t, int> indices;

    int add(const FeatureObservation &observation)
    {
        const uint64_t key = observationKey(observation.imageIndex,
                                            observation.featureIndex);
        const auto existing = indices.find(key);
        if (existing != indices.end()) {
            return existing->second;
        }
        const int index = static_cast<int>(parent.size());
        indices.emplace(key, index);
        parent.push_back(index);
        sizes.push_back(1);
        observations.push_back(observation);
        imageFeatures.push_back({{observation.imageIndex,
                                  observation.featureIndex}});
        return index;
    }

    int root(int index)
    {
        while (parent[index] != index) {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    }

    bool merge(int first, int second)
    {
        first = root(first);
        second = root(second);
        if (first == second) {
            return true;
        }
        for (const auto &[image, feature] : imageFeatures[first]) {
            const auto existing = imageFeatures[second].find(image);
            if (existing != imageFeatures[second].end()
                && existing->second != feature) {
                return false;
            }
        }
        if (sizes[first] < sizes[second]) {
            std::swap(first, second);
        }
        parent[second] = first;
        sizes[first] += sizes[second];
        imageFeatures[first].insert(imageFeatures[second].begin(),
                                    imageFeatures[second].end());
        imageFeatures[second].clear();
        return true;
    }
};

bool validObservation(const FeatureObservation &observation)
{
    return observation.imageIndex >= 0 && observation.featureIndex >= 0
           && std::isfinite(observation.imagePoint.x)
           && std::isfinite(observation.imagePoint.y);
}

} // namespace

FeatureTrackBuildResult FeatureTrackBuilder::build(
    const std::vector<PairwiseFeatureMatch> &matches,
    int minimumObservations)
{
    FeatureTrackBuildResult result;
    DisjointTracks sets;
    minimumObservations = std::max(2, minimumObservations);
    for (const PairwiseFeatureMatch &match : matches) {
        if (!validObservation(match.first) || !validObservation(match.second)
            || match.first.imageIndex == match.second.imageIndex) {
            ++result.ignoredInvalidMatches;
            continue;
        }
        const int first = sets.add(match.first);
        const int second = sets.add(match.second);
        if (sets.root(first) == sets.root(second)) {
            ++result.redundantMatches;
            continue;
        }
        if (!sets.merge(first, second)) {
            ++result.rejectedConflicts;
            continue;
        }
        ++result.acceptedMatches;
    }

    std::map<int, std::vector<FeatureObservation>> components;
    for (int index = 0; index < static_cast<int>(sets.observations.size()); ++index) {
        components[sets.root(index)].push_back(sets.observations[index]);
    }
    for (auto &[root, observations] : components) {
        (void)root;
        if (observations.size() < static_cast<size_t>(minimumObservations)) {
            continue;
        }
        std::sort(observations.begin(), observations.end(),
                  [](const FeatureObservation &first,
                     const FeatureObservation &second) {
                      return first.imageIndex != second.imageIndex
                          ? first.imageIndex < second.imageIndex
                          : first.featureIndex < second.featureIndex;
                  });
        result.tracks.push_back({-1, std::move(observations)});
    }
    std::sort(result.tracks.begin(), result.tracks.end(),
              [](const FeatureTrack &first, const FeatureTrack &second) {
                  const FeatureObservation &a = first.observations.front();
                  const FeatureObservation &b = second.observations.front();
                  return a.imageIndex != b.imageIndex
                      ? a.imageIndex < b.imageIndex
                      : a.featureIndex < b.featureIndex;
              });
    for (int id = 0; id < static_cast<int>(result.tracks.size()); ++id) {
        result.tracks[id].id = id;
    }
    return result;
}

} // namespace kestrel
