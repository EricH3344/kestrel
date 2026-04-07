#include "FileDialogHelper.h"
#include <QFileDialog>
#include <QQmlEngine>

FileDialogHelper::FileDialogHelper(QObject *parent)
    : QObject(parent)
{
}

QString FileDialogHelper::selectFolder(const QString &startPath)
{
    QString folderPath = QFileDialog::getExistingDirectory(
        nullptr,
        "Select Project Directory",
        startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (!folderPath.isEmpty()) {
        emit folderSelected(folderPath);
    }
    
    return folderPath;
}

QStringList FileDialogHelper::selectFiles(const QString &startPath, const QString &filter)
{
    QString fileFilter = filter;
    if (fileFilter.isEmpty()) {
        fileFilter = "All Files (*)";
    }
    
    QStringList selectedFiles = QFileDialog::getOpenFileNames(
        nullptr,
        "Select Files to Import",
        startPath,
        fileFilter
    );
    
    if (!selectedFiles.isEmpty()) {
        emit filesSelected(selectedFiles);
    }
    
    return selectedFiles;
}
