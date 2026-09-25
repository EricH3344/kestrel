#include "photogrammetry/io/CaptureGrouping.h"

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

kestrel::CaptureImageRecord record(const QString &path, const QString &captureId,
                                   int rigCameraIndex,
                                   const QString &legacyName = {},
                                   int legacyBand = -1)
{
    kestrel::CaptureImageRecord value;
    value.filePath = path;
    value.metadata.captureId = captureId;
    value.metadata.rigCameraIndex = rigCameraIndex;
    value.legacyCaptureName = legacyName;
    value.legacyBandIndex = legacyBand;
    return value;
}

bool metadataIdentityTest()
{
    const QVector<kestrel::CaptureImageRecord> records{
        record("C:/fixture/unrelated-blue.tif", "capture-42", 0),
        record("C:/fixture/unrelated-green.tif", "capture-42", 1)};
    const auto result = kestrel::CaptureGrouping::groupRecords(records);
    return expect(result.captures.size() == 1,
                  "CaptureId should group files regardless of filename.")
           && expect(result.captures[0].stableId == "capture-42",
                     "Physical CaptureId should be the stable identity.")
           && expect(result.captures[0].bands.contains(1)
                         && result.captures[0].bands.contains(2),
                     "Rig camera indices should map to one-based band indices.");
}

bool legacyFallbackTest()
{
    const QVector<kestrel::CaptureImageRecord> records{
        record("C:/fixture/IMG_0007_1.tif", {}, -1, "IMG_0007", 1),
        record("C:/fixture/IMG_0007_2.tif", {}, -1, "IMG_0007", 2)};
    const auto result = kestrel::CaptureGrouping::groupRecords(records);
    return expect(result.captures.size() == 1,
                  "Legacy band suffixes should remain a compatibility fallback.")
           && expect(result.captures[0].bands.size() == 2,
                     "Legacy files should retain both spectral bands.");
}

bool duplicateBandTest()
{
    const QVector<kestrel::CaptureImageRecord> records{
        record("C:/fixture/a.tif", "capture-9", 0),
        record("C:/fixture/b.tif", "capture-9", 0)};
    const auto result = kestrel::CaptureGrouping::groupRecords(records);
    return expect(result.captures.size() == 2,
                  "Duplicate physical bands must be isolated, not overwritten.")
           && expect(result.captures[0].bands.size() == 1
                         && result.captures[1].bands.size() == 1,
                     "Every duplicate source image should remain discoverable.")
           && expect(!result.captures[0].warnings.isEmpty()
                         && !result.captures[1].warnings.isEmpty(),
                     "Both sides of a duplicate collision should be diagnosable.");
}

} // namespace

int main()
{
    const bool success = metadataIdentityTest() && legacyFallbackTest()
                         && duplicateBandTest();
    if (success) {
        std::cout << "Capture grouping tests passed.\n";
        return 0;
    }
    return 1;
}
