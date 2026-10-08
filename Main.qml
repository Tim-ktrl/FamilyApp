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

    SavedItemsModel {
        id: savedItemsModel

        onItemsChanged: {
            if (!window.loadingData) {
                window.saveData();
            }
        }
    }

    function saveData() {
        var success = StorageManager.saveData(shoppingModel.toVariantList(), savedItemsModel.toVariantList());

        if (!success) {
            console.error("Failed to save FamilyApp data");
        }
    }

    function loadData() {
        loadingData = true;

        var data = StorageManager.loadData();

        if (data.savedItems) {
            savedItemsModel.loadFromVariantList(data.savedItems);
        }

        if (data.shoppingItems) {
            shoppingModel.loadFromVariantList(data.shoppingItems);
        }

        loadingData = false;

        console.log("FamilyApp storage:", StorageManager.storagePath());
    }

    Component.onCompleted: {
        loadData();

        console.log("Firebase initialized:", FirebaseManager.initialized);

        FirebaseManager.testWrite();
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
            }

            SavedItemsPage {
                savedModel: savedItemsModel
                listModel: shoppingModel
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
