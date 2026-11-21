#include "firebasemanager.h"
#include "familydatavalidation.h"
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QCoreApplication>
#endif

#include <QDebug>
#include <QLoggingCategory>
#include <QVariantMap>

#include <map>
#include <string>

#include <firebase/future.h>
#include <firebase/variant.h>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QMetaObject>
#include <QUuid>
#include <mutex>

Q_LOGGING_CATEGORY(firebaseListeners, "familyapp.firebase.listeners", QtWarningMsg)
Q_LOGGING_CATEGORY(firebaseSync, "familyapp.firebase.sync", QtWarningMsg)

namespace {
bool parseFirebaseItem(const firebase::database::DataSnapshot &snapshot,
                       const QString &collection, const QString &id, QVariantMap &item,
                       QString &error, bool removed = false)
{
    if (!snapshot.is_valid() || QUuid(id).isNull()
        || QString::fromStdString(snapshot.key_string()) != id) {
        error = "Invalid Firebase snapshot or UUID key.";
        return false;
    }
    item = {{"id", id}};
    if (removed)
        return true;
    QVariantMap fields;
    for (const auto &field : {"id", "name", "addedBy", "bought"}) {
        const auto value = snapshot.Child(field).value();
        if (value.is_string()) fields[field] = QString::fromUtf8(value.string_value());
        else if (value.is_bool()) fields[field] = value.bool_value();
        else if (!value.is_null()) fields[field] = 0; // Preserve an invalid type for validation.
    }
    return FamilyData::validateItem(collection, id, fields, item, error);
}
}

class FirebaseValueListener final : public firebase::database::ValueListener
{
public:
    FirebaseValueListener(FirebaseManager *manager, const QString &collection, quint64 attempt)
        : m_manager(manager), m_gate(std::make_shared<QtCallbackGate>(manager)),
          m_revision(manager->m_sdkRevision), m_connection(collection.isEmpty()),
          m_attempt(attempt), m_collection(collection) {}
    void invalidate() { m_gate->invalidate(); }
    void OnValueChanged(const firebase::database::DataSnapshot &snapshot) override
    {
        const auto revision = ++(*m_revision);
        QString error;
        QVariantMap entries;
        bool connected = false;
        if (!snapshot.is_valid()) {
            error = "Invalid Firebase value snapshot.";
        } else if (m_connection) {
            const auto value = snapshot.value();
            if (!value.is_bool()) error = "Invalid Firebase connection status.";
            else connected = value.bool_value();
        } else if (snapshot.exists() && !snapshot.value().is_map()) {
            error = "Invalid collection: " + m_collection;
        } else {
            for (const auto &child : snapshot.children()) {
                const auto id = QString::fromStdString(child.key_string());
                QVariantMap item;
                if (!parseFirebaseItem(child, m_collection, id, item, error)) break;
                entries[id] = item;
            }
        }
        auto *manager = m_manager;
        const auto attempt = m_attempt;
        const bool connection = m_connection;
        const auto collection = m_collection;
        m_gate->post([manager, attempt, connection, revision, entries, error, connected, collection]() {
            if (!connection && (attempt != manager->m_initialSync.attempt()
                                || manager->startupState() != FirebaseManager::Loading)) return;
            if (connection) {
                if (revision < manager->m_connectionRevision) return;
                manager->m_connectionRevision = revision;
            }
            manager->m_initialSync.observe(revision);
            if (!error.isEmpty()) {
                if (connection) {
                    manager->m_connectionListenerFailed = true;
                    manager->m_initialSync.setConnected(false);
                }
                manager->m_initialSync.fail(error);
            }
            else if (connection) {
                manager->m_initialSync.setConnected(connected);
                manager->updateStartupBlock();
            }
            else manager->m_initialSync.stageCollection(attempt, revision, collection, entries);
        });
    }
    void OnCancelled(const firebase::database::Error &error, const char *message) override
    {
        const auto revision = ++(*m_revision);
        const QString description = QStringLiteral("Firebase listener error %1: %2")
            .arg(static_cast<int>(error)).arg(QString::fromUtf8(message ? message : "Unknown error"));
        auto *manager = m_manager;
        const auto attempt = m_attempt;
        const bool connection = m_connection;
        m_gate->post([manager, attempt, connection, description, revision]() {
            if (connection) {
                if (revision < manager->m_connectionRevision) return;
                manager->m_connectionRevision = revision;
                manager->m_connectionListenerFailed = true;
                manager->m_initialSync.setConnected(false);
            }
            if (connection || (attempt == manager->m_initialSync.attempt()
                               && manager->startupState() == FirebaseManager::Loading))
                manager->m_initialSync.fail(description);
        });
    }
private:
    FirebaseManager *m_manager;
    std::shared_ptr<QtCallbackGate> m_gate;
    std::shared_ptr<std::atomic<quint64>> m_revision;
    bool m_connection;
    quint64 m_attempt;
    const QString m_collection;
};

