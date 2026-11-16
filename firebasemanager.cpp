#include "firebasemanager.h"

#include <QDebug>
#include <QDir>
#include <QVariantMap>

#include <map>
#include <string>
#include <vector>

#include <firebase/future.h>
#include <firebase/variant.h>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QMetaObject>
#include <QUuid>
#include <mutex>

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
    const auto name = snapshot.Child("name").value();
    const auto storedId = snapshot.Child("id").value();
    if (!name.is_string() || QString::fromUtf8(name.string_value()).trimmed().isEmpty()
        || (!storedId.is_null() && (!storedId.is_string()
            || QString::fromUtf8(storedId.string_value()) != id))) {
        error = "Malformed Firebase item name or stored ID.";
        return false;
    }
    item["name"] = QString::fromUtf8(name.string_value()).trimmed();
    if (collection == "shoppingItems") {
        const auto addedBy = snapshot.Child("addedBy").value();
        const auto bought = snapshot.Child("bought").value();
        if (!addedBy.is_string() || !bought.is_bool()) {
            error = "Malformed Firebase shopping fields.";
            return false;
        }
        item["addedBy"] = QString::fromUtf8(addedBy.string_value());
        item["bought"] = bought.bool_value();
    }
    return true;
}
}

// Firebase may call these methods off the Qt thread. The mutex protects the
// receiver only while posting; all QObject access happens on its own thread.
class FirebaseChildListener final : public firebase::database::ChildListener
{
public:
    FirebaseChildListener(FirebaseManager *manager, const QString &collection,
                          quint64 generation)
        : m_manager(manager), m_collection(collection), m_generation(generation) {}

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
        qDebug() << "Firebase child moved:" << m_collection
                 << QString::fromStdString(snapshot.key_string());
    }
    void OnCancelled(const firebase::database::Error &error,
                     const char *message) override
    {
        const QString description = QStringLiteral("%1: %2")
            .arg(static_cast<int>(error)).arg(QString::fromUtf8(message ? message : ""));
        qWarning() << "Firebase listener cancelled:" << m_collection << description;
        post("cancelled", {}, description);
    }

private:
    void receive(const QString &event, const firebase::database::DataSnapshot &snapshot)
    {
        if (!snapshot.is_valid()) {
            qWarning() << "Invalid Firebase snapshot:" << m_collection << event;
            return;
        }
        const QString id = QString::fromStdString(snapshot.key_string());
        qDebug() << "Firebase child" << event << m_collection << id;
        QVariantMap item;
        QString error;
        if (!parseFirebaseItem(snapshot, m_collection, id, item, error, event == "removed")) {
            qWarning() << error << m_collection << id;
            return;
        }
        post(event, item, {});
    }

    void post(const QString &event, const QVariantMap &item, const QString &error)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_manager)
            return;
        auto *manager = m_manager;
        const auto collection = m_collection;
        const auto generation = m_generation;
        QMetaObject::invokeMethod(manager, [manager, collection, generation, event, item, error]() {
            if (!manager->m_childListening || manager->m_syncGeneration != generation)
                return;
            if (event == "cancelled") {
                manager->stopChildListening();
                emit manager->childListeningFailed(collection, error);
            } else {
                manager->m_itemSync.incoming(collection, item.value("id").toString(), item, event == "removed");
            }
        }, Qt::QueuedConnection);
    }

    std::mutex m_mutex;
    FirebaseManager *m_manager;
    const QString m_collection;
    const quint64 m_generation;
};

void FirebaseManager::startChildListening()
{
    if (!m_initialized || !m_database || m_childListening)
        return;
    stopRealtimeListening();
    ++m_syncGeneration; // Also invalidates any outstanding REST response.
    m_downloadAgain = false;
    m_childListening = true;
    m_itemSync.setListening(true);
    m_shoppingListener = std::make_unique<FirebaseChildListener>(this, "shoppingItems", m_syncGeneration);
    m_savedListener = std::make_unique<FirebaseChildListener>(this, "savedItems", m_syncGeneration);
    const auto family = m_database->GetReference().Child("familyData");
    family.Child("shoppingItems").AddChildListener(m_shoppingListener.get());
    family.Child("savedItems").AddChildListener(m_savedListener.get());
    emit childListeningChanged();
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
    family.Child("shoppingItems").RemoveChildListener(m_shoppingListener.get());
    family.Child("savedItems").RemoveChildListener(m_savedListener.get());
    m_shoppingListener.reset();
    m_savedListener.reset();
    emit childListeningChanged();
}

