#ifndef PROJECTLOADER_H
#define PROJECTLOADER_H

#include <QObject>
#include <QStringList>

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
};

class ProjectLoader : public QObject
{
    Q_OBJECT
public:
    explicit ProjectLoader(QObject *parent = nullptr);
    
    Q_INVOKABLE ProjectData loadProject(const QString &kprojFilePath);
    Q_INVOKABLE bool isValidProjectFile(const QString &filePath);

signals:
    void projectLoaded(const QString &projectName);
    void projectLoadFailed(const QString &errorMessage);
};

#endif // PROJECTLOADER_H
