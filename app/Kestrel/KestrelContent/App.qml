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

    // Overlay to block interaction when dialog is open
    Rectangle {
        id: dialogOverlay
        anchors.fill: parent
        color: "#00000000"
        visible: false
        z: 1000
        
        MouseArea {
            anchors.fill: parent
            enabled: dialogOverlay.visible
            onClicked: {
                if (appWindow.createProjectWindow) {
                    // Play system alert sound
                    systemAlert.playSound()
                    // Restore if minimized
                    if (appWindow.createProjectWindow.visibility === Window.Minimized) {
                        appWindow.createProjectWindow.showNormal()
                    }
                    // Bring dialog to front
                    appWindow.createProjectWindow.raise()
                    appWindow.createProjectWindow.requestActivate()
                }
            }
        }
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
    
    function setDialogBlocking(blocking: bool) {
        dialogOverlay.visible = blocking
    }

    property var createProjectWindow: null

    function openCreateProjectDialog() {
        if (createProjectWindow === null) {
            let component = Qt.createComponent("CreateProjectWindow.qml")
            if (component.status === Component.Ready) {
                createProjectWindow = component.createObject(null, { parentAppWindow: appWindow })
            } else {
                console.error("Failed to load CreateProjectWindow:", component.errorString())
                return
            }
        }
        
        // Reset fields when opening the dialog
        createProjectWindow.resetFields()
        
        setDialogBlocking(true)
        createProjectWindow.visible = true
        createProjectWindow.raise()
        createProjectWindow.requestActivate()
    }

    Component.onCompleted: {
        showMap()
    }
}

