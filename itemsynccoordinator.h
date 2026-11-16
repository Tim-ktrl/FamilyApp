#pragma once

#include <QObject>
#include <QMap>
#include <QQueue>
#include <QVariantMap>

// Qt-thread policy only: the manager supplies SDK writes and item reads.
class ItemSyncCoordinator : public QObject
{
    Q_OBJECT
public:
    enum WriteKind { PutItem, SetBought, DeleteItem };
    enum Phase { Pending, Failed, Reconciling, ReconciliationFailed, Synchronized };
    struct Operation {
        quint64 generation;
        WriteKind kind;
        QVariantMap payload;
    };
    struct ItemState {
        QQueue<Operation> queue;
        quint64 activeGeneration = 0;
        quint64 latestGeneration = 0;
        quint64 acknowledgedGeneration = 0;
        quint64 readToken = 0;
        quint64 readGeneration = 0;
        quint64 readSession = 0;
        bool reading = false;
        bool dirty = false;
        Phase phase = Pending;
        QString error;
    };
    explicit ItemSyncCoordinator(QObject *parent = nullptr) : QObject(parent) {}
    quint64 enqueue(const QString &collection, const QString &id, WriteKind kind,
                    const QVariantMap &payload = {});
    void completeWrite(const QString &collection, const QString &id, quint64 generation,
                       const QString &error = {});
    void incoming(const QString &collection, const QString &id, const QVariantMap &item, bool removed);
    void completeRead(const QString &collection, const QString &id, quint64 token,
                      const QVariantMap &item, bool removed, const QString &error = {});
    void setListening(bool listening);
    void setBulkBusy(bool busy);
    bool bulkBusy() const { return m_bulkBusy; }
    int unresolvedCount() const;
    int failedCount() const;
    int unresolvedLocalCount() const;
    void verifyRemoteEvents(bool enabled) { m_verifyRemote = enabled; }
    void clearRemoteReads();
    void revalidateAcknowledged();
    quint64 mutationGeneration() const { return m_generation; }
    ItemState state(const QString &collection, const QString &id) const;

signals:
    void writeRequested(QString collection, QString id, quint64 generation, int kind, QVariantMap payload);
    void readRequested(QString collection, QString id, quint64 token);
    void itemReady(QString collection, QString id, QVariantMap item, bool removed);
    void itemFailed(QString collection, QString id, QString message);
    void stateChanged();

private:
    using Key = QPair<QString, QString>;
    void startNext(const Key &key);
    void requestRead(const Key &key);
    QMap<Key, ItemState> m_items;
    quint64 m_generation = 0;
    quint64 m_readToken = 0;
    quint64 m_session = 0;
    bool m_listening = false;
    bool m_bulkBusy = false;
    bool m_verifyRemote = false;
};
