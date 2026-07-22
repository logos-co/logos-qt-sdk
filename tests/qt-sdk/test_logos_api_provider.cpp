#include <gtest/gtest.h>
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_api_provider.h"
#include "logos_provider_object.h"
#include "logos_mode.h"
#include "logos_object.h"
#include "logos_rpc_status.h"
#include "plugin_registry.h"
#include "token_manager.h"
#include "implementations/qt_local/local_transport.h"

// Minimal provider for registration testing
class RegistrationTestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "reg_test"; }
    QString providerVersion() const override { return "1.0.0"; }
    QVariant callMethod(const QString&, const QVariantList&) override { return QVariant(); }
    QJsonArray getMethods() override { return QJsonArray(); }
};

// Provider that returns an observable marker so a round-trip through the
// published proxy can prove a call was actually dispatched (i.e. authorized).
class DispatchMarkerProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "marker"; }
    QString providerVersion() const override { return "1.0.0"; }
    QVariant callMethod(const QString& method, const QVariantList&) override {
        lastMethod = method;
        return QVariant(QStringLiteral("dispatched"));
    }
    QJsonArray getMethods() override { return QJsonArray(); }
    QString lastMethod;
};

class LogosApiProviderTest : public ::testing::Test {
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

TEST_F(LogosApiProviderTest, RegisterProviderObjectSucceeds)
{
    LogosAPI api("test_module");
    LogosAPIProvider* provider = api.getProvider();

    auto* testProv = new RegistrationTestProvider();
    EXPECT_TRUE(provider->registerObject("test_module", testProv));
}

TEST_F(LogosApiProviderTest, DoubleRegistrationFails)
{
    LogosAPI api("test_module");
    LogosAPIProvider* prov = api.getProvider();

    auto* p1 = new RegistrationTestProvider();
    auto* p2 = new RegistrationTestProvider();

    EXPECT_TRUE(prov->registerObject("test_module", p1));
    EXPECT_FALSE(prov->registerObject("test_module", p2));

    delete p2; // p1 is managed by ModuleProxy
}

TEST_F(LogosApiProviderTest, NullProviderFails)
{
    LogosAPI api("test_module");
    LogosAPIProvider* prov = api.getProvider();

    EXPECT_FALSE(prov->registerObject("test_module", static_cast<LogosProviderObject*>(nullptr)));
}

TEST_F(LogosApiProviderTest, EmptyNameFails)
{
    LogosAPI api("test_module");
    LogosAPIProvider* prov = api.getProvider();

    auto* p = new RegistrationTestProvider();
    EXPECT_FALSE(prov->registerObject("", p));
    delete p;
}

TEST_F(LogosApiProviderTest, SaveTokenWithoutRegistrationFails)
{
    LogosAPI api("test_module");
    LogosAPIProvider* prov = api.getProvider();
    EXPECT_FALSE(prov->saveToken("from_mod", "token123"));
}

TEST_F(LogosApiProviderTest, SaveTokenAfterRegistrationSucceeds)
{
    LogosAPI api("test_module");
    LogosAPIProvider* prov = api.getProvider();

    auto* p = new RegistrationTestProvider();
    prov->registerObject("test_module", p);

    EXPECT_TRUE(prov->saveToken("from_mod", "token123"));
}

TEST_F(LogosApiProviderTest, RegistryUrlContainsModuleName)
{
    LogosAPI api("my_module");
    LogosAPIProvider* prov = api.getProvider();
    QString url = prov->registryUrl();
    EXPECT_TRUE(url.contains("my_module"));
}

// ── setTokenValidator forwarded to the published proxy ──────────────────────
//
// Drive a real call through the provider's published proxy over the in-process
// Local transport, presenting a token the built-in scan rejects but the
// validator accepts. If the validator was wired through to the ModuleProxy the
// call dispatches; otherwise it is rejected. Covers both orderings: validator
// set before registerObject (applied to the proxy at creation) and after
// (applied immediately).
class ProviderTokenValidatorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Local);
        TokenManager::instance().clearAllTokens();
    }
    void TearDown() override
    {
        PluginRegistry::unregisterPlugin("marker_mod");
        TokenManager::instance().clearAllTokens();
        LogosModeConfig::setMode(m_savedMode);
    }

    // Dispatch "probe" through the Local transport with `token` and return the
    // result (invalid QVariant means the call was rejected).
    QVariant callThroughProxy(const QString& token)
    {
        LocalTransportConnection conn;
        LogosObject* obj = conn.requestObject("marker_mod", 5000);
        if (!obj) return QVariant();
        QVariant r = obj->callMethod(token, "probe", {}, 5000);
        obj->release();
        return r;
    }

    LogosMode m_savedMode;
};

TEST_F(ProviderTokenValidatorTest, ValidatorSetBeforeRegisterIsApplied)
{
    LogosAPI api("marker_mod");
    LogosAPIProvider* prov = api.getProvider();
    auto* backend = new DispatchMarkerProvider();

    // Validator installed BEFORE registration — must be applied when the proxy
    // is created inside registerObject.
    prov->setTokenValidator([](const QString& token, const QString& transport) {
        return token == "named-tok" && transport == "local";
    });
    ASSERT_TRUE(prov->registerObject("marker_mod", backend));

    // A token only the validator accepts -> dispatched.
    EXPECT_EQ(callThroughProxy("named-tok").toString(), "dispatched");
    // A token nobody accepts -> rejected (no dispatch).
    EXPECT_TRUE(logos::isUnauthorizedSentinel(callThroughProxy("garbage-tok")));
}

TEST_F(ProviderTokenValidatorTest, ValidatorSetAfterRegisterIsApplied)
{
    LogosAPI api("marker_mod");
    LogosAPIProvider* prov = api.getProvider();
    auto* backend = new DispatchMarkerProvider();

    ASSERT_TRUE(prov->registerObject("marker_mod", backend));
    // Validator installed AFTER registration — must reach the already-created
    // proxy.
    prov->setTokenValidator([](const QString& token, const QString&) {
        return token == "named-tok";
    });

    EXPECT_EQ(callThroughProxy("named-tok").toString(), "dispatched");
    EXPECT_TRUE(logos::isUnauthorizedSentinel(callThroughProxy("garbage-tok")));
}