// Firebase may call these methods off the Qt thread. The mutex protects the
// receiver only while posting; all QObject access happens on its own thread.
class FirebaseChildListener final : public firebase::database::ChildListener
{
public:
    FirebaseChildListener(FirebaseManager *manager, const QString &collection,
                          quint64 generation)
        : m_manager(manager), m_collection(collection), m_generation(generation),
          m_revision(manager->m_sdkRevision) {}

    void invalidate()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_manager = nullptr;
    }

    void OnChildAdded(const firebase::database::DataSnapshot &snapshot,
                      const char *) override { receive("added", snapshot); }
    void OnChildChanged(const firebase::database::DataSnapshot &snapshot,
                        const char *) override { receive("changed", snapshot); }
    void OnChildRemoved(const firebase::database::DataSnapshot &snapshot) override
    { receive("removed", snapshot); }
    void OnChildMoved(const firebase::database::DataSnapshot &snapshot,
                      const char *) override
    {
        // The UI preserves insertion order, not Firebase priority order.
        if (!snapshot.is_valid())
            return;
        qCDebug(firebaseListeners) << "Firebase child moved:" << m_collection
                 << QString::fromStdString(snapshot.key_string());
    }
    void OnCancelled(const firebase::database::Error &error,
                     const char *message) override
    {
        const QString description = QStringLiteral("%1: %2")
            .arg(static_cast<int>(error)).arg(QString::fromUtf8(message ? message : ""));
        qWarning() << "Firebase listener cancelled:" << m_collection << description;
        post("cancelled", {}, description, ++(*m_revision));
    }

private:
    void receive(const QString &event, const firebase::database::DataSnapshot &snapshot)
    {
        const auto revision = ++(*m_revision);
        if (!snapshot.is_valid()) {
            qWarning() << "Invalid Firebase snapshot:" << m_collection << event;
            return;
        }
        const QString id = QString::fromStdString(snapshot.key_string());
        qCDebug(firebaseListeners) << "Firebase child" << event << m_collection << id;
        QVariantMap item;
        QString error;
        if (!parseFirebaseItem(snapshot, m_collection, id, item, error, event == "removed")) {
            qWarning() << error << m_collection << id;
            if (!QUuid(id).isNull()) post(event, {{"id", id}}, {}, revision);
            return;
        }
        post(event, item, {}, revision);
    }

    void post(const QString &event, const QVariantMap &item, const QString &error, quint64 revision = 0)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_manager)
            return;
        auto *manager = m_manager;
        const auto collection = m_collection;
        const auto generation = m_generation;
        QMetaObject::invokeMethod(manager, [manager, collection, generation, event, item, error, revision]() {
            if (!manager->m_childListening || manager->m_syncGeneration != generation)
                return;
            if (event == "cancelled") {
                manager->stopChildListening();
                manager->m_initialSync.fail("Live listener cancelled: " + error);
            } else {
                manager->receiveChildEvent(collection, item, event == "removed", revision);
            }
        }, Qt::QueuedConnection);
    }

    std::mutex m_mutex;
    FirebaseManager *m_manager;
    const QString m_collection;
    const quint64 m_generation;
    std::shared_ptr<std::atomic<quint64>> m_revision;
};

