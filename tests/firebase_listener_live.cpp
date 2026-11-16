// Optional, read-only Windows integration probe. Not registered with CTest.
#include "firebasemanager.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <QDebug>
#include <firebase/log.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>
#endif

// SDK 13.13 test-only logger forwarding. Assert messages retain fatal abort behavior.
namespace firebase { void LogSetCallback(void (*)(LogLevel, const char *, void *), void *); }

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        const auto bytes = message.toUtf8();
        std::fprintf(stderr, "%s\n", bytes.constData());
        std::fflush(stderr);
    });
    if (!app.arguments().contains("--live-read-only")) {
        qCritical() << "Explicit --live-read-only is required.";
        return 2;
    }
    // Refuse normal application configuration: an isolated package gives this
    // probe a separate SDK disk cache. No StorageManager or real JSON cache is used.
    QFile file("google-services.json");
    if (!file.open(QIODevice::ReadOnly)) return 2;
    const auto clients = QJsonDocument::fromJson(file.readAll()).object().value("client").toArray();
    if (clients.isEmpty()) return 2;
    for (const auto &client : clients) {
        const auto package = client.toObject().value("client_info").toObject()
            .value("android_client_info").toObject().value("package_name").toString();
        if (!package.startsWith("com.familyapp.listenerfix.")) {
            qCritical() << "Refusing to share the application's SDK disk cache.";
            return 2;
        }
    }
    firebase::LogSetCallback([](firebase::LogLevel level, const char *message, void *) {
        std::fprintf(stderr, "[SDK %d] %s\n", static_cast<int>(level), message);
        std::fflush(stderr);
        if (level == firebase::kLogLevelAssert) std::abort();
    }, nullptr);

    auto manager = std::make_unique<FirebaseManager>();
    if (!manager->initialized()) return 2;
    auto *database = firebase::database::Database::GetInstance(firebase::App::GetInstance(),
        "https://familyapp-a9a07-default-rtdb.firebaseio.com");
    if (!database) return 2;
    // Exercise the persistent SDK view path that reproduced the original assert.
    database->set_persistence_enabled(true);
    const bool loadingExit = app.arguments().contains("--shutdown-loading");
    bool shuttingDown = false;
    int readyCount = 0, expectedOffline = 0, timeoutRecoveries = 0; // 1 = Retry, 2 = automatic reconnect.
    FirebaseManager::StartupState lastState = FirebaseManager::Loading;
    QSet<quint64> committed;
    auto fail = [&](const QString &reason) {
        qCritical() << "[LIVE FAIL]" << reason;
        app.exit(2);
    };
    QObject::connect(manager.get(), &FirebaseManager::initialDataVerified, &app,
        [&](quint64 attempt, const QVariantList &shopping, const QVariantList &saved) {
            if (committed.contains(attempt)) { fail("Duplicate snapshot commit"); return; }
            committed.insert(attempt);
            qInfo() << "[LIVE] REST-verified commit" << attempt << "shopping" << shopping.size() << "saved" << saved.size();
            // Headless acknowledgment only: no model or cache writes and no SDK writes.
            manager->completeInitialSync(attempt, true);
        });
    QObject::connect(manager.get(), &FirebaseManager::startupChanged, &app, [&]() {
        if (shuttingDown) return; // unique_ptr::reset clears its pointer before invoking the destructor.
        const auto state = manager->startupState();
        qInfo() << "[LIVE state]" << state << "connected" << manager->connected()
                << "children" << manager->childListening() << manager->startupError();
        if (state != FirebaseManager::Ready && manager->editingEnabled()) {
            fail("Editing enabled before Ready"); return;
        }
        const bool transition = state != lastState;
        lastState = state;
        if (!transition) return;
        if (state == FirebaseManager::Ready) {
            if (!manager->editingEnabled() || !manager->childListening()) {
                fail("Ready without active synchronization"); return;
            }
            ++readyCount;
            qInfo() << "[LIVE] Ready cycle" << readyCount;
            manager->startChildListening(); // Deliberate guard probe: must not re-register.
            if (readyCount == 1) {
                QTimer::singleShot(0, &app, [&]() {
                    manager->beginInitialSync(); // Repeated startup with children still active.
                    manager->beginInitialSync(); // Loading duplicate must be harmless.
                });
            } else if (readyCount == 2 || readyCount == 3) {
                expectedOffline = readyCount == 2 ? 1 : 2;
                QTimer::singleShot(0, &app, [&]() { database->GoOffline(); });
            } else if (readyCount == 4) {
                QTimer::singleShot(0, &app, [&]() {
                    qInfo() << "[LIVE] all startup/retry/reconnect checks passed";
                    app.quit();
                });
            } else fail("Unexpected extra Ready cycle");
        } else if (state == FirebaseManager::Error) {
            if (!manager->connected() && expectedOffline) {
                const int mode = expectedOffline;
                expectedOffline = 0;
                QTimer::singleShot(0, &app, [&, mode]() {
                    if (mode == 1) {
                        qInfo() << "[LIVE] explicit Retry while offline; editing must remain disabled";
                        manager->beginInitialSync();
                        manager->beginInitialSync();
                        if (manager->editingEnabled()) { fail("Offline Retry enabled editing"); return; }
                    } else qInfo() << "[LIVE] reconnect without explicit Retry";
                    database->GoOnline();
                });
            } else if (!loadingExit && !manager->connected() && readyCount >= 2
                       && timeoutRecoveries == 0 && manager->startupError().contains("within 20 seconds")) {
                ++timeoutRecoveries;
                qInfo() << "[LIVE] safe reconnect timeout observed; explicit Retry without changing the deadline";
                QTimer::singleShot(0, &app, [&]() {
                    manager->beginInitialSync();
                    database->GoOnline();
                });
            } else if (!loadingExit) fail(manager->startupError());
        }
    });
    QTimer::singleShot(0, &app, [&]() {
        manager->beginInitialSync();
        manager->beginInitialSync();
        if (loadingExit) QTimer::singleShot(0, &app, [&]() {
            if (manager->startupState() != FirebaseManager::Loading) {
                fail("Shutdown-loading scenario did not remain Loading"); return;
            }
            qInfo() << "[LIVE] shutdown with both startup listeners registered";
            app.quit();
        });
    });
    QTimer::singleShot(45000, &app, [&]() { fail("Live observation deadline expired"); });
    int result = app.exec();
    shuttingDown = true;
    manager.reset(); // Logs and exercises actual listener removal and SDK teardown.
    if (!loadingExit && readyCount != 4 && result == 0) result = 2;
    qInfo() << "[LIVE result]" << result << "Ready cycles" << readyCount << "commits" << committed.size()
            << "timeout recoveries" << timeoutRecoveries;
    return result;
}
