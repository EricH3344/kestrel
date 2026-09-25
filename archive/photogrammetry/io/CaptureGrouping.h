#ifndef CAPTUREGROUPING_H
#define CAPTUREGROUPING_H

#include "photogrammetry/io/TiffImageMetadata.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace kestrel {

struct CaptureImageRecord
{
    QString filePath;
    TiffImageMetadata metadata;
    QString legacyCaptureName;
    int legacyBandIndex = 0;
    QString metadataError;
};

struct GroupedCapture
{
    QString stableId;
    QString displayName;
    QMap<int, CaptureImageRecord> bands;
    QStringList warnings;
};

struct CaptureGroupingResult
{
    QVector<GroupedCapture> captures;
    int metadataFileCount = 0;
};

class CaptureGrouping
{
public:
    static CaptureGroupingResult groupFiles(const QStringList &filePaths);
    static CaptureGroupingResult groupRecords(
        const QVector<CaptureImageRecord> &records);
};

} // namespace kestrel

#endif // CAPTUREGROUPING_H
