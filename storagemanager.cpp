#include "storagemanager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

StorageManager::StorageManager(QObject *parent)
    : QObject(parent)
{
}

QString StorageManager::storagePath() const
{
  QString directoryPath =
      QStandardPaths::writableLocation(
          QStandardPaths::AppLocalDataLocation);

  QDir directory;

  if (!directory.exists(directoryPath))
  {
    directory.mkpath(directoryPath);
  }

  return directoryPath + "/familyapp_data.json";
}

bool StorageManager::saveData(
    const QVariantList &shoppingItems,
    const QVariantList &savedItems)
{
  QVariantMap data;

  data["shoppingItems"] = shoppingItems;
  data["savedItems"] = savedItems;

  QJsonObject jsonObject =
      QJsonObject::fromVariantMap(data);

  QJsonDocument jsonDocument(jsonObject);

  QSaveFile file(storagePath());

  if (!file.open(QIODevice::WriteOnly))
  {
    return false;
  }

  file.write(
      jsonDocument.toJson(QJsonDocument::Indented));

  return file.commit();
}

QVariantMap StorageManager::loadData()
{
  QFile file(storagePath());

  if (!file.exists())
  {
    return {};
  }

  if (!file.open(QIODevice::ReadOnly))
  {
    return {};
  }

  QByteArray fileData = file.readAll();

  QJsonParseError error;

  QJsonDocument document =
      QJsonDocument::fromJson(fileData, &error);

  if (error.error != QJsonParseError::NoError)
  {
    return {};
  }

  if (!document.isObject())
  {
    return {};
  }

  return document.object().toVariantMap();
}