void FirebaseManager::startChildListening()
{
    qCDebug(firebaseListeners) << "[Firebase listeners] startChildListening" << this << "active" << m_childListening
            << "generation" << m_syncGeneration << "attempt" << m_initialSync.attempt();
    if (!m_initialized || !m_database || m_childListening) {
        qCDebug(firebaseListeners) << "[Firebase listeners] child start skipped" << this;
        return;
    }
    ++m_syncGeneration; // Fence callbacks from an earlier child subscription.
    m_childListening = true;
    m_itemSync.setListening(true);
    m_shoppingListener = std::make_unique<FirebaseChildListener>(this, "shoppingItems", m_syncGeneration);
    m_savedListener = std::make_unique<FirebaseChildListener>(this, "savedItems", m_syncGeneration);
    const auto family = m_database->GetReference().Child("familyData");
    qCDebug(firebaseListeners) << "[Firebase listeners] AddChild /familyData/shoppingItems" << this
            << m_shoppingListener.get() << "generation" << m_syncGeneration;
    family.Child("shoppingItems").AddChildListener(m_shoppingListener.get());
    qCDebug(firebaseListeners) << "[Firebase listeners] AddChild /familyData/savedItems" << this
            << m_savedListener.get() << "generation" << m_syncGeneration;
    family.Child("savedItems").AddChildListener(m_savedListener.get());
    emit startupChanged();
}

void FirebaseManager::stopChildListening()
{
    if (!m_childListening)
        return;
    m_childListening = false;
    m_itemSync.setListening(false);
    ++m_syncGeneration;
    // Stop callbacks from posting before detaching and deleting their listeners.
    m_shoppingListener->invalidate();
    m_savedListener->invalidate();
    const auto family = m_database->GetReference().Child("familyData");
    qCDebug(firebaseListeners) << "[Firebase listeners] RemoveChild /familyData/shoppingItems" << this
            << m_shoppingListener.get() << "generation" << m_syncGeneration;
    family.Child("shoppingItems").RemoveChildListener(m_shoppingListener.get());
    qCDebug(firebaseListeners) << "[Firebase listeners] RemoveChild /familyData/savedItems" << this
            << m_savedListener.get() << "generation" << m_syncGeneration;
    family.Child("savedItems").RemoveChildListener(m_savedListener.get());
    m_shoppingListener.reset();
    m_savedListener.reset();
    emit startupChanged();
    if (m_initialSync.state() != InitialSyncCoordinator::Error)
        m_initialSync.fail("Live synchronization stopped. Retry initial synchronization to resume editing.");
}

FirebaseManager::FirebaseManager(QObject *parent)
    : QObject(parent), m_itemSync(this), m_initialSync(this),
      m_callbackGate(std::make_shared<QtCallbackGate>(this)),
      m_sdkRevision(std::make_shared<std::atomic<quint64>>(0))
{
    m_itemSync.verifyRemoteEvents(true);
    connect(&m_initialSync, &InitialSyncCoordinator::changed, this, &FirebaseManager::startupChanged);
    connect(&m_initialSync, &InitialSyncCoordinator::changed, this, [this]() {
        if (startupState() == Error)
            qWarning() << "Synchronization unavailable:" << startupError();
        if (startupState() != Loading) {
            const auto attempt = m_initialSync.attempt();
            QTimer::singleShot(0, this, [this, attempt]() {
                if (attempt == m_initialSync.attempt() && startupState() != Loading)
                    stopStartupValueListeners();
            });
        }
    });
    connect(&m_initialSync, &InitialSyncCoordinator::attemptStarted, this, &FirebaseManager::startInitialAttempt);
    connect(&m_initialSync, &InitialSyncCoordinator::verificationRequested, this, &FirebaseManager::verifyInitialSnapshot);
    connect(&m_initialSync, &InitialSyncCoordinator::snapshotVerified, this,
            [this](quint64 attempt, const QVariantMap &snapshot) {
        m_initialCommit = true;
        emit initialDataVerified(attempt, FamilyData::items(snapshot, "shoppingItems"), FamilyData::items(snapshot, "savedItems"));
        m_initialCommit = false;
    });
    connect(&m_itemSync, &ItemSyncCoordinator::stateChanged, this, &FirebaseManager::updateStartupBlock);
    connect(&m_itemSync, &ItemSyncCoordinator::writeRequested, this, &FirebaseManager::performItemWrite);
    connect(&m_itemSync, &ItemSyncCoordinator::readRequested, this, &FirebaseManager::performItemRead);
    connect(&m_itemSync, &ItemSyncCoordinator::stateChanged, this, &FirebaseManager::syncStateChanged);
    connect(&m_itemSync, &ItemSyncCoordinator::itemFailed, this,
            [this](const QString &collection, const QString &id, const QString &error) {
        qWarning() << "Item sync failed:" << collection << id << error;
        emit itemSyncFailed(collection, id, error);
    });
    connect(&m_itemSync, &ItemSyncCoordinator::itemReady, this,
            [this](const QString &collection, const QString &id, const QVariantMap &item, bool removed) {
        if (startupState() != Ready) return;
        if (collection == "shoppingItems") {
            if (removed) emit shoppingItemRemoved(id);
            else emit shoppingItemReceived(item);
        } else {
            if (removed) emit savedItemRemoved(id);
            else emit savedItemReceived(item);
        }
    });
    initializeFirebase();
}

