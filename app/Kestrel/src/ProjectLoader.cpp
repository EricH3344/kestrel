#include "ProjectLoader.h"
#include <QFile>
#include <QDataStream>

ProjectLoader::ProjectLoader(QObject *parent)
    : QObject(parent)
{
}

bool ProjectLoader::isValidProjectFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    quint32 magicNumber;
    stream >> magicNumber;
    
    file.close();
    
    // Check for "KPRO" magic number
    return magicNumber == 0x4B50524F;
}

ProjectData ProjectLoader::loadProject(const QString &kprojFilePath)
{
    ProjectData data;
    
    QFile file(kprojFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit projectLoadFailed("Cannot open project file");
        return data;
    }
    
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // Magic number
    quint32 magicNumber;
    stream >> magicNumber;
    
    if (magicNumber != 0x4B50524F) { // "KPRO"
        emit projectLoadFailed("Invalid project file format");
        file.close();
        return data;
    }
    
    // Version
    quint16 majorVersion, minorVersion;
    stream >> majorVersion >> minorVersion;
    
    if (majorVersion != 1) {
        emit projectLoadFailed("Unsupported project file version");
        file.close();
        return data;
    }
    
    // Project metadata
    stream >> data.projectName;
    stream >> data.projectPath;
    stream >> data.created;
    
    // Directory structure
    stream >> data.rawImagesPath;
    stream >> data.processedImagesPath;
    stream >> data.outputPath;
    stream >> data.metadataPath;
    
    // Imported files (efficient even with thousands)
    quint32 fileCount;
    stream >> fileCount;
    data.importedFiles.reserve(fileCount);
    for (quint32 i = 0; i < fileCount; ++i) {
        QString filePath;
        stream >> filePath;
        data.importedFiles.append(filePath);
    }
    
    file.close();
    
    emit projectLoaded(data.projectName);
    return data;
}
