#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonObject>
#include "logos_mode.h"
#include "logos_object.h"
#include "logos_provider_object.h"
#include "logos_api.h"
#include "token_manager.h"
#include "module_proxy.h"
#include "plugin_registry.h"
#include "local_transport.h"

// Test provider for local transport testing
class LocalTestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "local_test"; }
    QString providerVersion() const override { return "1.0.0"; }

    QVariant callMethod(const QString& methodName, const QVariantList& args) override
    {
        lastMethod = methodName;
        lastArgs = args;
        if (methodName == "add" && args.size() == 2)
            return QVariant(args[0].toInt() + args[1].toInt());
        if (methodName == "echo" && args.size() == 1)
            return args[0];
        return returnValue;
    }

    QJsonArray getMethods() override
    {
        QJsonArray arr;
        QJsonObject m;
        m["name"] = "add";
        arr.append(m);
        QJsonObject m2;
        m2["name"] = "echo";
        arr.append(m2);
        return arr;
    }

    void testEmitEvent(const QString& name, const QVariantList& data) { emitEvent(name, data); }

    QString lastMethod;
    QVariantList lastArgs;
    QVariant returnValue = QVariant(77);
};

class LocalTransportIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Local);
        m_provider = new LocalTestProvider();
        m_proxy = new ModuleProxy(m_provider);
        // Authorize the "auth" token the dispatch tests below present, so they
        // exercise the call path rather than the (now-enforced) authz rejection.
        m_proxy->saveToken("caller", "auth");
    }
    void TearDown() override
    {
        PluginRegistry::unregisterPlugin("local_mod");
        delete m_proxy;
        delete m_provider;
        TokenManager::instance().clearAllTokens();
        LogosModeConfig::setMode(m_savedMode);
    }
    LogosMode m_savedMode;
    LocalTestProvider* m_provider = nullptr;
    ModuleProxy* m_proxy = nullptr;
};

// -- LocalTransportHost --

TEST_F(LocalTransportIntegrationTest, PublishRegistersInPluginRegistry)
{
    LocalTransportHost host;
    EXPECT_TRUE(host.publishObject("local_mod", m_proxy));
    EXPECT_TRUE(PluginRegistry::hasPlugin("local_mod"));
}

TEST_F(LocalTransportIntegrationTest, UnpublishRemovesFromPluginRegistry)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);
    host.unpublishObject("local_mod");
    EXPECT_FALSE(PluginRegistry::hasPlugin("local_mod"));
}

// -- LocalTransportConnection --

TEST_F(LocalTransportIntegrationTest, ConnectionAlwaysConnected)
{
    LocalTransportConnection conn;
    EXPECT_TRUE(conn.isConnected());
    EXPECT_TRUE(conn.connectToHost());
    EXPECT_TRUE(conn.reconnect());
}

TEST_F(LocalTransportIntegrationTest, RequestObjectReturnsNullIfNotPublished)
{
    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("nonexistent", 5000);
    EXPECT_EQ(obj, nullptr);
}

TEST_F(LocalTransportIntegrationTest, RequestObjectReturnsLocalLogosObject)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);
    // Verify id() returns proxy address
    EXPECT_EQ(obj->id(), reinterpret_cast<quintptr>(m_proxy));
    obj->release();
}

// -- LocalLogosObject::callMethod --

TEST_F(LocalTransportIntegrationTest, CallMethodDelegatesToProvider)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QVariant r = obj->callMethod("auth", "add", {QVariant(3), QVariant(7)}, 5000);
    EXPECT_EQ(r.toInt(), 10);
    EXPECT_EQ(m_provider->lastMethod, "add");
    obj->release();
}

TEST_F(LocalTransportIntegrationTest, CallMethodEchoArg)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QVariant r = obj->callMethod("auth", "echo", {QVariant("hello")}, 5000);
    EXPECT_EQ(r.toString(), "hello");
    obj->release();
}

TEST_F(LocalTransportIntegrationTest, CallMethodDefaultReturn)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QVariant r = obj->callMethod("auth", "unknown", {}, 5000);
    EXPECT_EQ(r.toInt(), 77);
    obj->release();
}

// -- LocalLogosObject::getMethods --