FirebaseManager::~FirebaseManager()
{
    m_callbackGate->invalidate();
    stopStartupValueListeners();
    if (m_connectionListener) {
        qCDebug(firebaseListeners) << "[Firebase listeners] RemoveValue /.info/connected" << this << m_connectionListener.get();
        m_connectionListener->invalidate();
        m_database->GetReference(".info/connected").RemoveValueListener(m_connectionListener.get());
        m_connectionListener.reset();
    }
    stopChildListening();

    if (m_database != nullptr)
    {
        delete m_database;
        m_database = nullptr;
    }

    if (m_app != nullptr)
    {
        delete m_app;
        m_app = nullptr;
    }
}

bool FirebaseManager::initialized() const
{
    return m_initialized;
}

void FirebaseManager::beginInitialSync()
{
    m_initialSync.begin();
}

void FirebaseManager::startInitialAttempt(quint64 attempt)
{
    stopStartupValueListeners();
    if (!m_initialized) {
        delete m_database;
        m_database = nullptr;
        delete m_app;
        m_app = nullptr;
        initializeFirebase();
    }
    if (!m_initialized) {
        m_initialSync.fail("Firebase initialization failed.");
        return;
    }
    updateStartupBlock();
    // Only entries without any local operation history may be cleared.
    m_itemSync.clearRemoteReads();
    startChildListening();
    if (m_connectionListenerFailed && m_connectionListener) {
        qCDebug(firebaseListeners) << "[Firebase listeners] RemoveValue /.info/connected (failed)" << this << m_connectionListener.get();
        m_connectionListener->invalidate();
        m_database->GetReference(".info/connected").RemoveValueListener(m_connectionListener.get());
        m_connectionListener.reset();
        m_connectionListenerFailed = false;
    }
    if (!m_connectionListener) {
        m_connectionListener = std::make_unique<FirebaseValueListener>(this, QString(), 0);
        qCDebug(firebaseListeners) << "[Firebase listeners] AddValue /.info/connected" << this << m_connectionListener.get();
        m_database->GetReference(".info/connected").AddValueListener(m_connectionListener.get());
    }
    // Same-query value/child registrations share a view in the SDK. Never attach
    // an ancestor /familyData value query: its removal can restore child listens twice.
    m_startupShoppingListener = std::make_unique<FirebaseValueListener>(this, "shoppingItems", attempt);
    m_startupSavedListener = std::make_unique<FirebaseValueListener>(this, "savedItems", attempt);
    qCDebug(firebaseListeners) << "[Firebase listeners] AddValue /familyData/shoppingItems" << this
            << m_startupShoppingListener.get() << "attempt" << attempt;
    m_database->GetReference("familyData/shoppingItems").AddValueListener(m_startupShoppingListener.get());
    qCDebug(firebaseListeners) << "[Firebase listeners] AddValue /familyData/savedItems" << this
            << m_startupSavedListener.get() << "attempt" << attempt;
    m_database->GetReference("familyData/savedItems").AddValueListener(m_startupSavedListener.get());
}

void FirebaseManager::stopStartupValueListeners()
{
    if (m_startupReply) {
        auto *reply = m_startupReply.data();
        m_startupReply = nullptr;
        reply->abort();
    }
    if (m_startupShoppingListener) {
        qCDebug(firebaseListeners) << "[Firebase listeners] RemoveValue /familyData/shoppingItems" << this
                << m_startupShoppingListener.get() << "attempt" << m_initialSync.attempt();
        m_startupShoppingListener->invalidate();
        m_database->GetReference("familyData/shoppingItems").RemoveValueListener(m_startupShoppingListener.get());
        m_startupShoppingListener.reset();
    }
    if (m_startupSavedListener) {
        qCDebug(firebaseListeners) << "[Firebase listeners] RemoveValue /familyData/savedItems" << this
                << m_startupSavedListener.get() << "attempt" << m_initialSync.attempt();
        m_startupSavedListener->invalidate();
        m_database->GetReference("familyData/savedItems").RemoveValueListener(m_startupSavedListener.get());
        m_startupSavedListener.reset();
    }
}

