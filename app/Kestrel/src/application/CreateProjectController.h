#ifndef CREATEPROJECTCONTROLLER_H
#define CREATEPROJECTCONTROLLER_H

#include <QObject>
#include <QQuickWindow>

class CreateProjectController : public QObject
{
    Q_OBJECT
public:
    explicit CreateProjectController(QObject *parent = nullptr);
    
public slots:
    void minimize(QQuickWindow *window);
    void closeWindow(QQuickWindow *window);
};

#endif // CREATEPROJECTCONTROLLER_H
