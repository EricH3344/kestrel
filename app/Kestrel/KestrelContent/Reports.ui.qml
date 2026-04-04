import QtQuick

Rectangle {
    id: reports

    height: 1024
    width: 1440

    clip: true
    color: "#ffffff"

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
    Image {
        id: imageSelector

        y: 70

        clip: true
        source: Qt.resolvedUrl("assets/imageSelector.png")

        Item {
            id: frame_4

            height: 33
            width: 636

            Image {
                id: rectangle_1

                source: Qt.resolvedUrl("assets/rectangle_1.png")
            }
            Image {
                id: rectangle_3

                x: 201

                source: Qt.resolvedUrl("assets/rectangle_3.png")
            }
            Image {
                id: rectangle_2

                x: 402

                source: Qt.resolvedUrl("assets/rectangle_2.png")
            }
            Image {
                id: add

                x: 603

                clip: true
                source: Qt.resolvedUrl("assets/add.png")
            }
        }
    }
    Item {
        id: topBar

        height: 70
        width: 1440

        clip: true

        Image {
            id: menu_Separator

            y: 69

            source: Qt.resolvedUrl("assets/menu_Separator.png")
        }
        Image {
            id: tabs

            x: 500
            y: 30

            source: Qt.resolvedUrl("assets/tabs.png")
        }
        Rectangle {
            id: frame_2

            height: 32
            width: 1440

            color: "#f2f2f7"

            Image {
                id: parts_Title_Bar_Caption_Control_Group

                x: 1302

                source: Qt.resolvedUrl("assets/parts_Title_Bar_Caption_Control_Group.png")
            }
            Image {
                id: image_1

                x: 699
                y: 1

                source: Qt.resolvedUrl("assets/image_1.png")
            }
            Text {
                id: chromeMinimize

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
                id: chromeMinimize_1

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
                id: chromeMinimize_2

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
                id: chromeMinimize_3

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