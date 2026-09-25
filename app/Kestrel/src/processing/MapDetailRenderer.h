#pragma once

#include <QObject>
#include <QTemporaryDir>
#include <QUrl>

class MapDetailRenderer : public QObject
{
    Q_OBJECT
public:
    explicit MapDetailRenderer(QObject *parent = nullptr);

    Q_INVOKABLE void request(const QString &projectPath, double left, double top,
                             double width, double height, int outputWidth, int outputHeight);

signals:
    void detailReady(const QString &projectPath, const QUrl &url,
                     double left, double top, double width, double height);
    void detailFailed(const QString &message);

private:
    struct Request {
        QString projectPath;
        double left = 0;
        double top = 0;
        double width = 0;
        double height = 0;
        int outputWidth = 0;
        int outputHeight = 0;
        int serial = 0;
    };
    struct Result {
        QUrl url;
        QString error;
    };

    void start(const Request &request);
    static Result render(const Request &request, const QString &directory);

    QTemporaryDir m_directory;
    Request m_latest;
    bool m_busy = false;
    int m_serial = 0;
    QStringList m_images;
};
