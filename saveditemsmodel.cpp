#include "saveditemsmodel.h"

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

  default:
    return {};
  }
}

QHash<int, QByteArray>
SavedItemsModel::roleNames() const
{
  return {
      {NameRole, "name"}};
}

int SavedItemsModel::count() const
{
  return m_items.size();
}

void SavedItemsModel::addItem(
    const QString &name)
{
  QString cleanName = name.trimmed();

  if (cleanName.isEmpty())
  {
    return;
  }

  // Don't allow duplicate saved items.
  for (const SavedItem &item : m_items)
  {
    if (item.name.compare(
            cleanName,
            Qt::CaseInsensitive) == 0)
    {
      return;
    }
  }

  int newRow = m_items.size();

  beginInsertRows(
      QModelIndex(),
      newRow,
      newRow);

  SavedItem item;
  item.name = cleanName;

  m_items.append(item);

  endInsertRows();

  emit countChanged();
  emit itemsChanged();
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

    map["name"] = item.name;

    result.append(map);
  }

  return result;
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
    item.name = name;

    loadedItems.append(item);
  }

  beginResetModel();

  m_items = loadedItems;

  endResetModel();

  emit countChanged();
  emit itemsChanged();
}