import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    property var listModel
    property var savedModel

    signal dataChanged

    function saveItem(itemName) {

        // Check if item is already saved.
        for (var i = 0; i < savedModel.count; i++) {
            if (savedModel.get(i).name.toLowerCase() === itemName.toLowerCase()) {
                return;
            }
        }

        savedModel.append({
            "name": itemName
        });
        root.dataChanged();
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
                        root.listModel.append({
                            "name": itemName,
                            "addedBy": "Tim",
                            "bought": false
                        });

                        itemInput.text = "";

                        root.dataChanged();
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

                width: shoppingList.width
                height: 90

                radius: 10
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    spacing: 8

                    CheckBox {
                        checked: bought

                        onClicked: {
                            root.listModel.setProperty(index, "bought", checked);
                            root.dataChanged();
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true

                        Text {
                            text: name

                            font.pixelSize: 18
                            font.bold: true
                            font.strikeout: bought
                        }

                        Text {
                            text: "Added by " + addedBy

                            font.pixelSize: 12
                            opacity: 0.6
                        }
                    }

                    Button {
                        text: "Save"

                        onClicked: {
                            root.saveItem(name);
                        }
                    }

                    Button {
                        text: "Delete"

                        onClicked: {
                            root.listModel.remove(index);

                            root.dataChanged();
                        }
                    }
                }
            }
        }
    }
}