void FirebaseManager::updateStartupBlock()
{
    m_initialSync.setBlocked(m_itemSync.unresolvedLocalCount() > 0 || m_itemSync.bulkBusy());
    if (connected() && startupState() == Loading && m_ackReadAttempt != m_initialSync.attempt()) {
        m_ackReadAttempt = m_initialSync.attempt();
        m_itemSync.revalidateAcknowledged();
    }
}

void FirebaseManager::receiveChildEvent(const QString &collection, const QVariantMap &item,
                                      bool removed, quint64 revision)
{
    m_initialSync.observe(revision);
    // During startup only the verified full snapshot may reach models. A late
    // child callback after handoff is an invalidation, never a trusted payload.
    if (startupState() == Ready)
        m_itemSync.incoming(collection, item.value("id").toString(), item, removed);
}

void FirebaseManager::verifyInitialSnapshot(quint64 attempt, quint64 token)
{
    if (!m_childListening) {
        m_initialSync.fail("Live listeners are not active.");
        return;
    }
    const auto url = QString::fromStdString(m_database->GetReference("familyData").url()) + ".json";
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Cache-Control", "no-cache, no-store");
    request.setTransferTimeout(20000);
    auto *reply = m_networkManager.get(request);
    m_startupReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, token]() {
        if (m_startupReply == reply) m_startupReply = nullptr;
        QVariantMap snapshot;
        QString error;
        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            error = "Online snapshot verification failed: " + reply->errorString();
        else
            FamilyData::parseSnapshot(reply->readAll(), snapshot, error);
        reply->deleteLater();
        m_initialSync.verified(attempt, token, m_sdkRevision->load(), snapshot, error);
    });
}

void FirebaseManager::completeInitialSync(quint64 attempt, bool success, const QString &error)
{
    if (!m_initialCommit) return; // Only the current synchronous snapshot handler can commit.
    updateStartupBlock();
    m_initialSync.committed(attempt, success, error);
}

bool FirebaseManager::writesAllowed(const QString &collection, const QString &id)
{
    if (editingEnabled()) return true;
    emit itemSyncFailed(collection, id, "Editing is disabled until online synchronization is ready.");
    return false;
}

void FirebaseManager::initializeFirebase()
{
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    const auto activity = QNativeInterface::QAndroidApplication::context();
    if (QNativeInterface::QAndroidApplication::isActivityContext() && activity.isValid())
        m_app = firebase::App::Create(env.jniEnv(), activity.object<jobject>());
#else
    m_app = firebase::App::Create();
#endif

    if (m_app == nullptr)
    {
        qWarning()
            << "Failed to create Firebase App.";

        return;
    }

    firebase::InitResult initResult =
        firebase::kInitResultSuccess;

    m_database =
        firebase::database::Database::GetInstance(
            m_app,
            "https://familyapp-a9a07-default-rtdb.firebaseio.com",
            &initResult);

    if (m_database == nullptr)
    {
        qWarning()
            << "Firebase Database pointer is null.";

        return;
    }

    if (initResult !=
        firebase::kInitResultSuccess)
    {
        qWarning()
            << "Firebase Database initialization failed.";

        return;
    }

    m_initialized = true;

    emit initializedChanged();


}