TEST_F(LocalTransportIntegrationTest, GetMethodsDelegatesToProvider)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QJsonArray methods = obj->getMethods();
    EXPECT_EQ(methods.size(), 2);
    obj->release();
}

// -- LocalLogosObject::informModuleToken --

TEST_F(LocalTransportIntegrationTest, InformModuleTokenDelegatesToProvider)
{
    LogosAPI api("local_origin");
    m_provider->init(&api);

    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    // informModuleToken is privileged (F-002): only the trusted
    // core/capability_module channel may plant tokens. Seed the module's auth
    // secret (as the host does at init) and present it, so this drives the
    // delegation path through the transport rather than hitting the authz
    // rejection.
    TokenManager::instance().saveToken("capability_module", "trusted-secret");

    bool result = obj->informModuleToken("trusted-secret", "target_mod", "secret_tok", 5000);
    EXPECT_TRUE(result);
    EXPECT_EQ(TokenManager::instance().getToken("target_mod"), "secret_tok");
    obj->release();
}

// -- LocalLogosObject::onEvent (event subscription and delivery) --

TEST_F(LocalTransportIntegrationTest, OnEventReceivesProviderEvents)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QString receivedEvent;
    QVariantList receivedData;
    obj->onEvent("my_event", [&](const QString& name, const QVariantList& data) {
        receivedEvent = name;
        receivedData = data;
    });

    // Provider emits event -> ModuleProxy signal -> EventHelper -> callback
    m_provider->testEmitEvent("my_event", {QVariant(42)});

    EXPECT_EQ(receivedEvent, "my_event");
    ASSERT_EQ(receivedData.size(), 1);
    EXPECT_EQ(receivedData[0].toInt(), 42);
    obj->release();
}

TEST_F(LocalTransportIntegrationTest, OnEventMultipleCallbacksSameEvent)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int count1 = 0, count2 = 0;
    obj->onEvent("evt", [&](const QString&, const QVariantList&) { count1++; });
    obj->onEvent("evt", [&](const QString&, const QVariantList&) { count2++; });

    m_provider->testEmitEvent("evt", {});

    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
    obj->release();
}

TEST_F(LocalTransportIntegrationTest, OnEventDifferentEventsRouteCorrectly)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int countA = 0, countB = 0;
    obj->onEvent("eventA", [&](const QString&, const QVariantList&) { countA++; });
    obj->onEvent("eventB", [&](const QString&, const QVariantList&) { countB++; });

    m_provider->testEmitEvent("eventA", {});
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 0);

    m_provider->testEmitEvent("eventB", {});
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 1);
    obj->release();
}

// -- LocalLogosObject::disconnectEvents --

TEST_F(LocalTransportIntegrationTest, DisconnectEventsStopsDelivery)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int count = 0;
    obj->onEvent("evt", [&](const QString&, const QVariantList&) { count++; });

    m_provider->testEmitEvent("evt", {});
    EXPECT_EQ(count, 1);

    obj->disconnectEvents();
    m_provider->testEmitEvent("evt", {});
    EXPECT_EQ(count, 1); // no increment after disconnect
    obj->release();
}

// -- LocalLogosObject::release --

TEST_F(LocalTransportIntegrationTest, ReleaseDisconnectsEvents)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int count = 0;
    obj->onEvent("evt", [&](const QString&, const QVariantList&) { count++; });

    m_provider->testEmitEvent("evt", {});
    EXPECT_EQ(count, 1);

    obj->release();

    m_provider->testEmitEvent("evt", {});
    EXPECT_EQ(count, 1); // no delivery after release
}

// -- getPluginMethods special case through LocalLogosObject --

TEST_F(LocalTransportIntegrationTest, CallGetPluginMethodsSpecialCase)
{
    LocalTransportHost host;
    host.publishObject("local_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("local_mod", 5000);
    ASSERT_NE(obj, nullptr);

    // callRemoteMethod with "getPluginMethods" is special-cased in ModuleProxy
    QVariant r = obj->callMethod("auth", "getPluginMethods", {}, 5000);
    EXPECT_TRUE(r.toJsonArray().size() > 0);
    // Provider's callMethod should NOT have been called
    EXPECT_TRUE(m_provider->lastMethod.isEmpty());
    obj->release();
}
