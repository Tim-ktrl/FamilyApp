#pragma once

#include <QObject>
#include <QQmlEngine>

#include <firebase/app.h>
#include <firebase/database.h>

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

signals:
  void initializedChanged();

private:
  void initializeFirebase();

  firebase::App *m_app = nullptr;

  firebase::database::Database *m_database = nullptr;

  bool m_initialized = false;
};