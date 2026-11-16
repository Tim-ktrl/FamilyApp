#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QJsonDocument>
#include <QVariantMap>

class StartupFirebaseMock : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FirebaseManager)
    QML_SINGLETON
    Q_PROPERTY(bool initialized READ initialized CONSTANT)
    Q_PROPERTY(int startupState READ startupState NOTIFY startupChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY startupChanged)
    Q_PROPERTY(bool editingEnabled READ editingEnabled NOTIFY startupChanged)
    Q_PROPERTY(bool childListening READ childListening NOTIFY childListeningChanged)
    Q_PROPERTY(int unresolvedItemCount READ zero NOTIFY syncStateChanged)
    Q_PROPERTY(int failedItemCount READ zero NOTIFY syncStateChanged)
    Q_PROPERTY(bool bulkWriteInProgress READ no NOTIFY syncStateChanged)
public:
    enum StartupState { Loading, Ready, Error };
    Q_ENUM(StartupState)
    using QObject::QObject;
    bool initialized() const { return true; }
    int startupState() const { return m_state; }
    QString startupError() const { return "Test synchronization failure"; }
    bool connected() const { return m_connected; }
    bool editingEnabled() const { return m_state == Ready && m_connected && m_listening; }
    bool childListening() const { return m_listening; }
    int zero() const { return 0; }
    bool no() const { return false; }
    int beginCalls = 0, writeCalls = 0;
    Q_INVOKABLE void beginInitialSync() { ++beginCalls; m_state = Loading; m_listening = true; emit startupChanged(); emit childListeningChanged(); }
    Q_INVOKABLE void completeInitialSync(quint64, bool success, const QString & = {}) { m_state = success ? Ready : Error; emit startupChanged(); }
    Q_INVOKABLE void startChildListening() { m_listening = true; emit childListeningChanged(); }
    Q_INVOKABLE void stopChildListening() { m_listening = false; emit childListeningChanged(); }
    Q_INVOKABLE void startRealtimeListening() {}
    Q_INVOKABLE void stopRealtimeListening() {}
    Q_INVOKABLE void loadFamilyData() {}
    Q_INVOKABLE void saveFamilyData(const QVariantList &, const QVariantList &) { ++writeCalls; }
    Q_INVOKABLE void addShoppingItem(const QString &, const QString &, const QString &) { ++writeCalls; }
    Q_INVOKABLE void updateShoppingItemBought(const QString &, bool) { ++writeCalls; }
    Q_INVOKABLE void deleteShoppingItem(const QString &) { ++writeCalls; }
    Q_INVOKABLE void addSavedItem(const QString &, const QString &) { ++writeCalls; }
    Q_INVOKABLE void deleteSavedItem(const QString &) { ++writeCalls; }
    void offline() { m_connected = false; m_state = Error; emit startupChanged(); }
    void online() { m_connected = true; m_state = Loading; emit startupChanged(); }
signals:
    void startupChanged();
    void childListeningChanged();
    void syncStateChanged();
    void initialDataVerified(quint64 attempt, QVariantList shoppingItems, QVariantList savedItems);
    void shoppingItemReceived(QVariantMap item);
    void shoppingItemRemoved(QString itemId);
    void savedItemReceived(QVariantMap item);
    void savedItemRemoved(QString itemId);
    void childListeningFailed(QString collection, QString message);
    void itemSyncFailed(QString collection, QString itemId, QString message);
    void remoteDataChanged(QString path);
    void familyDataLoaded(QVariantList shoppingItems, QVariantList savedItems);
    void familyDataLoadFailed(QString message);
private:
    int m_state = Loading;
    bool m_connected = true, m_listening = false;
};

class StartupStorageMock : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StorageManager)
    QML_SINGLETON
public:
    using QObject::QObject;
    QByteArray json = "{}";
    bool failSaves = false;
    int saves = 0;
    Q_INVOKABLE QVariantMap loadData() const { return QJsonDocument::fromJson(json).toVariant().toMap(); }
    Q_INVOKABLE bool saveData(const QVariantList &shoppingItems, const QVariantList &savedItems)
    {
        ++saves;
        if (failSaves) return false;
        json = QJsonDocument::fromVariant(QVariantMap{{"shoppingItems", shoppingItems}, {"savedItems", savedItems}}).toJson();
        return true;
    }
    Q_INVOKABLE QString storagePath() const { return "in-memory test cache"; }
};
