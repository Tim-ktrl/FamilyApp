#include "itemsynccoordinator.h"

quint64 ItemSyncCoordinator::enqueue(const QString &collection, const QString &id,
                                    WriteKind kind, const QVariantMap &payload)
{
    const Key key{collection, id};
    auto &entry = m_items[key];
    const quint64 generation = ++m_generation;
    entry.latestGeneration = generation;
    entry.queue.enqueue({generation, kind, payload});
    entry.dirty = true;
    // A failed queue stays paused, including any subsequently added operations.
    if (entry.phase != Failed) {
        entry.phase = Pending;
        entry.error.clear();
    }
    emit stateChanged();
    startNext(key);
    return generation;
}

void ItemSyncCoordinator::startNext(const Key &key)
{
    auto &entry = m_items[key];
    if (m_bulkBusy || entry.phase == Failed || entry.activeGeneration || entry.queue.isEmpty())
        return;
    const Operation operation = entry.queue.head();
    entry.activeGeneration = operation.generation;
    entry.phase = Pending;
    emit writeRequested(key.first, key.second, operation.generation, operation.kind, operation.payload);
}

void ItemSyncCoordinator::completeWrite(const QString &collection, const QString &id,
                                       quint64 generation, const QString &error)
{
    const Key key{collection, id};
    auto it = m_items.find(key);
    if (it == m_items.end() || !generation || it->activeGeneration != generation)
        return;
    auto &entry = it.value();
    entry.activeGeneration = 0;
    if (!error.isEmpty()) {
        entry.phase = Failed;
        entry.error = error;
        emit stateChanged();
        emit itemFailed(collection, id, error);
        return;
    }
    entry.acknowledgedGeneration = generation;
    entry.queue.dequeue();
    if (!entry.queue.isEmpty()) {
        startNext(key);
        emit stateChanged();
    } else {
        requestRead(key);
    }
}

void ItemSyncCoordinator::incoming(const QString &collection, const QString &id,
                                  const QVariantMap &item, bool removed)
{
    if (!m_listening)
        return;
    const Key key{collection, id};
    auto it = m_items.find(key);
    if (it == m_items.end() && !m_verifyRemote) {
        emit itemReady(collection, id, item, removed);
        return;
    }
    if (it == m_items.end())
        it = m_items.insert(key, ItemState{});
    it->dirty = true;
    if (!it->queue.isEmpty() || it->phase == Failed || it->phase == ReconciliationFailed)
        return;
    // Never replay event payloads for locally touched UUIDs, even after ack.
    requestRead(key);
}

void ItemSyncCoordinator::requestRead(const Key &key)
{
    auto &entry = m_items[key];
    entry.phase = Reconciling;
    if (entry.reading) {
        entry.dirty = true;
        emit stateChanged();
        return;
    }
    entry.reading = true;
    entry.dirty = false;
    entry.readToken = ++m_readToken;
    entry.readGeneration = entry.latestGeneration;
    entry.readSession = m_session;
    emit stateChanged();
    emit readRequested(key.first, key.second, entry.readToken);
}

void ItemSyncCoordinator::completeRead(const QString &collection, const QString &id, quint64 token,
                                      const QVariantMap &item, bool removed, const QString &error)
{
    const Key key{collection, id};
    auto it = m_items.find(key);
    if (it == m_items.end() || !it->reading || it->readToken != token)
        return;
    auto &entry = it.value();
    entry.reading = false;
    if (!entry.queue.isEmpty() || entry.phase == Failed)
        return; // A newer local operation owns protection now.
    if (entry.readGeneration != entry.latestGeneration || entry.readSession != m_session) {
        requestRead(key);
        return;
    }
    if (!error.isEmpty()) {
        entry.phase = ReconciliationFailed;
        entry.error = error;
        emit stateChanged();
        emit itemFailed(collection, id, error);
        return;
    }
    if (entry.dirty) {
        requestRead(key);
        return;
    }
    entry.phase = Synchronized;
    entry.error.clear();
    emit stateChanged();
    if (m_listening)
        emit itemReady(collection, id, item, removed);
}

void ItemSyncCoordinator::setListening(bool listening)
{
    if (m_listening == listening)
        return;
    m_listening = listening;
    ++m_session;
    if (listening) {
        const auto keys = m_items.keys();
        for (const auto &key : keys) {
            const auto &entry = m_items[key];
            if (entry.queue.isEmpty() && entry.phase != Failed && entry.phase != ReconciliationFailed)
                requestRead(key);
        }
    }
}

void ItemSyncCoordinator::setBulkBusy(bool busy)
{
    if (m_bulkBusy == busy)
        return;
    m_bulkBusy = busy;
    emit stateChanged();
    if (!busy) {
        const auto keys = m_items.keys();
        for (const auto &key : keys)
            startNext(key);
    }
}

int ItemSyncCoordinator::unresolvedCount() const
{
    int count = 0;
    for (const auto &entry : m_items)
        if (entry.phase != Synchronized)
            ++count;
    return count;
}

int ItemSyncCoordinator::failedCount() const
{
    int count = 0;
    for (const auto &entry : m_items)
        if (entry.phase == Failed || entry.phase == ReconciliationFailed)
            ++count;
    return count;
}

ItemSyncCoordinator::ItemState ItemSyncCoordinator::state(const QString &collection, const QString &id) const
{
    return m_items.value({collection, id});
}

int ItemSyncCoordinator::unresolvedLocalCount() const
{
    int count = 0;
    for (const auto &entry : m_items)
        if (entry.latestGeneration && entry.phase != Synchronized)
            ++count;
    return count;
}

void ItemSyncCoordinator::clearRemoteReads()
{
    for (auto it = m_items.begin(); it != m_items.end();) {
        if (!it->latestGeneration) it = m_items.erase(it);
        else ++it;
    }
    emit stateChanged();
}

void ItemSyncCoordinator::revalidateAcknowledged()
{
    // An explicit online startup/reconnect may re-read an acknowledged item.
    // Failed writes and their queued operations are never retried or discarded.
    const auto keys = m_items.keys();
    for (const auto &key : keys) {
        const auto &entry = m_items[key];
        if (entry.acknowledgedGeneration && entry.queue.isEmpty()
            && entry.phase == ReconciliationFailed)
            requestRead(key);
    }
}
