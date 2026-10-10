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

FirebaseManager::FirebaseManager(QObject *parent)
    : QObject(parent)
{

    initializeFirebase();
}

FirebaseManager::~FirebaseManager()
{

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
    if (!m_initialized || m_database == nullptr)
    {
        qWarning() << "Firebase is not initialized.";
        return;
    }

    // Convert shopping items.
    std::vector<firebase::Variant> shoppingData;

    for (const QVariant &value : shoppingItems)
    {
        QVariantMap item = value.toMap();

        std::map<std::string, firebase::Variant> itemData;

        itemData["name"] = firebase::Variant(
            item.value("name").toString().toUtf8().toStdString());

        itemData["addedBy"] = firebase::Variant(
            item.value("addedBy").toString().toUtf8().toStdString());

        itemData["bought"] = firebase::Variant(
            item.value("bought").toBool());

        shoppingData.emplace_back(itemData);
    }

    // Convert saved items.
    std::vector<firebase::Variant> savedData;

    for (const QVariant &value : savedItems)
    {
        QVariantMap item = value.toMap();

        std::map<std::string, firebase::Variant> itemData;

        itemData["name"] = firebase::Variant(
            item.value("name").toString().toUtf8().toStdString());

        savedData.emplace_back(itemData);
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
    if (!m_initialized || m_database == nullptr)
    {
        emit familyDataLoadFailed("Firebase is not initialized.");
        return;
    }

    if (m_downloadInProgress)
    {
        qDebug() << "Download already in progress.";
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

    connect(reply, &QNetworkReply::finished, this,
            [this, reply]()
            {
                m_downloadInProgress = false;

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
