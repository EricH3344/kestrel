import QtQuick
import QtQuick.Shapes

Rectangle {
    id: database

    height: 1024
    width: 1440

    clip: true
    color: "#ffffff"

    ViewHeader { y: 0 }

    Rectangle {
        id: middleSection

        x: 299
        y: 102

        height: 852
        width: 1141

        border.color: "#b2b2b2"
        border.width: 1
        clip: true
        color: "transparent"
    }
    Rectangle {
        id: sideBar

        y: 102

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
    
}