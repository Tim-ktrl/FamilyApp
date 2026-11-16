#include "itemsynccoordinator.h"
#include "qtcallbackgate.h"
#include "shoppinglistmodel.h"
#include "saveditemsmodel.h"
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>
#include <memory>
#include <thread>

namespace {
const QString milk = "11111111-1111-4111-8111-111111111111";
const QString bread = "22222222-2222-4222-8222-222222222222";
QVariantMap item(const QString &id, const QString &name, bool bought = false)
{
    return {{"id", id}, {"name", name}, {"addedBy", "Tim"}, {"bought", bought}};
}

struct Rig {
    ItemSyncCoordinator sync;
    ShoppingListModel shopping;
    SavedItemsModel saved;
    QSignalSpy writes{&sync, &ItemSyncCoordinator::writeRequested};
    QSignalSpy reads{&sync, &ItemSyncCoordinator::readRequested};
    QSignalSpy ready{&sync, &ItemSyncCoordinator::itemReady};
    QSignalSpy failures{&sync, &ItemSyncCoordinator::itemFailed};
    QSignalSpy resetsShopping{&shopping, &QAbstractItemModel::modelReset};
    QSignalSpy resetsSaved{&saved, &QAbstractItemModel::modelReset};
    QAbstractItemModelTester shoppingTester{&shopping, QAbstractItemModelTester::FailureReportingMode::QtTest};
    QAbstractItemModelTester savedTester{&saved, QAbstractItemModelTester::FailureReportingMode::QtTest};
    QString collection;

    explicit Rig(const QString &c) : collection(c)
    {
        apply(milk, item(milk, "Milk"), false);
        apply(bread, item(bread, "Bread"), false);
        QObject::connect(&sync, &ItemSyncCoordinator::itemReady, &sync,
                         [this](const QString &c, const QString &id, const QVariantMap &data, bool removed) {
            if (c == collection) apply(id, data, removed);
        });
        sync.setListening(true);
    }
    void apply(const QString &id, const QVariantMap &data, bool removed)
    {
        if (collection == "shoppingItems") {
            if (removed) shopping.removeItemById(id);
            else shopping.upsertRemoteItem(id, data.value("name").toString(), data.value("addedBy").toString(), data.value("bought").toBool());
        } else {
            if (removed) saved.removeItemById(id);
            else saved.upsertRemoteItem(id, data.value("name").toString());
        }
    }
    QString name(const QString &id) const
    {
        if (collection == "shoppingItems")
            return shopping.data(shopping.index(shopping.findItemById(id)), ShoppingListModel::NameRole).toString();
        return saved.data(saved.index(saved.findItemById(id)), SavedItemsModel::NameRole).toString();
    }
    quint64 put(const QString &name)
    {
        const auto data = item(milk, name);
        apply(milk, data, false);
        return sync.enqueue(collection, milk, ItemSyncCoordinator::PutItem, data);
    }
    quint64 readToken() const { return reads.last().at(2).toULongLong(); }
};
}