FirebaseManager::FirebaseManager(QObject *parent)
    : QObject(parent), m_itemSync(this), m_callbackGate(std::make_shared<QtCallbackGate>(this))
{
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
    stopChildListening();
    stopRealtimeListening();

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

void FirebaseManager::initializeFirebase()
{
    qDebug()
        << "Firebase current working directory:"
        << QDir::currentPath();

    qDebug()
        << "Initializing Firebase...";

    m_app = firebase::App::Create();

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

    qDebug()
        << "Firebase initialized successfully.";

    qDebug()
        << "Database URL:"
        << m_database->url();
}

void FirebaseManager::testWrite()
{
    if (!m_initialized ||
        m_database == nullptr)
    {
        qWarning()
            << "Cannot write: Firebase is not initialized.";

        return;
    }

    firebase::database::DatabaseReference reference =
        m_database
            ->GetReference()
            .Child("debug")
            .Child("message");

    firebase::Future<void> future =
        reference.SetValue(
            firebase::Variant(
                "Hello from FamilyApp"));

    future.OnCompletion(
        [](const firebase::Future<void> &result)
        {
            if (result.error() == 0)
            {
                qDebug()
                    << "Firebase test write successful.";
            }
            else
            {
                qWarning()
                    << "Firebase test write failed:"
                    << result.error()
                    << result.error_message();
            }
        });
}

void FirebaseManager::saveFamilyData(
    const QVariantList &shoppingItems,
    const QVariantList &savedItems)
{
    if (m_childListening || unresolvedItemCount() || bulkWriteInProgress()) {
        qWarning() << "Bulk upload disabled during child listening or unresolved synchronization.";
        return;
    }
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    // Convert shopping items.
    // Convert shopping items into an ID-keyed map.
    std::map<std::string, firebase::Variant> shoppingData;

    for (const QVariant &value : shoppingItems)
    {
        QVariantMap item = value.toMap();

        QString id = item.value("id").toString().trimmed();

        if (id.isEmpty())
        {
            qWarning() << "Shopping item is missing an ID.";
            return;
        }

        std::map<std::string, firebase::Variant> itemData;

        itemData["id"] = firebase::Variant(
            id.toUtf8().toStdString());

        itemData["name"] = firebase::Variant(
            item.value("name").toString().toUtf8().toStdString());

        itemData["addedBy"] = firebase::Variant(
            item.value("addedBy").toString().toUtf8().toStdString());

        itemData["bought"] = firebase::Variant(
            item.value("bought").toBool());

        // Store the item using its ID as its Firebase key.
        shoppingData[id.toUtf8().toStdString()] =
            firebase::Variant(itemData);
    }

    // Convert saved items.
    // Convert saved items into an ID-keyed map.
    std::map<std::string, firebase::Variant> savedData;

    for (const QVariant &value : savedItems)
    {
        QVariantMap item = value.toMap();

        QString id = item.value("id").toString().trimmed();

        if (id.isEmpty())
        {
            qWarning() << "Saved item is missing an ID.";
            return;
        }

        std::map<std::string, firebase::Variant> itemData;

        itemData["id"] = firebase::Variant(
            id.toUtf8().toStdString());

        itemData["name"] = firebase::Variant(
            item.value("name").toString().toUtf8().toStdString());

        savedData[id.toUtf8().toStdString()] =
            firebase::Variant(itemData);
    }

    // Combine both lists.
    std::map<std::string, firebase::Variant> familyData;

    familyData["shoppingItems"] =
        firebase::Variant(shoppingData);

    familyData["savedItems"] =
        firebase::Variant(savedData);

    // Get our Firebase location.
    firebase::database::DatabaseReference reference =
        m_database->GetReference().Child("familyData");

    // Upload the data.
    ++m_syncGeneration; // Fence any REST snapshot started before this parent write.
    m_itemSync.setBulkBusy(true);
    firebase::Future<void> future =
        reference.SetValue(firebase::Variant(familyData));

    future.OnCompletion(
        [gate = m_callbackGate, this](const firebase::Future<void> &result)
        {
            const QString error = result.error() == 0 ? QString()
                : QStringLiteral("Bulk upload error %1: %2").arg(result.error())
                    .arg(QString::fromUtf8(result.error_message() ? result.error_message() : "Unknown error"));
            gate->post([this, error]() {
                m_itemSync.setBulkBusy(false);
                if (error.isEmpty()) qDebug() << "Family data saved to Firebase!";
                else emit familyDataLoadFailed("Bulk upload failed: " + error);
            });
        });
}

void FirebaseManager::loadFamilyData()
{
    if (m_childListening || unresolvedItemCount() || bulkWriteInProgress()) {
        qWarning() << "REST download disabled during child listening or unresolved synchronization.";
        return;
    }
    if (!m_initialized || m_database == nullptr)
    {
        emit familyDataLoadFailed("Firebase is not initialized.");
        return;
    }

    if (m_downloadInProgress)
    {
        m_downloadAgain = true;

        qDebug()
            << "Download in progress. Another refresh queued.";

        return;
    }

    m_downloadInProgress = true;

    auto reference =
        m_database->GetReference().Child("familyData");

    QString url =
        QString::fromStdString(reference.url()) + ".json";

    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Cache-Control", "no-cache");

    QNetworkReply *reply = m_networkManager.get(request);
    const auto generation = m_syncGeneration;
    const auto mutationGeneration = m_itemSync.mutationGeneration();

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, mutationGeneration]()
            {
                m_downloadInProgress = false;

                if (generation != m_syncGeneration || m_childListening
                    || mutationGeneration != m_itemSync.mutationGeneration()
                    || unresolvedItemCount() || bulkWriteInProgress()) {
                    const bool downloadAgain = m_downloadAgain && !m_childListening
                        && !unresolvedItemCount() && !bulkWriteInProgress();
                    m_downloadAgain = false;
                    reply->deleteLater();
                    if (downloadAgain)
                        QTimer::singleShot(0, this, [this]() { loadFamilyData(); });
                    return;
                }

                if (m_downloadAgain)
                {
                    m_downloadAgain = false;

                    QTimer::singleShot(0, this, [this]()
                                       { loadFamilyData(); });
                }

                QByteArray response = reply->readAll();
                auto networkError = reply->error();
                QString errorMessage = reply->errorString();

                reply->deleteLater();

                if (networkError != QNetworkReply::NoError)
                {
                    emit familyDataLoadFailed(errorMessage);
                    return;
                }

                QJsonParseError parseError;
                QJsonDocument document =
                    QJsonDocument::fromJson(response, &parseError);

                if (parseError.error != QJsonParseError::NoError ||
                    !document.isObject())
                {
                    emit familyDataLoadFailed(
                        "Invalid or missing Firebase JSON data.");
                    return;
                }

                QJsonObject data = document.object();

                // Convert JSON arrays or indexed objects to QVariantLists.
                auto convertItems = [](const QJsonValue &value)
                {
                    QVariantList items;

                    if (value.isArray())
                    {
                        for (const auto &entry : value.toArray())
                        {
                            if (entry.isObject())
                                items.append(entry.toObject().toVariantMap());
                        }
                    }
                    else if (value.isObject())
                    {
                        for (const auto &entry : value.toObject())
                        {
                            if (entry.isObject())
                                items.append(entry.toObject().toVariantMap());
                        }
                    }

                    return items;
                };

                QVariantList shoppingItems =
                    convertItems(data.value("shoppingItems"));

                QVariantList savedItems =
                    convertItems(data.value("savedItems"));

                qDebug() << "REST download successful!";
                qDebug() << "Shopping items:" << shoppingItems.size();
                qDebug() << "Saved items:" << savedItems.size();

                emit familyDataLoaded(shoppingItems, savedItems);
            });
}

