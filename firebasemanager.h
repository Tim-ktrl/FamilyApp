#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QByteArray>

#include <firebase/app.h>
#include <firebase/database.h>
#include <firebase/future.h>
#include <QNetworkReply>

class FirebaseManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(
        bool initialized
            READ initialized
                NOTIFY initializedChanged)

public:
    explicit FirebaseManager(
        QObject *parent = nullptr);

    ~FirebaseManager() override;

    bool initialized() const;

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

private:
    void initializeFirebase();

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