#ifndef STITCHINGCONTROLLER_H
#define STITCHINGCONTROLLER_H

#include <QFutureWatcher>
#include <QObject>
#include <QUrl>

class StitchingController : public QObject
{
    Q_OBJECT

public:
    explicit StitchingController(QObject *parent = nullptr);
    ~StitchingController() override;

    Q_INVOKABLE void stitchProject(const QString &projectPath);

signals:
    void stitchingStarted(const QString &projectPath);
    void stitchingStatusChanged(const QString &message);
    void stitchingCompleted(const QString &projectPath, const QUrl &previewUrl);
    void stitchingFailed(const QString &projectPath, const QString &errorMessage);

private:
    struct MosaicResult {
        bool success = false;
        QString projectPath;
        QUrl previewUrl;
        QString errorMessage;
    };

    MosaicResult buildMosaic(const QString &projectPath);
    void postStatus(const QString &message);

    QFutureWatcher<MosaicResult> m_watcher;
};

#endif // STITCHINGCONTROLLER_H
