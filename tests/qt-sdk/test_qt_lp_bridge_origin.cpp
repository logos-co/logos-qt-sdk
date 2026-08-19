// LpBridge's two factories, and the one thing that must never be true of them:
// that a consumer can end up calling out under someone else's name.
//
// `forTarget(api, t)` DERIVES the origin from `api->moduleName()`. That is
// correct exactly once — when `api` is the caller's own identity object — and
// wrong in every reuse: a wrapper handed a LogosAPI belonging to another module
// would open a client whose origin is the LENDER's name, and every capability
// decision downstream would be made about the lender. The transport cannot
// notice; it sees a well-formed call from a module that really does have that
// name.
//
// `forOrigin(origin, t)` is the answer for anything with no LogosAPI of its own
// — a cdylib module, whose provider surface is the std `logos_module_impl.h` C
// ABI. It STATES its origin. These tests pin that the stated name is what keys
// the bridge, so two origins can never share one.

#include <gtest/gtest.h>

#include <QString>

#include "logos_mock.h"
#include "logos_api.h"
#include "logos_qt_lp_bridge.h"

namespace {

class LpBridgeOriginTest : public ::testing::Test {
protected:
    void SetUp() override { m_mock = new LogosMockSetup(); }
    void TearDown() override { delete m_mock; }
    LogosMockSetup* m_mock = nullptr;
};

// A bridge exists without any LogosAPI at all — the premise of the whole
// origin-bound consumer surface. (Contrast forTarget, which cannot: with no
// api there is no name to derive.)
TEST_F(LpBridgeOriginTest, ForOriginNeedsNoLogosApi)
{
    logos::qt::LpBridge* b =
        logos::qt::LpBridge::forOrigin(QStringLiteral("br_consumer"), QStringLiteral("br_target"));
    EXPECT_NE(b, nullptr);

    EXPECT_EQ(logos::qt::LpBridge::forTarget(nullptr, QStringLiteral("br_target")), nullptr);
}

// Process-lifetime and keyed by the pair: the generated wrapper keeps this
// pointer, and a `bind_x(...)` temporary's subscriptions have to outlive the
// temporary.
TEST_F(LpBridgeOriginTest, ForOriginIsStablePerPair)
{
    logos::qt::LpBridge* a =
        logos::qt::LpBridge::forOrigin(QStringLiteral("br_stable"), QStringLiteral("br_t"));
    logos::qt::LpBridge* b =
        logos::qt::LpBridge::forOrigin(QStringLiteral("br_stable"), QStringLiteral("br_t"));
    EXPECT_EQ(a, b);
}

// THE assertion. Same target, two different stated origins: two different
// bridges, and therefore two different lp clients with two different identities.
// If the origin were ever ignored, defaulted, or taken from somewhere else,
// these would collapse into one — which is exactly what "borrowing an identity"
// looks like from here.
TEST_F(LpBridgeOriginTest, TheStatedOriginKeysTheBridge)
{
    logos::qt::LpBridge* mine =
        logos::qt::LpBridge::forOrigin(QStringLiteral("br_me"), QStringLiteral("br_shared"));
    logos::qt::LpBridge* theirs =
        logos::qt::LpBridge::forOrigin(QStringLiteral("br_them"), QStringLiteral("br_shared"));
    EXPECT_NE(mine, theirs);
}

TEST_F(LpBridgeOriginTest, TheTargetKeysTheBridgeToo)
{
    logos::qt::LpBridge* one =
        logos::qt::LpBridge::forOrigin(QStringLiteral("br_one_origin"), QStringLiteral("br_a"));
    logos::qt::LpBridge* two =
        logos::qt::LpBridge::forOrigin(QStringLiteral("br_one_origin"), QStringLiteral("br_b"));
    EXPECT_NE(one, two);
}

// The two factories name the same thing when they name the same pair, so a
// process holding both flavours keeps ONE client per (origin, target) — the
// invariant LpBridge documents — instead of silently opening a second transport
// to the same module.
TEST_F(LpBridgeOriginTest, ForTargetAndForOriginAgreeOnThePair)
{
    LogosAPI api(QStringLiteral("br_agreeing_module"));
    logos::qt::LpBridge* viaApi =
        logos::qt::LpBridge::forTarget(&api, QStringLiteral("br_agree_target"));
    logos::qt::LpBridge* viaOrigin = logos::qt::LpBridge::forOrigin(
        QStringLiteral("br_agreeing_module"), QStringLiteral("br_agree_target"));

    ASSERT_NE(viaApi, nullptr);
    EXPECT_EQ(viaApi, viaOrigin);
}

// Order-independence of the above, and the reason the shared registry adopts an
// api rather than ignoring it: reaching the pair through forOrigin FIRST must
// not leave a later Qt-plugin caller with a bridge that can never sync the
// host's bootstrap tokens into the plugin's own TokenManager. That failure mode
// is an empty auth token and a call that returns a default value with no error
// — the defect syncTokens exists to prevent.
TEST_F(LpBridgeOriginTest, AnApiArrivingSecondIsStillAdopted)
{
    logos::qt::LpBridge* viaOrigin = logos::qt::LpBridge::forOrigin(
        QStringLiteral("br_late_api_module"), QStringLiteral("br_late_target"));
    ASSERT_NE(viaOrigin, nullptr);

    LogosAPI api(QStringLiteral("br_late_api_module"));
    logos::qt::LpBridge* viaApi =
        logos::qt::LpBridge::forTarget(&api, QStringLiteral("br_late_target"));

    EXPECT_EQ(viaApi, viaOrigin);
    // Touching the client is what RUNS the installed token-sync hook, so this
    // line is what exercises the indirection the seam gained (`syncFromApi`
    // reached through a pointer rather than named inline — the change that lets
    // an origin-bound translation unit link none of the Qt host's identity
    // code). A hook that was never installed, or one reading the wrong bridge,
    // shows up here.
    //
    // What it does NOT check is the mirror's effect, and that is a property of
    // the process rather than of this test: the hook copies tokens out of the
    // LogosAPI's TokenManager and into `lp_token_save`, which writes to
    // `TokenManager::instance()` — the SAME object in a single-image test
    // binary. The two stores only differ across the plugin/host image boundary
    // the mirror exists for, so a round-trip assertion here would pass whether
    // or not the copy happened.
    EXPECT_NO_THROW((void)viaApi->client());
}

}  // namespace
