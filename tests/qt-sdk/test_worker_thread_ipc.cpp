// Regression test for worker-thread inter-module calls.
//
// Logos inter-module calls go over Qt Remote Objects, whose replicas only work
// on the thread that owns them — the module's main/event-loop thread. A module
// that calls another module from a WORKER thread (e.g. an embedded HTTP server
// serving /metrics) must therefore have that call executed on its owner thread.
//
// Before the fix, LogosAPIClient::invokeRemoteMethod ran on the *calling*
// thread. Over the remote transport that hangs on replica acquisition (no event
// loop on the worker thread). Here we use the in-process *local* transport,
// where the same root cause is observable deterministically: without the fix
// the provider runs on the worker thread; with the fix the SDK marshals the
// call onto the owner thread, so the provider runs there.
//
// This test FAILS without the marshaling change and PASSES with it.

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_api_provider.h"
#include "logos_mode.h"
#include "logos_provider_object.h"
#include "plugin_registry.h"
#include "token_manager.h"

namespace {

// Provider whose method records the thread it executed on.
class ThreadProbeProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "thread_probe"; }
    QString providerVersion() const override { return "1.0.0"; }

    QVariant callMethod(const QString& methodName, const QVariantList&) override
    {
        if (methodName == "whichThread") {
            calledThread.store(QThread::currentThread());
            return QVariant(QStringLiteral("ok"));
        }
        return QVariant();
    }

    QJsonArray getMethods() override { return QJsonArray(); }

    std::atomic<QThread*> calledThread{nullptr};
};

class WorkerThreadIpcTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Local);
        TokenManager::instance().clearAllTokens();
    }
    void TearDown() override
    {
        PluginRegistry::unregisterPlugin("thread_probe");
        TokenManager::instance().clearAllTokens();
        LogosModeConfig::setMode(m_savedMode);
    }
    LogosMode m_savedMode;
};

}  // namespace

TEST_F(WorkerThreadIpcTest, InvokeFromWorkerThreadRunsOnOwnerThread)
{
    QThread* const ownerThread = QThread::currentThread();

    // The ModuleProxy created by registerObject (owned by providerApi) keeps a
    // raw pointer to the provider while published, so the provider must outlive
    // it. Declaring `provider` before `providerApi` ensures that: at end of
    // scope, providerApi (and its proxy) is destroyed first, then the provider.
    ThreadProbeProvider provider;

    // Provider registered on the owner thread.
    LogosAPI providerApi("thread_probe");
    ASSERT_TRUE(providerApi.getProvider()->registerObject("thread_probe", &provider));
    // Authorize the token the consumer will present, so the call reaches the
    // provider instead of being rejected by the authz check.
    providerApi.getProvider()->saveToken("caller", "tok");

    // Consumer on the owner thread. Pre-seed the token so invokeRemoteMethod
    // skips the capability_module token dance and calls the provider directly.
    LogosAPI consumerApi("caller");
    TokenManager::instance().saveToken(QString("thread_probe"), QString("tok"));
    LogosAPIClient* client = consumerApi.getClient("thread_probe");
    ASSERT_NE(client, nullptr);

    std::atomic<bool> done{false};
    std::atomic<QThread*> workerThread{nullptr};
    QVariant result;

    // Call from a worker thread, exactly as an HTTP server thread would.
    std::thread worker([&]() {
        workerThread.store(QThread::currentThread());
        result = client->invokeRemoteMethod("thread_probe", "whichThread", QVariantList());
        done.store(true);
    });

    // Pump the owner thread's event loop so the fix's BlockingQueuedConnection
    // can run the call here. Bounded so a regression fails the assertion below
    // instead of hanging forever.
    for (int i = 0; i < 250 && !done.load(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    worker.join();

    ASSERT_TRUE(done.load()) << "worker-thread invokeRemoteMethod did not complete";
    EXPECT_NE(workerThread.load(), ownerThread) << "sanity: worker ran on a different thread";
    EXPECT_EQ(result.toString(), "ok") << "the call did not reach the provider";

    // The crux: the inter-module call must execute on the owner thread, not the
    // worker thread. Without the marshaling fix it runs on the worker thread.
    EXPECT_EQ(provider.calledThread.load(), ownerThread)
        << "inter-module call executed on the worker thread instead of the owner thread";
}
