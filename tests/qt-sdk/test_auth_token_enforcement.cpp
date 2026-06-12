// Security regression tests for the provider-side auth-token enforcement.
//
// These pin down two findings:
//
//   F-001  ModuleProxy::callRemoteMethod accepted authToken but never validated
//          it — any peer could dispatch any method with an empty/garbage token.
//
//   F-002  QtProviderObject::callMethod (the legacy QObject dispatch sink, always
//          reached *through* a ModuleProxy) carried a comment claiming auth-token
//          validation but performed none. Enforcing centrally in ModuleProxy
//          closes this path too.
//
// ModuleProxy::callRemoteMethod is the single provider-side dispatch sink for
// every transport (Local and plain/TCP both invoke it), so enforcement there is
// the choke point that authorizes both new-API (LogosProviderBase) providers and
// legacy QObject plugins wrapped by QtProviderObject.

#include <gtest/gtest.h>
#include <QJsonObject>
#include "logos_api.h"
#include "logos_provider_object.h"
#include "qt_provider_object.h"
#include "module_proxy.h"
#include "token_manager.h"
#include "logos_mode.h"
#include "logos_object.h"
#include "plugin_registry.h"
#include "local_transport.h"
#include "../../core/interface.h"

// ── New-API provider (LogosProviderBase) ────────────────────────────────────
class AuthTestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "auth_test"; }
    QString providerVersion() const override { return "1.0.0"; }

    QVariant callMethod(const QString& methodName, const QVariantList& args) override
    {
        lastMethodCalled = methodName;
        lastArgs = args;
        return QVariant(QStringLiteral("dispatched"));
    }

    QJsonArray getMethods() override
    {
        QJsonArray arr;
        QJsonObject m;
        m["type"] = "method";
        m["name"] = "privilegedMethod";
        arr.append(m);
        return arr;
    }

    QString lastMethodCalled;
    QVariantList lastArgs;
};

// ── Legacy QObject plugin wrapped by QtProviderObject (F-002 path) ───────────
class LegacyQObjectPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
public:
    QString name() const override { return "legacy_mod"; }
    QString version() const override { return "1.0.0"; }

    Q_INVOKABLE QString deleteAllData(const QString& arg)
    {
        privilegedCalled = true;
        return QStringLiteral("deleted:") + arg;
    }

    bool privilegedCalled = false;
};

class AuthTokenEnforcementTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        TokenManager::instance().clearAllTokens();
        m_provider = new AuthTestProvider();
    }
    void TearDown() override
    {
        delete m_provider;
        TokenManager::instance().clearAllTokens();
    }
    AuthTestProvider* m_provider = nullptr;
};

// ── F-001: dispatch must reject unauthorized tokens ─────────────────────────

TEST_F(AuthTokenEnforcementTest, RejectsGarbageTokenForNewApiProvider)
{
    ModuleProxy proxy(m_provider);

    // No token was ever issued to any caller. A peer supplies a garbage token.
    QVariant r = proxy.callRemoteMethod("garbage-token", "privilegedMethod", {QVariant(1)});

    EXPECT_FALSE(r.isValid()) << "unauthorized call must not return a dispatched result";
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty())
        << "provider method must NOT be dispatched without a valid token (F-001)";
}

TEST_F(AuthTokenEnforcementTest, RejectsEmptyToken)
{
    ModuleProxy proxy(m_provider);

    QVariant r = proxy.callRemoteMethod("", "privilegedMethod", {QVariant(1)});

    EXPECT_FALSE(r.isValid());
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty())
        << "empty token must be rejected (fail-closed)";
}

// ── F-001: a token actually issued to a caller is accepted ──────────────────

