/****************************************************************************
** Generated QML type registration code
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <QtQml/qqml.h>
#include <QtQml/qqmlmoduleregistration.h>

#if __has_include(<saveditemsmodel.h>)
#  include <saveditemsmodel.h>
#endif
#if __has_include(<shoppinglistmodel.h>)
#  include <shoppinglistmodel.h>
#endif
#if __has_include(<storagemanager.h>)
#  include <storagemanager.h>
#endif


#if !defined(QT_STATIC)
#define Q_QMLTYPE_EXPORT Q_DECL_EXPORT
#else
#define Q_QMLTYPE_EXPORT
#endif
Q_QMLTYPE_EXPORT void qml_register_types_PocketHello()
{
    QT_WARNING_PUSH QT_WARNING_DISABLE_DEPRECATED
    QMetaType::fromType<QAbstractItemModel *>().id();
    qmlRegisterEnum<QAbstractItemModel::LayoutChangeHint>("QAbstractItemModel::LayoutChangeHint");
    qmlRegisterEnum<QAbstractItemModel::CheckIndexOption>("QAbstractItemModel::CheckIndexOption");
    QMetaType::fromType<QAbstractListModel *>().id();
    qmlRegisterTypesAndRevisions<SavedItemsModel>("PocketHello", 1);
    qmlRegisterEnum<SavedItemsModel::Roles>("SavedItemsModel::Roles");
    qmlRegisterTypesAndRevisions<ShoppingListModel>("PocketHello", 1);
    qmlRegisterEnum<ShoppingListModel::Roles>("ShoppingListModel::Roles");
    qmlRegisterTypesAndRevisions<StorageManager>("PocketHello", 1);
    QT_WARNING_POP
    qmlRegisterModule("PocketHello", 1, 0);
}

static const QQmlModuleRegistration pocketHelloRegistration("PocketHello", qml_register_types_PocketHello);
