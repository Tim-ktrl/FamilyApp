import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    property var listModel
    property var savedModel

    function saveItem(itemName) {

        // Create the saved item locally.
        var newId = root.savedModel.addItem(itemName);

        // An empty ID means nothing was added.
        if (!newId) {
            console.log("Item already saved or invalid.");
            return;
        }

        // Create the same item in Firebase.
        if (FirebaseManager.initialized) {
            FirebaseManager.addSavedItem(newId, itemName);
        } else {
            console.warn("Firebase unavailable. Item saved locally only.");
        }
    }

    function removeItem(index, itemId) {
        if (!itemId) {
            console.error("Cannot delete item without an ID.");
            return;
        }

        // Delete from the local shopping list.
        root.listModel.removeItem(index);

        // Delete the same item from Firebase.
        if (FirebaseManager.initialized) {
            FirebaseManager.deleteShoppingItem(itemId);
        } else {
            console.warn("Firebase unavailable; item deleted locally only.");
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Text {
            text: "Family Shopping List"

            font.pixelSize: 28
            font.bold: true

            Layout.fillWidth: true

            horizontalAlignment: Text.AlignHCenter
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: itemInput

                Layout.fillWidth: true

                placeholderText: "Add item..."
            }

            Button {
                text: "Add"

                onClicked: {
                    var itemName = itemInput.text.trim();

                    if (itemName !== "") {

                        // Add locally and retrieve the generated ID.
                        var newId = root.listModel.addItem(itemName, "Tim");

                        if (newId !== "") {

                            // Add the same item to Firebase.
                            if (FirebaseManager.initialized) {
                                FirebaseManager.addShoppingItem(newId, itemName, "Tim");
                            }

                            itemInput.text = "";
                        }
                    }
                }
            }
        }

        ListView {
            id: shoppingList

            Layout.fillWidth: true
            Layout.fillHeight: true

            model: root.listModel

            spacing: 8
            clip: true

            delegate: Rectangle {
                id: itemDelegate

                required property int index
                required property string name
                required property string addedBy
                required property bool bought
                required property string itemId

                width: shoppingList.width
                height: 90

                radius: 10
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    spacing: 8

                    CheckBox {
                        checked: itemDelegate.bought

                        onClicked: {
                            root.listModel.setBought(itemDelegate.index, checked);

                            if (FirebaseManager.initialized) {
                                FirebaseManager.updateShoppingItemBought(itemDelegate.itemId, checked);
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true

                        Text {
                            text: itemDelegate.name

                            font.pixelSize: 18
                            font.bold: true

                            font.strikeout: itemDelegate.bought
                        }

                        Text {
                            text: "Added by " + itemDelegate.addedBy

                            font.pixelSize: 12
                            opacity: 0.6
                        }
                    }

                    Button {
                        text: "Save"

                        onClicked: {
                            root.saveItem(itemDelegate.name);
                        }
                    }

                    Button {
                        text: "Delete"

                        onClicked: {
                            root.removeItem(itemDelegate.index, itemDelegate.itemId);
                        }
                    }
                }
            }
        }
    }
}
