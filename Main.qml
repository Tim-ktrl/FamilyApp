import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    property bool loadingData: false
    property string syncError: ""

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
        // Both local and remote model changes update JSON only. Firebase writes
        // remain in the pages' user-action handlers, never in itemsChanged.
        var success = StorageManager.saveData(shoppingModel.toVariantList(), savedItemsModel.toVariantList());

        if (!success) {
            console.error("Failed to save FamilyApp data");
        }
    }

    function loadData() {
        loadingData = true;

        var data = StorageManager.loadData();

        try {
            if (data.savedItems) {
                savedItemsModel.loadFromVariantList(data.savedItems);
            }

            if (data.shoppingItems) {
                shoppingModel.loadFromVariantList(data.shoppingItems);
            }
        } finally {
            loadingData = false;
        }
    }

    Component.onCompleted: {
        loadData();

        FirebaseManager.beginInitialSync();
    }

    Connections {
        target: FirebaseManager

        function onInitialDataVerified(attempt, shoppingItems, savedItems) {
            // The manager emits only a fully validated, network-verified snapshot.
            // Cache persistence and both model loads complete before Ready.
            if (!StorageManager.saveData(shoppingItems, savedItems)) {
                FirebaseManager.completeInitialSync(attempt, false, "Could not save the verified local cache.");
                return;
            }
            window.loadingData = true;
            try {
                shoppingModel.loadFromVariantList(shoppingItems);
                savedItemsModel.loadFromVariantList(savedItems);
                FirebaseManager.completeInitialSync(attempt, true);
            } finally {
                window.loadingData = false;
            }
        }

        function onShoppingItemReceived(item) {
            shoppingModel.upsertRemoteItem(item.id, item.name, item.addedBy, item.bought);
        }

        function onShoppingItemRemoved(itemId) {
            shoppingModel.removeItemById(itemId);
        }

        function onSavedItemReceived(item) {
            savedItemsModel.upsertRemoteItem(item.id, item.name);
        }

        function onSavedItemRemoved(itemId) {
            savedItemsModel.removeItemById(itemId);
        }

        function onItemSyncFailed(collection, itemId, message) {
            window.syncError = collection + "/" + itemId + ": " + message;
            console.error("Item synchronization paused:", window.syncError);
        }

        function onSyncStateChanged() {
            if (FirebaseManager.failedItemCount === 0)
                window.syncError = "";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: FirebaseManager.startupState === FirebaseManager.Loading
            visible: running
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: FirebaseManager.startupState !== FirebaseManager.Ready
            text: FirebaseManager.startupState === FirebaseManager.Error
                  ? FirebaseManager.startupError
                  : "Verifying cloud data... Cached data is read-only."
        }

        Button {
            objectName: "retrySynchronizationButton"
            text: "Retry synchronization"
            Layout.fillWidth: true
            visible: FirebaseManager.startupState === FirebaseManager.Error
            onClicked: FirebaseManager.beginInitialSync()
        }

        StackLayout {
            objectName: "shoppingPages"
            enabled: FirebaseManager.editingEnabled
            Layout.fillWidth: true
            Layout.fillHeight: true

            currentIndex: navigation.currentIndex

            ShoppingListPage {
                objectName: "shoppingPage"
                listModel: shoppingModel
                savedModel: savedItemsModel
            }

            SavedItemsPage {
                objectName: "savedPage"
                savedModel: savedItemsModel
                listModel: shoppingModel
            }
        }

        Label {
            objectName: "itemSyncStatus"
            text: window.syncError !== "" ? "Synchronization paused: " + window.syncError
                  : "Synchronizing " + FirebaseManager.unresolvedItemCount + " item(s)..."
            visible: window.syncError !== "" || FirebaseManager.unresolvedItemCount > 0
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
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
