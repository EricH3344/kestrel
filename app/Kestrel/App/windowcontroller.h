#ifndef WINDOWCONTROLLER_H
#define WINDOWCONTROLLER_H

#include <QObject>
#include <QQmlEngine>

class WindowController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit WindowController(QObject *parent = nullptr);

    Q_INVOKABLE void minimize();
    Q_INVOKABLE void maximize();
    Q_INVOKABLE void closeWindow();

private:
};

#endif // WINDOWCONTROLLER_H
