#include "initialsynccoordinator.h"
#include "itemsynccoordinator.h"
#include "familydatavalidation.h"
#include "shoppinglistmodel.h"
#include "saveditemsmodel.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

namespace {
const QString milk = "11111111-1111-4111-8111-111111111111";
const QString bread = "22222222-2222-4222-8222-222222222222";
QVariantMap shopping(const QString &id, const QString &name, bool bought = false)
{
    return {{"id", id}, {"name", name}, {"addedBy", "Tim"}, {"bought", bought}};
}
QVariantMap family(const QVariantMap &shoppingItems = {}, const QVariantMap &savedItems = {})
{
    return {{"shoppingItems", shoppingItems}, {"savedItems", savedItems}};
}
struct Rig {
    InitialSyncCoordinator startup;
    ShoppingListModel model;
    SavedItemsModel saved;
    QSignalSpy requests{&startup, &InitialSyncCoordinator::verificationRequested};
    QSignalSpy commits{&startup, &InitialSyncCoordinator::snapshotVerified};
    QSignalSpy attempts{&startup, &InitialSyncCoordinator::attemptStarted};
    QByteArray cache = "original cache";
    bool saveSucceeds = true;
    Rig()
    {
        model.upsertRemoteItem(milk, "Cached stale milk", "Tim", false);
        QObject::connect(&startup, &InitialSyncCoordinator::snapshotVerified, &startup,
                         [this](quint64 attempt, const QVariantMap &data) {
            if (saveSucceeds) {
                cache = QJsonDocument::fromVariant(data).toJson();
                model.loadFromVariantList(FamilyData::items(data, "shoppingItems"));
                saved.loadFromVariantList(FamilyData::items(data, "savedItems"));
            }
            startup.committed(attempt, saveSucceeds, "cache write failed");
        });
    }
    quint64 token() const { return requests.last().at(1).toULongLong(); }
    void begin(const QVariantMap &data)
    {
        startup.begin();
        startup.setConnected(true);
        startup.stage(startup.attempt(), 1, data);
    }
};
}

