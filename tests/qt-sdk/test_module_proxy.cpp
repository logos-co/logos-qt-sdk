#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QJsonObject>
#include "event_test_helpers.h"
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_provider_object.h"
#include "module_proxy.h"

// Since logos-protocol#61 the proxy ADDITIVELY advertises name()/version() for
// a provider that does not list them itself, so an interface is the provider's
// own entries PLUS that identity pair. Assert membership by name rather than a
// bare count: a bare count turns the next additive change into a mystery
// number, which is exactly how these four assertions went stale.
static bool hasNamed(const QJsonArray& arr, const QString& name)
{
    for (const QJsonValue& v : arr)
        if (v.toObject().value("name").toString() == name) return true;
    return false;
}

// Minimal provider for proxy testing
class ProxyTestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "proxy_test"; }
    QString providerVersion() const override { return "1.0.0"; }

    QVariant callMethod(const QString& methodName, const QVariantList& args) override
    {
        lastMethodCalled = methodName;
        lastArgs = args;
        return returnValue;
    }

    // getMethods() returns the whole interface: methods AND events, each tagged
    // with a "type". The proxy slices it into getPluginMethods/Events/Interface.
    QJsonArray getMethods() override
    {
        QJsonArray arr;
        {
            QJsonObject m;
            m["type"] = "method";
            m["name"] = "testMethod";
            arr.append(m);
        }
        {
            QJsonObject e;
            e["type"] = "event";
            e["name"] = "testEvent";
            arr.append(e);
        }
        return arr;
    }

    // Expose protected emitEvent for testing
    void testEmitEvent(const QString& name, const QVariantList& data) { emitEvent(name, data); }

    QString lastMethodCalled;
    QVariantList lastArgs;
    QVariant returnValue = QVariant(99);
};

class ModuleProxyTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_mock = new LogosMockSetup();
        m_provider = new ProxyTestProvider();
    }
    void TearDown() override
    {
        delete m_provider;
        delete m_mock;
    }
    LogosMockSetup* m_mock = nullptr;
    ProxyTestProvider* m_provider = nullptr;
};

TEST_F(ModuleProxyTest, CallRemoteMethodDispatchesToProvider)
{
    ModuleProxy proxy(m_provider);
    proxy.saveToken("caller", "token"); // authorize the caller's token
    QVariant r = proxy.callRemoteMethod("token", "myMethod", {QVariant(1)});
    EXPECT_EQ(r.toInt(), 99);
    EXPECT_EQ(m_provider->lastMethodCalled, "myMethod");
    EXPECT_EQ(m_provider->lastArgs.size(), 1);
}

TEST_F(ModuleProxyTest, GetPluginMethodsDispatchesToProvider)
{
    ModuleProxy proxy(m_provider);
    // getPluginMethods() returns the method-typed entries only — the event the
    // provider also reports through getMethods() is filtered out.
    QJsonArray methods = proxy.getPluginMethods();
    EXPECT_TRUE(hasNamed(methods, "testMethod"));   // the provider's own
    EXPECT_TRUE(hasNamed(methods, "name"));         // injected identity pair
    EXPECT_TRUE(hasNamed(methods, "version"));
    EXPECT_FALSE(hasNamed(methods, "testEvent"));   // events still filtered out
    EXPECT_EQ(methods.size(), 3);
}

TEST_F(ModuleProxyTest, GetPluginEventsReturnsOnlyEvents)
{
    ModuleProxy proxy(m_provider);
    QJsonArray events = proxy.getPluginEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].toObject()["name"].toString(), "testEvent");
}

TEST_F(ModuleProxyTest, GetPluginInterfaceReturnsMethodsAndEvents)
{
    ModuleProxy proxy(m_provider);
    // The whole interface — the provider's method and event, plus the
    // injected name()/version() identity pair.
    QJsonArray iface = proxy.getPluginInterface();
    EXPECT_TRUE(hasNamed(iface, "testMethod"));
    EXPECT_TRUE(hasNamed(iface, "testEvent"));
    EXPECT_TRUE(hasNamed(iface, "name"));
    EXPECT_TRUE(hasNamed(iface, "version"));
    EXPECT_EQ(iface.size(), 4);
}

