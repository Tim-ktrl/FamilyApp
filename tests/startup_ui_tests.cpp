#include "startup_ui_mocks.h"
#include "shoppinglistmodel.h"
#include "saveditemsmodel.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QtTest>

class StartupUiTests : public QObject
{
    Q_OBJECT
private slots:
    void cacheAndEditingGate()
    {
        QQmlApplicationEngine engine;
        auto *firebase = engine.singletonInstance<StartupFirebaseMock *>("FamilyAppStartupTests", "FirebaseManager");
        auto *storage = engine.singletonInstance<StartupStorageMock *>("FamilyAppStartupTests", "StorageManager");
        QVERIFY(firebase);
        QVERIFY(storage);
        const QString id = "11111111-1111-4111-8111-111111111111";
        const QVariantMap cached{{"id", id}, {"name", "Cached stale Milk"}, {"addedBy", "Tim"}, {"bought", false}};
        storage->saveData({cached}, {});
        storage->saves = 0;
        engine.loadFromModule("FamilyAppStartupTests", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = engine.rootObjects().first();
        auto *pages = window->findChild<QQuickItem *>("shoppingPages");
        auto *shopping = window->findChild<ShoppingListModel *>();
        auto *saved = window->findChild<SavedItemsModel *>();
        QVERIFY(pages);
        QVERIFY(shopping);
        QVERIFY(saved);
        QCOMPARE(firebase->beginCalls, 1);
        QVERIFY(!pages->isEnabled());
        QCOMPARE(shopping->count(), 1);
        QCOMPARE(storage->saves, 0); // Merely reading cache must not rewrite it.
        QCOMPARE(firebase->writeCalls, 0);
        auto *shoppingPage = window->findChild<QObject *>("shoppingPage");
        auto *savedPage = window->findChild<QObject *>("savedPage");
        QVERIFY(shoppingPage);
        QVERIFY(savedPage);
        QVERIFY(QMetaObject::invokeMethod(shoppingPage, "removeItem", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(id))));
        QVERIFY(QMetaObject::invokeMethod(shoppingPage, "saveItem", Q_ARG(QVariant, QVariant("Blocked save"))));
        QVERIFY(QMetaObject::invokeMethod(savedPage, "addToShoppingList", Q_ARG(QVariant, QVariant("Blocked addition"))));
        QCOMPARE(shopping->count(), 1);
        QCOMPARE(saved->count(), 0);
        QCOMPARE(firebase->writeCalls, 0);

        const QVariantMap fresh{{"id", id}, {"name", "Verified Milk"}, {"addedBy", "Alex"}, {"bought", true}};
        emit firebase->initialDataVerified(1, {fresh}, {});
        QVERIFY(pages->isEnabled());
        QCOMPARE(shopping->data(shopping->index(0), ShoppingListModel::NameRole).toString(), QString("Verified Milk"));
        QCOMPARE(storage->saves, 1);
        QCOMPARE(storage->loadData().value("shoppingItems").toList().first().toMap().value("id").toString(), id);
        QCOMPARE(firebase->writeCalls, 0); // Actual Main.qml loading cannot echo writes.

        firebase->offline();
        QVERIFY(!pages->isEnabled());
        firebase->online();
        QVERIFY(!pages->isEnabled()); // Connected alone is not Ready.
        emit firebase->initialDataVerified(2, {}, {});
        QVERIFY(pages->isEnabled());
        QCOMPARE(shopping->count(), 0);
        QCOMPARE(saved->count(), 0);
        QCOMPARE(firebase->writeCalls, 0);

        emit firebase->shoppingItemReceived(fresh);
        QCOMPARE(shopping->count(), 1);
        QCOMPARE(storage->saves, 3); // Verified restart plus one incremental change.
        emit firebase->shoppingItemRemoved(id);
        QCOMPARE(shopping->count(), 0);
        QCOMPARE(firebase->writeCalls, 0);
    }
    void failedCacheCommitKeepsEditingLocked()
    {
        QQmlApplicationEngine engine;
        auto *firebase = engine.singletonInstance<StartupFirebaseMock *>("FamilyAppStartupTests", "FirebaseManager");
        auto *storage = engine.singletonInstance<StartupStorageMock *>("FamilyAppStartupTests", "StorageManager");
        engine.loadFromModule("FamilyAppStartupTests", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *pages = engine.rootObjects().first()->findChild<QQuickItem *>("shoppingPages");
        storage->failSaves = true;
        const auto previous = storage->json;
        emit firebase->initialDataVerified(1, {}, {});
        QCOMPARE(firebase->startupState(), int(StartupFirebaseMock::Error));
        QVERIFY(!pages->isEnabled());
        QCOMPARE(storage->json, previous);
        QCOMPARE(firebase->writeCalls, 0);
    }
    void userFeedbackWithoutDeveloperControls()
    {
        QQmlApplicationEngine engine;
        auto *firebase = engine.singletonInstance<StartupFirebaseMock *>("FamilyAppStartupTests", "FirebaseManager");
        engine.loadFromModule("FamilyAppStartupTests", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = engine.rootObjects().first();
        auto *pages = window->findChild<QQuickItem *>("shoppingPages");
        auto *retry = window->findChild<QQuickItem *>("retrySynchronizationButton");
        auto *status = window->findChild<QQuickItem *>("itemSyncStatus");
        QVERIFY(pages);
        QVERIFY(retry);
        QVERIFY(status);
        const QStringList retiredControls{"Start Live Synchronization", "Stop Live Synchronization",
            "Upload to Firebase", "Download from Firebase", "Auto Refresh", "Realtime (Test)", "Online prototype:"};
        for (auto *object : window->findChildren<QObject *>()) {
            const auto text = object->property("text").toString();
            for (const auto &retired : retiredControls)
                QVERIFY2(!text.contains(retired), qPrintable(text));
        }
        QVERIFY(!pages->isEnabled());
        QVERIFY(!retry->isVisible());
        firebase->offline();
        QVERIFY(retry->isVisible());
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(firebase->beginCalls, 2);
        QVERIFY(!pages->isEnabled());
        firebase->online();
        emit firebase->initialDataVerified(2, {}, {});
        QVERIFY(pages->isEnabled());
        firebase->failItem();
        QVERIFY(status->isVisible());
        QVERIFY(status->property("text").toString().contains("Write denied"));
        QCOMPARE(firebase->writeCalls, 0);
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    StartupUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "startup_ui_tests.moc"
