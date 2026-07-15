// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include "windowcontroller.h"
#include "application/SystemAlert.h"
#include "application/CreateProjectController.h"
#include "application/PathHelper.h"
#include "application/FileDialogHelper.h"
#include "project/ProjectCreator.h"
#include "project/ProjectLoader.h"
#include "photogrammetry/mosaic/StitchingController.h"

#include "autogen/environment.h"

int main(int argc, char *argv[])
{
    set_qt_environment();

    // Allow an existing project to be reprocessed without opening the UI.
    // This is also useful for diagnosing large field flights from a terminal.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--stitch-project") {
        QCoreApplication app(argc, argv);
        StitchingController stitchingController;
        QObject::connect(&stitchingController,
                         &StitchingController::stitchingStatusChanged,
                         [](const QString &message) { qInfo().noquote() << message; });
        QObject::connect(&stitchingController,
                         &StitchingController::stitchingCompleted,
                         &app,
                         [&app](const QString &, const QUrl &previewUrl) {
                             qInfo().noquote() << "Mosaic written to"
                                               << previewUrl.toLocalFile();
                             app.exit(0);
                         });
        QObject::connect(&stitchingController,
                         &StitchingController::stitchingFailed,
                         &app,
                         [&app](const QString &, const QString &errorMessage) {
                             qCritical().noquote() << errorMessage;
                             app.exit(2);
                         });
        const QString projectPath = QString::fromLocal8Bit(argv[2]);
        QTimer::singleShot(0, &stitchingController,
                           [&stitchingController, projectPath] {
                               stitchingController.stitchProject(projectPath);
                           });
        return app.exec();
    }

    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    
    // Register WindowController with QML
    engine.rootContext()->setContextProperty("windowController", new WindowController(&app));
    
    // Register SystemAlert with QML
    engine.rootContext()->setContextProperty("systemAlert", new SystemAlert(&app));
    
    // Register CreateProjectController with QML
    engine.rootContext()->setContextProperty("createProjectController", new CreateProjectController(&app));
    
    // Register PathHelper with QML
    engine.rootContext()->setContextProperty("pathHelper", new PathHelper(&app));
    
    // Register FileDialogHelper with QML
    engine.rootContext()->setContextProperty("fileDialogHelper", new FileDialogHelper(&app));
    
    // Register ProjectCreator with QML
    engine.rootContext()->setContextProperty("projectCreator", new ProjectCreator(&app));
    
    // Register ProjectLoader with QML
    engine.rootContext()->setContextProperty("projectLoader", new ProjectLoader(&app));

    engine.rootContext()->setContextProperty("stitchingController", new StitchingController(&app));
    
    const QUrl url(mainQmlFile);
    QObject::connect(
                &engine, &QQmlApplicationEngine::objectCreated, &app,
                [url, &engine](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            QCoreApplication::exit(-1);
        } else if (obj && url == objUrl) {
            // Expose the App's currentScreen property to QML context
            engine.rootContext()->setContextProperty("appModel", obj);
        }
    }, Qt::QueuedConnection);

    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.addImportPath(":/");
    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