TEST_F(ModuleProxyTest, GetPluginMethodsSpecialCaseInCallRemoteMethod)
{
    ModuleProxy proxy(m_provider);
    QVariant r = proxy.callRemoteMethod("token", "getPluginMethods");
    // Should return the methods array as QVariant, not dispatch to provider's callMethod
    EXPECT_TRUE(r.toJsonArray().size() > 0);
    // Provider's callMethod should NOT have been called for "getPluginMethods"
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty());
}

TEST_F(ModuleProxyTest, GetPluginEventsAndInterfaceSpecialCaseInCallRemoteMethod)
{
    ModuleProxy proxy(m_provider);

    QVariant ev = proxy.callRemoteMethod("token", "getPluginEvents");
    EXPECT_EQ(ev.toJsonArray().size(), 1);

    QVariant iface = proxy.callRemoteMethod("token", "getPluginInterface");
    // provider's method + event, plus the injected name()/version() pair.
    EXPECT_EQ(iface.toJsonArray().size(), 4);

    // Both are intercepted by the proxy, never dispatched to the provider.
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty());
}

TEST_F(ModuleProxyTest, NullProviderHandling)
{
    ModuleProxy proxy(nullptr);
    QVariant r = proxy.callRemoteMethod("token", "fn");
    EXPECT_FALSE(r.isValid());
    EXPECT_EQ(proxy.getPluginMethods().size(), 0);
}

TEST_F(ModuleProxyTest, EmptyMethodNameHandling)
{
    ModuleProxy proxy(m_provider);
    QVariant r = proxy.callRemoteMethod("token", "");
    EXPECT_FALSE(r.isValid());
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty());
}

TEST_F(ModuleProxyTest, SaveTokenValidation)
{
    ModuleProxy proxy(m_provider);

    EXPECT_TRUE(proxy.saveToken("mod", "tok"));
    EXPECT_FALSE(proxy.saveToken("", "tok")); // empty module name
    EXPECT_FALSE(proxy.saveToken("mod", "")); // empty token
}

TEST_F(ModuleProxyTest, EventForwarding)
{
    ModuleProxy proxy(m_provider);
    QSignalSpy spy(&proxy, &ModuleProxy::eventResponse);

    // The proxy sets up event listener on construction.
    // When provider emits event, proxy should emit signal.
    m_provider->testEmitEvent("my_event", {QVariant("data")});

    ASSERT_TRUE(waitForEvent([&] { return spy.count() == 1; }));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "my_event");
    QVariantList data = spy.at(0).at(1).value<QVariantList>();
    ASSERT_EQ(data.size(), 1);
    EXPECT_EQ(data[0].toString(), "data");
}

TEST_F(ModuleProxyTest, InformModuleTokenDelegatesToProvider)
{
    LogosAPI api("origin");
    m_provider->init(&api);
    ModuleProxy proxy(m_provider);

    // informModuleToken is privileged: only the trusted core/capability_module
    // channel may plant tokens (F-002). The host seeds the module's own auth
    // secret under "core"/"capability_module" at init; the legitimate caller
    // presents that secret. Seed it and present it so this exercises the
    // delegation path rather than the (now-enforced) authz rejection.
    TokenManager::instance().clearAllTokens();
    TokenManager::instance().saveToken("capability_module", "trusted-secret");

    bool result = proxy.informModuleToken("trusted-secret", "target_mod", "tok123");
    EXPECT_TRUE(result);
    // The provider's informModuleToken files the caller's token INBOUND.
    EXPECT_EQ(TokenManager::instance().inbound().token("target_mod"), "tok123");

    // Don't leak the seeded secret into sibling tests sharing the singleton.
    TokenManager::instance().clearAllTokens();
}
