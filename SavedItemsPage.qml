import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    property var savedModel
    property var listModel

    function addToShoppingList(itemName) {
        // Create a new shopping item.
        var newId = root.listModel.addItem(itemName, "Tim");

        if (!newId) {
            console.warn("Could not add item to shopping list.");
            return;
        }

        // Create the new shopping item in Firebase.
        if (FirebaseManager.initialized) {
            FirebaseManager.addShoppingItem(newId, itemName, "Tim");
        } else {
            console.warn("Firebase unavailable; added locally only.");
        }
    }

    function removeItem(index, itemId) {
        if (!itemId) {
            console.error("Cannot delete saved item without an ID.");
            return;
        }

        root.savedModel.removeItem(index);

        if (FirebaseManager.initialized) {
            FirebaseManager.deleteSavedItem(itemId);
        } else {
            console.warn("Firebase unavailable; removed locally only.");
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Text {
            text: "Saved Items"

            font.pixelSize: 28
            font.bold: true

            Layout.fillWidth: true

            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            text: "Items you frequently buy can be kept here."

            Layout.fillWidth: true

            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap

            opacity: 0.7
        }

        Text {
            text: "No saved items yet."

            visible: root.savedModel.count === 0

            Layout.fillWidth: true
            Layout.fillHeight: true

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            font.pixelSize: 18
            opacity: 0.6
        }

        ListView {
            id: savedList

            Layout.fillWidth: true
            Layout.fillHeight: true

            visible: root.savedModel.count > 0

            model: root.savedModel

            spacing: 8
            clip: true

            delegate: Rectangle {
                id: savedDelegate

                width: savedList.width
                height: 75

                radius: 10
                border.width: 1

                required property int index
                required property string name
                required property string itemId

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    spacing: 8

                    Text {
                        text: name

                        font.pixelSize: 18
                        font.bold: true

                        Layout.fillWidth: true
                    }

                    Button {
                        text: "Add to List"

                        onClicked: {
                            root.addToShoppingList(name);
                        }
                    }

                    Button {
                        text: "Remove"

                        onClicked: {
                            root.removeItem(savedDelegate.index, savedDelegate.itemId);
                        }
                    }
                }
            }
        }
    }
}
