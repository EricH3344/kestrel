import QtQuick
import QtQuick.Shapes

Rectangle {
    id: reports

    anchors.fill: parent
    clip: true
    color: "#ffffff"

    ViewHeader { y: 0 }

    Rectangle {
        id: middleSection

        x: 299
        y: 102

        height: 891
        width: 1141

        border.color: "#b2b2b2"
        border.width: 1
        clip: true
        color: "transparent"
    }
    Rectangle {
        id: sideBar

        y: 102

        height: 891
        width: 300

        border.color: "#b3b3b3"
        border.width: 1
        color: "transparent"
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