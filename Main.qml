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

        // Save newly generated IDs back to local storage.
        if (data.savedItems || data.shoppingItems) {
            saveData();
        }

        console.log("FamilyApp storage:", StorageManager.storagePath());
    }

    Component.onCompleted: {
        loadData();

        console.log(JSON.stringify(shoppingModel.toVariantList()));
        console.log("Saved items:", JSON.stringify(savedItemsModel.toVariantList()));

        console.log("Firebase initialized:", FirebaseManager.initialized);
    }

    Connections {
        target: FirebaseManager

        function onRemoteDataChanged(path) {
            console.log("Realtime update detected:", path);

            FirebaseManager.loadFamilyData();
        }

        function onFamilyDataLoaded(shoppingItems, savedItems) {
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

        running: autoRefreshButton.checked && FirebaseManager.initialized

        onTriggered: {
            console.log("Checking Firebase for updates...");
            FirebaseManager.loadFamilyData();
        }
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

        Button {
            id: autoRefreshButton

            text: checked ? "Stop Auto Refresh (Test)" : "Start Auto Refresh (Test)"

            checkable: true
            checked: false

            Layout.fillWidth: true

            enabled: FirebaseManager.initialized
        }

        Button {
            text: "Upload to Firebase (Test)"

            Layout.fillWidth: true

            enabled: FirebaseManager.initialized

            onClicked: {
                FirebaseManager.saveFamilyData(shoppingModel.toVariantList(), savedItemsModel.toVariantList());
            }
        }

        Button {
            text: "Download from Firebase (Test)"

            Layout.fillWidth: true
            enabled: FirebaseManager.initialized

            onClicked: {
                FirebaseManager.loadFamilyData();
            }
        }

        Button {
            id: realtimeButton

            text: checked ? "Stop Realtime (Test)" : "Start Realtime (Test)"

            checkable: true

            Layout.fillWidth: true

            enabled: FirebaseManager.initialized

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
