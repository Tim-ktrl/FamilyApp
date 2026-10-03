#include "shoppinglistmodel.h"

ShoppingListModel::ShoppingListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ShoppingListModel::rowCount(
    const QModelIndex &parent) const
{
  if (parent.isValid())
  {
    return 0;
  }

  return m_items.size();
}

QVariant ShoppingListModel::data(
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

  const ShoppingItem &item =
      m_items.at(index.row());

  switch (role)
  {

  case NameRole:
    return item.name;

  case AddedByRole:
    return item.addedBy;

  case BoughtRole:
    return item.bought;

  default:
    return {};
  }
}

bool ShoppingListModel::setData(
    const QModelIndex &index,
    const QVariant &value,
    int role)
{
  if (!index.isValid())
  {
    return false;
  }

  if (index.row() < 0 ||
      index.row() >= m_items.size())
  {
    return false;
  }

  ShoppingItem &item = m_items[index.row()];

  switch (role)
  {

  case NameRole:
  {
    QString newName =
        value.toString().trimmed();

    if (newName.isEmpty() ||
        newName == item.name)
    {
      return false;
    }

    item.name = newName;
    break;
  }

  case AddedByRole:
  {

    QString newAddedBy =
        value.toString();

    if (newAddedBy == item.addedBy)
    {
      return false;
    }

    item.addedBy = newAddedBy;
    break;
  }

  case BoughtRole:
  {

    bool newBought =
        value.toBool();

    if (newBought == item.bought)
    {
      return false;
    }

    item.bought = newBought;
    break;
  }

  default:
    return false;
  }

  emit dataChanged(
      index,
      index,
      {role});

  emit itemsChanged();

  return true;
}

QHash<int, QByteArray>
ShoppingListModel::roleNames() const
{
  return {
      {NameRole, "name"},
      {AddedByRole, "addedBy"},
      {BoughtRole, "bought"}};
}

Qt::ItemFlags ShoppingListModel::flags(
    const QModelIndex &index) const
{
  if (!index.isValid())
  {
    return Qt::NoItemFlags;
  }

  return Qt::ItemIsEnabled |
         Qt::ItemIsSelectable |
         Qt::ItemIsEditable;
}

int ShoppingListModel::count() const
{
  return m_items.size();
}

void ShoppingListModel::addItem(
    const QString &name,
    const QString &addedBy)
{
  QString cleanName = name.trimmed();

  if (cleanName.isEmpty())
  {
    return;
  }

  int newRow = m_items.size();

  beginInsertRows(
      QModelIndex(),
      newRow,
      newRow);

  ShoppingItem item;

  item.name = cleanName;
  item.addedBy = addedBy;
  item.bought = false;

  m_items.append(item);

  endInsertRows();

  emit countChanged();
  emit itemsChanged();
}

void ShoppingListModel::removeItem(int row)
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

void ShoppingListModel::setBought(
    int row,
    bool bought)
{
  if (row < 0 ||
      row >= m_items.size())
  {
    return;
  }

  QModelIndex modelIndex =
      index(row, 0);

  setData(
      modelIndex,
      bought,
      BoughtRole);
}

QVariantList
ShoppingListModel::toVariantList() const
{
  QVariantList result;

  for (const ShoppingItem &item : m_items)
  {

    QVariantMap map;

    map["name"] = item.name;
    map["addedBy"] = item.addedBy;
    map["bought"] = item.bought;

    result.append(map);
  }

  return result;
}

void ShoppingListModel::loadFromVariantList(
    const QVariantList &items)
{
  QVector<ShoppingItem> loadedItems;

  for (const QVariant &value : items)
  {

    QVariantMap map =
        value.toMap();

    QString name =
        map.value("name")
            .toString()
            .trimmed();

    if (name.isEmpty())
    {
      continue;
    }

    ShoppingItem item;

    item.name = name;

    item.addedBy =
        map.value(
               "addedBy",
               "Unknown")
            .toString();

    item.bought =
        map.value(
               "bought",
               false)
            .toBool();

    loadedItems.append(item);
  }

  beginResetModel();

  m_items = loadedItems;

  endResetModel();

  emit countChanged();
  emit itemsChanged();
}