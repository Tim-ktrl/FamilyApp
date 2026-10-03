#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVector>

struct ShoppingItem
{
  QString name;
  QString addedBy;
  bool bought = false;
};

class ShoppingListModel : public QAbstractListModel
{
  Q_OBJECT
  QML_ELEMENT

  Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
  enum Roles
  {
    NameRole = Qt::UserRole + 1,
    AddedByRole,
    BoughtRole
  };

  Q_ENUM(Roles)

  explicit ShoppingListModel(QObject *parent = nullptr);

  int rowCount(
      const QModelIndex &parent = QModelIndex()) const override;

  QVariant data(
      const QModelIndex &index,
      int role = Qt::DisplayRole) const override;

  bool setData(
      const QModelIndex &index,
      const QVariant &value,
      int role) override;

  QHash<int, QByteArray> roleNames() const override;

  Qt::ItemFlags flags(
      const QModelIndex &index) const override;

  int count() const;

  Q_INVOKABLE void addItem(
      const QString &name,
      const QString &addedBy);

  Q_INVOKABLE void removeItem(int row);

  Q_INVOKABLE void setBought(
      int row,
      bool bought);

  Q_INVOKABLE QVariantList toVariantList() const;

  Q_INVOKABLE void loadFromVariantList(
      const QVariantList &items);

signals:
  void itemsChanged();
  void countChanged();

private:
  QVector<ShoppingItem> m_items;
};