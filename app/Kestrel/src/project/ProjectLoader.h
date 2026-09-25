#ifndef PROJECTLOADER_H
#define PROJECTLOADER_H

#include <QObject>
#include <QDateTime>
#include <QStringList>
#include <QVariantMap>

struct FlightData
{
    QString id;
    QString relativePath;
    QDateTime capturedAt;
    QStringList structure;
    QStringList importedFiles;
};

struct ProjectData
{
    QString projectName;
    QString projectPath;
    QDateTime created;
    QString rawImagesPath;
    QString processedImagesPath;
    QString outputPath;
    QString metadataPath;
    QStringList importedFiles;
    QList<FlightData> flights;
};

class ProjectLoader : public QObject
{
    Q_OBJECT
public:
    explicit ProjectLoader(QObject *parent = nullptr);
    
    Q_INVOKABLE ProjectData loadProject(const QString &kprojFilePath);
    Q_INVOKABLE bool isValidProjectFile(const QString &filePath);
    Q_INVOKABLE QVariantMap openProject(const QString &kprojFilePath);
    Q_INVOKABLE QVariantMap mapMetadata(const QString &projectPath);
    Q_INVOKABLE QVariantMap addFlight(const QString &kprojFilePath,
                                     const QStringList &files);
    static FlightData inspectFlight(const QStringList &files);
    static bool writeProject(const QString &kprojFilePath, const ProjectData &data);

signals:
    void projectLoaded(const QString &projectName);
    void projectLoadFailed(const QString &errorMessage);
};

#endif // PROJECTLOADER_H
