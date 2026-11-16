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
        if (QUuid(id).isNull()) {
            qWarning() << "Ignoring non-UUID Firebase key:" << m_collection << id;
            return;
        }
        QVariantMap item{{"id", id}};
        if (event != "removed") {
            const auto name = snapshot.Child("name").value();
            const auto storedId = snapshot.Child("id").value();
            if (!name.is_string() || QString::fromUtf8(name.string_value()).trimmed().isEmpty()
                || (!storedId.is_null() && (!storedId.is_string()
                    || QString::fromUtf8(storedId.string_value()) != id))) {
                qWarning() << "Malformed Firebase item:" << m_collection << id;
                return;
            }
            item["name"] = QString::fromUtf8(name.string_value()).trimmed();
            if (m_collection == "shoppingItems") {
                const auto addedBy = snapshot.Child("addedBy").value();
                const auto bought = snapshot.Child("bought").value();
                if (!addedBy.is_string() || !bought.is_bool()) {
                    qWarning() << "Malformed Firebase shopping fields:" << id;
                    return;
                }
                item["addedBy"] = QString::fromUtf8(addedBy.string_value());
                item["bought"] = bought.bool_value();
            }
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
            } else if (collection == "shoppingItems") {
                if (event == "removed")
                    emit manager->shoppingItemRemoved(item.value("id").toString());
                else
                    emit manager->shoppingItemReceived(item);
            } else {
                if (event == "removed")
                    emit manager->savedItemRemoved(item.value("id").toString());
                else
                    emit manager->savedItemReceived(item);
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
    : QObject(parent)
{

    initializeFirebase();
}

FirebaseManager::~FirebaseManager()
{

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
    if (m_childListening) {
        qWarning() << "Bulk upload disabled during Child Listener Test.";
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
    firebase::Future<void> future =
        reference.SetValue(firebase::Variant(familyData));

    future.OnCompletion(
        [](const firebase::Future<void> &result)
        {
            if (result.error() == 0)
            {
                qDebug() << "Family data saved to Firebase!";
            }
            else
            {
                qWarning()
                    << "Firebase save failed:"
                    << result.error()
                    << result.error_message();
            }
        });
}

void FirebaseManager::loadFamilyData()
{
    if (m_childListening) {
        qWarning() << "REST download disabled during Child Listener Test.";
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

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation]()
            {
                m_downloadInProgress = false;

                if (generation != m_syncGeneration || m_childListening) {
                    const bool downloadAgain = m_downloadAgain && !m_childListening;
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

void FirebaseManager::updateShoppingItemBought(
    const QString &itemId,
    bool bought)
{
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    if (itemId.isEmpty())
    {
        qWarning() << "Cannot update item without ID.";
        return;
    }

    std::string id = itemId.toUtf8().toStdString();

    firebase::database::DatabaseReference reference =
        m_database->GetReference()
            .Child("familyData")
            .Child("shoppingItems")
            .Child(id.c_str())
            .Child("bought");

    firebase::Future<void> future =
        reference.SetValue(firebase::Variant(bought));

    future.OnCompletion(
        [](const firebase::Future<void> &result)
        {
            if (result.error() == 0)
            {
                qDebug()
                    << "Shopping item bought status updated!";
            }
            else
            {
                qWarning()
                    << "Failed to update item:"
                    << result.error_message();
            }
        });
}

void FirebaseManager::addShoppingItem(
    const QString &itemId,
    const QString &name,
    const QString &addedBy)
{
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    if (itemId.isEmpty() || name.trimmed().isEmpty())
    {
        qWarning() << "Invalid shopping item.";
        return;
    }

    // Convert the item into Firebase data.
    std::map<std::string, firebase::Variant> itemData;

    itemData["id"] = firebase::Variant(
        itemId.toUtf8().toStdString());

    itemData["name"] = firebase::Variant(
        name.toUtf8().toStdString());

    itemData["addedBy"] = firebase::Variant(
        addedBy.toUtf8().toStdString());

    itemData["bought"] = firebase::Variant(false);

    // Find this specific item's location.
    std::string id = itemId.toUtf8().toStdString();

    firebase::database::DatabaseReference reference =
        m_database->GetReference()
            .Child("familyData")
            .Child("shoppingItems")
            .Child(id.c_str());

    // Upload only the new item.
    firebase::Future<void> future =
        reference.SetValue(firebase::Variant(itemData));

    future.OnCompletion(
        [](const firebase::Future<void> &result)
        {
            if (result.error() == 0)
            {
                qDebug() << "Shopping item added to Firebase!";
            }
            else
            {
                qWarning()
                    << "Failed to add shopping item:"
                    << result.error_message();
            }
        });
}

void FirebaseManager::deleteShoppingItem(
    const QString &itemId)
{
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    if (itemId.isEmpty())
    {
        qWarning() << "Cannot delete item without an ID.";
        return;
    }

    // Convert QString to std::string.
    std::string id = itemId.toUtf8().toStdString();

    // Find this specific item in Firebase.
    firebase::database::DatabaseReference reference =
        m_database->GetReference()
            .Child("familyData")
            .Child("shoppingItems")
            .Child(id.c_str());

    // Delete only this item.
    firebase::Future<void> future =
        reference.RemoveValue();

    future.OnCompletion(
        [](const firebase::Future<void> &result)
        {
            if (result.error() == 0)
            {
                qDebug()
                    << "Shopping item deleted from Firebase!";
            }
            else
            {
                qWarning()
                    << "Failed to delete shopping item:"
                    << result.error_message();
            }
        });
}

void FirebaseManager::addSavedItem(
    const QString &itemId,
    const QString &name)
{
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    QString cleanName = name.trimmed();

    if (itemId.isEmpty() || cleanName.isEmpty())
    {
        qWarning() << "Invalid saved item.";
        return;
    }

    // Construct the saved item's data.
    std::map<std::string, firebase::Variant> itemData;

    itemData["id"] = firebase::Variant(
        itemId.toUtf8().toStdString());

    itemData["name"] = firebase::Variant(
        cleanName.toUtf8().toStdString());

    // Navigate to this saved item's unique location.
    std::string id = itemId.toUtf8().toStdString();

    firebase::database::DatabaseReference reference =
        m_database->GetReference()
            .Child("familyData")
            .Child("savedItems")
            .Child(id.c_str());

    // Write only this saved item.
    firebase::Future<void> future =
        reference.SetValue(firebase::Variant(itemData));

    future.OnCompletion(
        [](const firebase::Future<void> &result)
        {
            if (result.error() == 0)
            {
                qDebug()
                    << "Saved item added to Firebase!";
            }
            else
            {
                qWarning()
                    << "Failed to add saved item:"
                    << result.error_message();
            }
        });
}

void FirebaseManager::deleteSavedItem(
    const QString &itemId)
{
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    if (itemId.isEmpty())
    {
        qWarning() << "Cannot delete saved item without an ID.";
        return;
    }

    std::string id = itemId.toUtf8().toStdString();

    firebase::database::DatabaseReference reference =
        m_database->GetReference()
            .Child("familyData")
            .Child("savedItems")
            .Child(id.c_str());

    firebase::Future<void> future =
        reference.RemoveValue();

    future.OnCompletion(
        [](const firebase::Future<void> &result)
        {
            if (result.error() == 0)
            {
                qDebug()
                    << "Saved item deleted from Firebase!";
            }
            else
            {
                qWarning()
                    << "Failed to delete saved item:"
                    << result.error_message();
            }
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
