#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QVariantMap>
#include <memory>
#include "itemsynccoordinator.h"
#include "qtcallbackgate.h"
#include "initialsynccoordinator.h"
#include <atomic>

#include <firebase/app.h>
#include <firebase/database.h>
#include <QNetworkReply>
#include <QPointer>

class FirebaseChildListener;
class FirebaseValueListener;

class FirebaseManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(
        bool initialized
            READ initialized
                NOTIFY initializedChanged)

    Q_PROPERTY(int unresolvedItemCount READ unresolvedItemCount NOTIFY syncStateChanged)
    Q_PROPERTY(int failedItemCount READ failedItemCount NOTIFY syncStateChanged)
    Q_PROPERTY(StartupState startupState READ startupState NOTIFY startupChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY startupChanged)
    Q_PROPERTY(bool editingEnabled READ editingEnabled NOTIFY startupChanged)

public:
    enum StartupState { Loading, Ready, Error };
    Q_ENUM(StartupState)
    explicit FirebaseManager(
        QObject *parent = nullptr);

    ~FirebaseManager() override;

    bool initialized() const;
    bool childListening() const { return m_childListening; }
    int unresolvedItemCount() const { return m_itemSync.unresolvedCount(); }
    int failedItemCount() const { return m_itemSync.failedCount(); }
    StartupState startupState() const { return static_cast<StartupState>(m_initialSync.state()); }
    QString startupError() const { return m_initialSync.error(); }
    bool connected() const { return m_initialSync.connected(); }
    bool editingEnabled() const { return m_initialSync.canEdit() && m_childListening; }
    Q_INVOKABLE void beginInitialSync();
    Q_INVOKABLE void completeInitialSync(quint64 attempt, bool success, const QString &error = {});
    void startChildListening(); // Public for the isolated listener-registration probe.

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

signals:
    void initializedChanged();

    void shoppingItemReceived(QVariantMap item);
    void shoppingItemRemoved(QString itemId);
    void savedItemReceived(QVariantMap item);
    void savedItemRemoved(QString itemId);
    void syncStateChanged();
    void itemSyncFailed(QString collection, QString itemId, QString message);
    void startupChanged();
    void initialDataVerified(quint64 attempt, QVariantList shoppingItems, QVariantList savedItems);

private:
    friend class FirebaseChildListener;
    friend class FirebaseValueListener;
    void initializeFirebase();
    void stopChildListening();
    void performItemWrite(const QString &collection, const QString &id, quint64 generation,
                          int kind, const QVariantMap &payload);
    void performItemRead(const QString &collection, const QString &id, quint64 token);
    void startInitialAttempt(quint64 attempt);
    void verifyInitialSnapshot(quint64 attempt, quint64 token);
    void receiveChildEvent(const QString &collection, const QVariantMap &item, bool removed, quint64 revision);
    void stopStartupValueListeners();
    bool writesAllowed(const QString &collection, const QString &id);
    void updateStartupBlock();

    ItemSyncCoordinator m_itemSync;
    InitialSyncCoordinator m_initialSync;
    std::shared_ptr<QtCallbackGate> m_callbackGate;
    std::shared_ptr<std::atomic<quint64>> m_sdkRevision;
    std::unique_ptr<FirebaseValueListener> m_startupShoppingListener;
    std::unique_ptr<FirebaseValueListener> m_startupSavedListener;
    std::unique_ptr<FirebaseValueListener> m_connectionListener;
    bool m_initialCommit = false;
    quint64 m_connectionRevision = 0;
    quint64 m_ackReadAttempt = 0;
    bool m_connectionListenerFailed = false;
    QPointer<QNetworkReply> m_startupReply;

    std::unique_ptr<FirebaseChildListener> m_shoppingListener;
    std::unique_ptr<FirebaseChildListener> m_savedListener;
    bool m_childListening = false;
    quint64 m_syncGeneration = 0;

    firebase::App *m_app = nullptr;

    firebase::database::Database *m_database = nullptr;

    bool m_initialized = false;

    QNetworkAccessManager m_networkManager;

};