class PendingWriteTests : public QObject
{
    Q_OBJECT
private slots:
    void sequences_data()
    {
        QTest::addColumn<QString>("collection");
        QTest::newRow("shopping") << QString("shoppingItems");
        QTest::newRow("saved") << QString("savedItems");
    }
    void sequences()
    {
        QFETCH(QString, collection);
        Rig r(collection);
        // A/B: Milk is protected while Bread remains live, including removals.
        const auto generation = r.put("Milk local");
        r.sync.incoming(collection, bread, item(bread, "Bread remote"), false);
        r.sync.incoming(collection, milk, item(milk, "Milk stale"), false);
        r.sync.incoming(collection, milk, {}, true);
        QCOMPARE(r.name(bread), QString("Bread remote"));
        QCOMPARE(r.name(milk), QString("Milk local"));
        QCOMPARE(r.sync.unresolvedCount(), 1);
        QCOMPARE(r.reads.count(), 0);
        r.sync.completeWrite(collection, milk, generation);
        QCOMPARE(r.reads.count(), 1);
        QCOMPARE(r.sync.state(collection, milk).acknowledgedGeneration, generation);
        QCOMPARE(r.name(milk), QString("Milk local"));
        r.sync.completeRead(collection, milk, r.readToken(), item(milk, "Milk local"), false);
        QCOMPARE(r.sync.unresolvedCount(), 0);

        // C: An old payload queued before ack but delivered after reconciliation
        // can only trigger another current read, never directly change Milk.
        const auto second = r.put("Milk newest");
        QMetaObject::invokeMethod(&r.sync, [&r, collection]() {
            r.sync.incoming(collection, milk, item(milk, "Milk old queued"), false);
        }, Qt::QueuedConnection);
        r.sync.completeWrite(collection, milk, second);
        r.sync.completeRead(collection, milk, r.readToken(), item(milk, "Milk newest"), false);
        QCoreApplication::sendPostedEvents(&r.sync, QEvent::MetaCall);
        QCOMPARE(r.name(milk), QString("Milk newest"));
        QCOMPARE(r.sync.unresolvedCount(), 1);
        r.sync.completeRead(collection, milk, r.readToken(), item(milk, "Milk newest"), false);
        QCOMPARE(r.sync.unresolvedCount(), 0);

        // D: FIFO, stale/duplicate acknowledgments, and newest intent.
        const auto firstQueued = r.put("Milk first");
        const auto nextQueued = r.put("Milk second");
        const int writesBeforeAck = r.writes.count();
        QCOMPARE(r.sync.state(collection, milk).queue.size(), 2);
        r.sync.completeWrite(collection, milk, nextQueued); // Not yet sent.
        QCOMPARE(r.sync.state(collection, milk).activeGeneration, firstQueued);
        r.sync.completeWrite(collection, milk, firstQueued);
        QCOMPARE(r.writes.count(), writesBeforeAck + 1);
        QCOMPARE(r.sync.state(collection, milk).activeGeneration, nextQueued);
        r.sync.completeWrite(collection, milk, firstQueued); // Duplicate.
        QCOMPARE(r.sync.state(collection, milk).activeGeneration, nextQueued);
        QCOMPARE(r.name(milk), QString("Milk second"));
        r.sync.completeWrite(collection, milk, nextQueued);
        r.sync.completeRead(collection, milk, r.readToken(), item(milk, "Milk second"), false);

        // E: A delete tombstone survives old add/change payloads.
        r.apply(milk, {}, true);
        const auto deletion = r.sync.enqueue(collection, milk, ItemSyncCoordinator::DeleteItem);
        r.sync.incoming(collection, milk, item(milk, "Milk resurrected"), false);
        QVERIFY(r.name(milk).isEmpty());
        QCOMPARE(r.sync.state(collection, milk).queue.head().kind, ItemSyncCoordinator::DeleteItem);
        r.sync.completeWrite(collection, milk, deletion);
        r.sync.incoming(collection, milk, item(milk, "Milk old"), false);
        const auto oldRead = r.readToken();
        r.sync.completeRead(collection, milk, oldRead, item(milk, "Milk old"), false);
        QVERIFY(r.name(milk).isEmpty()); // Dirty read is discarded.
        QVERIFY(r.readToken() != oldRead);
        r.sync.completeRead(collection, milk, r.readToken(), {}, true);
        QVERIFY(r.name(milk).isEmpty());
        QCOMPARE(r.sync.unresolvedCount(), 0);

        // F: Failed heads and later queued intent stay protected and visible.
        const auto failing = r.put("Milk unsynced");
        r.sync.completeWrite(collection, milk, failing, "permission denied");
        const int sentBeforeFailure = r.writes.count();
        r.put("Milk latest unsynced");
        r.sync.incoming(collection, milk, item(milk, "Remote conflicting"), false);
        r.sync.incoming(collection, milk, {}, true);
        QCOMPARE(r.writes.count(), sentBeforeFailure);
        QCOMPARE(r.failures.count(), 1);
        QCOMPARE(r.sync.failedCount(), 1);
        QCOMPARE(r.sync.state(collection, milk).queue.size(), 2);
        QCOMPARE(r.sync.state(collection, milk).error, QString("permission denied"));
        QCOMPARE(r.name(milk), QString("Milk latest unsynced"));
        r.sync.incoming(collection, bread, item(bread, "Bread still live"), false);
        QCOMPARE(r.name(bread), QString("Bread still live"));
        QCOMPARE(r.resetsShopping.count(), 0);
        QCOMPARE(r.resetsSaved.count(), 0);
    }

