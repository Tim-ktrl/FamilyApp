import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    visible: true
    width: 390
    height: 720
    title: "FamilyApp"

    ListModel {
        id: shoppingModel
    }

    ListModel {
        id: savedItemsModel
    }

    function shoppingItemsToArray() {
        var items = [];

        for (var i = 0; i < shoppingModel.count; i++) {
            var item = shoppingModel.get(i);

            items.push({
                "name": item.name,
                "addedBy": item.addedBy,
                "bought": item.bought
            });
        }

        return items;
    }

    function savedItemsToArray() {
        var items = [];

        for (var i = 0; i < savedItemsModel.count; i++) {
            var item = savedItemsModel.get(i);

            items.push({
                "name": item.name
            });
        }

        return items;
    }

    function saveData() {
        var success = StorageManager.saveData(shoppingItemsToArray(), savedItemsToArray());

        if (!success) {
            console.error("Failed to save FamilyApp data");
        }
    }

    function loadData() {
        var data = StorageManager.loadData();

        shoppingModel.clear();
        savedItemsModel.clear();

        if (data.shoppingItems) {
            for (var i = 0; i < data.shoppingItems.length; i++) {
                var item = data.shoppingItems[i];

                shoppingModel.append({
                    "name": item.name,
                    "addedBy": item.addedBy,
                    "bought": item.bought
                });
            }
        }

        if (data.savedItems) {
            for (var j = 0; j < data.savedItems.length; j++) {
                var savedItem = data.savedItems[j];

                savedItemsModel.append({
                    "name": savedItem.name
                });
            }
        }

        console.log("FamilyApp storage:", StorageManager.storagePath());
    }

    Component.onCompleted: {
        loadData();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true

            currentIndex: navigation.currentIndex

            ShoppingListPage {
                listModel: shoppingModel
                savedModel: savedItemsModel

                onDataChanged: {
                    window.saveData();
                }
            }

            SavedItemsPage {
                savedModel: savedItemsModel
                listModel: shoppingModel

                onDataChanged: {
                    window.saveData();
                }
            }
        }

        TabBar {
            id: navigation

            Layout.fillWidth: true

            TabButton {
                text: "Shopping List"
            }

            TabButton {
                text: "Saved Items"
            }
        }
    }
}
