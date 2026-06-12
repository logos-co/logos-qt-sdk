// Security regression test for finding F-002:
//
//   ModuleProxy::informModuleToken ignored its authToken (Q_UNUSED) and
//   unconditionally planted the supplied token into this module's TokenManager.
//   Because isAuthorized() accepts a match against ANY stored token value, a
//   peer could plant a token of its own choosing and then present that same
//   token to callRemoteMethod() to dispatch any business method — fully
//   bypassing the capability gate (CWE-862 Missing Authorization).
//
// The legitimate flow only ever reaches informModuleToken from the trusted
// core / capability_module channel:
//
//   * the host seeds this module's TokenManager with the module's authToken
//     under the keys "core" and "capability_module" at init
//     (logos-liblogos module_initializer.cpp), and
//   * the only callers — logos_core's notifyCapabilityModule() and the
//     capability module's requestModule() — present exactly that secret as the
//     authToken argument.
//
// So the fix gates informModuleToken on that seed secret: a caller that cannot
// present the module's own "core"/"capability_module" token may not plant
// anything. These tests pin that down — the exploit must be rejected, and the
// trusted channel must still succeed.

#include <gtest/gtest.h>
#include <QJsonObject>
#include "logos_api.h"
#include "logos_provider_object.h"
#include "module_proxy.h"
#include "token_manager.h"

// Minimal new-API provider whose privileged method records when it is reached.
class TokenAuthTestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "token_auth_test"; }
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

class InformModuleTokenAuthTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        TokenManager::instance().clearAllTokens();
        m_provider = new TokenAuthTestProvider();
        // The provider must be initialized with a LogosAPI so its
        // informModuleToken() can reach the TokenManager — this mirrors a real
        // loaded module and ensures that, absent the authz gate, a planted token
        // would actually be stored (i.e. the test fails for the right reason).
        m_api = new LogosAPI("token_auth_test");
        m_provider->init(m_api);
    }
    void TearDown() override
    {
        delete m_provider;
        delete m_api;
        TokenManager::instance().clearAllTokens();
    }

    // Seed the per-module secret the host plants at module init: the module's
    // own authToken stored under "core" and "capability_module". Only the
    // trusted core/capability_module channel knows this value.
    void seedTrustedSecret(const QString& secret)
    {
        TokenManager::instance().saveToken("core", secret);
        TokenManager::instance().saveToken("capability_module", secret);
    }

    TokenAuthTestProvider* m_provider = nullptr;
    LogosAPI* m_api = nullptr;
};

// ── F-002 exploit: a peer plants its own token, then uses it ────────────────
//
// This is the core regression. With the vulnerable code (Q_UNUSED(authToken)),
// the empty/garbage authToken is ignored, "PWN-TOKEN" lands in the
// TokenManager, and the subsequent callRemoteMethod is authorized — the
// EXPECT_FALSE below fails. With the fix, the plant is rejected and the
// privileged call never dispatches.
TEST_F(InformModuleTokenAuthTest, PeerCannotPlantTokenThenAuthorizeCall)
{
    ModuleProxy proxy(m_provider);

    // The module has been loaded; the host seeded its trusted secret. The peer
    // does NOT know it.
    seedTrustedSecret("the-module-core-secret");

    // 1) Attacker tries to plant a token of its choosing with no/garbage auth.
    bool plantedEmpty = proxy.informModuleToken("", "attacker", "PWN-TOKEN");
    bool plantedGarbage = proxy.informModuleToken("not-the-secret", "attacker", "PWN-TOKEN-2");

    EXPECT_FALSE(plantedEmpty)
        << "informModuleToken with an empty authToken must be rejected (F-002)";
    EXPECT_FALSE(plantedGarbage)
        << "informModuleToken with a non-trusted authToken must be rejected (F-002)";

    // 2) Neither planted value may exist in the token store...
    EXPECT_TRUE(TokenManager::instance().getToken("attacker").isEmpty())
        << "a rejected plant must not be stored";

    // 3) ...and presenting them to callRemoteMethod must NOT authorize a call.
    QVariant r1 = proxy.callRemoteMethod("PWN-TOKEN", "privilegedMethod", {QVariant(1)});
    QVariant r2 = proxy.callRemoteMethod("PWN-TOKEN-2", "privilegedMethod", {QVariant(1)});
    EXPECT_FALSE(r1.isValid());
    EXPECT_FALSE(r2.isValid());
    EXPECT_TRUE(m_provider->lastMethodCalled.isEmpty())
        << "a planted token must never reach the provider — the capability gate "
           "must hold (F-002)";
}

// ── The trusted channel still works ─────────────────────────────────────────
//
// The host/core (and capability_module) present the module's own seeded secret
// as the authToken. That call must succeed and store the token, so the real
// capability handshake keeps functioning.
TEST_F(InformModuleTokenAuthTest, TrustedChannelCanPlantToken)
{
    ModuleProxy proxy(m_provider);
    seedTrustedSecret("the-module-core-secret");

    bool ok = proxy.informModuleToken("the-module-core-secret", "caller_mod", "issued-tok");

    EXPECT_TRUE(ok) << "the trusted core/capability_module channel must be able to plant tokens";
    EXPECT_EQ(TokenManager::instance().getToken("caller_mod"), "issued-tok");

    // And the token it issued is now usable by that caller — the intended flow.
    QVariant r = proxy.callRemoteMethod("issued-tok", "privilegedMethod", {QVariant(7)});
    EXPECT_EQ(r.toString(), "dispatched");
    EXPECT_EQ(m_provider->lastMethodCalled, "privilegedMethod");
}

// A peer cannot smuggle a plant through by presenting a *business* token it was
// legitimately issued (e.g. some other module's issued token that happens to be
// in the store). informModuleToken is privileged: only the core/capability
// secret unlocks it, not any-stored-token the way callRemoteMethod's
// isAuthorized() works.
TEST_F(InformModuleTokenAuthTest, IssuedBusinessTokenCannotUnlockPlanting)
{
    ModuleProxy proxy(m_provider);
    seedTrustedSecret("the-module-core-secret");
    // A normal per-caller token exists in the store (as if previously issued).
    TokenManager::instance().saveToken("some_caller", "a-business-token");

    // Presenting that business token as the authToken must NOT authorize a plant.
    bool planted = proxy.informModuleToken("a-business-token", "attacker", "PWN-TOKEN");

    EXPECT_FALSE(planted)
        << "only the core/capability_module secret may authorize informModuleToken, "
           "not an arbitrary issued business token";
    EXPECT_TRUE(TokenManager::instance().getToken("attacker").isEmpty());
}
