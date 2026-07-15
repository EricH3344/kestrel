#include "photogrammetry/io/CaptureGrouping.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace kestrel {

namespace {

const QRegularExpression kBandSuffix(
    "^(.*[_-][0-9]+)_([1-9][0-9]?)$",
    QRegularExpression::CaseInsensitiveOption);

CaptureImageRecord recordForFile(const QString &filePath)
{
    CaptureImageRecord record;
    record.filePath = filePath;
    const QFileInfo info(filePath);
    const QRegularExpressionMatch match =
        kBandSuffix.match(info.completeBaseName());
    if (match.hasMatch()) {
        record.legacyCaptureName = match.captured(1);
        record.legacyBandIndex = match.captured(2).toInt();
    } else {
        record.legacyCaptureName = info.completeBaseName();
    }
    TiffMetadataReader::read(filePath, &record.metadata, &record.metadataError);
    return record;
}

} // namespace

CaptureGroupingResult CaptureGrouping::groupFiles(const QStringList &filePaths)
{
    QStringList sortedPaths = filePaths;
    std::sort(sortedPaths.begin(), sortedPaths.end(),
              [](const QString &first, const QString &second) {
                  return QString::localeAwareCompare(first, second) < 0;
              });
    QVector<CaptureImageRecord> records;
    records.reserve(sortedPaths.size());
    for (const QString &filePath : sortedPaths) {
        records.append(recordForFile(filePath));
    }
    return groupRecords(records);
}

CaptureGroupingResult CaptureGrouping::groupRecords(
    const QVector<CaptureImageRecord> &records)
{
    CaptureGroupingResult result;
    QMap<QString, int> groupIndices;
    for (const CaptureImageRecord &record : records) {
        const QFileInfo info(record.filePath);
        const bool hasPhysicalId = !record.metadata.captureId.isEmpty();
        const QString groupKey = hasPhysicalId
            ? "capture:" + record.metadata.captureId
            : (record.legacyBandIndex > 0
                   ? "legacy:" + info.absolutePath() + "/"
                         + record.legacyCaptureName
                   : "file:" + info.absoluteFilePath());
        int bandIndex = record.metadata.rigCameraIndex >= 0
            ? record.metadata.rigCameraIndex + 1 : record.legacyBandIndex;
        if (bandIndex <= 0) {
            bandIndex = 1;
        }

        int groupIndex = groupIndices.value(groupKey, -1);
        if (groupIndex < 0) {
            GroupedCapture capture;
            capture.stableId = hasPhysicalId
                ? record.metadata.captureId : groupKey;
            capture.displayName = record.legacyCaptureName.isEmpty()
                ? info.completeBaseName() : record.legacyCaptureName;
            result.captures.append(capture);
            groupIndex = result.captures.size() - 1;
            groupIndices.insert(groupKey, groupIndex);
        }

        if (result.captures[groupIndex].bands.contains(bandIndex)) {
            result.captures[groupIndex].warnings.append(
                QString("duplicate band index %1 (%2)")
                    .arg(bandIndex).arg(info.fileName()));
            // Preserve every image rather than silently overwriting it. A
            // duplicate becomes a standalone capture that validation can show.
            GroupedCapture duplicate;
            duplicate.stableId = groupKey + ":duplicate:"
                                 + info.absoluteFilePath();
            duplicate.displayName = info.completeBaseName();
            duplicate.warnings.append("duplicate physical-capture band was isolated");
            duplicate.bands.insert(bandIndex, record);
            if (record.metadataError.isEmpty()) {
                ++result.metadataFileCount;
                for (const QString &warning : record.metadata.validationWarnings()) {
                    duplicate.warnings.append(info.fileName() + ": " + warning);
                }
            } else {
                duplicate.warnings.append(record.metadataError);
            }
            result.captures.append(duplicate);
            continue;
        }

        GroupedCapture &capture = result.captures[groupIndex];
        capture.bands.insert(bandIndex, record);
        if (record.metadataError.isEmpty()) {
            ++result.metadataFileCount;
            for (const QString &warning : record.metadata.validationWarnings()) {
                capture.warnings.append(info.fileName() + ": " + warning);
            }
        } else {
            capture.warnings.append(record.metadataError);
        }
    }
    for (GroupedCapture &capture : result.captures) {
        QString expectedTimestamp;
        for (const CaptureImageRecord &record : capture.bands) {
            if (record.metadata.captureTime.isEmpty()) {
                continue;
            }
            if (expectedTimestamp.isEmpty()) {
                expectedTimestamp = record.metadata.captureTime;
            } else if (expectedTimestamp != record.metadata.captureTime) {
                capture.warnings.append(
                    "spectral files have inconsistent capture timestamps");
                break;
            }
        }
    }
    return result;
}

} // namespace kestrel
