import QtQuick
import QtQuick.Shapes
import QtQuick.Layouts

Column {
    id: viewHeader

    property int currentScreen: 2
    property list<QtObject> projectsList: [
        QtObject { property string projectName: "Image_001"; property bool isActive: true },
        QtObject { property string projectName: "Image_002"; property bool isActive: false },
        QtObject { property string projectName: "Image_003"; property bool isActive: false }
    ]
    property var appWindow: null

    anchors.left: parent.left
    anchors.right: parent.right
    spacing: 0
    
    // Sync currentScreen with app's currentScreen
    Binding {
        target: viewHeader
        property: "currentScreen"
        value: viewHeader.appWindow && typeof viewHeader.appWindow.currentScreen !== 'undefined' ? viewHeader.appWindow.currentScreen : 2
        when: viewHeader.appWindow !== null
    }
    
    // Sync projectsList with app's openProjects
    Binding {
        target: viewHeader
        property: "projectsList"
        value: viewHeader.appWindow && viewHeader.appWindow.openProjects !== undefined ? viewHeader.appWindow.openProjects : viewHeader.projectsList
        when: viewHeader.appWindow !== null && viewHeader.appWindow.openProjects !== undefined
    }

    Item {
        id: topBar

        height: 70
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true

        Rectangle {
            id: menu_Separator_4

            y: 69

            height: 1
            anchors.left: parent.left
            anchors.right: parent.right
            border.color: "#000000"
            border.width: 1
            color: "transparent"
            radius: 8

            Rectangle {
                id: rule_4

                height: 1
                anchors.left: parent.left
                anchors.right: parent.right
                color: "#b3b3b3"
            }
        }
        Item {
            id: tabs

            y: 32

            height: 38
            anchors.horizontalCenter: parent.horizontalCenter
            width: 440

            // Animation duration in milliseconds - adjust for different velocities
            property int animationDuration: 300

            // Computed target position and width based on currentScreen
            property int targetX: {
                switch(viewHeader.currentScreen) {
                    case 0: return 105; // Database
                    case 1: return 200; // Flight Logs
                    case 3: return 308; // Reports
                    default: return 47;  // Map
                }
            }
            property int targetWidth: {
                switch(viewHeader.currentScreen) {
                    case 0: return 95;   // Database
                    case 1: return 108;  // Flight Logs
                    case 3: return 108;  // Reports
                    default: return 58;  // Map
                }
            }

            Text {
                id: mapText
                x: 47
                y: 0
                width: 58
                height: 38
                color: viewHeader.currentScreen === 2 ? "#303030" : "#767676"
                text: "Map"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                lineHeight: 22.4
                lineHeightMode: Text.FixedHeight
                textFormat: Text.PlainText
                font.weight: Font.Normal
                font.family: "Inter"
            }
            MouseArea {
                x: 47
                y: 0
                width: 58
                height: 38
                onClicked: if (viewHeader.appWindow) viewHeader.appWindow.showMap()
            }

            Text {
                id: dbText
                x: 105
                y: 0
                width: 95
                height: 38
                color: viewHeader.currentScreen === 0 ? "#303030" : "#767676"
                text: "Database"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                lineHeight: 22.4
                lineHeightMode: Text.FixedHeight
                textFormat: Text.PlainText
                font.weight: Font.Normal
                font.family: "Inter"
            }
            MouseArea {
                x: 105
                y: 0
                width: 95
                height: 38
                onClicked: if (viewHeader.appWindow) viewHeader.appWindow.showDatabase()
            }

            Text {
                id: flText
                x: 200
                y: 0
                width: 108
                height: 38
                color: viewHeader.currentScreen === 1 ? "#303030" : "#767676"
                text: "Flight Logs"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                lineHeight: 22.4
                lineHeightMode: Text.FixedHeight
                textFormat: Text.PlainText
                font.weight: Font.Normal
                font.family: "Inter"
            }
            MouseArea {
                x: 200
                y: 0
                width: 108
                height: 38
                onClicked: if (viewHeader.appWindow) viewHeader.appWindow.showFlightLogs()
            }

            Text {
                id: reportsText
                x: 308
                y: 0
                width: 108
                height: 38
                color: viewHeader.currentScreen === 3 ? "#303030" : "#767676"
                text: "Reports"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                lineHeight: 22.4
                lineHeightMode: Text.FixedHeight
                textFormat: Text.PlainText
                font.weight: Font.Normal
                font.family: "Inter"
            }
            MouseArea {
                x: 308
                y: 0
                width: 108
                height: 38
                onClicked: if (viewHeader.appWindow) viewHeader.appWindow.showReports()
            }

            Rectangle {
                id: selectedViewStroke
                y: 37
                height: 1
                color: "#303030"
                x: tabs.targetX
                width: tabs.targetWidth

                PropertyAnimation {
                    id: xAnimation
                    target: selectedViewStroke
                    property: "x"
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
                
                PropertyAnimation {
                    id: widthAnimation
                    target: selectedViewStroke
                    property: "width"
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
                
                Connections {
                    target: tabs
                    function onTargetXChanged() {
                        xAnimation.stop();
                        xAnimation.from = selectedViewStroke.x;
                        xAnimation.to = tabs.targetX;
                        xAnimation.start();
                    }
                    function onTargetWidthChanged() {
                        widthAnimation.stop();
                        widthAnimation.from = selectedViewStroke.width;
                        widthAnimation.to = tabs.targetWidth;
                        widthAnimation.start();
                    }
                }
            }
        }
        Rectangle {
            id: frame_2

            height: 32
            anchors.left: parent.left
            anchors.right: parent.right
            color: "#f2f2f7"
            anchors.top: parent.top

            Rectangle {
                id: parts_Title_Bar_Caption_Control_Group


                height: 32
                width: 138

                color: "transparent"
                anchors.right: parent.right
                topRightRadius: 7

                Item {
                    id: parts_Title_Bar_Caption_Control_Button

                    height: 32
                    width: 46

                    Rectangle {
                        id: base

                        height: 32
                        width: 46

                        color: mouseArea1.containsMouse ? "#e0e0e0" : "#00ffffff"
                    }
                    Text {
                        id: chromeMinimize

                        x: 15
                        y: 8

                        height: 16
                        width: 17

                        color: "#e4000000"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 10
                        font.weight: Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        lineHeight: 16
                        lineHeightMode: Text.FixedHeight
                        text: ""
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: mouseArea1
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: windowController.minimize()
                    }
                }
                Item {
                    id: parts_Title_Bar_Caption_Control_Button_1

                    x: 46

                    height: 32
                    width: 46

                    Rectangle {
                        id: base_1

                        height: 32
                        width: 46

                        color: mouseArea2.containsMouse ? "#e0e0e0" : "#00ffffff"
                    }
                    Text {
                        id: chromeMaximize

                        x: 15
                        y: 8

                        height: 16
                        width: 17

                        color: "#e4000000"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 10
                        font.weight: Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        lineHeight: 16
                        lineHeightMode: Text.FixedHeight
                        text: windowController.maximized ? "\uE923" : "\uE922"
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: mouseArea2
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: windowController.maximize()
                    }
                }
                Item {
                    id: parts_Title_Bar_Caption_Control_Button_2

                    x: 92

                    height: 32
                    width: 46

                    Rectangle {
                        id: base_2

                        height: 32
                        width: 46

                        color: mouseArea3.containsMouse ? "#c70039" : "#00ffffff"
                    }
                    Text {
                        id: chromeClose

                        x: 15
                        y: 8

                        height: 16
                        width: 17

                        color: "#e4000000"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 10
                        font.weight: Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        lineHeight: 16
                        lineHeightMode: Text.FixedHeight
                        text: ""
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: mouseArea3
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: windowController.closeWindow()
                    }
                }
            }
            Image {
                id: image_1

                y: 1

                source: Qt.resolvedUrl("assets/image_5.png")
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                id: chromeMinimize_1

                x: 3
                y: 8

                height: 16
                width: 39

                color: "#e4000000"
                font.family: "Roboto"
                font.pixelSize: 12
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 16
                lineHeightMode: Text.FixedHeight
                text: "File"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                id: chromeMinimize_2

                x: 48
                y: 8

                height: 16
                width: 39

                color: "#e4000000"
                font.family: "Roboto"
                font.pixelSize: 12
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 16
                lineHeightMode: Text.FixedHeight
                text: "Edit"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                id: chromeMinimize_3

                x: 93
                y: 8

                height: 16
                width: 39

                color: "#e4000000"
                font.family: "Roboto"
                font.pixelSize: 12
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 16
                lineHeightMode: Text.FixedHeight
                text: "View"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                id: chromeMinimize_4

                x: 138
                y: 8

                height: 16
                width: 55

                color: "#e4000000"
                font.family: "Roboto"
                font.pixelSize: 12
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 16
                lineHeightMode: Text.FixedHeight
                text: "Settings"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Item {
        id: imageSelector

        y: 0

        height: 33
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true

        Item {
            id: frame_4

            height: 33
            anchors.left: parent.left
            anchors.right: parent.right

            function addNewProject() {
                // Store current scroll position before adding
                projectsFlickable.savedScrollX = projectsFlickable.contentX;
                
                var newProject = Qt.createQmlObject(
                    'import QtQml; QtObject { property string projectName: "Image_' + String(viewHeader.projectsList.length + 1).padStart(3, "0") + '"; property bool isActive: false }',
                    frame_4
                );
                var updatedProjects = viewHeader.projectsList.slice();
                updatedProjects.push(newProject);
                
                // Update local list
                viewHeader.projectsList = updatedProjects;
                
                // Update app window's openProjects if available
                if (viewHeader.appWindow && typeof viewHeader.appWindow.openProjects !== 'undefined') {
                    viewHeader.appWindow.openProjects = updatedProjects;
                }
            }

            function scrollRight() {
                // Calculate current number of visible cards and scroll by that amount
                var cardEffectiveWidth = 199;  // 200 - 1 spacing
                var visibleCards = Math.floor(projectsFlickable.width / cardEffectiveWidth);
                var newContentX = projectsFlickable.contentX + (visibleCards * cardEffectiveWidth);
                projectsFlickable.contentX = newContentX;
            }

            function scrollLeft() {
                // Calculate current number of visible cards and scroll by that amount
                var cardEffectiveWidth = 199;  // 200 - 1 spacing
                var visibleCards = Math.floor(projectsFlickable.width / cardEffectiveWidth);
                var newContentX = projectsFlickable.contentX - (visibleCards * cardEffectiveWidth);
                projectsFlickable.contentX = newContentX;
            }

            // Left arrow button (overlays on cards)
            Rectangle {
                id: leftArrow
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                height: 33
                width: projectsFlickable.contentX > 0 ? 33 : 0
                color: "#ffffff"
                border.color: "#b3b3b3"
                border.width: 1
                clip: true
                z: 10
                
                Shape {
                    anchors.centerIn: parent
                    height: 16
                    width: 10
                    
                    ShapePath {
                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 0
                        
                        PathSvg {
                            path: "M 10 0 L 0 8 L 10 16 L 10 0 Z"
                        }
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    onClicked: frame_4.scrollLeft()
                }
            }

            // Right arrow button (overlays on cards)
            Rectangle {
                id: rightArrow
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 33
                width: projectsFlickable.contentX < (projectsFlickable.contentWidth - projectsFlickable.width) ? 33 : 0
                color: "#ffffff"
                border.color: "#b3b3b3"
                border.width: 1
                clip: true
                z: 10
                
                Shape {
                    anchors.centerIn: parent
                    height: 16
                    width: 10
                    
                    ShapePath {
                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 0
                        
                        PathSvg {
                            path: "M 0 0 L 10 8 L 0 16 L 0 0 Z"
                        }
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    onClicked: frame_4.scrollRight()
                }
            }

            Flickable {
                id: projectsFlickable
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                
                property real savedScrollX: 0
                
                contentWidth: projectCardsRow.width
                contentHeight: 33
                flickableDirection: Flickable.HorizontalFlick
                
                Row {
                    id: projectCardsRow
                    height: 33
                    spacing: -1
                    
                    onWidthChanged: {
                        // Restore saved scroll position when row is recalculated after adding a card
                        if (projectsFlickable.savedScrollX !== undefined && projectsFlickable.savedScrollX !== null) {
                            projectsFlickable.contentX = projectsFlickable.savedScrollX;
                            projectsFlickable.savedScrollX = null;
                        }
                    }

                    Repeater {
                        model: viewHeader.projectsList
                        
                        Rectangle {
                            id: projectCard
                            
                            height: 33
                            width: 200
                            
                            border.color: "#b3b3b3"
                            border.width: 1
                            color: "transparent"
                            
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 1
                                color: modelData.isActive ? "#f2f2f7" : "#ffffff"
                            }
                            
                            Text {
                                anchors.centerIn: parent
                                height: 33
                                width: 201
                                
                                color: "#000000"
                                font.family: "Inter"
                                font.pixelSize: 16
                                font.weight: Font.Normal
                                horizontalAlignment: Text.AlignHCenter
                                lineHeight: 22.40
                                lineHeightMode: Text.FixedHeight
                                text: modelData.projectName
                                textFormat: Text.PlainText
                                verticalAlignment: Text.AlignVCenter
                            }
                            
                            Item {
                                x: 164
                                y: 6
                                height: 20
                                width: 20
                                clip: true
                                
                                Shape {
                                    x: 4.17
                                    y: 4.17
                                    height: 11.67
                                    width: 11.67
                                    
                                    ShapePath {
                                        fillColor: "#1d1b20"
                                        fillRule: ShapePath.WindingFill
                                        joinStyle: ShapePath.MiterJoin
                                        strokeColor: "#00000000"
                                        strokeStyle: ShapePath.SolidLine
                                        strokeWidth: 0.03
                                        
                                        PathSvg {
                                            path: "M 1.1666666785875952 11.666666984558105 L 0 10.500000603993742 L 4.666666714350381 5.833333492279053 L 0 1.1666666785875952 L 1.1666666785875952 0 L 5.833333492279053 4.666666714350381 L 10.500000603993742 0 L 11.666666984558105 1.1666666785875952 L 7.000000667572035 5.833333492279053 L 11.666666984558105 10.500000603993742 L 10.500000603993742 11.666666984558105 L 5.833333492279053 7.000000667572035 L 1.1666666785875952 11.666666984558105 Z"
                                        }
                                    }
                                }
                                
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        // Close/remove project logic here
                                    }
                                }
                            }
                        }
                    }

                    // Add button inside the row (flows with cards)
                    Rectangle {
                        id: add
                        height: 33
                        width: 33
                        
                        color: "#ffffff"
                        border.color: "#b3b3b3"
                        border.width: 1
                        clip: true
                        
                        Shape {
                            anchors.centerIn: parent
                            height: 16
                            width: 16
                            
                            ShapePath {
                                fillColor: "#323232"
                                fillRule: ShapePath.WindingFill
                                joinStyle: ShapePath.MiterJoin
                                strokeColor: "#00000000"
                                strokeStyle: ShapePath.SolidLine
                                strokeWidth: 0
                                
                                PathSvg {
                                    path: "M 8 0 C 8.44183 0 8.8 0.358172 8.8 0.8 L 8.8 7.2 L 15.2 7.2 C 15.6418 7.2 16 7.55817 16 8 C 16 8.44183 15.6418 8.8 15.2 8.8 L 8.8 8.8 L 8.8 15.2 C 8.8 15.6418 8.44183 16 8 16 C 7.55817 16 7.2 15.6418 7.2 15.2 L 7.2 8.8 L 0.8 8.8 C 0.358172 8.8 0 8.44183 0 8 C 0 7.55817 0.358172 7.2 0.8 7.2 L 7.2 7.2 L 7.2 0.8 C 7.2 0.358172 7.55817 0 8 0 Z"
                                }
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            onClicked: if (viewHeader.appWindow) viewHeader.appWindow.openCreateProjectDialog()
                        }
                    }
                }
            }
        }
        
        Rectangle {
            id: menu_Separator_3

            y: 32

            height: 1
            anchors.left: parent.left
            anchors.right: parent.right

            border.color: "#000000"
            border.width: 1
            color: "transparent"
            radius: 8

            Rectangle {
                id: rule_3

                height: 1
                anchors.left: parent.left
                anchors.right: parent.right

                color: "#b3b3b3"
            }
        }
    }

}
