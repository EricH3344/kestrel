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
    Q_INVOKABLE void refreshPreview(const QString &projectPath);

signals:
    void stitchingStarted(const QString &projectPath);
    void stitchingStatusChanged(const QString &message);
    void stitchingProgressChanged(double percent);
    void stitchingCompleted(const QString &projectPath, const QUrl &previewUrl);
    void stitchingFailed(const QString &projectPath, const QString &errorMessage);

private:
    struct MosaicResult {
        QString projectPath;
        QUrl previewUrl;
        QString errorMessage;
    };

    MosaicResult runOdm(const QString &projectPath, bool previewOnly);
    void postStatus(const QString &message);
    void postProgress(double percent);

    QFutureWatcher<MosaicResult> m_watcher;
};

#endif
