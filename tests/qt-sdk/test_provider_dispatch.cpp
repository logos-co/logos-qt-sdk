#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonObject>
#include <QtTest/QSignalSpy>
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_api_provider.h"
#include "logos_object.h"
#include "module_proxy.h"
#include "plugin_registry.h"
#include "local_transport.h"
#include "fixtures/sample_provider.h"

/**
 * Tests the LOGOS_PROVIDER + LOGOS_METHOD code generation pipeline end-to-end.
 *
 * SampleProvider uses the LOGOS_PROVIDER macro (which declares callMethod/getMethods
 * but doesn't implement them). The implementation is in the generated
 * logos_provider_dispatch.cpp, produced by running the actual logos-cpp-generator
 * with --provider-header at build time.
 *
 * If these tests break, it means a change to the SDK or generator broke the
 * contract that real modules (like logos-package-manager-module) depend on.
 */
class ProviderDispatchTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_provider = new SampleProvider();
    }
    void TearDown() override
    {
        delete m_provider;
    }
    SampleProvider* m_provider = nullptr;
};

// -- callMethod dispatch for various signatures --

TEST_F(ProviderDispatchTest, EchoStringArg)
{
    QVariant r = m_provider->callMethod("echo", {QVariant("hello world")});
    EXPECT_EQ(r.toString(), "hello world");
    EXPECT_EQ(m_provider->lastEcho, "hello world");
}

TEST_F(ProviderDispatchTest, InstallPluginBoolArg)
{
    QVariant r = m_provider->callMethod("installPlugin", {QVariant("/path/to/plugin"), QVariant(true)});
    EXPECT_TRUE(r.toBool());
    EXPECT_EQ(m_provider->lastPluginPath, "/path/to/plugin");
    EXPECT_TRUE(m_provider->lastSkipFlag);
}

TEST_F(ProviderDispatchTest, InstallPluginBoolFalse)
{
    m_provider->callMethod("installPlugin", {QVariant("/other"), QVariant(false)});
    EXPECT_FALSE(m_provider->lastSkipFlag);
}

TEST_F(ProviderDispatchTest, GetPackagesNoArgs)
{
    QVariant r = m_provider->callMethod("getPackages", {});
    QJsonArray arr = r.toJsonArray();
    EXPECT_EQ(arr.size(), 2);
    EXPECT_EQ(arr[0].toObject()["name"].toString(), "pkg_a");
}

TEST_F(ProviderDispatchTest, GetPackagesOverloadWithCategory)
{
    QVariant r = m_provider->callMethod("getPackages", {QVariant("network")});
    QJsonArray arr = r.toJsonArray();
    EXPECT_EQ(arr.size(), 1);
    EXPECT_EQ(arr[0].toObject()["category"].toString(), "network");
    EXPECT_EQ(m_provider->lastCategory, "network");
}

TEST_F(ProviderDispatchTest, GetCategories)
{
    QVariant r = m_provider->callMethod("getCategories", {});
    QStringList cats = r.toStringList();
    EXPECT_EQ(cats.size(), 3);
    EXPECT_TRUE(cats.contains("network"));
    EXPECT_TRUE(cats.contains("storage"));
    EXPECT_TRUE(cats.contains("ui"));
}

TEST_F(ProviderDispatchTest, ResolveDependencies)
{
    QStringList pkgs = {"pkg_a", "pkg_b"};
    QVariant r = m_provider->callMethod("resolveDependencies", {QVariant(pkgs)});
    QStringList resolved = r.toStringList();
    EXPECT_EQ(resolved.size(), 3);
    EXPECT_TRUE(resolved.contains("common_dep"));
    EXPECT_EQ(m_provider->lastDependencies, pkgs);
}

TEST_F(ProviderDispatchTest, AddIntArgs)
{
    QVariant r = m_provider->callMethod("add", {QVariant(10), QVariant(20)});
    EXPECT_EQ(r.toInt(), 30);
    EXPECT_EQ(m_provider->lastA, 10);
    EXPECT_EQ(m_provider->lastB, 20);
}

TEST_F(ProviderDispatchTest, SetDirectoryVoidReturn)
{
    QVariant r = m_provider->callMethod("setDirectory", {QVariant("/tmp/plugins")});
    // void methods return QVariant(true) per generator convention
    EXPECT_TRUE(r.toBool());
    EXPECT_EQ(m_provider->lastDirectory, "/tmp/plugins");
}

