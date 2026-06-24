#ifndef LOGOS_QT_SDK_EVENT_TEST_HELPERS_H
#define LOGOS_QT_SDK_EVENT_TEST_HELPERS_H

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

template <typename Pred>
inline bool waitForEvent(Pred pred, int timeoutMs = 1000)
{
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

inline void drainEvents(int forMs = 100)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < forMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

#endif // LOGOS_QT_SDK_EVENT_TEST_HELPERS_H
