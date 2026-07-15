#ifndef PATHHELPER_H
#define PATHHELPER_H

#include <QObject>

class PathHelper : public QObject
{
    Q_OBJECT
public:
    explicit PathHelper(QObject *parent = nullptr);
    
    Q_INVOKABLE QString getDocumentsPath();
    Q_INVOKABLE QString getDefaultProjectPath(const QString &projectName);
    Q_INVOKABLE QString updateProjectNameInPath(const QString &currentPath, const QString &newProjectName);
};

#endif // PATHHELPER_H
