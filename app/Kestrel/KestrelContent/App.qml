import QtQuick
import QtQuick.Controls
import KestrelContent

Window {
    id: appWindow
    width: 1440
    height: 1024

    visible: true
    title: "Kestrel Companion App"
    flags: Qt.FramelessWindowHint | Qt.Window

    property int currentScreen: 2
    property string currentScreenPath: "MapScreen.ui.qml"
    property list<QtObject> openProjects: []
    property string pendingProjectName: ""
    property var projectCreationProgressWindow: null
    property string activeMosaicUrl: ""
    property bool isStitching: false
    property string stitchingStatus: "Create a project to generate a field mosaic."

    function addProject(projectName, projectPath) {
        // Keep the project list in the application model so the header and
        // future project-switching views share the same active project.
        var updatedProjects = openProjects.slice()
        for (var i = 0; i < updatedProjects.length; ++i) {
            updatedProjects[i].isActive = false
        }

        var newProject = Qt.createQmlObject(
                    'import QtQml; QtObject {'
                    + 'property string projectName: ' + JSON.stringify(projectName)
                    + '; property string projectPath: ' + JSON.stringify(projectPath)
                    + '; property string mosaicUrl: ""'
                    + '; property bool isActive: true }',
                    appWindow)
        updatedProjects.push(newProject)
        openProjects = updatedProjects
    }

    function setProjectMosaic(projectPath, mosaicUrl) {
        for (var i = 0; i < openProjects.length; ++i) {
            if (openProjects[i].projectPath === projectPath) {
                openProjects[i].mosaicUrl = mosaicUrl
                if (openProjects[i].isActive) {
                    activeMosaicUrl = mosaicUrl
                }
                return
            }
        }
    }

    function showProjectCreationProgress(projectName) {
        pendingProjectName = projectName
        if (projectCreationProgressWindow === null) {
            projectCreationProgressWindow = projectCreationProgressComponent.createObject(null)
        }

        projectCreationProgressWindow.projectName = projectName
        projectCreationProgressWindow.currentFile = 0
        projectCreationProgressWindow.totalFiles = 0
        projectCreationProgressWindow.statusMessage = "Preparing project..."
        projectCreationProgressWindow.show()
        projectCreationProgressWindow.raise()
        projectCreationProgressWindow.requestActivate()
    }

    Component {
        id: projectCreationProgressComponent

        Window {
            id: progressWindow
            width: 420
            height: 180
            title: "Creating Project"
            flags: Qt.Dialog | Qt.FramelessWindowHint
            modality: Qt.ApplicationModal
            visible: false

            property string projectName: ""
            property int currentFile: 0
            property int totalFiles: 0
            property string statusMessage: "Preparing project..."

            Rectangle {
                anchors.fill: parent
                color: "#ffffff"
                border.color: "#b3b3b3"
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14

                    Text {
                        text: "Creating " + progressWindow.projectName
                        color: "#1e1e1e"
                        font.family: "Inter"
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Text {
                        text: progressWindow.statusMessage
                        color: "#5c5c5c"
                        font.family: "Inter"
                        font.pixelSize: 14
                    }

                    ProgressBar {
                        width: parent.width
                        from: 0
                        to: Math.max(1, progressWindow.totalFiles)
                        value: progressWindow.currentFile
                    }

                    Text {
                        text: progressWindow.totalFiles > 0
                              ? progressWindow.currentFile + " of " + progressWindow.totalFiles + " TIFF files imported"
                              : "Setting up project folders..."
                        color: "#5c5c5c"
                        font.family: "Inter"
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    Connections {
        target: projectCreator

        function onProjectCreationProgress(currentFile, totalFiles) {
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.currentFile = currentFile
                projectCreationProgressWindow.totalFiles = totalFiles
                projectCreationProgressWindow.statusMessage = "Importing TIFF files..."
            }
        }

        function onProjectCreationCompleted(projectPath) {
            addProject(pendingProjectName, projectPath)
            pendingProjectName = ""
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.close()
            }
            stitchingController.stitchProject(projectPath)
        }

        function onProjectCreationFailed(errorMessage) {
            console.error("Project creation failed:", errorMessage)
            pendingProjectName = ""
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.close()
            }
        }
    }

    Connections {
        target: stitchingController

        function onStitchingStarted(projectPath) {
            isStitching = true
            activeMosaicUrl = ""
            stitchingStatus = "Preparing imported TIFF files for stitching..."
        }

        function onStitchingStatusChanged(message) {
            stitchingStatus = message
        }

        function onStitchingCompleted(projectPath, previewUrl) {
            isStitching = false
            stitchingStatus = "Field mosaic ready."
            setProjectMosaic(projectPath, previewUrl)
        }

        function onStitchingFailed(projectPath, errorMessage) {
            isStitching = false
            stitchingStatus = errorMessage
            console.error("Stitching failed:", errorMessage)
        }
    }

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
        property var applicationModel: appWindow
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

