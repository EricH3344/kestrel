#include "PathHelper.h"
#include <QStandardPaths>
#include <QDir>

PathHelper::PathHelper(QObject *parent)
    : QObject(parent)
{
}

QString PathHelper::getDocumentsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString PathHelper::getDefaultProjectPath(const QString &projectName)
{
    QString documentsPath = getDocumentsPath();
    QString projectPath = QDir(documentsPath).filePath("Kestrel Projects/" + projectName);
    return projectPath;
}

QString PathHelper::updateProjectNameInPath(const QString &currentPath, const QString &newProjectName)
{
    // Extract the parent directory of the current path
    QFileInfo fileInfo(currentPath);
    QString parentDir = fileInfo.dir().path();
    // Combine parent directory with new project name
    QString newPath = QDir(parentDir).filePath(newProjectName);
    return newPath;
}
