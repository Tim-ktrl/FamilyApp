#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlEngine>

class StorageManager : public QObject
{
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

public:
  explicit StorageManager(QObject *parent = nullptr);

  Q_INVOKABLE bool saveData(
      const QVariantList &shoppingItems,
      const QVariantList &savedItems);

  Q_INVOKABLE QVariantMap loadData();

  Q_INVOKABLE QString storagePath() const;
};