#include "windowcontroller.h"
#include <QGuiApplication>
#include <QWindow>
#include <QCoreApplication>
#include <QApplication>
#include <QWidget>
#include <QMainWindow>

WindowController::WindowController(QObject *parent)
    : QObject(parent)
{
}

void WindowController::minimize()
{
    QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (app) {
        const auto topLevelWindows = app->topLevelWindows();
        if (!topLevelWindows.isEmpty()) {
            QWindow *appWindow = topLevelWindows.first();
            appWindow->showMinimized();
        }
    }
}

void WindowController::maximize()
{
    QWindow *window = qobject_cast<QWindow *>(parent());
    if (!window) {
        // Try to get from QML root object's window
        QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
        if (app) {
            const auto topLevelWindows = app->topLevelWindows();
            if (!topLevelWindows.isEmpty()) {
                QWindow *appWindow = topLevelWindows.first();
                if (appWindow->visibility() == QWindow::Maximized) {
                    appWindow->showNormal();
                } else {
                    appWindow->showMaximized();
                }
            }
        }
    } else {
        if (window->visibility() == QWindow::Maximized) {
            window->showNormal();
        } else {
            window->showMaximized();
        }
    }
}

void WindowController::closeWindow()
{
    QWindow *window = qobject_cast<QWindow *>(parent());
    if (!window) {
        QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
        if (app) {
            app->quit();
        }
    } else {
        window->close();
    }
}