    void checkboxSerialization()
    {
        Rig r("shoppingItems");
        r.shopping.setBought(r.shopping.findItemById(milk), true);
        const auto first = r.sync.enqueue(r.collection, milk, ItemSyncCoordinator::SetBought, {{"bought", true}});
        r.shopping.setBought(r.shopping.findItemById(milk), false);
        const auto second = r.sync.enqueue(r.collection, milk, ItemSyncCoordinator::SetBought, {{"bought", false}});
        QCOMPARE(r.writes.count(), 1);
        QCOMPARE(r.writes.first().at(4).toMap().value("bought").toBool(), true);
        r.sync.completeWrite(r.collection, milk, first);
        QCOMPARE(r.writes.count(), 2);
        QCOMPARE(r.writes.last().at(4).toMap().value("bought").toBool(), false);
        r.sync.completeWrite(r.collection, milk, first);
        QCOMPARE(r.sync.state(r.collection, milk).activeGeneration, second);
        r.sync.completeWrite(r.collection, milk, second);
        r.sync.completeRead(r.collection, milk, r.readToken(), item(milk, "Milk", false), false);
        QCOMPARE(r.shopping.data(r.shopping.index(0), ShoppingListModel::BoughtRole).toBool(), false);
    }

    void readsAndSessions_data() { sequences_data(); }
    void readsAndSessions()
    {
        QFETCH(QString, collection);
        Rig r(collection);
        const auto first = r.put("Milk first");
        r.sync.completeWrite(collection, milk, first);
        const auto staleToken = r.readToken();
        const auto second = r.put("Milk second");
        r.sync.completeWrite(collection, milk, second);
        QCOMPARE(r.reads.count(), 1); // No overlapping reads.
        r.sync.completeRead(collection, milk, staleToken, item(milk, "Milk first"), false);
        QCOMPARE(r.reads.count(), 2);
        r.sync.completeRead(collection, milk, staleToken, {}, true); // Old duplicate.
        QCOMPARE(r.name(milk), QString("Milk second"));
        r.sync.completeRead(collection, milk, r.readToken(), item(milk, "Milk second"), false);

        r.sync.incoming(collection, milk, {}, true);
        const auto previousSession = r.readToken();
        r.sync.setListening(false);
        r.sync.setListening(true);
        r.sync.completeRead(collection, milk, previousSession, {}, true);
        QCOMPARE(r.name(milk), QString("Milk second"));
        QVERIFY(r.readToken() != previousSession);
        r.sync.completeRead(collection, milk, r.readToken(), item(milk, "Milk fresh"), false);
        QCOMPARE(r.name(milk), QString("Milk fresh"));

        r.sync.setListening(false);
        const auto offGeneration = r.put("Local while off");
        r.sync.completeWrite(collection, milk, offGeneration);
        r.sync.completeRead(collection, milk, r.readToken(), {}, true);
        QCOMPARE(r.name(milk), QString("Local while off")); // Opt-in application.
        QCOMPARE(r.sync.unresolvedCount(), 0);
        r.sync.setListening(true);
        r.sync.completeRead(collection, milk, r.readToken(), {}, true);
        QVERIFY(r.name(milk).isEmpty());

        const auto failing = r.put("Protected read failure");
        r.sync.completeWrite(collection, milk, failing);
        const int readsBeforeFailure = r.reads.count();
        r.sync.incoming(collection, milk, {}, true); // A dirty read must still report failure.
        r.sync.completeRead(collection, milk, r.readToken(), {}, false, "read failed or malformed");
        QCOMPARE(r.sync.failedCount(), 1);
        QCOMPARE(r.reads.count(), readsBeforeFailure);
        const int reads = r.reads.count();
        r.sync.incoming(collection, milk, {}, true);
        r.sync.setListening(false);
        r.sync.setListening(true);
        QCOMPARE(r.reads.count(), reads); // No automatic retry loop.
        QCOMPARE(r.name(milk), QString("Protected read failure"));
    }

