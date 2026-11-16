#include "initialsynccoordinator.h"
#include "familydatavalidation.h"

InitialSyncCoordinator::InitialSyncCoordinator(QObject *parent) : QObject(parent)
{
    m_deadline.setSingleShot(true);
    m_deadline.setTimerType(Qt::PreciseTimer);
    m_deadline.setInterval(20000);
    connect(&m_deadline, &QTimer::timeout, this, &InitialSyncCoordinator::expire);
}

void InitialSyncCoordinator::begin()
{
    if (m_started && m_state == Loading)
        return;
    m_started = true;
    ++m_attempt;
    m_state = Loading;
    m_error.clear();
    m_haveCandidate = m_inFlight = m_committing = m_scheduled = false;
    m_candidateRevision = 0;
    m_collections.clear();
    m_collectionRevisions.clear();
    m_reconnect = false;
    m_elapsed.start();
    m_deadline.start();
    emit changed();
    emit attemptStarted(m_attempt);
}

void InitialSyncCoordinator::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    if (!connected && m_started) {
        m_reconnect = true;
        fail("Firebase disconnected. Editing is disabled until revalidation succeeds.");
    } else if (connected && m_reconnect) {
        begin();
    }
    emit changed();
    verify();
}

void InitialSyncCoordinator::setBlocked(bool blocked)
{
    m_blocked = blocked;
    verify();
}

void InitialSyncCoordinator::observe(quint64 revision)
{
    m_revision = qMax(m_revision, revision);
}

void InitialSyncCoordinator::stage(quint64 attempt, quint64 revision, const QVariantMap &snapshot)
{
    if (attempt != m_attempt || m_state != Loading || revision < m_candidateRevision)
        return;
    observe(revision);
    m_candidateRevision = revision;
    m_candidate = snapshot;
    m_haveCandidate = true;
    verify();
}

void InitialSyncCoordinator::stageCollection(quint64 attempt, quint64 revision,
                                            const QString &collection, const QVariantMap &items)
{
    if (attempt != m_attempt || m_state != Loading)
        return;
    if (collection != "shoppingItems" && collection != "savedItems") {
        fail("Invalid startup collection: " + collection);
        return;
    }
    if (m_collectionRevisions.contains(collection) && revision <= m_collectionRevisions.value(collection))
        return;
    QVariantMap validated;
    QString error;
    for (auto it = items.cbegin(); it != items.cend(); ++it) {
        QVariantMap item;
        if (it.value().typeId() != QMetaType::QVariantMap
            || !FamilyData::validateItem(collection, it.key(), it.value().toMap(), item, error)) {
            fail(error.isEmpty() ? "Invalid startup item: " + collection + "/" + it.key() : error);
            return;
        }
        validated[it.key()] = item;
    }
    m_collections[collection] = validated;
    m_collectionRevisions[collection] = revision;
    observe(revision);
    // Empty is a valid received snapshot; absence is not. Never combine a partial attempt.
    if (m_collectionRevisions.size() == 2)
        stage(attempt, qMax(m_collectionRevisions.value("shoppingItems"),
                           m_collectionRevisions.value("savedItems")), m_collections);
}

void InitialSyncCoordinator::verify()
{
    if (!m_started || m_state != Loading || !m_connected || m_blocked
        || !m_haveCandidate || m_inFlight || m_committing || m_scheduled)
        return;
    if (m_elapsed.hasExpired(20000)) { expire(); return; }
    m_requested = m_candidate;
    m_requestRevision = m_revision;
    m_inFlight = true;
    emit verificationRequested(m_attempt, ++m_token);
}

void InitialSyncCoordinator::later()
{
    if (m_scheduled)
        return;
    m_scheduled = true;
    const auto attempt = m_attempt;
    QTimer::singleShot(100, this, [this, attempt]() {
        if (attempt != m_attempt || m_state != Loading) return;
        m_scheduled = false;
        verify();
    });
}

void InitialSyncCoordinator::verified(quint64 attempt, quint64 token, quint64 currentRevision,
                                     const QVariantMap &snapshot, const QString &error)
{
    if (attempt != m_attempt || token != m_token || !m_inFlight || m_state != Loading)
        return;
    if (m_elapsed.hasExpired(20000)) { expire(); return; }
    m_inFlight = false;
    observe(currentRevision);
    if (!error.isEmpty()) {
        fail(error);
        return;
    }
    if (!m_connected || m_blocked || m_revision != m_requestRevision
        || m_candidate != m_requested || snapshot != m_requested) {
        later();
        return;
    }
    m_committing = true;
    emit snapshotVerified(attempt, snapshot);
}

void InitialSyncCoordinator::committed(quint64 attempt, bool success, const QString &error)
{
    if (attempt != m_attempt || !m_committing || m_state != Loading)
        return;
    if (m_elapsed.hasExpired(20000)) { expire(); return; }
    m_committing = false;
    if (!success || !m_connected || m_blocked) {
        fail(error.isEmpty() ? "Initial snapshot could not be committed safely." : error);
        return;
    }
    m_deadline.stop();
    m_state = Ready;
    emit changed();
}

void InitialSyncCoordinator::fail(const QString &error)
{
    if (!m_started)
        return;
    m_deadline.stop();
    m_state = Error;
    m_error = error;
    m_inFlight = m_committing = m_scheduled = false;
    emit changed();
}

void InitialSyncCoordinator::expire()
{
    if (m_state == Loading && m_started)
        fail(m_blocked ? "Initial sync blocked by unresolved local writes or an upload."
                       : "Could not verify a stable online snapshot within 20 seconds.");
}
