#include "shoppinglistmodel.h"
#include "saveditemsmodel.h"
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

class ModelSyncTests : public QObject
{
    Q_OBJECT
private slots:
    void shoppingRemoteChanges()
    {
        ShoppingListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QString milk = model.addItem("Milk", "Tim");
        const QString bread = model.addItem("Bread", "Tim");
        model.loadFromVariantList(model.toVariantList()); // Startup JSON cache path.
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy count(&model, &ShoppingListModel::countChanged);
        QSignalSpy items(&model, &ShoppingListModel::itemsChanged);

        model.upsertRemoteItem(milk, "Milk", "Tim", false);
        QCOMPARE(model.count(), 2);
        QCOMPARE(items.count(), 0);
        model.upsertRemoteItem(bread, "Brown bread", "Alex", true);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 1);
        QCOMPARE(changed.at(0).at(2).value<QList<int>>(),
                 (QList<int>{ShoppingListModel::NameRole, ShoppingListModel::AddedByRole, ShoppingListModel::BoughtRole}));
        QCOMPARE(model.data(model.index(0), ShoppingListModel::NameRole).toString(), QString("Milk"));
        QCOMPARE(model.data(model.index(0), ShoppingListModel::BoughtRole).toBool(), false);
        QCOMPARE(count.count(), 0);

        model.upsertRemoteItem(milk, "Milk", "Tim", true);
        QCOMPARE(changed.last().at(2).value<QList<int>>(), (QList<int>{ShoppingListModel::BoughtRole}));
        const QString remoteId = "11111111-1111-4111-8111-111111111111";
        model.upsertRemoteItem(remoteId, "Eggs", "Alex", false);
        model.upsertRemoteItem(remoteId, "Eggs", "Alex", false);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(model.toVariantList().at(2).toMap().value("id").toString(), remoteId);
        model.removeItemById(milk);
        model.removeItemById(milk);
        model.removeItemById("missing");
        QCOMPARE(removed.count(), 1);
        QCOMPARE(model.findItemById(bread), 0);
        QCOMPARE(model.count(), 2);
        QCOMPARE(count.count(), 2);
        QCOMPARE(reset.count(), 0);
    }

    void cachedSavedItems()
    {
        SavedItemsModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QString id = "22222222-2222-4222-8222-222222222222";
        model.loadFromVariantList({QVariantMap{{"id", id}, {"name", "Milk"}}});
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy items(&model, &SavedItemsModel::itemsChanged);
        QSignalSpy count(&model, &SavedItemsModel::countChanged);
        model.upsertRemoteItem(id, "Milk");
        QCOMPARE(model.count(), 1);
        QCOMPARE(items.count(), 0);
        model.upsertRemoteItem(id, "Oat milk");
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(2).value<QList<int>>(), (QList<int>{SavedItemsModel::NameRole}));
        QCOMPARE(count.count(), 0);
        const QString otherId = "33333333-3333-4333-8333-333333333333";
        model.upsertRemoteItem(otherId, "Oat milk");
        QCOMPARE(model.count(), 2); // Remote identity is UUID, not display name.
        model.removeItemById(id);
        model.removeItemById(id);
        QCOMPARE(model.findItemById(otherId), 0);
        QCOMPARE(model.toVariantList().at(0).toMap().value("id").toString(), otherId);
        QCOMPARE(count.count(), 2);
        QCOMPARE(items.count(), 3);
        QCOMPARE(reset.count(), 0);
    }

    void existingLocalOperations()
    {
        ShoppingListModel shopping;
        SavedItemsModel saved;
        QVERIFY(shopping.addItem(" ", "Tim").isEmpty());
        const QString id = shopping.addItem("Milk", "Tim");
        QVERIFY(!QUuid(id).isNull());
        shopping.setBought(0, true);
        QCOMPARE(shopping.toVariantList().at(0).toMap().value("bought").toBool(), true);
        QVERIFY(!saved.addItem("Milk").isEmpty());
        QVERIFY(saved.addItem("milk").isEmpty());
        shopping.upsertRemoteItem("", "Eggs", "Tim", false);
        saved.upsertRemoteItem("remote", " ");
        QCOMPARE(shopping.count(), 1);
        QCOMPARE(saved.count(), 1);
        shopping.removeItem(0);
        saved.removeItem(0);
        QCOMPARE(shopping.count(), 0);
        QCOMPARE(saved.count(), 0);
    }
};

QTEST_GUILESS_MAIN(ModelSyncTests)
#include "model_sync_tests.moc"
