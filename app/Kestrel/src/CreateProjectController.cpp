#include "CreateProjectController.h"

CreateProjectController::CreateProjectController(QObject *parent)
    : QObject(parent)
{
}

void CreateProjectController::minimize(QQuickWindow *window)
{
    if (window) {
        window->showMinimized();
    }
}

void CreateProjectController::closeWindow(QQuickWindow *window)
{
    if (window) {
        window->close();
    }
}