class InitialSyncTests : public QObject
{
    Q_OBJECT
private slots:
    void collectionSnapshots_data()
    {
        QTest::addColumn<QString>("first");
        QTest::newRow("shopping-first") << QString("shoppingItems");
        QTest::newRow("saved-first") << QString("savedItems");
    }
    void collectionSnapshots()
    {
        QFETCH(QString, first);
        Rig r;
        r.startup.begin();
        r.startup.setConnected(true);
        const auto attempt = r.startup.attempt();
        const QString second = first == "shoppingItems" ? "savedItems" : "shoppingItems";
        const QVariantMap shop{{milk, shopping(milk, "Verified Milk")}};
        const QVariantMap saved{{bread, QVariantMap{{"id", bread}, {"name", "Bread"}}}};
        r.startup.stageCollection(attempt, 10, first, first == "shoppingItems" ? shop : saved);
        r.startup.stageCollection(attempt, 10, first, first == "shoppingItems" ? shop : saved);
        QCOMPARE(r.requests.count(), 0); // One collection, even duplicated, is not a full snapshot.
        QVERIFY(!r.startup.canEdit());
        r.startup.stageCollection(attempt, 9, second, second == "shoppingItems" ? shop : saved);
        QCOMPARE(r.requests.count(), 1); // Cross-collection callback order need not be monotonic.
        r.startup.verified(attempt, r.token(), 10, family(shop, saved));
        QVERIFY(r.startup.canEdit());
        QCOMPARE(r.saved.count(), 1);
        QCOMPARE(r.model.data(r.model.index(0), ShoppingListModel::NameRole).toString(), QString("Verified Milk"));
        r.startup.stageCollection(attempt, 100, first, {{"invalid", QVariantMap{}}});
        QVERIFY(r.startup.canEdit()); // Queued temporary callbacks after Ready are inert.
    }
    void emptyCollectionsAndInvalidPartial()
    {
        Rig r;
        r.startup.begin();
        r.startup.setConnected(true);
        r.startup.stageCollection(r.startup.attempt(), 1, "savedItems", {});
        QCOMPARE(r.requests.count(), 0);
        r.startup.stageCollection(r.startup.attempt(), 2, "shoppingItems", {});
        QCOMPARE(r.requests.count(), 1);
        r.startup.verified(r.startup.attempt(), r.token(), 2, family());
        QCOMPARE(r.model.count(), 0);
        Rig invalid;
        invalid.startup.begin();
        invalid.startup.setConnected(true);
        invalid.startup.stageCollection(invalid.startup.attempt(), 1, "shoppingItems", {{milk, shopping(milk, "Milk")}});
        invalid.startup.stageCollection(invalid.startup.attempt(), 2, "savedItems", {{bread, QVariantMap{{"name", 42}}}});
        QCOMPARE(invalid.startup.state(), InitialSyncCoordinator::Error);
        QCOMPARE(invalid.requests.count(), 0);
        QCOMPARE(invalid.cache, QByteArray("original cache"));
    }
    void collectionAttemptsAndRevisions()
    {
        Rig r;
        r.startup.begin();
        r.startup.setConnected(true);
        const auto oldAttempt = r.startup.attempt();
        r.startup.stageCollection(oldAttempt, 1, "shoppingItems", {{milk, shopping(milk, "Old Milk")}});
        r.startup.fail("simulated download failure");
        r.startup.begin();
        const auto current = r.startup.attempt();
        r.startup.begin(); // Duplicate begin must not create another attempt/listener set.
        QCOMPARE(r.startup.attempt(), current);
        r.startup.stageCollection(current, 10, "savedItems", {});
        r.startup.stageCollection(oldAttempt, 100, "shoppingItems", {{milk, shopping(milk, "Obsolete")}});
        QCOMPARE(r.requests.count(), 0);
        r.startup.stageCollection(current, 11, "shoppingItems", {{milk, shopping(milk, "New Milk")}});
        r.startup.stageCollection(current, 9, "shoppingItems", {{milk, shopping(milk, "Older callback")}});
        const auto firstToken = r.token();
        r.startup.stageCollection(current, 12, "shoppingItems", {{milk, shopping(milk, "Newest Milk")}});
        r.startup.verified(current, firstToken, 12, family({{milk, shopping(milk, "New Milk")}}));
        QCOMPARE(r.commits.count(), 0);
        QTRY_COMPARE(r.requests.count(), 2);
        r.startup.verified(current, r.token(), 12, family({{milk, shopping(milk, "Newest Milk")}}));
        QVERIFY(r.startup.canEdit());
        QCOMPARE(r.model.data(r.model.index(0), ShoppingListModel::NameRole).toString(), QString("Newest Milk"));
    }
    void disconnectWithPartialCollections()
    {
        Rig r;
        r.startup.begin();
        r.startup.setConnected(true);
        const auto oldAttempt = r.startup.attempt();
        r.startup.stageCollection(oldAttempt, 1, "shoppingItems", {});
        r.startup.setConnected(false);
        QVERIFY(!r.startup.canEdit());
        r.startup.setConnected(true);
        const auto current = r.startup.attempt();
        QVERIFY(current != oldAttempt);
        r.startup.stageCollection(oldAttempt, 100, "savedItems", {});
        r.startup.stageCollection(current, 3, "savedItems", {});
        QCOMPARE(r.requests.count(), 0);
        r.startup.stageCollection(current, 4, "shoppingItems", {});
        r.startup.verified(current, r.token(), 4, family());
        QVERIFY(r.startup.canEdit());
    }
    void validation_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<bool>("valid");
        QTest::newRow("null database") << QByteArray("null") << true;
        QTest::newRow("empty database") << QByteArray("{}") << true;
        QTest::newRow("null collections") << QByteArray("{\"shoppingItems\":null,\"savedItems\":null}") << true;
        QTest::newRow("bad json") << QByteArray("{") << false;
        QTest::newRow("empty body") << QByteArray() << false;
        QTest::newRow("bad root") << QByteArray("[]") << false;
        QTest::newRow("bad collection") << QByteArray("{\"savedItems\":[]}") << false;
        QTest::newRow("bad id") << QByteArray("{\"savedItems\":{\"bad-id\":{\"name\":\"Milk\"}}}") << false;
        QTest::newRow("partial invalid") << QJsonDocument::fromVariant(family(
            {{milk, shopping(milk, "Milk")}}, {{bread, QVariantMap{{"name", 42}}}})).toJson() << false;
        QTest::newRow("bad bought") << QJsonDocument::fromVariant(family(
            {{milk, QVariantMap{{"name", "Milk"}, {"addedBy", "Tim"}, {"bought", "false"}}}})).toJson() << false;
    }
    void validation()
    {
        QFETCH(QByteArray, json);
        QFETCH(bool, valid);
        QVariantMap parsed{{"sentinel", true}};
        QString error;
        QCOMPARE(FamilyData::parseSnapshot(json, parsed, error), valid);
        if (valid) {
            QCOMPARE(FamilyData::items(parsed, "shoppingItems").size(), 0);
            QCOMPARE(FamilyData::items(parsed, "savedItems").size(), 0);
        } else {
            QVERIFY(!error.isEmpty());
            QCOMPARE(parsed, (QVariantMap{{"sentinel", true}}));
        }
    }
    void emptyAndExisting()
    {
        Rig empty;
        empty.begin(family());
        QVERIFY(!empty.startup.canEdit());
        empty.startup.verified(empty.startup.attempt(), empty.token(), 1, family());
        QVERIFY(empty.startup.canEdit());
        QCOMPARE(empty.model.count(), 0); // Closed-app remote deletion prunes stale JSON row.
        Rig existing;
        const auto data = family({{milk, shopping(milk, "Fresh milk", true)}},
                                 {{bread, QVariantMap{{"id", bread}, {"name", "Bread"}}}});
        QVariantMap parsed;
        QString error;
        QVERIFY(FamilyData::parseSnapshot(QJsonDocument::fromVariant(data).toJson(), parsed, error));
        existing.begin(parsed);
        existing.startup.verified(existing.startup.attempt(), existing.token(), 1, parsed);
        QCOMPARE(existing.model.count(), 1);
        QCOMPARE(existing.saved.count(), 1);
        QCOMPARE(existing.model.data(existing.model.index(0), ShoppingListModel::ItemIdRole).toString(), milk);
        QVERIFY(existing.model.data(existing.model.index(0), ShoppingListModel::BoughtRole).toBool());
    }
    void handoff_data()
    {
        QTest::addColumn<QString>("change");
        QTest::addColumn<QString>("collection");
        QTest::addColumn<bool>("sdkFirst");
        for (const auto &collection : {QString("shoppingItems"), QString("savedItems")})
            for (const auto &change : {QString("add"), QString("modify"), QString("delete")})
                for (bool sdkFirst : {false, true})
                    QTest::newRow(qPrintable(collection + "-" + change + (sdkFirst ? "-sdk-first" : "-rest-first")))
                        << change << collection << sdkFirst;
    }
    void handoff()
    {
        QFETCH(QString, change);
        QFETCH(QString, collection);
        QFETCH(bool, sdkFirst);
        Rig r;
        auto oldData = family();
        oldData[collection] = QVariantMap{{milk, shopping(milk, "Milk")}};
        QVariantMap entries{{milk, shopping(milk, "Milk")}};
        if (change == "add") entries[bread] = shopping(bread, "Bread");
        if (change == "modify") entries[milk] = shopping(milk, "Milk newer", true);
        if (change == "delete") entries.remove(milk);
        auto newData = family();
        newData[collection] = entries;
        r.begin(oldData);
        const auto oldToken = r.token();
        // Neither REST-first nor SDK-first delivery authorizes an obsolete candidate.
        if (sdkFirst) {
            r.startup.stage(r.startup.attempt(), 3, newData);
            r.startup.verified(r.startup.attempt(), oldToken, 3, oldData);
        } else {
            // The SDK callback exists at ingress but its Qt delivery is still queued.
            r.startup.verified(r.startup.attempt(), oldToken, 2, newData);
        }
        QCOMPARE(r.commits.count(), 0);
        QCOMPARE(r.cache, QByteArray("original cache"));
        r.startup.stage(r.startup.attempt(), 3, newData);
        r.startup.stage(r.startup.attempt(), 1, oldData); // Late older SDK snapshot.
        QTRY_COMPARE(r.requests.count(), 2);
        r.startup.verified(r.startup.attempt(), oldToken, 3, oldData);
        QCOMPARE(r.commits.count(), 0);
        r.startup.verified(r.startup.attempt(), r.token(), 3, newData);
        QCOMPARE(r.startup.state(), InitialSyncCoordinator::Ready);
        QCOMPARE(r.model.toVariantList(), FamilyData::items(newData, "shoppingItems"));
        QCOMPARE(r.saved.toVariantList().size(), FamilyData::items(newData, "savedItems").size());
        if (collection == "savedItems" && entries.contains(milk))
            QCOMPARE(r.saved.data(r.saved.index(r.saved.findItemById(milk)), SavedItemsModel::NameRole).toString(),
                     entries.value(milk).toMap().value("name").toString());
        r.startup.verified(r.startup.attempt(), r.token(), 3, oldData); // Duplicate.
        QCOMPARE(r.commits.count(), 1);

        // A late cached child after Ready is only an invalidation. An online item
        // read decides insertion/update/removal, without a whole-model reset.
        ItemSyncCoordinator live;
        live.verifyRemoteEvents(true);
        live.setListening(true);
        QSignalSpy reads(&live, &ItemSyncCoordinator::readRequested);
        QSignalSpy ready(&live, &ItemSyncCoordinator::itemReady);
        QSignalSpy resets(&r.model, &QAbstractItemModel::modelReset);
        QSignalSpy savedResets(&r.saved, &QAbstractItemModel::modelReset);
        connect(&live, &ItemSyncCoordinator::itemReady, &live,
                [&r](const QString &collection, const QString &id, const QVariantMap &item, bool removed) {
            if (collection == "shoppingItems") {
                if (removed) r.model.removeItemById(id);
                else r.model.upsertRemoteItem(id, item.value("name").toString(), item.value("addedBy").toString(), item.value("bought").toBool());
            } else {
                if (removed) r.saved.removeItemById(id);
                else r.saved.upsertRemoteItem(id, item.value("name").toString());
            }
        });
        live.incoming(collection, milk, shopping(milk, "Stale replay"), false);
        QCOMPARE(ready.count(), 0);
        const auto current = entries.value(milk).toMap();
        live.completeRead(collection, milk, reads.last().at(2).toULongLong(), current, current.isEmpty());
        QCOMPARE(r.model.toVariantList(), FamilyData::items(newData, "shoppingItems"));
        QCOMPARE(resets.count(), 0);
        QCOMPARE(savedResets.count(), 0);
    }
    void failuresRetryAndOfflineCache()
    {
        Rig r;
        r.startup.begin();
        r.startup.stage(r.startup.attempt(), 1, family());
        QCOMPARE(r.requests.count(), 0); // Cached SDK data while offline is insufficient.
        r.startup.expire();
        QCOMPARE(r.startup.state(), InitialSyncCoordinator::Error);
        QCOMPARE(r.cache, QByteArray("original cache"));
        r.startup.begin();
        const auto attempt = r.startup.attempt();
        r.startup.begin();
        QCOMPARE(r.startup.attempt(), attempt); // No duplicate subscriptions/request attempt.
        r.startup.setConnected(true);
        r.startup.stage(attempt, 2, family());
        r.startup.verified(attempt, r.token(), 2, {}, "HTTP failed");
        QCOMPARE(r.startup.state(), InitialSyncCoordinator::Error);
        QCOMPARE(r.commits.count(), 0);
        r.startup.begin();
        r.startup.stage(attempt, 2, family()); // Obsolete SDK delivery.
        r.startup.verified(attempt, r.token(), 2, family());
        QCOMPARE(r.commits.count(), 0);
        r.startup.stage(r.startup.attempt(), 3, family());
        r.startup.verified(r.startup.attempt(), r.token(), 3, family());
        QVERIFY(r.startup.canEdit());
    }
    void mismatchTimeoutAndCacheFailure()
    {
        Rig r;
        const auto cached = family({{milk, shopping(milk, "Old cached SDK value")}});
        r.begin(cached);
        r.startup.verified(r.startup.attempt(), r.token(), 1, family());
        QCOMPARE(r.commits.count(), 0);
        r.startup.expire();
        QCOMPARE(r.startup.state(), InitialSyncCoordinator::Error);
        r.startup.verified(r.startup.attempt(), r.token(), 1, cached);
        QCOMPARE(r.commits.count(), 0);
        QCOMPARE(r.cache, QByteArray("original cache"));
        Rig noDisk;
        noDisk.saveSucceeds = false;
        noDisk.begin(family());
        noDisk.startup.verified(noDisk.startup.attempt(), noDisk.token(), 1, family());
        QVERIFY(!noDisk.startup.canEdit());
        QCOMPARE(noDisk.startup.state(), InitialSyncCoordinator::Error);
        QCOMPARE(noDisk.model.count(), 1);
    }
    void reconnectionPendingProtection()
    {
        Rig r;
        r.begin(family());
        r.startup.verified(r.startup.attempt(), r.token(), 1, family());
        ItemSyncCoordinator writes;
        QSignalSpy requests(&writes, &ItemSyncCoordinator::writeRequested);
        QSignalSpy reads(&writes, &ItemSyncCoordinator::readRequested);
        connect(&writes, &ItemSyncCoordinator::stateChanged, &r.startup, [&]() {
            r.startup.setBlocked(writes.unresolvedLocalCount() > 0);
        });
        const auto gen = writes.enqueue("shoppingItems", milk, ItemSyncCoordinator::DeleteItem);
        writes.verifyRemoteEvents(true);
        writes.setListening(true);
        writes.incoming("shoppingItems", bread, shopping(bread, "Remote Bread"), false);
        writes.clearRemoteReads();
        QCOMPARE(writes.unresolvedLocalCount(), 1);
        QCOMPARE(writes.state("shoppingItems", milk).queue.head().kind, ItemSyncCoordinator::DeleteItem);
        r.startup.setConnected(false);
        QVERIFY(!r.startup.canEdit());
        r.startup.setConnected(true);
        r.startup.stage(r.startup.attempt(), 3, family());
        const int before = r.requests.count();
        QCOMPARE(before, 1); // The earlier initial attempt only.
        writes.incoming("shoppingItems", milk, shopping(milk, "Stale"), false);
        QCOMPARE(writes.state("shoppingItems", milk).queue.size(), 1);
        writes.completeWrite("shoppingItems", milk, gen);
        QCOMPARE(r.requests.count(), before);
        writes.completeRead("shoppingItems", milk, reads.last().at(2).toULongLong(), {}, true);
        QCOMPARE(r.requests.count(), before + 1);
        r.startup.verified(r.startup.attempt(), r.token(), 3, family());
        QVERIFY(r.startup.canEdit());
        QCOMPARE(requests.count(), 1);

        const auto failed = writes.enqueue("savedItems", bread, ItemSyncCoordinator::DeleteItem);
        r.startup.setConnected(false);
        writes.completeWrite("savedItems", bread, failed, "permission denied");
        r.startup.setConnected(true);
        r.startup.stage(r.startup.attempt(), 4, family());
        const int committed = r.commits.count();
        r.startup.expire();
        QCOMPARE(r.startup.state(), InitialSyncCoordinator::Error);
        QCOMPARE(r.commits.count(), committed);
        QCOMPARE(writes.state("savedItems", bread).queue.head().generation, failed);
    }
    void acknowledgedReadRecovery()
    {
        ItemSyncCoordinator writes;
        QSignalSpy sent(&writes, &ItemSyncCoordinator::writeRequested);
        QSignalSpy reads(&writes, &ItemSyncCoordinator::readRequested);
        const auto generation = writes.enqueue("shoppingItems", milk, ItemSyncCoordinator::DeleteItem);
        writes.completeWrite("shoppingItems", milk, generation);
        writes.completeRead("shoppingItems", milk, reads.last().at(2).toULongLong(), {}, true, "connection lost");
        QCOMPARE(writes.unresolvedLocalCount(), 1);
        writes.revalidateAcknowledged();
        QCOMPARE(reads.count(), 2);
        QCOMPARE(sent.count(), 1); // Revalidation never repeats the acknowledged write.
        writes.completeRead("shoppingItems", milk, reads.last().at(2).toULongLong(), {}, true);
        QCOMPARE(writes.unresolvedLocalCount(), 0);
        const auto failed = writes.enqueue("savedItems", bread, ItemSyncCoordinator::DeleteItem);
        writes.completeWrite("savedItems", bread, failed, "write denied");
        writes.revalidateAcknowledged();
        QCOMPARE(reads.count(), 2);
        QCOMPARE(writes.state("savedItems", bread).queue.head().generation, failed);
    }
};

QTEST_GUILESS_MAIN(InitialSyncTests)
#include "initial_sync_tests.moc"
