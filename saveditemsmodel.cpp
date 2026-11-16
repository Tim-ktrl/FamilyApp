#include "saveditemsmodel.h"
#include <QUuid>

SavedItemsModel::SavedItemsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SavedItemsModel::rowCount(
    const QModelIndex &parent) const
{
  if (parent.isValid())
  {
    return 0;
  }

  return m_items.size();
}

QVariant SavedItemsModel::data(
    const QModelIndex &index,
    int role) const
{
  if (!index.isValid())
  {
    return {};
  }

  if (index.row() < 0 ||
      index.row() >= m_items.size())
  {
    return {};
  }

  const SavedItem &item =
      m_items.at(index.row());

  switch (role)
  {
  case NameRole:
    return item.name;

  case ItemIdRole:
    return item.id;

  default:
    return {};
  }
}

QHash<int, QByteArray>
SavedItemsModel::roleNames() const
{
  return {
      {NameRole, "name"},
      {ItemIdRole, "itemId"}};
}

int SavedItemsModel::count() const
{
  return m_items.size();
}

QString SavedItemsModel::addItem(
    const QString &name)
{
  QString cleanName = name.trimmed();

  if (cleanName.isEmpty())
  {
    return {};
  }

  // Don't allow duplicate saved items.
  for (const SavedItem &item : m_items)
  {
    if (item.name.compare(
            cleanName,
            Qt::CaseInsensitive) == 0)
    {
      return {};
    }
  }

  int newRow = m_items.size();

  beginInsertRows(QModelIndex(), newRow, newRow);

  SavedItem item;

  item.id = QUuid::createUuid().toString(
      QUuid::WithoutBraces);

  item.name = cleanName;

  m_items.append(item);

  endInsertRows();

  emit countChanged();
  emit itemsChanged();

  return item.id;
}

void SavedItemsModel::removeItem(int row)
{
  if (row < 0 ||
      row >= m_items.size())
  {
    return;
  }

  beginRemoveRows(
      QModelIndex(),
      row,
      row);

  m_items.removeAt(row);

  endRemoveRows();

  emit countChanged();
  emit itemsChanged();
}

QVariantList
SavedItemsModel::toVariantList() const
{
  QVariantList result;

  for (const SavedItem &item : m_items)
  {
    QVariantMap map;

    map["id"] = item.id;
    map["name"] = item.name;

    result.append(map);
  }

  return result;
}

int SavedItemsModel::findItemById(const QString &itemId) const
{
  for (int row = 0; row < m_items.size(); ++row)
    if (m_items.at(row).id == itemId)
      return row;
  return -1;
}

void SavedItemsModel::upsertRemoteItem(const QString &itemId, const QString &name)
{
  const QString cleanName = name.trimmed();
  if (itemId.isEmpty() || cleanName.isEmpty())
    return;
  const int row = findItemById(itemId);
  if (row < 0) {
    const int newRow = m_items.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_items.append({itemId, cleanName});
    endInsertRows();
    emit countChanged();
    emit itemsChanged();
  } else if (m_items.at(row).name != cleanName) {
    m_items[row].name = cleanName;
    emit dataChanged(index(row, 0), index(row, 0), {NameRole});
    emit itemsChanged();
  }
}

void SavedItemsModel::removeItemById(const QString &itemId)
{
  removeItem(findItemById(itemId));
}

void SavedItemsModel::loadFromVariantList(
    const QVariantList &items)
{
  QVector<SavedItem> loadedItems;

  for (const QVariant &value : items)
  {
    QVariantMap map = value.toMap();

    QString name =
        map.value("name")
            .toString()
            .trimmed();

    if (name.isEmpty())
    {
      continue;
    }

    SavedItem item;

    QString existingId = map.value("id").toString();

    item.id = existingId.isEmpty()
                  ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                  : existingId;

    item.name = name;

    loadedItems.append(item);
  }

  beginResetModel();

  m_items = loadedItems;

  endResetModel();

  emit countChanged();
  emit itemsChanged();
}
