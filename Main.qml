import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    property bool loadingData: false

    visible: true
    width: 390
    height: 720
    title: "FamilyApp"

    ShoppingListModel {
        id: shoppingModel

        onItemsChanged: {
            if (!window.loadingData) {
                window.saveData();
            }
        }
    }

    ListModel {
        id: savedItemsModel
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
        var success = StorageManager.saveData(shoppingModel.toVariantList(), savedItemsToArray());

        if (!success) {
            console.error("Failed to save FamilyApp data");
        }
    }

    function loadData() {
        loadingData = true;

        var data = StorageManager.loadData();

        savedItemsModel.clear();

        if (data.savedItems) {
            for (var i = 0; i < data.savedItems.length; i++) {
                var savedItem = data.savedItems[i];

                savedItemsModel.append({
                    "name": savedItem.name
                });
            }
        }

        if (data.shoppingItems) {
            shoppingModel.loadFromVariantList(data.shoppingItems);
        }

        loadingData = false;

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
