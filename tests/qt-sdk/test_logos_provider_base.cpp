#include <gtest/gtest.h>
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_provider_object.h"

// Minimal concrete subclass for testing LogosProviderBase
class TestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "test_provider"; }
    QString providerVersion() const override { return "1.0.0"; }

    bool onInitCalled = false;
    LogosAPI* receivedApi = nullptr;

    QVariant callMethod(const QString& methodName, const QVariantList& args) override
    {
        if (methodName == "echo") {
            return args.isEmpty() ? QVariant() : args[0];
        }
        if (methodName == "add" && args.size() == 2) {
            return QVariant(args[0].toInt() + args[1].toInt());
        }
        return QVariant();
    }

    QJsonArray getMethods() override { return QJsonArray(); }

    // Expose protected emitEvent for testing
    void testEmitEvent(const QString& name, const QVariantList& data) { emitEvent(name, data); }

protected:
    void onInit(LogosAPI* api) override
    {
        onInitCalled = true;
        receivedApi = api;
    }
};

class LogosProviderBaseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_mock = new LogosMockSetup();
    }
    void TearDown() override
    {
        delete m_mock;
    }
    LogosMockSetup* m_mock = nullptr;
};

TEST_F(LogosProviderBaseTest, InitStoresApiAndCallsOnInit)
{
    LogosAPI api("test_module");
    TestProvider provider;

    provider.init(&api);

    EXPECT_TRUE(provider.onInitCalled);
    EXPECT_EQ(provider.receivedApi, &api);
}

TEST_F(LogosProviderBaseTest, ProviderNameAndVersion)
{
    TestProvider provider;
    EXPECT_EQ(provider.providerName(), "test_provider");
    EXPECT_EQ(provider.providerVersion(), "1.0.0");
}

TEST_F(LogosProviderBaseTest, EmitEventCallsListener)
{
    TestProvider provider;

    QString receivedEvent;
    QVariantList receivedData;

    provider.setEventListener([&](const QString& name, const QVariantList& data) {
        receivedEvent = name;
        receivedData = data;
    });

    provider.testEmitEvent("test_event", {QVariant(42)});

    EXPECT_EQ(receivedEvent, "test_event");
    ASSERT_EQ(receivedData.size(), 1);
    EXPECT_EQ(receivedData[0].toInt(), 42);
}

TEST_F(LogosProviderBaseTest, EmitEventWithoutListenerIsNoOp)
{
    TestProvider provider;
    // Should not crash
    provider.testEmitEvent("test_event", {QVariant(1)});
}

TEST_F(LogosProviderBaseTest, SetEventListenerReplacesCallback)
{
    TestProvider provider;

    int callCount1 = 0, callCount2 = 0;
    provider.setEventListener([&](const QString&, const QVariantList&) { callCount1++; });
    provider.testEmitEvent("e", {});
    EXPECT_EQ(callCount1, 1);

    provider.setEventListener([&](const QString&, const QVariantList&) { callCount2++; });
    provider.testEmitEvent("e", {});
    EXPECT_EQ(callCount1, 1); // not called again
    EXPECT_EQ(callCount2, 1);
}

TEST_F(LogosProviderBaseTest, InformModuleTokenSavesViaTokenManager)
{
    LogosAPI api("test_module");
    TestProvider provider;
    provider.init(&api);

    EXPECT_TRUE(provider.informModuleToken("other_mod", "tok123"));
    EXPECT_EQ(TokenManager::instance().getToken("other_mod"), "tok123");
}

TEST_F(LogosProviderBaseTest, InformModuleTokenFailsIfNotInitialized)
{
    TestProvider provider;
    // Not initialized — logosAPI is null
    EXPECT_FALSE(provider.informModuleToken("mod", "tok"));
}

TEST_F(LogosProviderBaseTest, CallMethodDispatch)
{
    TestProvider provider;
    EXPECT_EQ(provider.callMethod("echo", {QVariant("hello")}).toString(), "hello");
    EXPECT_EQ(provider.callMethod("add", {QVariant(3), QVariant(4)}).toInt(), 7);
}