TEST_F(ProviderDispatchTest, TestEventVoidReturnEmitsEvent)
{
    QString receivedEvent;
    QVariantList receivedData;
    m_provider->setEventListener([&](const QString& name, const QVariantList& data) {
        receivedEvent = name;
        receivedData = data;
    });

    QVariant r = m_provider->callMethod("testEvent", {QVariant("fired!")});
    EXPECT_TRUE(r.toBool());
    EXPECT_EQ(m_provider->lastEventMessage, "fired!");
    EXPECT_EQ(receivedEvent, "testEvent");
    ASSERT_EQ(receivedData.size(), 1);
    EXPECT_EQ(receivedData[0].toString(), "fired!");
}

TEST_F(ProviderDispatchTest, UnknownMethodReturnsInvalid)
{
    QVariant r = m_provider->callMethod("nonExistentMethod", {});
    EXPECT_FALSE(r.isValid());
}

// -- getMethods() metadata --

TEST_F(ProviderDispatchTest, GetMethodsReturnsAllMethods)
{
    QJsonArray methods = m_provider->getMethods();
    // Should have all LOGOS_METHOD entries (including overloaded getPackages)
    EXPECT_GE(methods.size(), 9);

    // Verify some method metadata
    QSet<QString> names;
    for (const QJsonValue& v : methods) {
        names.insert(v.toObject()["name"].toString());
    }
    EXPECT_TRUE(names.contains("echo"));
    EXPECT_TRUE(names.contains("installPlugin"));
    EXPECT_TRUE(names.contains("getPackages"));
    EXPECT_TRUE(names.contains("getCategories"));
    EXPECT_TRUE(names.contains("resolveDependencies"));
    EXPECT_TRUE(names.contains("add"));
    EXPECT_TRUE(names.contains("setDirectory"));
    EXPECT_TRUE(names.contains("testEvent"));
}

TEST_F(ProviderDispatchTest, GetMethodsContainsParameterInfo)
{
    QJsonArray methods = m_provider->getMethods();
    // Find "installPlugin" and verify parameters
    for (const QJsonValue& v : methods) {
        QJsonObject m = v.toObject();
        if (m["name"].toString() == "installPlugin") {
            EXPECT_TRUE(m.contains("parameters"));
            QJsonArray params = m["parameters"].toArray();
            EXPECT_EQ(params.size(), 2);
            EXPECT_EQ(params[0].toObject()["type"].toString(), "QString");
            EXPECT_EQ(params[1].toObject()["type"].toString(), "bool");
            return;
        }
    }
    FAIL() << "installPlugin not found in getMethods()";
}

TEST_F(ProviderDispatchTest, GetMethodsReturnTypes)
{
    QJsonArray methods = m_provider->getMethods();
    for (const QJsonValue& v : methods) {
        QJsonObject m = v.toObject();
        if (m["name"].toString() == "echo") {
            EXPECT_EQ(m["returnType"].toString(), "QString");
        }
        if (m["name"].toString() == "setDirectory") {
            EXPECT_EQ(m["returnType"].toString(), "void");
        }
        if (m["name"].toString() == "add") {
            EXPECT_EQ(m["returnType"].toString(), "int");
        }
    }
}

// -- LOGOS_PROVIDER macro basics --

TEST_F(ProviderDispatchTest, ProviderNameFromMacro)
{
    EXPECT_EQ(m_provider->providerName(), "sample_provider");
}

TEST_F(ProviderDispatchTest, ProviderVersionFromMacro)
{
    EXPECT_EQ(m_provider->providerVersion(), "2.0.0");
}

// -- Full stack: SampleProvider -> ModuleProxy -> LocalLogosObject -> callMethod --

class ProviderDispatchLocalStackTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Local);
        TokenManager::instance().clearAllTokens();
        m_provider = new SampleProvider();
        m_proxy = new ModuleProxy(m_provider);
        // Authorize the "auth" token the full-stack tests present.
        m_proxy->saveToken("caller", "auth");
    }
    void TearDown() override
    {
        PluginRegistry::unregisterPlugin("sample_mod");
        delete m_proxy;
        delete m_provider;
        LogosModeConfig::setMode(m_savedMode);
    }
    LogosMode m_savedMode;
    SampleProvider* m_provider = nullptr;
    ModuleProxy* m_proxy = nullptr;
};

