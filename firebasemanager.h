#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QTimer>
#include <QNetworkAccessManager>

#include <firebase/app.h>
#include <firebase/database.h>
#include <firebase/future.h>

class FirebaseManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(
        bool initialized
            READ initialized
                NOTIFY initializedChanged)

public:
    explicit FirebaseManager(
        QObject *parent = nullptr);

    ~FirebaseManager() override;

    bool initialized() const;

    Q_INVOKABLE void testWrite();

    Q_INVOKABLE void saveFamilyData(
        const QVariantList &shoppingItems,
        const QVariantList &savedItems);

    Q_INVOKABLE void loadFamilyData();

signals:
    void initializedChanged();

    void familyDataLoaded(
        QVariantList shoppingItems,
        QVariantList savedItems);

    void familyDataLoadFailed(QString message);

private:
    void initializeFirebase();

    firebase::App *m_app = nullptr;

    firebase::database::Database *m_database = nullptr;

    bool m_initialized = false;

      QNetworkAccessManager m_networkManager;

    bool m_downloadInProgress = false;
};