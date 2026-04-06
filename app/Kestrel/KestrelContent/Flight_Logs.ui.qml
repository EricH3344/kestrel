import QtQuick
import QtQuick.Shapes

Rectangle {
    id: flight_Logs

    anchors.fill: parent
    clip: true
    color: "#ffffff"

    ViewHeader { 
        y: 0
    }

    Rectangle {
        id: middleSection

        x: 332
        y: 102

        height: 891
        width: 1108

        border.color: "#b2b2b2"
        border.width: 1
        clip: true
        color: "transparent"
    }
    Rectangle {
        id: sideBar

        y: 102

        height: 891
        width: 333

        border.color: "#b3b3b3"
        border.width: 1
        color: "transparent"

        Rectangle {
            id: frame_15

            x: 1
            y: 1

            height: 36
            width: 331

            clip: true
            color: "#f2f2f2"

            Text {
                id: important_Timestamps

                x: 80
                y: 7

                height: 22
                width: 172

                color: "#000000"
                font.family: "Inter"
                font.pixelSize: 16
                font.weight: Font.Normal
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 22.40
                lineHeightMode: Text.FixedHeight
                text: "Important Timestamps"
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
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
}