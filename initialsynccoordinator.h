#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantMap>

// Startup policy only. SDK/HTTP callbacks supply validated snapshots and tokens.
class InitialSyncCoordinator : public QObject
{
    Q_OBJECT
public:
    enum State { Loading, Ready, Error };
    Q_ENUM(State)
    explicit InitialSyncCoordinator(QObject *parent = nullptr);
    void begin();
    void setConnected(bool connected);
    void setBlocked(bool blocked);
    void observe(quint64 revision);
    void stage(quint64 attempt, quint64 revision, const QVariantMap &snapshot);
    void stageCollection(quint64 attempt, quint64 revision, const QString &collection,
                         const QVariantMap &items);
    void verified(quint64 attempt, quint64 token, quint64 currentRevision,
                  const QVariantMap &snapshot, const QString &error = {});
    void committed(quint64 attempt, bool success, const QString &error = {});
    void fail(const QString &error);
    void expire();
    State state() const { return m_state; }
    QString error() const { return m_error; }
    bool connected() const { return m_connected; }
    quint64 attempt() const { return m_attempt; }
    bool started() const { return m_started; }
    bool canEdit() const { return m_state == Ready && m_connected; }
signals:
    void changed();
    void attemptStarted(quint64 attempt);
    void verificationRequested(quint64 attempt, quint64 token);
    void snapshotVerified(quint64 attempt, QVariantMap snapshot);
private:
    void verify();
    void later();
    QTimer m_deadline;
    QElapsedTimer m_elapsed;
    State m_state = Loading;
    QString m_error;
    QVariantMap m_candidate, m_requested;
    QVariantMap m_collections;
    QMap<QString, quint64> m_collectionRevisions;
    quint64 m_attempt = 0, m_token = 0, m_revision = 0, m_requestRevision = 0;
    quint64 m_candidateRevision = 0;
    bool m_started = false, m_connected = false, m_blocked = false;
    bool m_haveCandidate = false, m_inFlight = false, m_committing = false;
    bool m_scheduled = false, m_reconnect = false;
};
