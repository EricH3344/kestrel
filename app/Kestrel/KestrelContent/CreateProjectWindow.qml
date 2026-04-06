import QtQuick
import QtQuick.Controls

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
            anchors.fill: parent
            parentWindow: createProjectWindow
        }
    }
}