TEST_F(AuthTokenEnforcementTest, AcceptsTokenIssuedViaTokenManager)
{
    ModuleProxy proxy(m_provider);

    // The capability flow stores the issued token in TokenManager keyed by the
    // requesting module (informModuleToken -> TokenManager::saveToken).
    TokenManager::instance().saveToken("caller_mod", "issued-tok");

    QVariant r = proxy.callRemoteMethod("issued-tok", "privilegedMethod", {QVariant(1)});

    EXPECT_EQ(r.toString(), "dispatched");
    EXPECT_EQ(m_provider->lastMethodCalled, "privilegedMethod");
}

TEST_F(AuthTokenEnforcementTest, AcceptsTokenIssuedViaLegacySaveToken)
{
    ModuleProxy proxy(m_provider);

    // Legacy provider-side token store (LogosAPIProvider::saveToken -> ModuleProxy::saveToken).
    ASSERT_TRUE(proxy.saveToken("caller_mod", "legacy-tok"));

    QVariant r = proxy.callRemoteMethod("legacy-tok", "privilegedMethod", {QVariant(1)});

    EXPECT_EQ(r.toString(), "dispatched");
    EXPECT_EQ(m_provider->lastMethodCalled, "privilegedMethod");
}

TEST_F(AuthTokenEnforcementTest, RejectsTokenNotMatchingAnyIssued)
{
    ModuleProxy proxy(m_provider);

    // A valid token exists for some caller, but the peer presents a different one.
    TokenManager::instance().saveToken("caller_mod", "issued-tok");

    QVariant r = proxy.callRemoteMethod("some-other-token", "privilegedMethod", {QVariant(1)});

    EXPECT_FALSE(r.isValid());
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty())
        << "a token never issued by this module must not be honoured (replay/forge)";
}

// The core discriminator, on a SINGLE issued-token state: a wrong token is
// blocked AND never reaches the provider, while the correct token dispatches.
// This is the exact behaviour the dropped guard used to provide; if the guard
// is removed again the first half of this test fails (the wrong token goes
// through), which is precisely the F-001 regression.
TEST_F(AuthTokenEnforcementTest, WrongTokenBlockedCorrectTokenAllowed)
{
    ModuleProxy proxy(m_provider);
    TokenManager::instance().saveToken("caller_mod", "the-real-token");

    // 1) Wrong token: must NOT dispatch, must NOT return a result.
    QVariant wrong = proxy.callRemoteMethod("not-the-token", "privilegedMethod", {QVariant(1)});
    EXPECT_FALSE(wrong.isValid()) << "wrong token must not produce a result";
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty())
        << "wrong token must not reach the provider (the F-001 guard)";
    EXPECT_TRUE(m_provider->lastArgs.isEmpty())
        << "wrong token must not forward args to the provider";

    // 2) Correct token, same state: must dispatch through to the provider.
    QVariant ok = proxy.callRemoteMethod("the-real-token", "privilegedMethod", {QVariant(7)});
    EXPECT_EQ(ok.toString(), "dispatched");
    EXPECT_EQ(m_provider->lastMethodCalled, "privilegedMethod");
    ASSERT_EQ(m_provider->lastArgs.size(), 1);
    EXPECT_EQ(m_provider->lastArgs[0].toInt(), 7);
}

// ── Introspection stays reachable (used for interface discovery during the
//    handshake, before a token exists; also exposed via a separate ungated
//    transport channel). Gating it here would be false security and would break
//    bootstrap, so the special-cases must remain ungated. ─────────────────────

TEST_F(AuthTokenEnforcementTest, IntrospectionRemainsReachableWithoutToken)
{
    ModuleProxy proxy(m_provider);

    QVariant methods = proxy.callRemoteMethod("garbage-token", "getPluginMethods");
    EXPECT_GT(methods.toJsonArray().size(), 0);
    // Introspection must not be mistaken for a dispatched business method.
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty());
}

// ── F-002: the legacy QObject path (QtProviderObject), reached through a
//    ModuleProxy, must also be authorized. ──────────────────────────────────

class LegacyPluginAuthTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        TokenManager::instance().clearAllTokens();
        m_plugin = new LegacyQObjectPlugin();
        // QtProviderObject::callMethod bails early if logosAPI is null; set it so
        // that, absent enforcement, dispatch would actually reach the method.
        m_api = new LogosAPI("legacy_mod");
        m_plugin->logosAPI = m_api;
        m_qtProvider = new QtProviderObject(m_plugin);
    }
    void TearDown() override
    {
        delete m_qtProvider;
        delete m_plugin;
        delete m_api;
        TokenManager::instance().clearAllTokens();
    }
    LegacyQObjectPlugin* m_plugin = nullptr;
    QtProviderObject* m_qtProvider = nullptr;
    LogosAPI* m_api = nullptr;
};

TEST_F(LegacyPluginAuthTest, RejectsGarbageTokenForLegacyQObjectPlugin)
{
    ModuleProxy proxy(m_qtProvider);

    QVariant r = proxy.callRemoteMethod("garbage-token", "deleteAllData",
                                        {QVariant("everything")});

    EXPECT_FALSE(r.isValid());
    EXPECT_FALSE(m_plugin->privilegedCalled)
        << "legacy QObject privileged method invoked with no valid token (F-002)";
}

TEST_F(LegacyPluginAuthTest, AcceptsIssuedTokenForLegacyQObjectPlugin)
{
    ModuleProxy proxy(m_qtProvider);
    TokenManager::instance().saveToken("caller_mod", "issued-tok");

    QVariant r = proxy.callRemoteMethod("issued-tok", "deleteAllData",
                                        {QVariant("everything")});

    EXPECT_TRUE(m_plugin->privilegedCalled);
    EXPECT_EQ(r.toString(), "deleted:everything");
}

// ── End-to-end through the real Local transport ─────────────────────────────
//
// The findings describe the threat as "any peer that can reach the object".
// The reject tests above call ModuleProxy directly; this one drives the full
// consumer-facing path a remote caller actually uses —
// LocalLogosObject::callMethod(authToken, ...) -> ModuleProxy::callRemoteMethod
// -> provider — to prove enforcement holds at the transport boundary, not just
// when ModuleProxy is poked in isolation.

class AuthEnforcementTransportTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Local);
        TokenManager::instance().clearAllTokens();
        m_provider = new AuthTestProvider();
        m_proxy = new ModuleProxy(m_provider);
    }
    void TearDown() override
    {
        PluginRegistry::unregisterPlugin("auth_mod");
        delete m_proxy;
        delete m_provider;
        TokenManager::instance().clearAllTokens();
        LogosModeConfig::setMode(m_savedMode);
    }
    LogosMode m_savedMode;
    AuthTestProvider* m_provider = nullptr;
    ModuleProxy* m_proxy = nullptr;
};

TEST_F(AuthEnforcementTransportTest, WrongTokenBlockedThroughTransport)
{
    LocalTransportHost host;
    host.publishObject("auth_mod", m_proxy);
    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("auth_mod", 5000);
    ASSERT_NE(obj, nullptr);

    TokenManager::instance().saveToken("caller_mod", "the-real-token");

    // A peer with the wrong token hits the published object directly.
    QVariant r = obj->callMethod("forged-token", "privilegedMethod", {QVariant(1)}, 5000);
    EXPECT_FALSE(r.isValid());
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty())
        << "forged token must not reach the provider through the transport";

    obj->release();
}

TEST_F(AuthEnforcementTransportTest, CorrectTokenAllowedThroughTransport)
{
    LocalTransportHost host;
    host.publishObject("auth_mod", m_proxy);
    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("auth_mod", 5000);
    ASSERT_NE(obj, nullptr);

    TokenManager::instance().saveToken("caller_mod", "the-real-token");

    QVariant r = obj->callMethod("the-real-token", "privilegedMethod", {QVariant(1)}, 5000);
    EXPECT_EQ(r.toString(), "dispatched");
    EXPECT_EQ(m_provider->lastMethodCalled, "privilegedMethod");

    obj->release();
}

#include "test_auth_token_enforcement.moc"
