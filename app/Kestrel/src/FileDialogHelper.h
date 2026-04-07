#ifndef FILEDIALOGHELPER_H
#define FILEDIALOGHELPER_H

#include <QObject>

class FileDialogHelper : public QObject
{
    Q_OBJECT
public:
    explicit FileDialogHelper(QObject *parent = nullptr);
    
    Q_INVOKABLE QString selectFolder(const QString &startPath = "");
    Q_INVOKABLE QStringList selectFiles(const QString &startPath = "", const QString &filter = "");

signals:
    void folderSelected(const QString &folderPath);
    void filesSelected(const QStringList &filePaths);
};

#endif // FILEDIALOGHELPER_H
