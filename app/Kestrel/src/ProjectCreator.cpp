#include "ProjectCreator.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDataStream>

ProjectCreator::ProjectCreator(QObject *parent)
    : QObject(parent)
{
}

bool ProjectCreator::createProject(const QString &projectName, 
                                   const QString &projectDirectory,
                                   const QStringList &importedFiles)
{
    if (projectName.isEmpty() || projectDirectory.isEmpty()) {
        emit projectCreationFailed("Project name and directory cannot be empty");
        return false;
    }
    
    emit projectCreationStarted(projectName);
    
    // Create the project directory structure
    QDir projectDir(projectDirectory);
    
    if (!projectDir.exists()) {
        if (!projectDir.mkpath(projectDirectory)) {
            emit projectCreationFailed("Failed to create project directory");
            return false;
        }
    }
    
    // Create subdirectories for different file types
    projectDir.mkdir("raw_images");
    projectDir.mkdir("processed_images");
    projectDir.mkdir("output");
    projectDir.mkdir("metadata");
    
    // Process imported files
    if (!processImportedFiles(projectDirectory, importedFiles)) {
        emit projectCreationFailed("Failed to process imported files");
        return false;
    }
    
    // Create the binary .kproj file
    if (!createBinaryProjectFile(projectDirectory, projectName, importedFiles)) {
        emit projectCreationFailed("Failed to create project file");
        return false;
    }
    
    emit projectCreationCompleted(projectDirectory);
    return true;
}

bool ProjectCreator::processImportedFiles(const QString &projectPath, const QStringList &files)
{
    int totalFiles = files.count();
    
    for (int i = 0; i < totalFiles; ++i) {
        const QString &sourceFile = files.at(i);
        QFileInfo sourceInfo(sourceFile);
        
        // Copy file to raw_images directory
        QString destFile = projectPath + "/raw_images/" + sourceInfo.fileName();
        
        if (!QFile::copy(sourceFile, destFile)) {
            return false;
        }
        
        // Emit progress signal
        emit projectCreationProgress(i + 1, totalFiles);
    }
    
    return true;
}

bool ProjectCreator::createBinaryProjectFile(const QString &projectPath, const QString &projectName, const QStringList &files)
{
    QString kprojPath = projectPath + "/" + projectName + ".kproj";
    QFile kprojFile(kprojPath);
    
    if (!kprojFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QDataStream stream(&kprojFile);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // Magic number to identify file format
    quint32 magicNumber = 0x4B50524F; // "KPRO" in hex
    stream << magicNumber;
    
    // Format version (binary format v1.0)
    quint16 majorVersion = 1;
    quint16 minorVersion = 0;
    stream << majorVersion << minorVersion;
    
    // Project metadata
    stream << projectName;
    stream << projectPath;
    stream << QDateTime::currentDateTime();
    
    // Directory structure
    stream << QString(projectPath + "/raw_images");
    stream << QString(projectPath + "/processed_images");
    stream << QString(projectPath + "/output");
    stream << QString(projectPath + "/metadata");
    
    // Imported files list (efficient for thousands of files)
    stream << (quint32)files.count();
    for (const QString &file : files) {
        stream << file;
    }
    
    kprojFile.close();
    return true;
}
