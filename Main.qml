import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    property bool loadingData: false
    property bool applyingRemoteChange: false
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

        if (applyingRemoteChange)
            console.log("Remote item change persisted to local cache:", success);

        if (!success) {
            console.error("Failed to save FamilyApp data");
        }
    }

    function applyRemoteChange(action) {
        applyingRemoteChange = true;
        try {
            action();
        } finally {
            applyingRemoteChange = false;
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

        console.log("FamilyApp storage:", StorageManager.storagePath());
    }

    Component.onCompleted: {
        loadData();

        console.log(JSON.stringify(shoppingModel.toVariantList()));
        console.log("Saved items:", JSON.stringify(savedItemsModel.toVariantList()));

        console.log("Firebase initialized:", FirebaseManager.initialized);
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
            window.applyRemoteChange(function () {
                shoppingModel.upsertRemoteItem(item.id, item.name, item.addedBy, item.bought);
            });
        }

        function onShoppingItemRemoved(itemId) {
            window.applyRemoteChange(function () { shoppingModel.removeItemById(itemId); });
        }

        function onSavedItemReceived(item) {
            window.applyRemoteChange(function () { savedItemsModel.upsertRemoteItem(item.id, item.name); });
        }

        function onSavedItemRemoved(itemId) {
            window.applyRemoteChange(function () { savedItemsModel.removeItemById(itemId); });
        }

        function onChildListeningChanged() {
            childListenerButton.checked = FirebaseManager.childListening;
        }

        function onChildListeningFailed(collection, message) {
            console.error("Child Listener Test stopped:", collection, message);
        }

        function onItemSyncFailed(collection, itemId, message) {
            window.syncError = collection + "/" + itemId + ": " + message;
            console.error("Item synchronization paused:", window.syncError);
        }

        function onSyncStateChanged() {
            if (FirebaseManager.failedItemCount === 0)
                window.syncError = "";
        }

        function onRemoteDataChanged(path) {
            console.log("Realtime update detected:", path);

            FirebaseManager.loadFamilyData();
        }

        function onFamilyDataLoaded(shoppingItems, savedItems) {
            if (FirebaseManager.childListening || FirebaseManager.unresolvedItemCount > 0 || FirebaseManager.bulkWriteInProgress)
                return;
            console.log("QML RECEIVED FIREBASE DATA!");

            console.log("Downloaded shopping count:", shoppingItems.length);
            console.log("Downloaded saved count:", savedItems.length);

            console.log("Shopping count BEFORE:", shoppingModel.count);
            console.log("Saved count BEFORE:", savedItemsModel.count);

            window.loadingData = true;

            try {
                shoppingModel.loadFromVariantList(shoppingItems);
                savedItemsModel.loadFromVariantList(savedItems);
            } finally {
                window.loadingData = false;
            }

            console.log("Shopping count AFTER:", shoppingModel.count);
            console.log("Saved count AFTER:", savedItemsModel.count);

            console.log("Downloaded shopping data:", JSON.stringify(shoppingItems));

            console.log("Current shopping model:", JSON.stringify(shoppingModel.toVariantList()));

            var success = StorageManager.saveData(shoppingModel.toVariantList(), savedItemsModel.toVariantList());

            console.log("Local cache updated:", success);
        }

        function onFamilyDataLoadFailed(message) {
            console.error("Firebase download failed:", message);
        }
    }

    Timer {
        interval: 5000
        repeat: true
        triggeredOnStart: true

        running: autoRefreshButton.checked && FirebaseManager.initialized && FirebaseManager.startupState === FirebaseManager.Ready && !FirebaseManager.childListening
                 && FirebaseManager.unresolvedItemCount === 0 && !FirebaseManager.bulkWriteInProgress

        onTriggered: {
            console.log("Checking Firebase for updates...");
            FirebaseManager.loadFamilyData();
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

        Button {
            id: childListenerButton

            text: checked ? "Stop Live Synchronization" : "Start Live Synchronization"
            checkable: true
            checked: false
            Layout.fillWidth: true
            enabled: FirebaseManager.initialized && FirebaseManager.startupState === FirebaseManager.Ready

            onClicked: {
                if (checked) {
                    autoRefreshButton.checked = false;
                    realtimeButton.checked = false;
                    FirebaseManager.startChildListening();
                } else {
                    FirebaseManager.stopChildListening();
                }
                checked = FirebaseManager.childListening;
            }
        }

        Label {
            text: "Online prototype: startup uses cloud data. Pending writes are protected for this session."
            visible: FirebaseManager.childListening
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Label {
            text: window.syncError !== "" ? "Synchronization paused: " + window.syncError
                  : FirebaseManager.bulkWriteInProgress ? "Uploading family data..."
                  : "Synchronizing " + FirebaseManager.unresolvedItemCount + " item(s)..."
            visible: window.syncError !== "" || FirebaseManager.unresolvedItemCount > 0 || FirebaseManager.bulkWriteInProgress
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Button {
            id: autoRefreshButton

            text: checked ? "Stop Auto Refresh (Test)" : "Start Auto Refresh (Test)"

            checkable: true
            checked: false

            Layout.fillWidth: true

            enabled: FirebaseManager.initialized && FirebaseManager.startupState === FirebaseManager.Ready && !FirebaseManager.childListening
                     && FirebaseManager.unresolvedItemCount === 0 && !FirebaseManager.bulkWriteInProgress
        }

        Button {
            text: "Upload to Firebase (Test)"

            Layout.fillWidth: true

            enabled: FirebaseManager.initialized && FirebaseManager.startupState === FirebaseManager.Ready && !FirebaseManager.childListening
                     && FirebaseManager.unresolvedItemCount === 0 && !FirebaseManager.bulkWriteInProgress

            onClicked: {
                FirebaseManager.saveFamilyData(shoppingModel.toVariantList(), savedItemsModel.toVariantList());
            }
        }

        Button {
            text: "Download from Firebase (Test)"

            Layout.fillWidth: true
            enabled: FirebaseManager.initialized && FirebaseManager.startupState === FirebaseManager.Ready && !FirebaseManager.childListening
                     && FirebaseManager.unresolvedItemCount === 0 && !FirebaseManager.bulkWriteInProgress

            onClicked: {
                FirebaseManager.loadFamilyData();
            }
        }

        Button {
            id: realtimeButton

            text: checked ? "Stop Realtime (Test)" : "Start Realtime (Test)"

            checkable: true

            Layout.fillWidth: true

            enabled: FirebaseManager.initialized && FirebaseManager.startupState === FirebaseManager.Ready && !FirebaseManager.childListening

            onClicked: {
                if (checked) {
                    FirebaseManager.startRealtimeListening();
                } else {
                    FirebaseManager.stopRealtimeListening();
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
