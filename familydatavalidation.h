#pragma once
#include <QVariantMap>
#include <QByteArray>

namespace FamilyData {
bool validateItem(const QString &collection, const QString &id, const QVariantMap &fields,
                  QVariantMap &item, QString &error);
bool parseSnapshot(const QByteArray &json, QVariantMap &snapshot, QString &error);
QVariantList items(const QVariantMap &snapshot, const QString &collection);
}