void FirebaseManager::updateShoppingItemBought(const QString &itemId, bool bought)
{
    if (!m_initialized || !m_database || itemId.isEmpty()) {
        emit itemSyncFailed("shoppingItems", itemId, "Cannot update an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("shoppingItems", itemId, ItemSyncCoordinator::SetBought, {{"bought", bought}});
}

void FirebaseManager::addShoppingItem(const QString &itemId, const QString &name, const QString &addedBy)
{
    if (!m_initialized || !m_database || itemId.isEmpty() || name.trimmed().isEmpty()) {
        emit itemSyncFailed("shoppingItems", itemId, "Cannot add an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("shoppingItems", itemId, ItemSyncCoordinator::PutItem,
                       {{"id", itemId}, {"name", name.trimmed()}, {"addedBy", addedBy}, {"bought", false}});
}

void FirebaseManager::deleteShoppingItem(const QString &itemId)
{
    if (!m_initialized || !m_database || itemId.isEmpty()) {
        emit itemSyncFailed("shoppingItems", itemId, "Cannot delete an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("shoppingItems", itemId, ItemSyncCoordinator::DeleteItem);
}

void FirebaseManager::addSavedItem(const QString &itemId, const QString &name)
{
    if (!m_initialized || !m_database || itemId.isEmpty() || name.trimmed().isEmpty()) {
        emit itemSyncFailed("savedItems", itemId, "Cannot add an uninitialized or invalid item.");
        return;
    }
    m_itemSync.enqueue("savedItems", itemId, ItemSyncCoordinator::PutItem,
                       {{"id", itemId}, {"name", name.trimmed()}});
}

void FirebaseManager::deleteSavedItem(const QString &itemId)
{
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
    qDebug() << "Item write started:" << collection << id << generation << kind;
    future.OnCompletion([gate = m_callbackGate, this, collection, id, generation](const firebase::Future<void> &result) {
        const QString error = result.error() == 0 ? QString()
            : QStringLiteral("Write error %1: %2").arg(result.error())
                  .arg(QString::fromUtf8(result.error_message() ? result.error_message() : "Unknown error"));
        gate->post([this, collection, id, generation, error]() {
            qDebug() << "Item write completed:" << collection << id << generation << error;
            m_itemSync.completeWrite(collection, id, generation, error);
        });
    });
}

void FirebaseManager::performItemRead(const QString &collection, const QString &id, quint64 token)
{
    auto reference = m_database->GetReference().Child("familyData")
                         .Child(collection.toUtf8().constData()).Child(id.toUtf8().constData());
    auto future = reference.GetValue();
    qDebug() << "Item reconciliation started:" << collection << id << token;
    future.OnCompletion([gate = m_callbackGate, this, collection, id, token]
                        (const firebase::Future<firebase::database::DataSnapshot> &result) {
        QString error;
        QVariantMap item;
        bool removed = false;
        if (result.error() != 0) {
            error = QStringLiteral("Read error %1: %2").arg(result.error())
                .arg(QString::fromUtf8(result.error_message() ? result.error_message() : "Unknown error"));
        } else if (!result.result() || !result.result()->is_valid()) {
            error = "Invalid Firebase reconciliation snapshot.";
        } else {
            removed = !result.result()->exists();
            parseFirebaseItem(*result.result(), collection, id, item, error, removed);
        }
        gate->post([this, collection, id, token, item, removed, error]() {
            qDebug() << "Item reconciliation completed:" << collection << id << token << error;
            m_itemSync.completeRead(collection, id, token, item, removed, error);
        });
    });
}

void FirebaseManager::startRealtimeListening()
{
    if (m_childListening) {
        qWarning() << "SSE disabled during Child Listener Test.";
        return;
    }
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    if (m_streamReply != nullptr)
    {
        qDebug() << "Realtime listener already running.";
        return;
    }

    m_streamBuffer.clear();
    m_currentEventType.clear();
    m_currentEventData.clear();

    m_waitingForInitialSnapshot = true;

    // Get the Firebase location to monitor.
    auto reference =
        m_database->GetReference().Child("familyData");

    QString url =
        QString::fromStdString(reference.url()) + ".json";

    QNetworkRequest request{QUrl(url)};

    // Tell Firebase we want a continuous event stream.
    request.setRawHeader("Accept", "text/event-stream");

    // Allow HTTPS redirects used by Firebase.
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);

    // Open the connection.
    QNetworkReply *reply = m_networkManager.get(request);

    m_streamReply = reply;

    qDebug() << "Realtime listener started.";

    // Called whenever new data arrives.
    connect(
        reply,
        &QIODevice::readyRead,
        this,
        [this, reply]()
        {
            if (reply != m_streamReply)
                return;

            processRealtimeData(reply->readAll());
        });

    // Called if the connection closes.
    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply]()
        {
            bool wasActive = (m_streamReply == reply);

            if (wasActive)
            {
                m_streamReply = nullptr;

                if (reply->error() != QNetworkReply::NoError)
                {
                    qWarning()
                        << "Realtime connection error:"
                        << reply->errorString();
                }
                else
                {
                    qDebug() << "Realtime connection closed.";
                }
            }

            reply->deleteLater();
        });
}

void FirebaseManager::stopRealtimeListening()
{
    if (m_streamReply == nullptr)
    {
        return;
    }

    QNetworkReply *reply = m_streamReply;

    m_streamReply = nullptr;

    reply->abort();

    qDebug() << "Realtime listener stopped.";
}

void FirebaseManager::processRealtimeData(
    const QByteArray &bytes)
{
    // Keep incoming bytes until we have complete lines.
    m_streamBuffer.append(bytes);

    while (true)
    {
        qsizetype newlinePos = m_streamBuffer.indexOf('\n');

        if (newlinePos < 0)
            break;

        QByteArray line = m_streamBuffer.left(newlinePos);
        m_streamBuffer.remove(0, newlinePos + 1);

        // Handle Windows-style line endings.
        if (line.endsWith('\r'))
            line.chop(1);

        // A blank line marks the end of an SSE event.
        if (line.isEmpty())
        {
            QByteArray eventType = m_currentEventType;
            QByteArray eventData = m_currentEventData;

            m_currentEventType.clear();
            m_currentEventData.clear();

            if (eventType == "put" || eventType == "patch")
            {
                QJsonParseError error;

                QJsonDocument document =
                    QJsonDocument::fromJson(eventData, &error);

                if (error.error != QJsonParseError::NoError ||
                    !document.isObject())
                {
                    qWarning() << "Invalid realtime event JSON.";
                    continue;
                }

                QJsonObject payload = document.object();

                QString path =
                    payload.value("path").toString();

                if (path.isEmpty())
                    continue;

                // Skip the initial full snapshot for this test.
                if (m_waitingForInitialSnapshot &&
                    eventType == "put" &&
                    path == "/")
                {
                    m_waitingForInitialSnapshot = false;

                    qDebug() << "Initial Firebase snapshot received.";
                    continue;
                }

                qDebug() << "Firebase changed at:" << path;

                emit remoteDataChanged(path);
            }
            else if (eventType == "cancel" ||
                     eventType == "auth_revoked")
            {
                qWarning()
                    << "Firebase stream stopped:"
                    << eventType
                    << eventData;

                stopRealtimeListening();
                return;
            }

            // Ignore keep-alive events.
            continue;
        }

        if (line.startsWith(':'))
            continue;

        if (line.startsWith("event:"))
        {
            m_currentEventType = line.mid(6).trimmed();
        }
        else if (line.startsWith("data:"))
        {
            if (!m_currentEventData.isEmpty())
                m_currentEventData.append('\n');

            m_currentEventData.append(line.mid(5).trimmed());
        }
    }
}
