import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    visible: true
    width: 390
    height: 720
    title: "FamilyApp"

    // Main shopping list data
    ListModel {
        id: shoppingModel

        ListElement {
            name: "Milk"
            addedBy: "Tim"
            bought: false
        }

        ListElement {
            name: "Eggs"
            addedBy: "Tim"
            bought: false
        }
    }

    // Items we want to remember for later
    ListModel {
        id: savedItemsModel

        ListElement {
            name: "Bread"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            id: pageStack

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