    void failedDeleteAndAdd_data() { sequences_data(); }
    void failedDeleteAndAdd()
    {
        QFETCH(QString, collection);
        Rig r(collection);
        r.apply(milk, {}, true);
        const auto deletion = r.sync.enqueue(collection, milk, ItemSyncCoordinator::DeleteItem);
        r.sync.completeWrite(collection, milk, deletion, "delete denied");
        r.sync.incoming(collection, milk, item(milk, "Milk stale"), false);
        QVERIFY(r.name(milk).isEmpty());
        QCOMPARE(r.sync.state(collection, milk).queue.head().kind, ItemSyncCoordinator::DeleteItem);
        const QString added = "33333333-3333-4333-8333-333333333333";
        const auto data = item(added, "New local");
        r.apply(added, data, false);
        const auto addition = r.sync.enqueue(collection, added, ItemSyncCoordinator::PutItem, data);
        r.sync.incoming(collection, added, {}, true);
        QCOMPARE(r.name(added), QString("New local"));
        r.sync.completeWrite(collection, added, addition, "add denied");
        r.sync.incoming(collection, added, {}, true);
        QCOMPARE(r.name(added), QString("New local"));
        QCOMPARE(r.sync.failedCount(), 2);
    }

    void collectionIsolationAndBulk()
    {
        ItemSyncCoordinator sync;
        QSignalSpy writes(&sync, &ItemSyncCoordinator::writeRequested);
        QSignalSpy reads(&sync, &ItemSyncCoordinator::readRequested);
        QSignalSpy ready(&sync, &ItemSyncCoordinator::itemReady);
        sync.setListening(true);
        sync.setBulkBusy(true);
        const auto generation = sync.enqueue("shoppingItems", milk, ItemSyncCoordinator::DeleteItem);
        QCOMPARE(writes.count(), 0);
        QCOMPARE(sync.mutationGeneration(), generation);
        sync.incoming("savedItems", milk, item(milk, "Saved remote"), false);
        QCOMPARE(ready.count(), 1);
        sync.incoming("shoppingItems", milk, item(milk, "Shopping stale"), false);
        QCOMPARE(ready.count(), 1);
        sync.setBulkBusy(false);
        QCOMPARE(writes.count(), 1);
        sync.completeWrite("shoppingItems", milk, generation);
        QCOMPARE(sync.unresolvedCount(), 1); // Legacy guard remains through read.
        sync.completeRead("shoppingItems", milk, reads.last().at(2).toULongLong(), {}, true);
        QCOMPARE(sync.unresolvedCount(), 0);
        QVERIFY(!sync.bulkBusy());
    }

    void callbackLifetimeAndThread()
    {
        auto receiver = std::make_unique<QObject>();
        auto gate = std::make_shared<QtCallbackGate>(receiver.get());
        int delivered = 0;
        QThread *deliveryThread = nullptr;
        std::thread worker([&]() {
            gate->post([&]() { ++delivered; deliveryThread = QThread::currentThread(); });
        });
        worker.join();
        QCOMPARE(delivered, 0);
        QCoreApplication::sendPostedEvents(receiver.get(), QEvent::MetaCall);
        QCOMPARE(delivered, 1);
        QCOMPARE(deliveryThread, QThread::currentThread());
        gate->post([&]() { ++delivered; });
        gate->invalidate();
        receiver.reset();
        gate->post([&]() { ++delivered; }); // Late SDK completion.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(delivered, 1);
    }
};

QTEST_GUILESS_MAIN(PendingWriteTests)
#include "pending_write_tests.moc"
