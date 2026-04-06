import QtQuick
import QtQuick.Shapes

Rectangle {
    id: create_Project

    height: 500
    width: 400

    border.color: "#b3b3b3"
    border.width: 1
    clip: true
    color: "#ffffff"
    
    property var parentWindow: null

    Item {
        id: group_8

        x: 14

        height: 32
        width: 386

        Rectangle {
            id: parts_Title_Bar_Caption_Control_Group

            x: 294

            height: 32
            width: 92

            color: "transparent"
            topRightRadius: 7

            Item {
                id: parts_Title_Bar_Caption_Control_Button

                height: 32
                width: 46

                Rectangle {
                    id: base

                    height: 32
                    width: 46

                    color: "#00ffffff"
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
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (create_Project.parentWindow) {
                            create_Project.parentWindow.showMinimized()
                        }
                    }
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

                    color: "#00ffffff"
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
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (create_Project.parentWindow) {
                            create_Project.parentWindow.close()
                        }
                    }
                }
            }
        }
        Image {
            id: image_1

            y: 8

            source: Qt.resolvedUrl("assets/image_6.png")
        }
        Text {
            id: create_project_1

            x: 28
            y: 6

            height: 20
            width: 90

            color: "#000000"
            font.family: "Inter"
            font.pixelSize: 12
            font.weight: Font.Normal
            horizontalAlignment: Text.AlignLeft
            lineHeight: 16.80
            lineHeightMode: Text.FixedHeight
            text: "Create project"
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
    }
    Item {
        id: frame_17

        x: 14
        y: 110

        height: 150
        width: 371

        Item {
            id: file_plus

            x: 13
            y: 36

            height: 40
            width: 40

            clip: true

            Shape {
                id: icon

                x: 6.67
                y: 3.33

                height: 33.33
                width: 26.67

                ShapePath {
                    id: icon_ShapePath0

                    fillColor: "#00000000"
                    fillRule: ShapePath.WindingFill
                    strokeColor: "#1e1e1e"
                    strokeWidth: 3.50

                    PathSvg {
                        id: icon_ShapePath0_PathSvg0

                        path: "M 16.66666603088379 0 L 3.3333332538604736 0 C 2.4492782950401306 7.40148665436918e-16 1.6014317870140076 0.3511893153190613 0.9763105511665344 0.9763105511665344 C 0.3511893153190613 1.6014317870140076 2.960594661747672e-15 2.4492782950401306 0 3.3333332538604736 L 0 30 C 1.480297330873836e-15 30.884054958820343 0.3511893153190613 31.731900095939636 0.9763105511665344 32.35702133178711 C 1.6014317870140076 32.98214256763458 2.4492782950401306 33.33333206176758 3.3333332538604736 33.33333206176758 L 23.333332061767578 33.33333206176758 C 24.21738702058792 33.33333206176758 25.065234065055847 32.98214256763458 25.69035530090332 32.35702133178711 C 26.315476536750793 31.731900095939636 26.66666603088379 30.884054958820343 26.66666603088379 30 L 26.66666603088379 10 L 16.66666603088379 0 Z"
                    }
                }
                ShapePath {
                    id: icon_ShapePath1

                    fillColor: "#00000000"
                    fillRule: ShapePath.WindingFill
                    strokeColor: "#1e1e1e"
                    strokeWidth: 3.50

                    PathSvg {
                        id: icon_ShapePath1_PathSvg0

                        path: "M 16.66666603088379 0 L 16.666667938232422 10 L 26.66666603088379 10"
                    }
                }
            }
        }
        Rectangle {
            id: rectangle_238

            height: 124
            width: 371

            border.color: "#000000"
            border.width: 1
            color: "transparent"
        }
        Text {
            id: import_Files

            x: 64
            y: 35

            height: 21
            width: 244

            color: "#000000"
            font.family: "Inter"
            font.pixelSize: 16
            font.weight: Font.Normal
            horizontalAlignment: Text.AlignLeft
            lineHeight: 22.40
            lineHeightMode: Text.FixedHeight
            text: "Import Files"
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            id: can_select_files_or_folders_of_TIFF_s

            x: 64
            y: 55

            height: 21
            width: 308

            color: "#b3b3b3"
            font.family: "Inter"
            font.pixelSize: 16
            font.weight: Font.Normal
            horizontalAlignment: Text.AlignLeft
            lineHeight: 22.40
            lineHeightMode: Text.FixedHeight
            text: "Can select files or folders of *.TIFF’s"
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            id: x_files_imported

            x: 245
            y: 129

            height: 21
            width: 127

            color: "#000000"
            font.family: "Inter"
            font.pixelSize: 12
            font.weight: Font.Normal
            horizontalAlignment: Text.AlignRight
            lineHeight: 16.80
            lineHeightMode: Text.FixedHeight
            text: "X files imported"
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
    }
    Text {
        id: options

        x: 14
        y: 270

        height: 21
        width: 127

        color: "#000000"
        font.family: "Inter"
        font.pixelSize: 16
        font.weight: Font.Normal
        horizontalAlignment: Text.AlignLeft
        lineHeight: 22.40
        lineHeightMode: Text.FixedHeight
        text: "Options"
        textFormat: Text.PlainText
        verticalAlignment: Text.AlignVCenter
    }
    Item {
        id: group_6

        x: 14
        y: 68

        height: 27
        width: 336

        Item {
            id: input_Field

            x: 85

            height: 27
            width: 251

            Rectangle {
                id: input

                height: 27
                width: 251

                border.color: "#d9d9d9"
                border.width: 1
                color: "#ffffff"
                radius: 8

                Text {
                    id: value

                    x: 16
                    y: 5.50

                    height: 16
                    width: 220

                    color: "#1e1e1e"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    lineHeight: 16
                    lineHeightMode: Text.FixedHeight
                    text: ""
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.WordWrap
                }
            }
        }
        Text {
            id: project_Home

            y: 4

            height: 20
            width: 89.92

            color: "#000000"
            font.family: "Inter"
            font.pixelSize: 12
            font.weight: Font.Normal
            horizontalAlignment: Text.AlignLeft
            lineHeight: 16.80
            lineHeightMode: Text.FixedHeight
            text: "Project Home"
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
    }
    Item {
        id: group_5

        x: 14
        y: 32

        height: 27
        width: 371

        Item {
            id: input_Field_1

            x: 85

            height: 27
            width: 286

            Text {
                id: label

                height: 22
                width: 287

                color: "#1e1e1e"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Label"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                visible: false
                wrapMode: Text.WordWrap
            }
            Text {
                id: description

                height: 22
                width: 287

                color: "#757575"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Description"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                visible: false
                wrapMode: Text.WordWrap
            }
            Rectangle {
                id: input_1

                height: 27
                width: 286

                border.color: "#d9d9d9"
                border.width: 1
                color: "#ffffff"
                radius: 8

                Text {
                    id: value_1

                    x: 16
                    y: 5.50

                    height: 16
                    width: 255

                    color: "#1e1e1e"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    lineHeight: 16
                    lineHeightMode: Text.FixedHeight
                    text: ""
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                id: error

                y: 35

                height: 22
                width: 38

                color: "#1e1e1e"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Error"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                visible: false
                wrapMode: Text.WordWrap
            }
        }
        Text {
            id: project_Name

            y: 4

            height: 20
            width: 86

            color: "#000000"
            font.family: "Inter"
            font.pixelSize: 12
            font.weight: Font.Normal
            horizontalAlignment: Text.AlignLeft
            lineHeight: 16.80
            lineHeightMode: Text.FixedHeight
            text: "Project Name"
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
    }
    Item {
        id: folder

        x: 358
        y: 68

        height: 27
        width: 27

        clip: true

        Shape {
            id: icon_1

            x: 2.25
            y: 3.37

            height: 20.25
            width: 22.50

            ShapePath {
                id: icon_1_ShapePath0

                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                strokeColor: "#1e1e1e"
                strokeWidth: 3

                PathSvg {
                    id: icon_1_ShapePath0_PathSvg0

                    path: "M 22.500001907348633 18.000000536441803 C 22.500001907348633 18.596737693995237 22.262948064655056 19.169034026563168 21.84099117457562 19.590990900993347 C 21.419034284496185 20.012947775423527 20.84673793070331 20.25 20.250000751018487 20.25 L 2.2500001505017266 20.25 C 1.6532629708169027 20.25 1.080966566732607 20.012947775423527 0.6590096766531723 19.590990900993347 C 0.23705278657373774 19.169034026563168 4.996003944994169e-16 18.596737693995237 0 18.000000536441803 L 0 2.2500000670552254 C -2.4980019724970844e-16 1.653262909501791 0.23705278657373774 1.0809665266424417 0.6590096766531723 0.6590096522122622 C 1.080966566732607 0.23705277778208256 1.6532629708169027 4.996003759705715e-16 2.2500001505017266 0 L 7.875000828504568 0 L 10.125000375509243 3.375 L 20.250000751018487 3.375 C 20.84673793070331 3.375000000000001 21.419034284496185 3.6120530292391777 21.84099117457562 4.034009903669357 C 22.262948064655056 4.455966778099537 22.500001907348633 5.028263110667467 22.500001907348633 5.6250002682209015 L 22.500001907348633 18.000000536441803 Z"
                }
            }
        }
    }
    Item {
        id: group_7

        x: 209
        y: 456

        height: 30
        width: 176

        Rectangle {
            id: button

            x: 93

            height: 30
            width: 83

            border.color: "#2c2c2c"
            border.width: 1
            clip: true
            color: "#2c2c2c"
            radius: 8

            Text {
                id: button_1

                x: 16
                y: 7

                height: 16
                width: 52

                color: "#f5f5f5"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                lineHeight: 16
                lineHeightMode: Text.FixedHeight
                text: "Create"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WordWrap
            }
        }
        Rectangle {
            id: button_2

            height: 30
            width: 83

            border.color: "#767676"
            border.width: 1
            clip: true
            color: "#e3e3e3"
            radius: 8

            Text {
                id: button_3

                x: 15
                y: 7

                height: 16
                width: 54

                color: "#1e1e1e"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                lineHeight: 16
                lineHeightMode: Text.FixedHeight
                text: "Cancel"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WordWrap
            }
        }
    }
    Rectangle {
        id: menu_Separator

        x: 14
        y: 102

        height: 1
        width: 371

        border.color: "#000000"
        border.width: 1
        color: "transparent"
        radius: 8

        Rectangle {
            id: rule

            height: 1
            width: 371

            color: "#b3b3b3"
        }
    }
    Rectangle {
        id: menu_Separator_1

        x: 14
        y: 262

        height: 1
        width: 371

        border.color: "#000000"
        border.width: 1
        color: "transparent"
        radius: 8

        Rectangle {
            id: rule_1

            height: 1
            width: 371

            color: "#b3b3b3"
        }
    }
    Rectangle {
        id: menu_Separator_2

        x: 134.50
        y: 362.50

        height: 1
        width: 130

        border.color: "#000000"
        border.width: 1
        color: "transparent"
        radius: 8
        rotation: -90

        Rectangle {
            id: rule_2

            height: 1
            width: 130

            color: "#b3b3b3"
        }
    }
    Rectangle {
        id: menu_Separator_3

        x: 14
        y: 446

        height: 1
        width: 371

        border.color: "#000000"
        border.width: 1
        color: "transparent"
        radius: 8

        Rectangle {
            id: rule_3

            height: 1
            width: 371

            color: "#b3b3b3"
        }
    }
    Item {
        id: checkbox_Field

        x: 14
        y: 298

        height: 39
        width: 151

        Item {
            id: checkbox_and_Label

            height: 22
            width: 151

            Rectangle {
                id: checkbox

                y: 3

                height: 16
                width: 16

                clip: true
                color: "#2c2c2c"
                radius: 4

                Item {
                    id: check

                    height: 16
                    width: 16

                    clip: true

                    Shape {
                        id: icon_2

                        x: 2.67
                        y: 4

                        height: 7.33
                        width: 10.67

                        ShapePath {
                            id: icon_2_ShapePath0

                            fillColor: "#00000000"
                            fillRule: ShapePath.WindingFill
                            strokeColor: "#f5f5f5"
                            strokeWidth: 1.60

                            PathSvg {
                                id: icon_2_ShapePath0_PathSvg0

                                path: "M 10.666666030883789 0 L 3.33333308696747 7.3333330154418945 L 0 3.999999722567469"
                            }
                        }
                    }
                }
            }
            Text {
                id: toggle_Stitching

                x: 28

                height: 22
                width: 124

                color: "#1e1e1e"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Toggle Stitching"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WordWrap
            }
        }
        Item {
            id: description_Row

            y: 22

            height: 17
            width: 131

            Item {
                id: space

                y: 0.50

                height: 16
                width: 16

                clip: true
            }
            Text {
                id: for_single_images_

                x: 28

                height: 17
                width: 104

                color: "#757575"
                font.family: "Inter"
                font.pixelSize: 12
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                lineHeight: 16.80
                lineHeightMode: Text.FixedHeight
                text: "For single images."
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WordWrap
            }
        }
    }
}