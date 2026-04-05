import QtQuick
import QtQuick.Shapes
import QtQuick.Layouts

Column {
    id: viewHeader
    
    width: 1440
    spacing: 0

    Item {
        id: topBar

        height: 70
        width: 1440

        clip: true

        Rectangle {
            id: menu_Separator_4

            y: 69

            height: 1
            width: 1440

            border.color: "#000000"
            border.width: 1
            color: "transparent"
            radius: 8

            Rectangle {
                id: rule_4

                height: 1
                width: 1440

                color: "#b3b3b3"
            }
        }
        Item {
            id: tabs

            x: 479
            y: 32

            height: 38
            width: 440

            Image {
                id: tab

                x: 47.50

                source: Qt.resolvedUrl("assets/tab_10.png")
            }
            Image {
                id: tab_1

                x: 104.50

                source: Qt.resolvedUrl("assets/tab_11.png")
            }
            Image {
                id: tab_2

                x: 200.50

                source: Qt.resolvedUrl("assets/tab_12.png")
            }
            Image {
                id: tab_3

                x: 308.50

                source: Qt.resolvedUrl("assets/tab_13.png")
            }
        }
        Rectangle {
            id: frame_2

            height: 32
            width: 1440

            color: "#f2f2f7"

            Rectangle {
                id: parts_Title_Bar_Caption_Control_Group

                x: 1302

                height: 32
                width: 138

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
                        text: ""
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignVCenter
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

                        color: "#00ffffff"
                        topRightRadius: 7
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
                }
            }
            Image {
                id: image_1

                x: 699
                y: 1

                source: Qt.resolvedUrl("assets/image_5.png")
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
        width: 1440

        clip: true

        Item {
            id: frame_4

            height: 33
            width: 636

            Rectangle {
                id: rectangle_1

                height: 33
                width: 200

                border.color: "#b3b3b3"
                border.width: 1
                color: "transparent"

                Rectangle {
                    id: rectangle_2

                    height: 33
                    width: 200

                    color: "#ffffff"
                }
                Text {
                    id: image_001

                    height: 33
                    width: 201

                    color: "#000000"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 22.40
                    lineHeightMode: Text.FixedHeight
                    text: "Image_001"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignVCenter
                }
                Item {
                    id: close_2

                    x: 164
                    y: 6

                    height: 20
                    width: 20

                    clip: true

                    Shape {
                        id: icon_3

                        x: 4.17
                        y: 4.17

                        height: 11.67
                        width: 11.67

                        ShapePath {
                            id: icon_3_ShapePath0

                            fillColor: "#1d1b20"
                            fillRule: ShapePath.WindingFill
                            joinStyle: ShapePath.MiterJoin
                            strokeColor: "#00000000"
                            strokeStyle: ShapePath.SolidLine
                            strokeWidth: 0.03

                            PathSvg {
                                id: icon_3_ShapePath0_PathSvg0

                                path: "M 1.1666666785875952 11.666666984558105 L 0 10.500000603993742 L 4.666666714350381 5.833333492279053 L 0 1.1666666785875952 L 1.1666666785875952 0 L 5.833333492279053 4.666666714350381 L 10.500000603993742 0 L 11.666666984558105 1.1666666785875952 L 7.000000667572035 5.833333492279053 L 11.666666984558105 10.500000603993742 L 10.500000603993742 11.666666984558105 L 5.833333492279053 7.000000667572035 L 1.1666666785875952 11.666666984558105 Z"
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: rectangle_3

                x: 201

                height: 33
                width: 200

                border.color: "#b3b3b3"
                border.width: 1
                color: "transparent"

                Rectangle {
                    id: rectangle_4

                    height: 33
                    width: 200

                    color: "#ffffff"
                }
                Text {
                    id: image_002

                    height: 33
                    width: 201

                    color: "#000000"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 22.40
                    lineHeightMode: Text.FixedHeight
                    text: "Image_002"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignVCenter
                }
                Item {
                    id: close_3

                    x: 164
                    y: 6

                    height: 20
                    width: 20

                    clip: true

                    Shape {
                        id: icon_4

                        x: 4.17
                        y: 4.17

                        height: 11.67
                        width: 11.67

                        ShapePath {
                            id: icon_4_ShapePath0

                            fillColor: "#1d1b20"
                            fillRule: ShapePath.WindingFill
                            joinStyle: ShapePath.MiterJoin
                            strokeColor: "#00000000"
                            strokeStyle: ShapePath.SolidLine
                            strokeWidth: 0.03

                            PathSvg {
                                id: icon_4_ShapePath0_PathSvg0

                                path: "M 1.1666666785875952 11.666666984558105 L 0 10.500000603993742 L 4.666666714350381 5.833333492279053 L 0 1.1666666785875952 L 1.1666666785875952 0 L 5.833333492279053 4.666666714350381 L 10.500000603993742 0 L 11.666666984558105 1.1666666785875952 L 7.000000667572035 5.833333492279053 L 11.666666984558105 10.500000603993742 L 10.500000603993742 11.666666984558105 L 5.833333492279053 7.000000667572035 L 1.1666666785875952 11.666666984558105 Z"
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: rectangle_5

                x: 402

                height: 33
                width: 200

                border.color: "#b3b3b3"
                border.width: 1
                color: "transparent"

                Rectangle {
                    id: rectangle_6

                    height: 33
                    width: 200

                    color: "#f2f2f7"
                }
                Text {
                    id: image_003

                    height: 33
                    width: 201

                    color: "#000000"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 22.40
                    lineHeightMode: Text.FixedHeight
                    text: "Image_003"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignVCenter
                }
                Item {
                    id: close_4

                    x: 164
                    y: 6

                    height: 20
                    width: 20

                    clip: true

                    Shape {
                        id: icon_5

                        x: 4.17
                        y: 4.17

                        height: 11.67
                        width: 11.67

                        ShapePath {
                            id: icon_5_ShapePath0

                            fillColor: "#1d1b20"
                            fillRule: ShapePath.WindingFill
                            joinStyle: ShapePath.MiterJoin
                            strokeColor: "#00000000"
                            strokeStyle: ShapePath.SolidLine
                            strokeWidth: 0.03

                            PathSvg {
                                id: icon_5_ShapePath0_PathSvg0

                                path: "M 1.1666666785875952 11.666666984558105 L 0 10.500000603993742 L 4.666666714350381 5.833333492279053 L 0 1.1666666785875952 L 1.1666666785875952 0 L 5.833333492279053 4.666666714350381 L 10.500000603993742 0 L 11.666666984558105 1.1666666785875952 L 7.000000667572035 5.833333492279053 L 11.666666984558105 10.500000603993742 L 10.500000603993742 11.666666984558105 L 5.833333492279053 7.000000667572035 L 1.1666666785875952 11.666666984558105 Z"
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: add

                x: 603

                height: 33
                width: 33

                clip: true
                color: "#ffffff"

                Shape {
                    id: icon_6

                    x: 6.88
                    y: 6.88

                    height: 19.25
                    width: 19.25

                    ShapePath {
                        id: icon_6_ShapePath0

                        fillColor: "#1d1b20"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 0.03

                        PathSvg {
                            id: icon_6_ShapePath0_PathSvg0

                            path: "M 8.25 11 L 0 11 L 0 8.25 L 8.25 8.25 L 8.25 0 L 11 0 L 11 8.25 L 19.25 8.25 L 19.25 11 L 11 11 L 11 19.25 L 8.25 19.25 L 8.25 11 Z"
                        }
                    }
                }
            }
        }
        Rectangle {
            id: menu_Separator_3

            y: 32

            height: 1
            width: 1440

            border.color: "#000000"
            border.width: 1
            color: "transparent"
            radius: 8

            Rectangle {
                id: rule_3

                height: 1
                width: 1440

                color: "#b3b3b3"
            }
        }
    }
    
}
