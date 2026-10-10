#include "firebasemanager.h"

#include <QDebug>
#include <QDir>
#include <QVariantMap>

#include <map>
#include <string>
#include <vector>

#include <firebase/future.h>
#include <firebase/variant.h>

FirebaseManager::FirebaseManager(QObject *parent)
    : QObject(parent)
{
  m_loadTimer.setInterval(50);

  connect(
      &m_loadTimer,
      &QTimer::timeout,
      this,
      &FirebaseManager::checkLoadResult);

  initializeFirebase();
}

FirebaseManager::~FirebaseManager()
{
  m_loadTimer.stop();

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
          "https://familyapp-a9a07-default-rtdb.firebaseio.com/",
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
    qWarning() << "Firebase is not initialized.";
    emit familyDataLoadFailed(
        "Firebase is not initialized.");
    return;
  }

  if (m_loadTimer.isActive())
  {
    qDebug() << "Firebase download already running.";
    return;
  }

  qDebug() << "Downloading family data...";

  firebase::database::DatabaseReference reference =
      m_database->GetReference().Child("familyData");

  m_loadFuture = reference.GetValue();

  if (m_loadFuture.status() ==
      firebase::kFutureStatusInvalid)
  {
    emit familyDataLoadFailed(
        "Could not start Firebase download.");
    return;
  }

  m_loadTimer.start();
}

void FirebaseManager::checkLoadResult()
{
  if (m_loadFuture.status() ==
      firebase::kFutureStatusPending)
  {
    return;
  }

  m_loadTimer.stop();

  if (m_loadFuture.status() !=
      firebase::kFutureStatusComplete)
  {
    emit familyDataLoadFailed(
        "Firebase download did not complete.");
    return;
  }

  if (m_loadFuture.error() !=
      firebase::database::kErrorNone)
  {
    QString message =
        QString::fromUtf8(m_loadFuture.error_message());

    qWarning() << "Firebase download failed:" << message;

    emit familyDataLoadFailed(message);
    return;
  }

  const firebase::database::DataSnapshot *snapshot =
      m_loadFuture.result();

  if (snapshot == nullptr ||
      !snapshot->is_valid() ||
      !snapshot->exists() ||
      !snapshot->has_children())
  {
    emit familyDataLoadFailed(
        "No family data found in Firebase.");
    return;
  }

  QVariantList shoppingItems;
  QVariantList savedItems;

  // Read shopping list items.
  auto shoppingSnapshot =
      snapshot->Child("shoppingItems");

  for (const auto &itemSnapshot :
       shoppingSnapshot.children())
  {
    auto name = itemSnapshot.Child("name").value();

    if (!name.is_string())
    {
      continue;
    }

    QVariantMap item;

    item["name"] =
        QString::fromUtf8(name.string_value());

    auto addedBy =
        itemSnapshot.Child("addedBy").value();

    item["addedBy"] =
        addedBy.is_string()
            ? QString::fromUtf8(addedBy.string_value())
            : QString("Unknown");

    auto bought =
        itemSnapshot.Child("bought").value();

    item["bought"] =
        bought.is_bool() ? bought.bool_value() : false;

    shoppingItems.append(item);
  }

  // Read saved items.
  auto savedSnapshot =
      snapshot->Child("savedItems");

  for (const auto &itemSnapshot :
       savedSnapshot.children())
  {
    auto name = itemSnapshot.Child("name").value();

    if (!name.is_string())
    {
      continue;
    }

    QVariantMap item;

    item["name"] =
        QString::fromUtf8(name.string_value());

    savedItems.append(item);
  }

  qDebug() << "Firebase download successful!";
  qDebug() << "Shopping items:" << shoppingItems.size();
  qDebug() << "Saved items:" << savedItems.size();

  emit familyDataLoaded(shoppingItems, savedItems);
}