void FirebaseManager::updateShoppingItemBought(const QString &itemId, bool bought)
{
    if (!writesAllowed("shoppingItems", itemId)) return;
    if (!m_initialized || !m_database || itemId.isEmpty()) {
        emit itemSyncFailed("shoppingItems", itemId, "Cannot update an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("shoppingItems", itemId, ItemSyncCoordinator::SetBought, {{"bought", bought}});
}

void FirebaseManager::addShoppingItem(const QString &itemId, const QString &name, const QString &addedBy)
{
    if (!writesAllowed("shoppingItems", itemId)) return;
    if (!m_initialized || !m_database || itemId.isEmpty() || name.trimmed().isEmpty()) {
        emit itemSyncFailed("shoppingItems", itemId, "Cannot add an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("shoppingItems", itemId, ItemSyncCoordinator::PutItem,
                       {{"id", itemId}, {"name", name.trimmed()}, {"addedBy", addedBy}, {"bought", false}});
}

void FirebaseManager::deleteShoppingItem(const QString &itemId)
{
    if (!writesAllowed("shoppingItems", itemId)) return;
    if (!m_initialized || !m_database || itemId.isEmpty()) {
        emit itemSyncFailed("shoppingItems", itemId, "Cannot delete an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("shoppingItems", itemId, ItemSyncCoordinator::DeleteItem);
}

void FirebaseManager::addSavedItem(const QString &itemId, const QString &name)
{
    if (!writesAllowed("savedItems", itemId)) return;
    if (!m_initialized || !m_database || itemId.isEmpty() || name.trimmed().isEmpty()) {
        emit itemSyncFailed("savedItems", itemId, "Cannot add an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("savedItems", itemId, ItemSyncCoordinator::PutItem,
                       {{"id", itemId}, {"name", name.trimmed()}});
}

void FirebaseManager::deleteSavedItem(const QString &itemId)
{
    if (!writesAllowed("savedItems", itemId)) return;
    if (!m_initialized || !m_database || itemId.isEmpty()) {
        emit itemSyncFailed("savedItems", itemId, "Cannot delete an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("savedItems", itemId, ItemSyncCoordinator::DeleteItem);
}

void FirebaseManager::performItemWrite(const QString &collection, const QString &id, quint64 generation,
                                      int kind, const QVariantMap &payload)
{
    auto reference = m_database->GetReference().Child("familyData")
                         .Child(collection.toUtf8().constData()).Child(id.toUtf8().constData());
    firebase::Future<void> future;
    if (kind == ItemSyncCoordinator::DeleteItem) {
        future = reference.RemoveValue();
    } else if (kind == ItemSyncCoordinator::SetBought) {
        future = reference.Child("bought").SetValue(firebase::Variant(payload.value("bought").toBool()));
    } else {
        std::map<std::string, firebase::Variant> fields;
        for (auto it = payload.cbegin(); it != payload.cend(); ++it) {
            fields[it.key().toStdString()] = it.key() == "bought"
                ? firebase::Variant(it.value().toBool())
                : firebase::Variant(it.value().toString().toUtf8().toStdString());
        }
        future = reference.SetValue(firebase::Variant(fields));
    }
    qCDebug(firebaseSync) << "Item write started:" << collection << id << generation << kind;
    future.OnCompletion([gate = m_callbackGate, this, collection, id, generation](const firebase::Future<void> &result) {
        const QString error = result.error() == 0 ? QString()
            : QStringLiteral("Write error %1: %2").arg(result.error())
                  .arg(QString::fromUtf8(result.error_message() ? result.error_message() : "Unknown error"));
        gate->post([this, collection, id, generation, error]() {
            qCDebug(firebaseSync) << "Item write completed:" << collection << id << generation << error;
            m_itemSync.completeWrite(collection, id, generation, error);
        });
    });
}

void FirebaseManager::performItemRead(const QString &collection, const QString &id, quint64 token)
{
    const auto reference = m_database->GetReference("familyData")
        .Child(collection.toUtf8().constData()).Child(id.toUtf8().constData());
    QNetworkRequest request{QUrl(QString::fromStdString(reference.url()) + ".json")};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Cache-Control", "no-cache, no-store");
    request.setTransferTimeout(20000);
    auto *reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, collection, id, token]() {
        QString error;
        QVariantMap item;
        bool removed = false;
        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            error = "Online item verification failed: " + reply->errorString();
        } else {
            const auto bytes = reply->readAll();
            removed = bytes.trimmed() == "null";
            if (!removed) {
                QJsonParseError parseError;
                const auto document = QJsonDocument::fromJson(bytes, &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject())
                    error = "Invalid online item snapshot.";
                else FamilyData::validateItem(collection, id, document.object().toVariantMap(), item, error);
            }
        }
        reply->deleteLater();
        m_itemSync.completeRead(collection, id, token, item, removed, error);
    });
}
