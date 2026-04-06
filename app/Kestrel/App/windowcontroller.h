#ifndef WINDOWCONTROLLER_H
#define WINDOWCONTROLLER_H

#include <QObject>
#include <QQmlEngine>

class WindowController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool maximized READ isMaximized NOTIFY maximizedChanged)

public:
    explicit WindowController(QObject *parent = nullptr);

    Q_INVOKABLE void minimize();
    Q_INVOKABLE void maximize();
    Q_INVOKABLE void closeWindow();
    
    bool isMaximized() const;

signals:
    void maximizedChanged();

private:
    bool m_maximized = false;
};

#endif // WINDOWCONTROLLER_H