TEST_F(ProviderDispatchLocalStackTest, CallThroughLocalTransport)
{
    LocalTransportHost host;
    host.publishObject("sample_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("sample_mod", 5000);
    ASSERT_NE(obj, nullptr);

    // Echo through full stack
    QVariant r = obj->callMethod("auth", "echo", {QVariant("stack test")}, 5000);
    EXPECT_EQ(r.toString(), "stack test");

    // Add through full stack
    r = obj->callMethod("auth", "add", {QVariant(100), QVariant(200)}, 5000);
    EXPECT_EQ(r.toInt(), 300);

    // Overloaded getPackages(category) through full stack
    r = obj->callMethod("auth", "getPackages", {QVariant("ui")}, 5000);
    QJsonArray arr = r.toJsonArray();
    EXPECT_EQ(arr.size(), 1);
    EXPECT_EQ(arr[0].toObject()["category"].toString(), "ui");

    obj->release();
}

TEST_F(ProviderDispatchLocalStackTest, EventsThroughFullStack)
{
    LocalTransportHost host;
    host.publishObject("sample_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("sample_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QString receivedEvent;
    QVariantList receivedData;
    obj->onEvent("testEvent", [&](const QString& name, const QVariantList& data) {
        receivedEvent = name;
        receivedData = data;
    });

    // Call testEvent which emits an event internally
    obj->callMethod("auth", "testEvent", {QVariant("hello events")}, 5000);

    EXPECT_EQ(receivedEvent, "testEvent");
    ASSERT_EQ(receivedData.size(), 1);
    EXPECT_EQ(receivedData[0].toString(), "hello events");

    obj->release();
}

TEST_F(ProviderDispatchLocalStackTest, GetMethodsThroughFullStack)
{
    LocalTransportHost host;
    host.publishObject("sample_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("sample_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QJsonArray methods = obj->getMethods();
    EXPECT_GE(methods.size(), 9);

    obj->release();
}

// -- Full stack with LogosAPI: Provider registration + Client call --

class ProviderDispatchApiStackTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Local);
        TokenManager::instance().clearAllTokens();
    }
    void TearDown() override
    {
        PluginRegistry::unregisterPlugin("sample_mod");
        LogosModeConfig::setMode(m_savedMode);
    }
    LogosMode m_savedMode;
};

TEST_F(ProviderDispatchApiStackTest, RegisterProviderAndCallViaClient)
{
    // Provider side: register a SampleProvider
    LogosAPI providerApi("sample_mod");
    auto* provider = new SampleProvider();
    bool registered = providerApi.getProvider()->registerObject("sample_mod", static_cast<LogosProviderObject*>(provider));
    EXPECT_TRUE(registered);

    // Client side: call methods via LogosAPIClient
    // Seed token so client skips capability_module lookup
    TokenManager::instance().saveToken("sample_mod", "test-tok");

    LogosAPI clientApi("client_mod");
    LogosAPIClient* client = clientApi.getClient("sample_mod");

    QVariant r = client->invokeRemoteMethod("sample_mod", "echo", QVariant("api stack"));
    EXPECT_EQ(r.toString(), "api stack");

    r = client->invokeRemoteMethod("sample_mod", "add", QVariant(5), QVariant(7));
    EXPECT_EQ(r.toInt(), 12);

    r = client->invokeRemoteMethod("sample_mod", "getCategories");
    QStringList cats = r.toStringList();
    EXPECT_EQ(cats.size(), 3);
}

TEST_F(ProviderDispatchApiStackTest, AsyncCallThroughFullApi)
{
    LogosAPI providerApi("sample_mod");
    auto* provider = new SampleProvider();
    providerApi.getProvider()->registerObject("sample_mod", static_cast<LogosProviderObject*>(provider));

    TokenManager::instance().saveToken("sample_mod", "test-tok");
    LogosAPI clientApi("client_mod");
    LogosAPIClient* client = clientApi.getClient("sample_mod");

    bool called = false;
    QVariant received;
    client->invokeRemoteMethodAsync("sample_mod", "echo", QVariant("async test"),
        [&](QVariant v) { called = true; received = v; });

    QCoreApplication::processEvents();

    EXPECT_TRUE(called);
    EXPECT_EQ(received.toString(), "async test");
}
