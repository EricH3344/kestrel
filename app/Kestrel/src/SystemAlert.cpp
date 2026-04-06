#include "SystemAlert.h"

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <Foundation/Foundation.h>
#elif defined(Q_OS_LINUX)
#include <QProcess>
#endif

SystemAlert::SystemAlert(QObject *parent)
    : QObject(parent)
{
}

void SystemAlert::playSound()
{
#ifdef Q_OS_WIN
    // Use Windows MessageBeep for standard alert sound
    MessageBeep(MB_ICONEXCLAMATION);
#elif defined(Q_OS_MAC)
    // Use macOS system alert sound
    NSBeep();
#elif defined(Q_OS_LINUX)
    // Use system bell or paplay for Linux
    QProcess::execute("paplay", QStringList() << "/usr/share/sounds/freedesktop/stereo/complete.oga");
#endif
}
