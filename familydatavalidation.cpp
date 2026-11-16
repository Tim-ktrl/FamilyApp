#include "familydatavalidation.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

bool FamilyData::validateItem(const QString &collection, const QString &id, const QVariantMap &fields,
                              QVariantMap &item, QString &error)
{
    const auto name = fields.value("name");
    const auto storedId = fields.value("id");
    if (QUuid(id).isNull() || name.typeId() != QMetaType::QString || name.toString().trimmed().isEmpty()
        || (!storedId.isNull() && (storedId.typeId() != QMetaType::QString || storedId.toString() != id))) {
        error = "Invalid UUID, name, or stored ID: " + collection + "/" + id;
        return false;
    }
    item = {{"id", id}, {"name", name.toString().trimmed()}};
    if (collection == "shoppingItems") {
        if (fields.value("addedBy").typeId() != QMetaType::QString
            || fields.value("bought").typeId() != QMetaType::Bool) {
            error = "Invalid shopping fields: " + id;
            return false;
        }
        item["addedBy"] = fields.value("addedBy");
        item["bought"] = fields.value("bought");
    }
    return true;
}

bool FamilyData::parseSnapshot(const QByteArray &json, QVariantMap &snapshot, QString &error)
{
    QVariantMap root;
    if (json.trimmed() != "null") {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(json, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            error = "Invalid familyData JSON (expected object or null).";
            return false;
        }
        root = document.object().toVariantMap();
    }
    QVariantMap validated;
    for (const auto &collection : {QString("shoppingItems"), QString("savedItems")}) {
        const auto value = root.value(collection);
        QVariantMap entries;
        if (!value.isNull()) {
            if (value.typeId() != QMetaType::QVariantMap) {
                error = "Invalid collection: " + collection;
                return false;
            }
            const auto fields = value.toMap();
            for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
                QVariantMap item;
                if (it.value().typeId() != QMetaType::QVariantMap
                    || !validateItem(collection, it.key(), it.value().toMap(), item, error)) {
                    if (error.isEmpty()) error = "Invalid item: " + collection + "/" + it.key();
                    return false;
                }
                entries[it.key()] = item;
            }
        }
        validated[collection] = entries;
    }
    snapshot = validated; // Never return partial data.
    return true;
}

QVariantList FamilyData::items(const QVariantMap &snapshot, const QString &collection)
{
    QVariantList result;
    const auto entries = snapshot.value(collection).toMap();
    for (const auto &item : entries)
        result.append(item);
    return result;
}
