#include "windowcontroller.h"
#include <QGuiApplication>
#include <QWindow>
#include <QCoreApplication>

WindowController::WindowController(QObject *parent)
    : QObject(parent), m_maximized(false)
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
    QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (app) {
        const auto topLevelWindows = app->topLevelWindows();
        if (!topLevelWindows.isEmpty()) {
            QWindow *appWindow = topLevelWindows.first();
            if (appWindow->visibility() == QWindow::Maximized) {
                appWindow->showNormal();
                m_maximized = false;
            } else {
                appWindow->showMaximized();
                m_maximized = true;
            }
            emit maximizedChanged();
        }
    }
}

void WindowController::closeWindow()
{
    QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (app) {
        app->quit();
    }
}

bool WindowController::isMaximized() const
{
    return m_maximized;
}
