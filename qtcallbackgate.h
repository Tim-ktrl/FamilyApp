#pragma once

#include <QObject>
#include <QMetaObject>
#include <functional>
#include <mutex>
#include <utility>

// Shared by SDK callbacks. Invalidate before the receiver starts teardown.
class QtCallbackGate
{
public:
    explicit QtCallbackGate(QObject *receiver) : m_receiver(receiver) {}
    void post(std::function<void()> callback)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_receiver)
            QMetaObject::invokeMethod(m_receiver, std::move(callback), Qt::QueuedConnection);
    }
    void invalidate()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_receiver = nullptr;
    }
private:
    std::mutex m_mutex;
    QObject *m_receiver;
};
