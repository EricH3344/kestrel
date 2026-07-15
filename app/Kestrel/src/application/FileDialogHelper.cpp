#include "application/FileDialogHelper.h"
#include <QFileDialog>
#include <QDirIterator>
#include <QFileInfo>
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

QStringList FileDialogHelper::selectTiffFiles(const QString &startPath)
{
    return selectFiles(startPath, "TIFF Files (*.tif *.tiff)");
}

QStringList FileDialogHelper::selectTiffFilesFromFolder(const QString &startPath)
{
    QString folderPath = QFileDialog::getExistingDirectory(
        nullptr,
        "Select Folder to Import",
        startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (folderPath.isEmpty()) {
        return {};
    }

    QStringList tiffFiles;
    QDirIterator iterator(folderPath, QDir::Files, QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        const QString filePath = iterator.next();
        const QFileInfo fileInfo(filePath);
        const QString suffix = fileInfo.suffix().toLower();
        if (suffix == "tif" || suffix == "tiff") {
            tiffFiles.append(filePath);
        }
    }

    if (!tiffFiles.isEmpty()) {
        emit filesSelected(tiffFiles);
    }

    return tiffFiles;
}
