#ifndef PROJECTCREATOR_H
#define PROJECTCREATOR_H

#include <QObject>
#include <QStringList>

class ProjectCreator : public QObject
{
    Q_OBJECT
public:
    explicit ProjectCreator(QObject *parent = nullptr);
    
    Q_INVOKABLE bool createProject(const QString &projectName, 
                                   const QString &projectDirectory,
                                   const QStringList &importedFiles);

signals:
    void projectCreationStarted(const QString &projectName);
    void projectCreationProgress(int currentFile, int totalFiles);
    void projectCreationCompleted(const QString &projectPath);
    void projectCreationFailed(const QString &errorMessage);

private:
    bool processImportedFiles(const QString &projectPath, const QStringList &files);
    bool createBinaryProjectFile(const QString &projectPath, const QString &projectName, const QStringList &files);
};

#endif // PROJECTCREATOR_H
