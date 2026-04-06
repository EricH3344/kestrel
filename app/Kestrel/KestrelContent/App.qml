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

    property int currentScreen: 2
    property string currentScreenPath: "MapScreen.ui.qml"
    property list<QtObject> openProjects: [
        QtObject { property string projectName: "Image_001"; property bool isActive: true },
        QtObject { property string projectName: "Image_002"; property bool isActive: false },
        QtObject { property string projectName: "Image_003"; property bool isActive: false }
    ]

    // Window dragging support
    DragHandler {
        target: null
        onActiveChanged: {
            if (active) {
                appWindow.startSystemMove()
            }
        }
    }

    // ViewHeader - persistent across all views
    ViewHeader {
        id: viewHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        appWindow: appWindow
    }

    Loader {
        id: screenLoader
        anchors.top: viewHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
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

    // Initialize with MapScreen
    Component.onCompleted: {
        showMap()
    }
}

