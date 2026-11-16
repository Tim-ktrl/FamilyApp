#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QByteArray>
#include <QVariantMap>
#include <memory>
#include "itemsynccoordinator.h"
#include "qtcallbackgate.h"

#include <firebase/app.h>
#include <firebase/database.h>
#include <firebase/future.h>
#include <QNetworkReply>

class FirebaseChildListener;

class FirebaseManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(
        bool initialized
            READ initialized
                NOTIFY initializedChanged)

    Q_PROPERTY(bool childListening READ childListening NOTIFY childListeningChanged)
    Q_PROPERTY(int unresolvedItemCount READ unresolvedItemCount NOTIFY syncStateChanged)
    Q_PROPERTY(int failedItemCount READ failedItemCount NOTIFY syncStateChanged)
    Q_PROPERTY(bool bulkWriteInProgress READ bulkWriteInProgress NOTIFY syncStateChanged)

public:
    explicit FirebaseManager(
        QObject *parent = nullptr);

    ~FirebaseManager() override;

    bool initialized() const;
    bool childListening() const { return m_childListening; }
    int unresolvedItemCount() const { return m_itemSync.unresolvedCount(); }
    int failedItemCount() const { return m_itemSync.failedCount(); }
    bool bulkWriteInProgress() const { return m_itemSync.bulkBusy(); }
    Q_INVOKABLE void startChildListening();
    Q_INVOKABLE void stopChildListening();

    Q_INVOKABLE void testWrite();

    Q_INVOKABLE void saveFamilyData(
        const QVariantList &shoppingItems,
        const QVariantList &savedItems);

    Q_INVOKABLE void loadFamilyData();

    Q_INVOKABLE void updateShoppingItemBought(
        const QString &itemId,
        bool bought);

    Q_INVOKABLE void addShoppingItem(
        const QString &itemId,
        const QString &name,
        const QString &addedBy);

    Q_INVOKABLE void deleteShoppingItem(
        const QString &itemId);

    Q_INVOKABLE void addSavedItem(
        const QString &itemId,
        const QString &name);

    Q_INVOKABLE void deleteSavedItem(
        const QString &itemId);

    Q_INVOKABLE void startRealtimeListening();

    Q_INVOKABLE void stopRealtimeListening();

signals:
    void initializedChanged();

    void familyDataLoaded(
        QVariantList shoppingItems,
        QVariantList savedItems);

    void familyDataLoadFailed(QString message);

    void remoteDataChanged(QString path);
    void childListeningChanged();
    void childListeningFailed(QString collection, QString message);
    void shoppingItemReceived(QVariantMap item);
    void shoppingItemRemoved(QString itemId);
    void savedItemReceived(QVariantMap item);
    void savedItemRemoved(QString itemId);
    void syncStateChanged();
    void itemSyncFailed(QString collection, QString itemId, QString message);

private:
    friend class FirebaseChildListener;
    void initializeFirebase();
    void performItemWrite(const QString &collection, const QString &id, quint64 generation,
                          int kind, const QVariantMap &payload);
    void performItemRead(const QString &collection, const QString &id, quint64 token);

    ItemSyncCoordinator m_itemSync;
    std::shared_ptr<QtCallbackGate> m_callbackGate;

    std::unique_ptr<FirebaseChildListener> m_shoppingListener;
    std::unique_ptr<FirebaseChildListener> m_savedListener;
    bool m_childListening = false;
    quint64 m_syncGeneration = 0;

    firebase::App *m_app = nullptr;

    firebase::database::Database *m_database = nullptr;

    bool m_initialized = false;

    QNetworkAccessManager m_networkManager;

    bool m_downloadInProgress = false;

    QNetworkReply *m_streamReply = nullptr;

    void processRealtimeData(const QByteArray &bytes);

    QByteArray m_streamBuffer;
    QByteArray m_currentEventType;
    QByteArray m_currentEventData;

    bool m_waitingForInitialSnapshot = true;

    bool m_downloadAgain = false;
};
