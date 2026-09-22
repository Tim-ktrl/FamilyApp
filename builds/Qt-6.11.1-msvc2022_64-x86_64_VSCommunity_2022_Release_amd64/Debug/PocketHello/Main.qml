import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window

    visible: true
    width: 390
    height: 720
    title: "Pocket Hello"

    property int tapCount: 0

    Column {
        anchors.centerIn: parent
        width: parent.width * 0.8
        spacing: 16

        Text {
            width: parent.width
            text: "My First Qt Phone App"
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 28
            font.bold: true
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: tapCount === 0
                  ? "Press the button to test the app."
                  : "You tapped the button " + tapCount + " time"
                    + (tapCount === 1 ? "." : "s.")

            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 18
            wrapMode: Text.WordWrap
        }

        Button {
            width: parent.width
            text: "Tap me"

            onClicked: {
                tapCount += 2
            }
        }

        Button {
            width: parent.width
            text: "Reset"

            onClicked: {
                tapCount = 0
            }
        }
    }
}