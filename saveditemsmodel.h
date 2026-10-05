#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVector>

struct SavedItem
{
  QString name;
};

class SavedItemsModel : public QAbstractListModel
{
  Q_OBJECT
  QML_ELEMENT

  Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
  enum Roles
  {
    NameRole = Qt::UserRole + 1
  };

  Q_ENUM(Roles)

  explicit SavedItemsModel(QObject *parent = nullptr);

  int rowCount(
      const QModelIndex &parent = QModelIndex()) const override;

  QVariant data(
      const QModelIndex &index,
      int role = Qt::DisplayRole) const override;

  QHash<int, QByteArray> roleNames() const override;

  int count() const;

  Q_INVOKABLE void addItem(const QString &name);

  Q_INVOKABLE void removeItem(int row);

  Q_INVOKABLE QVariantList toVariantList() const;

  Q_INVOKABLE void loadFromVariantList(
      const QVariantList &items);

signals:
  void itemsChanged();
  void countChanged();

private:
  QVector<SavedItem> m_items;
};