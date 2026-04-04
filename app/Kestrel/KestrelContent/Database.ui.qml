import QtQuick
import QtQuick.Shapes

Rectangle {
    id: database

    height: 1024
    width: 1440

    clip: true
    color: "#ffffff"

    Rectangle {
        id: middleSection

        x: 299
        y: 141

        height: 852
        width: 1141

        border.color: "#b2b2b2"
        border.width: 1
        clip: true
        color: "transparent"
    }
    Rectangle {
        id: sideBar

        y: 141

        height: 852
        width: 300

        border.color: "#b3b3b3"
        border.width: 1
        color: "transparent"

        Rectangle {
            id: frame_14

            x: 1
            y: 1

            height: 36
            width: 298

            clip: true
            color: "#f2f2f2"

            Text {
                id: browser

                y: 8

                height: 20
                width: 81

                color: "#000000"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Browser"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
            Item {
                id: database_1

                x: 238
                y: 7

                height: 22
                width: 22

                clip: true

                Shape {
                    id: icon

                    x: 2.75
                    y: 1.83

                    height: 18.33
                    width: 16.50

                    ShapePath {
                        id: icon_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        strokeColor: "#1e1e1e"
                        strokeWidth: 3

                        PathSvg {
                            id: icon_ShapePath0_PathSvg0

                            path: "M 8.25 5.49999992847443 C 12.806350231170654 5.49999992847443 16.5 4.268783245601007 16.5 2.749999964237215 C 16.5 1.2312170106989648 12.806350231170654 0 8.25 0 C 3.6936507523059845 0 0 1.2312170106989648 0 2.749999964237215 C 0 4.268783245601007 3.6936507523059845 5.49999992847443 8.25 5.49999992847443 Z"
                        }
                    }
                    ShapePath {
                        id: icon_ShapePath1

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        strokeColor: "#1e1e1e"
                        strokeWidth: 3

                        PathSvg {
                            id: icon_ShapePath1_PathSvg0

                            path: "M 16.5 9.166666984558105 C 16.5 10.68833365547657 12.833333551883698 11.91666694879532 8.25 11.91666694879532 C 3.6666664481163025 11.91666694879532 0 10.68833365547657 0 9.166666984558105"
                        }
                    }
                    ShapePath {
                        id: icon_ShapePath2

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        strokeColor: "#1e1e1e"
                        strokeWidth: 3

                        PathSvg {
                            id: icon_ShapePath2_PathSvg0

                            path: "M 0 2.749999964237215 L 0 15.583334004878996 C 0 17.10500067579746 3.6666664481163025 18.33333396911621 8.25 18.33333396911621 C 12.833333551883698 18.33333396911621 16.5 17.10500067579746 16.5 15.583334004878996 L 16.5 2.749999964237215"
                        }
                    }
                }
            }
            Item {
                id: filter

                x: 268
                y: 7

                height: 22
                width: 22

                clip: true

                Shape {
                    id: icon_1

                    x: 1.83
                    y: 2.75

                    height: 16.50
                    width: 18.33

                    ShapePath {
                        id: icon_1_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        strokeColor: "#1e1e1e"
                        strokeWidth: 3.50

                        PathSvg {
                            id: icon_1_ShapePath0_PathSvg0

                            path: "M 18.333332061767578 0 L 0 0 L 7.333332929611203 8.671666526794434 L 7.333332929611203 14.666666316986085 L 10.999999656677232 16.5 L 10.999999656677232 8.671666526794434 L 18.333332061767578 0 Z"
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        id: bottomBar

        y: 992

        height: 32
        width: 1440

        border.color: "#898989"
        border.width: 1
        clip: true
        color: "transparent"

        Rectangle {
            id: frame_1

            height: 32
            width: 1440

            color: "#f2f2f7"
        }
    }
    Item {
        id: frame_13

        y: 101

        height: 40
        width: 1440

        Item {
            id: tabs

            x: 642

            height: 40
            width: 156

            Image {
                id: tab

                x: -8.50

                source: Qt.resolvedUrl("assets/tab_4.png")
            }
            Image {
                id: tab_1

                x: 94.50

                source: Qt.resolvedUrl("assets/tab_5.png")
            }
        }
    }
    Item {
        id: imageSelector

        y: 69

        height: 33
        width: 1440

        clip: true

        Item {
            id: frame_4

            height: 33
            width: 602

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
                    id: close

                    x: 164
                    y: 6

                    height: 20
                    width: 20

                    clip: true

                    Shape {
                        id: icon_2

                        x: 4.17
                        y: 4.17

                        height: 11.67
                        width: 11.67

                        ShapePath {
                            id: icon_2_ShapePath0

                            fillColor: "#1d1b20"
                            fillRule: ShapePath.WindingFill
                            joinStyle: ShapePath.MiterJoin
                            strokeColor: "#00000000"
                            strokeStyle: ShapePath.SolidLine
                            strokeWidth: 0.03

                            PathSvg {
                                id: icon_2_ShapePath0_PathSvg0

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
                    id: close_1

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
                    id: close_2

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
        }
        Rectangle {
            id: add

            x: 603

            height: 33
            width: 33

            clip: true
            color: "#ffffff"

            Shape {
                id: icon_5

                x: 6.88
                y: 6.88

                height: 19.25
                width: 19.25

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

                        path: "M 8.25 11 L 0 11 L 0 8.25 L 8.25 8.25 L 8.25 0 L 11 0 L 11 8.25 L 19.25 8.25 L 19.25 11 L 11 11 L 11 19.25 L 8.25 19.25 L 8.25 11 Z"
                    }
                }
            }
        }
        Rectangle {
            id: menu_Separator

            x: -1
            y: 32

            height: 1
            width: 1440

            border.color: "#000000"
            border.width: 1
            color: "transparent"
            radius: 8

            Rectangle {
                id: rule

                height: 1
                width: 1440

                color: "#b3b3b3"
            }
        }
    }
    Item {
        id: topBar

        height: 70
        width: 1440

        clip: true

        Rectangle {
            id: menu_Separator_1

            y: 69

            height: 1
            width: 1440

            border.color: "#000000"
            border.width: 1
            color: "transparent"
            radius: 8

            Rectangle {
                id: rule_1

                height: 1
                width: 1440

                color: "#b3b3b3"
            }
        }
        Item {
            id: tabs_1

            x: 500
            y: 30

            height: 40
            width: 440

            Image {
                id: tab_2

                x: 47.50

                source: Qt.resolvedUrl("assets/tab_6.png")
            }
            Image {
                id: tab_3

                x: 104.50

                source: Qt.resolvedUrl("assets/tab_7.png")
            }
            Image {
                id: tab_4

                x: 200.50

                source: Qt.resolvedUrl("assets/tab_8.png")
            }
            Image {
                id: tab_5

                x: 308.50

                source: Qt.resolvedUrl("assets/tab_9.png")
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

                source: Qt.resolvedUrl("assets/image_3.png")
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
}