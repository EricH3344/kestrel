import QtQuick
import QtQuick.Controls
import KestrelContent

Window {
    id: createProjectWindow
    width: 400
    height: 500
    title: "Create Project"
    visible: false
    flags: Qt.FramelessWindowHint | Qt.Window
    
    property int lastMouseX: 0
    property int lastMouseY: 0
    property var parentAppWindow: null
    
    function resetFields() {
        createProject.projectName = "new project"
        var defaultPath = pathHelper.getDefaultProjectPath("new project")
        createProject.projectDirectory = defaultPath
        createProject.basePath = pathHelper.getDocumentsPath() + "/Kestrel Projects"
        createProject.importedFiles = []
        createProject.importedFilesCount = 0
    }
    
    onClosing: {
        if (parentAppWindow) {
            parentAppWindow.setDialogBlocking(false)
        }
    }
    
    Rectangle {
        anchors.fill: parent
        color: "#ffffff"
        border.color: "#b3b3b3"
        border.width: 1

        MouseArea {
            id: dragArea
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 32
            cursorShape: Qt.DragMoveCursor
            
            onPressed: (mouse) => {
                createProjectWindow.lastMouseX = mouse.x
                createProjectWindow.lastMouseY = mouse.y
            }
            
            onPositionChanged: (mouse) => {
                if (pressed) {
                    createProjectWindow.x += (mouse.x - createProjectWindow.lastMouseX)
                    createProjectWindow.y += (mouse.y - createProjectWindow.lastMouseY)
                }
            }
        }

        Create_Project {
            id: createProject
            anchors.fill: parent
            parentWindow: createProjectWindow
            
            Component.onCompleted: {
                var defaultPath = pathHelper.getDefaultProjectPath(projectName)
                projectDirectory = defaultPath
                basePath = pathHelper.getDocumentsPath() + "/Kestrel Projects"
            }
            
            // When project name changes, update the full path using basePath + new name
            onProjectNameChanged: {
                projectDirectory = basePath + "/" + projectName
            }
            
            // When directory is manually edited, update the base path for future name changes
            onProjectDirectoryChanged: {
                var newBasePath = createProject.projectDirectory.substring(0, 
                    createProject.projectDirectory.lastIndexOf("/"))
                if (newBasePath !== basePath) {
                    basePath = newBasePath
                }
            }

            // Handle Create button click
            onProjectCreateRequested: (projectName, projectDirectory, importedFiles) => {
                projectCreator.createProject(projectName, projectDirectory, importedFiles)
            }

            // Handle Cancel button click
            onProjectCancelRequested: {
                createProjectWindow.close()
            }
        }

        Connections {
            target: projectCreator
            onProjectCreationCompleted: (projectPath) => {
                console.log("Project created successfully at:", projectPath)
                createProjectWindow.resetFields()
                createProjectWindow.close()
            }
            onProjectCreationFailed: (errorMessage) => {
                console.error("Project creation failed:", errorMessage)
            }
            onProjectCreationStarted: (projectName) => {
                console.log("Creating project:", projectName)
            }
            onProjectCreationProgress: (currentFile, totalFiles) => {
                console.log("Importing file", currentFile, "of", totalFiles)
            }
        }
    }
}
