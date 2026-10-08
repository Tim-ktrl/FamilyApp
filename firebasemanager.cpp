#include "firebasemanager.h"

#include <QDebug>
#include <QDir>

#include <firebase/future.h>
#include <firebase/variant.h>

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