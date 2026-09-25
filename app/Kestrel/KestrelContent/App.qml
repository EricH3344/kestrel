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
    property string activeProjectPath: ""
    property string activeProjectFile: ""
    property string activeFlightPath: ""
    property var activeFlights: []
    property string activeFlightId: ""
    property var activeMapMetadata: ({})
    property bool isStitching: false
    property string stitchingStatus: "Create a project to generate a field mosaic."

    function addProject(projectName, projectPath) {
        // Keep the project list in the application model so the header and
        // future project-switching views share the same active project.
        var updatedProjects = openProjects.slice()
        for (var i = 0; i < updatedProjects.length; ++i) {
            updatedProjects[i].isActive = false
            if (updatedProjects[i].projectPath === projectPath) {
                updatedProjects[i].isActive = true
                openProjects = updatedProjects
                activeMosaicUrl = updatedProjects[i].mosaicUrl
                activeProjectPath = projectPath
                activeMapMetadata = updatedProjects[i].mapMetadata
                return
            }
        }

        var newProject = Qt.createQmlObject(
                    'import QtQml; QtObject {'
                    + 'property string projectName: ' + JSON.stringify(projectName)
                    + '; property string projectPath: ' + JSON.stringify(projectPath)
                    + '; property string mosaicUrl: ""'
                    + '; property var mapMetadata: ({})'
                    + '; property bool isActive: true }',
                    appWindow)
        updatedProjects.push(newProject)
        openProjects = updatedProjects
        activeProjectPath = projectPath
        activeMapMetadata = ({})
    }

    function openExistingProject() {
        const filePath = fileDialogHelper.selectProjectFile()
        if (!filePath)
            return
        const project = projectLoader.openProject(filePath)
        if (!project.projectPath)
            return

        addProject(project.projectName, project.projectPath)
        activeProjectFile = project.projectFile
        activeFlights = project.flights
        showMap()
        if (activeFlights.length > 0)
            selectFlight(activeFlights[0], true)
    }

    function selectFlight(flight, refreshPreview) {
        activeFlightId = flight.id
        activeFlightPath = flight.flightPath
        activeMosaicUrl = flight.previewUrl || ""
        activeMapMetadata = projectLoader.mapMetadata(flight.flightPath)
        setProjectMosaic(activeProjectPath, activeMosaicUrl)
        setProjectMapMetadata(activeProjectPath, activeMapMetadata)
        stitchingStatus = activeMosaicUrl ? "Field mosaic ready."
                                          : "This flight has no ODM mosaic yet."
        if (flight.hasOrthophoto && (refreshPreview || !activeMosaicUrl) && !isStitching) {
            stitchingStatus = "Refreshing mosaic preview from ODM's GeoTIFF..."
            stitchingController.refreshPreview(flight.flightPath)
        }
    }

    function addFlightFromFolder() {
        if (!activeProjectFile)
            return
        const files = fileDialogHelper.selectTiffFilesFromFolder()
        if (!files.length)
            return
        showProjectCreationProgress("Importing flight")
        projectCreationProgressWindow.statusMessage = "Copying flight TIFF files..."
        const added = projectLoader.addFlight(activeProjectFile, files)
        if (!added.flightPath) {
            if (projectCreationProgressWindow)
                projectCreationProgressWindow.close()
            return
        }
        const project = projectLoader.openProject(activeProjectFile)
        activeFlights = project.flights
        for (let i = 0; i < activeFlights.length; ++i) {
            if (activeFlights[i].id === added.flightId) {
                selectFlight(activeFlights[i], false)
                break
            }
        }
        stitchingController.stitchProject(added.flightPath)
    }

    Dialog {
        id: projectOpenErrorDialog
        anchors.centerIn: parent
        title: "Cannot open project"
        modal: true
        standardButtons: Dialog.Ok
        property string errorMessage: ""

        contentItem: Text {
            text: projectOpenErrorDialog.errorMessage
            width: 320
            wrapMode: Text.WordWrap
            color: "#303030"
        }
    }

    Connections {
        target: projectLoader
        function onProjectLoadFailed(errorMessage) {
            projectOpenErrorDialog.errorMessage = errorMessage
            projectOpenErrorDialog.open()
        }
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

    function setProjectMapMetadata(projectPath, metadata) {
        for (var i = 0; i < openProjects.length; ++i) {
            if (openProjects[i].projectPath === projectPath) {
                openProjects[i].mapMetadata = metadata
                if (openProjects[i].isActive)
                    activeMapMetadata = metadata
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
        projectCreationProgressWindow.percent = 0
        projectCreationProgressWindow.waitingForOdm = false
        projectCreationProgressWindow.statusMessage = "Preparing project..."
        projectCreationProgressWindow.detailMessage = "Setting up project folders..."
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
            title: "Preparing Field Mosaic"
            flags: Qt.Dialog | Qt.FramelessWindowHint
            modality: Qt.ApplicationModal
            visible: false

            property string projectName: ""
            property int currentFile: 0
            property int totalFiles: 0
            property real percent: 0
            property bool waitingForOdm: false
            property string statusMessage: "Preparing project..."
            property string detailMessage: "Setting up project folders..."

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
                        text: "Preparing " + progressWindow.projectName
                        color: "#1e1e1e"
                        font.family: "Inter"
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Text {
                        text: progressWindow.statusMessage
                        width: parent.width
                        elide: Text.ElideRight
                        color: "#5c5c5c"
                        font.family: "Inter"
                        font.pixelSize: 14
                    }

                    ProgressBar {
                        width: parent.width
                        from: 0
                        to: 100
                        value: progressWindow.percent
                        indeterminate: progressWindow.waitingForOdm
                    }

                    Text {
                        text: progressWindow.detailMessage
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
                projectCreationProgressWindow.percent = totalFiles > 0
                        ? 10 * currentFile / totalFiles : 0
                projectCreationProgressWindow.statusMessage = "Importing TIFF files..."
                projectCreationProgressWindow.detailMessage = currentFile + " of " + totalFiles + " TIFF files imported"
            }
        }

        function onProjectCreationCompleted(projectPath) {
            addProject(pendingProjectName, projectPath)
            activeProjectFile = projectPath + "/" + pendingProjectName + ".kproj"
            const project = projectLoader.openProject(activeProjectFile)
            activeFlights = project.flights
            if (activeFlights.length > 0)
                selectFlight(activeFlights[0], false)
            pendingProjectName = ""
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
            if (projectPath === activeFlightPath) {
                activeMosaicUrl = ""
                activeMapMetadata = ({})
            }
            stitchingStatus = "Preparing imported TIFF files for ODM..."
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.percent = Math.max(10, projectCreationProgressWindow.percent)
                projectCreationProgressWindow.waitingForOdm = true
                projectCreationProgressWindow.statusMessage = stitchingStatus
                projectCreationProgressWindow.detailMessage = "Waiting for ODM progress..."
            }
        }

        function onStitchingStatusChanged(message) {
            stitchingStatus = message
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.statusMessage = message
                if (message.indexOf("exporting the Map preview") !== -1) {
                    projectCreationProgressWindow.waitingForOdm = false
                    projectCreationProgressWindow.percent = 95
                    projectCreationProgressWindow.detailMessage = "Exporting Map preview..."
                }
            }
        }

        function onStitchingProgressChanged(percent) {
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.waitingForOdm = false
                projectCreationProgressWindow.percent = Math.max(
                            projectCreationProgressWindow.percent, 10 + 0.85 * percent)
                projectCreationProgressWindow.detailMessage = "ODM processing: " + Math.round(percent) + "%"
            }
        }

        function onStitchingCompleted(projectPath, previewUrl) {
            isStitching = false
            stitchingStatus = "Field mosaic ready."
            const updated = activeFlights.slice()
            for (let i = 0; i < updated.length; ++i) {
                if (updated[i].flightPath === projectPath) {
                    updated[i].previewUrl = previewUrl
                    updated[i].hasOrthophoto = true
                }
            }
            activeFlights = updated
            if (projectPath === activeFlightPath) {
                activeMosaicUrl = previewUrl
                activeMapMetadata = projectLoader.mapMetadata(projectPath)
                setProjectMosaic(activeProjectPath, previewUrl)
                setProjectMapMetadata(activeProjectPath, activeMapMetadata)
            }
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.percent = 100
                projectCreationProgressWindow.close()
            }
        }

        function onStitchingFailed(projectPath, errorMessage) {
            isStitching = false
            stitchingStatus = errorMessage
            if (projectCreationProgressWindow) {
                projectCreationProgressWindow.close()
            }
            console.error("Stitching failed:", errorMessage)
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

