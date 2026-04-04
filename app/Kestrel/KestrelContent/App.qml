import QtQuick
import QtQuick.Controls
import Kestrel

Window {
    id: appWindow
    width: 1440
    height: 1024

    visible: true
    title: "Kestrel Companion App"
    flags: Qt.FramelessWindowHint | Qt.Window

    property int currentScreen: 0
    property string currentScreenPath: "Database.ui.qml"

    // Window dragging support
    DragHandler {
        target: null
        onActiveChanged: {
            if (active) {
                appWindow.startSystemMove()
            }
        }
    }

    Loader {
        id: screenLoader
        anchors.fill: parent
        source: currentScreenPath
    }

    // Navigation functions
    function showDatabase() {
        currentScreenPath = "Database.ui.qml"
        currentScreen = 0
    }

    function showFlightLogs() {
        currentScreenPath = "Flight_Logs.ui.qml"
        currentScreen = 1
    }

    function showMap() {
        currentScreenPath = "MapScreen.ui.qml"
        currentScreen = 2
    }

    function showReports() {
        currentScreenPath = "Reports.ui.qml"
        currentScreen = 3
    }

    // Window control functions
    function minimize() {
        appWindow.showMinimized()
    }

    function maximize() {
        if (appWindow.visibility === Window.Maximized) {
            appWindow.showNormal()
        } else {
            appWindow.showMaximized()
        }
    }

    function close() {
        appWindow.close()
    }

    // Initialize with Database screen
    Component.onCompleted: {
        showDatabase()
    }
}

