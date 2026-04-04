import QtQuick
import QtQuick.Shapes

Rectangle {
    id: map

    height: 1024
    width: 1440

    clip: true
    color: "#ffffff"

    Rectangle {
        id: rightSide

        x: 1140
        y: 136

        height: 857
        width: 300

        border.color: "#b2b2b2"
        border.width: 1
        clip: true
        color: "transparent"

        Item {
            id: frame_6

            x: 1
            y: 453

            height: 403
            width: 298

            clip: true

            Text {
                id: view_in_Database

                x: 15
                y: 43

                height: 14
                width: 113

                color: "#000000"
                font.family: "Roboto"
                font.pixelSize: 12
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignLeft
                text: "View in Database"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
            }
            Rectangle {
                id: frame_10

                height: 36
                width: 298

                clip: true
                color: "#f2f2f2"

                Text {
                    id: plot_Actions

                    x: 104
                    y: 7

                    height: 22
                    width: 92

                    color: "#000000"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    lineHeight: 22.40
                    lineHeightMode: Text.FixedHeight
                    text: "Plot Actions"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.WordWrap
                }
            }
        }
        Rectangle {
            id: menu_Separator

            x: 1
            y: 452

            height: 1
            width: 298

            border.color: "#000000"
            border.width: 1
            color: "transparent"
            radius: 8

            Rectangle {
                id: rule

                height: 1
                width: 298

                color: "#b3b3b3"
            }
        }
        Item {
            id: frame_7

            y: 1

            height: 451
            width: 300

            clip: true

            Item {
                id: group_2

                y: 89

                height: 86
                width: 300

                Item {
                    id: rectangle_36

                    height: 86
                    width: 300
                }
                Text {
                    id: ground_Coverage_NDRE_AUC_NDRE_Spectral_Maturity_

                    x: 11
                    y: 9

                    height: 59
                    width: 153

                    color: "#000000"
                    font.family: "Roboto"
                    font.pixelSize: 12
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    text: "Ground Coverage: NDRE:  AUC-NDRE:
Spectral Maturity Date: 
Senescence Rate:  "
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.WordWrap
                }
            }
            Item {
                id: group_3

                y: 42

                height: 47
                width: 300

                Item {
                    id: rectangle_34

                    height: 47
                    width: 300
                }
                Text {
                    id: plot_Crop_Type_

                    x: 11
                    y: 8

                    height: 29
                    width: 113

                    color: "#000000"
                    font.family: "Roboto"
                    font.pixelSize: 12
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    text: "Plot #:  Crop Type: "
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                }
            }
            Rectangle {
                id: frame_11

                x: 1
                y: 1

                height: 36
                width: 298

                clip: true
                color: "#f2f2f2"

                Text {
                    id: plot_Details

                    x: 106
                    y: 7

                    height: 22
                    width: 87

                    color: "#000000"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    lineHeight: 22.40
                    lineHeightMode: Text.FixedHeight
                    text: "Plot Details"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.WordWrap
                }
                Item {
                    id: close

                    x: 270
                    y: 8

                    height: 20
                    width: 20

                    clip: true

                    Shape {
                        id: icon

                        x: 4.17
                        y: 4.17

                        height: 11.67
                        width: 11.67

                        ShapePath {
                            id: icon_ShapePath0

                            fillColor: "#1d1b20"
                            fillRule: ShapePath.WindingFill
                            joinStyle: ShapePath.MiterJoin
                            strokeColor: "#00000000"
                            strokeStyle: ShapePath.SolidLine
                            strokeWidth: 0.03

                            PathSvg {
                                id: icon_ShapePath0_PathSvg0

                                path: "M 1.1666666785875952 11.666666984558105 L 0 10.500000603993742 L 4.666666714350381 5.833333492279053 L 0 1.1666666785875952 L 1.1666666785875952 0 L 5.833333492279053 4.666666714350381 L 10.500000603993742 0 L 11.666666984558105 1.1666666785875952 L 7.000000667572035 5.833333492279053 L 11.666666984558105 10.500000603993742 L 10.500000603993742 11.666666984558105 L 5.833333492279053 7.000000667572035 L 1.1666666785875952 11.666666984558105 Z"
                            }
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        id: middleSection

        y: 135

        height: 858
        width: 1141

        border.color: "#b2b2b2"
        border.width: 1
        clip: true
        color: "transparent"

        Image {
            id: image_2

            x: 52

            source: Qt.resolvedUrl("assets/image_4.png")
        }
    }
    Rectangle {
        id: frame_8

        x: 1
        y: 136

        height: 856
        width: 298

        border.color: "#b2b2b2"
        border.width: 1
        clip: true
        color: "#ffffff"

        Rectangle {
            id: frame_12

            height: 36
            width: 298

            clip: true
            color: "#f2f2f2"

            Item {
                id: close_1

                x: 270
                y: 8

                height: 20
                width: 20

                clip: true

                Shape {
                    id: icon_1

                    x: 4.17
                    y: 4.17

                    height: 11.67
                    width: 11.67

                    ShapePath {
                        id: icon_1_ShapePath0

                        fillColor: "#1d1b20"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 0.03

                        PathSvg {
                            id: icon_1_ShapePath0_PathSvg0

                            path: "M 1.1666666785875952 11.666666984558105 L 0 10.500000603993742 L 4.666666714350381 5.833333492279053 L 0 1.1666666785875952 L 1.1666666785875952 0 L 5.833333492279053 4.666666714350381 L 10.500000603993742 0 L 11.666666984558105 1.1666666785875952 L 7.000000667572035 5.833333492279053 L 11.666666984558105 10.500000603993742 L 10.500000603993742 11.666666984558105 L 5.833333492279053 7.000000667572035 L 1.1666666785875952 11.666666984558105 Z"
                        }
                    }
                }
            }
            Text {
                id: render_View

                x: 91
                y: 8

                height: 20
                width: 117

                color: "#000000"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Render View"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
        }
        Item {
            id: frame_13

            y: 36

            height: 820
            width: 298

            clip: true

            Item {
                id: frame_14

                x: 25
                y: 13

                height: 25
                width: 199

                clip: true

                Text {
                    id: render_Overlay_

                    y: 2

                    height: 22
                    width: 122

                    color: "#000000"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 22.40
                    lineHeightMode: Text.FixedHeight
                    text: "Render Overlay:"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                }
            }
            Item {
                id: select_Field

                x: 157
                y: 6

                height: 40
                width: 120

                Text {
                    id: label

                    height: 22
                    width: 121

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
                    width: 121

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
                    id: select

                    height: 40
                    width: 120

                    border.color: "#d9d9d9"
                    border.width: 1
                    color: "#ffffff"
                    radius: 8

                    Text {
                        id: rGBA

                        x: 16
                        y: 12

                        height: 16
                        width: 69

                        color: "#1e1e1e"
                        font.family: "Inter"
                        font.pixelSize: 16
                        font.weight: Font.Normal
                        horizontalAlignment: Text.AlignLeft
                        lineHeight: 16
                        lineHeightMode: Text.FixedHeight
                        text: "RGBA"
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignTop
                        wrapMode: Text.WordWrap
                    }
                    Item {
                        id: chevron_down

                        x: 92
                        y: 12

                        height: 16
                        width: 16

                        clip: true

                        Shape {
                            id: icon_2

                            x: 4
                            y: 6

                            height: 4
                            width: 8

                            ShapePath {
                                id: icon_2_ShapePath0

                                fillColor: "#00000000"
                                fillRule: ShapePath.WindingFill
                                strokeColor: "#1e1e1e"
                                strokeWidth: 1.60

                                PathSvg {
                                    id: icon_2_ShapePath0_PathSvg0

                                    path: "M 0 0 L 4 4 L 8 0"
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: menu_Separator_1

                y: 52

                height: 1
                width: 298

                border.color: "#000000"
                border.width: 1
                color: "transparent"
                radius: 8

                Rectangle {
                    id: rule_1

                    height: 1
                    width: 298

                    color: "#b3b3b3"
                }
            }
            Rectangle {
                id: menu_Separator_2

                y: 400

                height: 1
                width: 298

                border.color: "#000000"
                border.width: 1
                color: "transparent"
                radius: 8

                Rectangle {
                    id: rule_2

                    height: 1
                    width: 298

                    color: "#b3b3b3"
                }
            }
            Rectangle {
                id: frame_15

                y: 401

                height: 36
                width: 298

                clip: true
                color: "#f2f2f2"

                Text {
                    id: layers

                    x: 91
                    y: 8

                    height: 20
                    width: 117

                    color: "#000000"
                    font.family: "Inter"
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 22.40
                    lineHeightMode: Text.FixedHeight
                    text: "Layers"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Rectangle {
                id: scrollbar_Custom

                x: 285
                y: 58

                height: 337
                width: 8

                color: "#e8e8e8"
                radius: 100

                Rectangle {
                    id: bar

                    height: 33
                    width: 8

                    color: "#7a7a7a"
                    radius: 100
                }
            }
        }
    }
    Item {
        id: toolbar

        y: 103

        height: 33
        width: 1440

        clip: true

        Item {
            id: group_4

            x: 15
            y: 2

            height: 32
            width: 306

            Item {
                id: file_upload

                y: 3

                height: 24
                width: 24

                clip: true

                Shape {
                    id: _vector

                    height: 24
                    width: 24

                    ShapePath {
                        id: _vector_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_ShapePath0_PathSvg0

                            path: "M 0 0 L 24 0 L 24 24 L 0 24 L 0 0 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_1

                    x: 5
                    y: 3

                    height: 17
                    width: 14

                    ShapePath {
                        id: _vector_1_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_1_ShapePath0_PathSvg0

                            path: "M 4 13 L 10 13 L 10 7 L 14 7 L 7 0 L 0 7 L 4 7 L 4 13 Z M 0 15 L 14 15 L 14 17 L 0 17 L 0 15 Z"
                        }
                    }
                }
            }
            Item {
                id: pan_tool

                x: 35
                y: 6

                height: 18
                width: 18

                clip: true

                Item {
                    id: group

                    height: 18
                    width: 18

                    Shape {
                        id: _vector_2

                        height: 18
                        width: 18

                        ShapePath {
                            id: _vector_2_ShapePath0

                            fillColor: "#00000000"
                            joinStyle: ShapePath.MiterJoin
                            strokeColor: "#00000000"
                            strokeStyle: ShapePath.SolidLine
                            strokeWidth: 1

                            PathSvg {
                                id: _vector_2_ShapePath0_PathSvg0

                                path: "M 0 0 L 18 0 L 18 18 L 0 18 L 0 0 Z"
                            }
                        }
                    }
                }
                Item {
                    id: group_1

                    x: 0.75

                    height: 18
                    width: 16.50

                    Item {
                        id: group_5

                        height: 18
                        width: 16.50

                        Item {
                            id: group_6

                            height: 18
                            width: 16.50

                            Shape {
                                id: _vector_3

                                height: 18
                                width: 16.50

                                ShapePath {
                                    id: _vector_3_ShapePath0

                                    fillColor: "#323232"
                                    fillRule: ShapePath.WindingFill
                                    joinStyle: ShapePath.MiterJoin
                                    strokeColor: "#00000000"
                                    strokeStyle: ShapePath.SolidLine
                                    strokeWidth: 1

                                    PathSvg {
                                        id: _vector_3_ShapePath0_PathSvg0

                                        path: "M 16.499998092651367 4.125 L 16.499998092651367 15 C 16.499998092651367 16.650000035762787 15.149998284469946 18 13.499998439442027 18 L 8.024998929283852 18 C 7.214998990730826 18 6.449999540502342 17.67749959230423 5.887499605525591 17.107499599456787 L 0 11.122499942779541 C 0 11.122499942779541 0.9449998556687114 10.199999942444265 0.9749998515302526 10.184999942779541 C 1.1399998315626967 10.04249994456768 1.3424997714974747 9.96749997138977 1.567499754428871 9.96749997138977 C 1.732499734461315 9.96749997138977 1.8824998199397824 10.012499855831265 2.01749980969862 10.087499856948853 C 2.0474998055601614 10.094999856781214 5.249999393116344 11.932499885559082 5.249999393116344 11.932499885559082 L 5.249999393116344 3 C 5.249999393116344 2.3775000125169754 5.752499347545882 1.875 6.374999263069846 1.875 C 6.997499178593809 1.875 7.499999133023349 2.3775000125169754 7.499999133023349 3 L 7.499999133023349 8.25 L 8.249999046325684 8.25 L 8.249999046325684 1.125 C 8.249999046325684 0.5025000125169754 8.752499000755222 0 9.374998916279186 0 C 9.99749883180315 0 10.499998786232688 0.5025000125169754 10.499998786232688 1.125 L 10.499998786232688 8.25 L 11.249998699535023 8.25 L 11.249998699535023 1.875 C 11.249998699535023 1.2525000125169754 11.752498653964562 0.75 12.374998569488525 0.75 C 12.997498485012489 0.75 13.499998439442027 1.2525000125169754 13.499998439442027 1.875 L 13.499998439442027 8.25 L 14.249998352744361 8.25 L 14.249998352744361 4.125 C 14.249998352744361 3.5025000125169754 14.7524983071739 3 15.374998222697863 3 C 15.997498138221827 3 16.499998092651367 3.5025000125169754 16.499998092651367 4.125 Z"
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item {
                id: cursor_Default_White

                x: 64

                height: 32
                width: 32

                clip: true

                Shape {
                    id: rectangle_237

                    x: 10
                    y: 8

                    height: 13.85
                    width: 12.49

                    ShapePath {
                        id: rectangle_237_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.OddEvenFill
                        strokeColor: "#323232"
                        strokeWidth: 1.20

                        PathSvg {
                            id: rectangle_237_ShapePath0_PathSvg0

                            path: "M 7.840662002563477 8.516070365905762 L 12.486547470092773 6.864937782287598 L 0 0 L 3.6402504444122314 13.776422500610352 L 6.366279125213623 9.667984962463379 L 9.630036354064941 13.845402717590332 L 11.104418754577637 12.693489074707031 L 7.840662002563477 8.516070365905762 L 7.840662002563477 8.516070365905762 Z"
                        }
                    }
                }
            }
            Item {
                id: zoom_in

                x: 107
                y: 3

                height: 24
                width: 24

                clip: true

                Shape {
                    id: _vector_4

                    height: 24
                    width: 24

                    ShapePath {
                        id: _vector_4_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_4_ShapePath0_PathSvg0

                            path: "M 0 0 L 24 0 L 24 24 L 0 24 L 0 0 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_5

                    x: 3
                    y: 3

                    height: 17.49
                    width: 17.49

                    ShapePath {
                        id: _vector_5_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_5_ShapePath0_PathSvg0

                            path: "M 12.5 11 L 11.710000038146973 11 L 11.430000305175781 10.729999542236328 C 12.410000324249268 9.589999556541443 13 8.110000014305115 13 6.5 C 13 2.9100000858306885 10.089999914169312 0 6.5 0 C 2.9100000858306885 0 0 2.9100000858306885 0 6.5 C 0 10.089999914169312 2.9100000858306885 13 6.5 13 C 8.110000014305115 13 9.589999556541443 12.410000324249268 10.729999542236328 11.430000305175781 L 11 11.710000038146973 L 11 12.5 L 16 17.489999771118164 L 17.489999771118164 16 L 12.5 11 L 12.5 11 Z M 6.5 11 C 4.009999990463257 11 2 8.990000009536743 2 6.5 C 2 4.009999990463257 4.009999990463257 2 6.5 2 C 8.990000009536743 2 11 4.009999990463257 11 6.5 C 11 8.990000009536743 8.990000009536743 11 6.5 11 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_6

                    x: 7
                    y: 7

                    height: 5
                    width: 5

                    ShapePath {
                        id: _vector_6_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_6_ShapePath0_PathSvg0

                            path: "M 5 3 L 3 3 L 3 5 L 2 5 L 2 3 L 0 3 L 0 2 L 2 2 L 2 0 L 3 0 L 3 2 L 5 2 L 5 3 Z"
                        }
                    }
                }
            }
            Item {
                id: zoom_out

                x: 142
                y: 3

                height: 24
                width: 24

                clip: true

                Shape {
                    id: _vector_7

                    height: 24
                    width: 24

                    ShapePath {
                        id: _vector_7_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_7_ShapePath0_PathSvg0

                            path: "M 0 0 L 24 0 L 24 24 L 0 24 L 0 0 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_8

                    x: 3
                    y: 3

                    height: 17.49
                    width: 17.49

                    ShapePath {
                        id: _vector_8_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_8_ShapePath0_PathSvg0

                            path: "M 12.5 11 L 11.710000038146973 11 L 11.430000305175781 10.729999542236328 C 12.410000324249268 9.589999556541443 13 8.110000014305115 13 6.5 C 13 2.9100000858306885 10.089999914169312 0 6.5 0 C 2.9100000858306885 0 0 2.9100000858306885 0 6.5 C 0 10.089999914169312 2.9100000858306885 13 6.5 13 C 8.110000014305115 13 9.589999556541443 12.410000324249268 10.729999542236328 11.430000305175781 L 11 11.710000038146973 L 11 12.5 L 16 17.489999771118164 L 17.489999771118164 16 L 12.5 11 L 12.5 11 Z M 6.5 11 C 4.009999990463257 11 2 8.990000009536743 2 6.5 C 2 4.009999990463257 4.009999990463257 2 6.5 2 C 8.990000009536743 2 11 4.009999990463257 11 6.5 C 11 8.990000009536743 8.990000009536743 11 6.5 11 Z M 4 6 L 9 6 L 9 7 L 4 7 L 4 6 Z"
                        }
                    }
                }
            }
            Item {
                id: fit_screen

                x: 177
                y: 3

                height: 24
                width: 24

                clip: true

                Shape {
                    id: _vector_9

                    height: 24
                    width: 24

                    ShapePath {
                        id: _vector_9_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_9_ShapePath0_PathSvg0

                            path: "M 0 0 L 24 0 L 24 24 L 0 24 L 0 0 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_10

                    x: 2
                    y: 4

                    height: 16
                    width: 20

                    ShapePath {
                        id: _vector_10_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_10_ShapePath0_PathSvg0

                            path: "M 15 0 L 18 0 C 19.100000023841858 0 20 0.8999999761581421 20 2 L 20 4 L 18 4 L 18 2 L 15 2 L 15 0 Z M 2 4 L 2 2 L 5 2 L 5 0 L 2 0 C 0.8999999761581421 0 0 0.8999999761581421 0 2 L 0 4 L 2 4 Z M 18 12 L 18 14 L 15 14 L 15 16 L 18 16 C 19.100000023841858 16 20 15.100000023841858 20 14 L 20 12 L 18 12 Z M 5 14 L 2 14 L 2 12 L 0 12 L 0 14 C 0 15.100000023841858 0.8999999761581421 16 2 16 L 5 16 L 5 14 Z M 16 4 L 4 4 L 4 12 L 16 12 L 16 4 Z"
                        }
                    }
                }
            }
            Item {
                id: pin_drop

                x: 212
                y: 3

                height: 24
                width: 24

                clip: true

                Shape {
                    id: _vector_11

                    height: 24
                    width: 24

                    ShapePath {
                        id: _vector_11_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_11_ShapePath0_PathSvg0

                            path: "M 0 0 L 24 0 L 24 24 L 0 24 L 0 0 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_12

                    x: 5
                    y: 2

                    height: 20
                    width: 14

                    ShapePath {
                        id: _vector_12_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_12_ShapePath0_PathSvg0

                            path: "M 13 6 C 13 2.690000057220459 10.309999942779541 0 7 0 C 3.690000057220459 0 1 2.690000057220459 1 6 C 1 10.5 7 17 7 17 C 7 17 13 10.5 13 6 Z M 5 6 C 5 4.899999976158142 5.899999976158142 4 7 4 C 8.100000023841858 4 9 4.899999976158142 9 6 C 9 7.100000023841858 8.110000014305115 8 7 8 C 5.899999976158142 8 5 7.100000023841858 5 6 Z M 0 18 L 0 20 L 14 20 L 14 18 L 0 18 Z"
                        }
                    }
                }
            }
            Item {
                id: border_color

                x: 247
                y: 3

                height: 24
                width: 24

                clip: true

                Shape {
                    id: _vector_13

                    height: 24
                    width: 24

                    ShapePath {
                        id: _vector_13_ShapePath0

                        fillColor: "#00000000"
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_13_ShapePath0_PathSvg0

                            path: "M 0 0 L 24 0 L 24 24 L 0 24 L 0 0 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_14

                    x: 2
                    y: 2

                    height: 22
                    width: 20

                    ShapePath {
                        id: _vector_14_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_14_ShapePath0_PathSvg0

                            path: "M 20 22.002500534057617 L 0 22.002500534057617 L 0 18.002500534057617 L 20 18.002500534057617 L 20 22.002500534057617 Z M 11.0600004196167 3.192500114440918 L 14.809999465942383 6.94249963760376 L 5.75 16.002500534057617 L 2 16.002500534057617 L 2 12.2524995803833 L 11.0600004196167 3.192500114440918 Z M 15.879999160766602 5.872499942779541 L 12.130000114440918 2.122499942779541 L 13.960000038146973 0.29249998927116394 C 14.350000023841858 -0.09749999642372131 14.980000853538513 -0.09749999642372131 15.370000839233398 0.29249998927116394 L 17.709999084472656 2.632500171661377 C 18.09999907016754 3.022500157356262 18.09999907016754 3.652500033378601 17.709999084472656 4.042500019073486 L 15.879999160766602 5.872499942779541 Z"
                        }
                    }
                }
            }
            Item {
                id: play_arrow

                x: 282
                y: 3

                height: 24
                width: 24

                clip: true

                Shape {
                    id: _vector_15

                    height: 24
                    width: 24

                    ShapePath {
                        id: _vector_15_ShapePath0

                        fillColor: "#00000000"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_15_ShapePath0_PathSvg0

                            path: "M 0 0 L 24 0 L 24 24 L 0 24 L 0 0 Z"
                        }
                    }
                }
                Shape {
                    id: _vector_16

                    x: 8
                    y: 5

                    height: 14
                    width: 11

                    ShapePath {
                        id: _vector_16_ShapePath0

                        fillColor: "#323232"
                        fillRule: ShapePath.WindingFill
                        joinStyle: ShapePath.MiterJoin
                        strokeColor: "#00000000"
                        strokeStyle: ShapePath.SolidLine
                        strokeWidth: 1

                        PathSvg {
                            id: _vector_16_ShapePath0_PathSvg0

                            path: "M 0 0 L 0 14 L 11 7 L 0 0 Z"
                        }
                    }
                }
            }
        }
    }
    Item {
        id: imageSelector

        y: 70

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
        Item {
            id: fields

            x: 776
            y: -1

            height: 35
            width: 630

            Item {
                id: textbox

                x: 90
                y: 4

                height: 28
                width: 124

                Text {
                    id: label_1

                    height: 11
                    width: 66.80

                    color: "#000000"
                    font.capitalization: Font.AllUppercase
                    font.family: "Roboto"
                    font.letterSpacing: 1.80
                    font.pixelSize: 9
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    text: "TEXT FIELD"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    visible: false
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    id: textbox_1

                    height: 30
                    width: 124

                    border.color: "#a6a6a6"
                    border.width: 1
                    color: "#ffffff"
                    radius: 4

                    Text {
                        id: text_field_data

                        x: 8
                        y: 8

                        height: 14
                        width: 109

                        color: "#a6a6a6"
                        font.family: "Roboto"
                        font.pixelSize: 12
                        font.weight: Font.Normal
                        horizontalAlignment: Text.AlignLeft
                        text: "Text field data"
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignTop
                        visible: false
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Text {
                id: image_4

                y: 2

                height: 33
                width: 85

                color: "#000000"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Coordinate"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
            Item {
                id: textbox_2

                x: 298
                y: 4

                height: 28
                width: 124

                Text {
                    id: label_2

                    height: 11
                    width: 66.80

                    color: "#000000"
                    font.capitalization: Font.AllUppercase
                    font.family: "Roboto"
                    font.letterSpacing: 1.80
                    font.pixelSize: 9
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    text: "TEXT FIELD"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    visible: false
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    id: textbox_3

                    height: 30
                    width: 124

                    border.color: "#a6a6a6"
                    border.width: 1
                    color: "#ffffff"
                    radius: 4

                    Text {
                        id: text_field_data_1

                        x: 8
                        y: 8

                        height: 14
                        width: 109

                        color: "#a6a6a6"
                        font.family: "Roboto"
                        font.pixelSize: 12
                        font.weight: Font.Normal
                        horizontalAlignment: Text.AlignLeft
                        text: "Text field data"
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignTop
                        visible: false
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Text {
                id: image_5

                x: 247
                y: 1

                height: 33
                width: 46

                color: "#000000"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Scale"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
            Item {
                id: textbox_4

                x: 506
                y: 3

                height: 28
                width: 124

                Text {
                    id: label_3

                    height: 11
                    width: 66.80

                    color: "#000000"
                    font.capitalization: Font.AllUppercase
                    font.family: "Roboto"
                    font.letterSpacing: 1.80
                    font.pixelSize: 9
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    text: "TEXT FIELD"
                    textFormat: Text.PlainText
                    verticalAlignment: Text.AlignTop
                    visible: false
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    id: textbox_5

                    height: 30
                    width: 124

                    border.color: "#a6a6a6"
                    border.width: 1
                    color: "#ffffff"
                    radius: 4

                    Text {
                        id: text_field_data_2

                        x: 8
                        y: 8

                        height: 14
                        width: 109

                        color: "#a6a6a6"
                        font.family: "Roboto"
                        font.pixelSize: 12
                        font.weight: Font.Normal
                        horizontalAlignment: Text.AlignLeft
                        text: "Text field data"
                        textFormat: Text.PlainText
                        verticalAlignment: Text.AlignTop
                        visible: false
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Text {
                id: image_6

                x: 455

                height: 33
                width: 46

                color: "#000000"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Plot #"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
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
}