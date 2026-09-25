#include "project/ProjectCreator.h"
#include "project/ProjectLoader.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDataStream>
#include <QCoreApplication>
#include <QEventLoop>

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

        // Project creation currently runs on the UI thread. Process paint and
        // queued QML updates between files so the progress window stays live.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    
    return true;
}

bool ProjectCreator::createBinaryProjectFile(const QString &projectPath, const QString &projectName, const QStringList &files)
{
    ProjectData data;
    data.projectName = projectName;
    data.projectPath = projectPath;
    data.created = QDateTime::currentDateTime();
    data.rawImagesPath = projectPath + "/raw_images";
    data.processedImagesPath = projectPath + "/processed_images";
    data.outputPath = projectPath + "/output";
    data.metadataPath = projectPath + "/metadata";
    data.importedFiles = files;
    FlightData firstFlight = ProjectLoader::inspectFlight(files);
    firstFlight.relativePath = ".";
    data.flights.append(firstFlight);
    return ProjectLoader::writeProject(projectPath + "/" + projectName + ".kproj", data);
}
