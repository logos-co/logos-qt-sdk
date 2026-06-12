#include <gtest/gtest.h>
#include "logos_mock.h"

class LogosMockSetupTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Ensure we start in Remote mode
        LogosModeConfig::setMode(LogosMode::Remote);
        MockStore::instance().reset();
        TokenManager::instance().clearAllTokens();
    }
    void TearDown() override
    {
        LogosModeConfig::setMode(LogosMode::Remote);
    }
};

TEST_F(LogosMockSetupTest, SwitchesToMockMode)
{
    {
        LogosMockSetup mock;
        EXPECT_TRUE(LogosModeConfig::isMock());
    }
    // After destruction, mode is restored
    EXPECT_TRUE(LogosModeConfig::isRemote());
}

TEST_F(LogosMockSetupTest, RestoresPreviousMode)
{
    LogosModeConfig::setMode(LogosMode::Local);
    {
        LogosMockSetup mock;
        EXPECT_TRUE(LogosModeConfig::isMock());
    }
    EXPECT_TRUE(LogosModeConfig::isLocal());
    // Restore for teardown
    LogosModeConfig::setMode(LogosMode::Remote);
}

TEST_F(LogosMockSetupTest, ResetsStoreOnConstruction)
{
    MockStore::instance().when("mod", "fn").thenReturn(QVariant(1));
    MockStore::instance().recordAndReturn("mod", "fn", {});

    LogosMockSetup mock;

    // Previous expectations and calls cleared
    EXPECT_FALSE(MockStore::instance().wasCalled("mod", "fn"));
    QVariant r = MockStore::instance().recordAndReturn("mod", "fn", {});
    EXPECT_FALSE(r.isValid());
}

TEST_F(LogosMockSetupTest, ClearsTokensOnConstruction)
{
    TokenManager::instance().saveToken("stale", "token");

    LogosMockSetup mock;

    EXPECT_FALSE(TokenManager::instance().hasToken("stale"));
}

TEST_F(LogosMockSetupTest, WhenPreSeedsToken)
{
    LogosMockSetup mock;
    mock.when("my_module", "doStuff").thenReturn(QVariant("ok"));

    EXPECT_TRUE(TokenManager::instance().hasToken("my_module"));
}

TEST_F(LogosMockSetupTest, WhenSetsExpectation)
{
    LogosMockSetup mock;
    mock.when("mod", "fn").thenReturn(QVariant(99));

    QVariant r = MockStore::instance().recordAndReturn("mod", "fn", {});
    EXPECT_EQ(r.toInt(), 99);
}

TEST_F(LogosMockSetupTest, VerificationDelegates)
{
    LogosMockSetup mock;
    mock.when("mod", "fn").thenReturn(QVariant(1));

    MockStore::instance().recordAndReturn("mod", "fn", {QVariant("arg")});

    EXPECT_TRUE(mock.wasCalled("mod", "fn"));
    EXPECT_TRUE(mock.wasCalledWith("mod", "fn", {QVariant("arg")}));
    EXPECT_EQ(mock.callCount("mod", "fn"), 1);
    EXPECT_EQ(mock.lastArgs("mod", "fn").size(), 1);
}

TEST_F(LogosMockSetupTest, ResetsStoreOnDestruction)
{
    {
        LogosMockSetup mock;
        mock.when("mod", "fn").thenReturn(QVariant(1));
        MockStore::instance().recordAndReturn("mod", "fn", {});
    }
    // After destruction, store is clean
    EXPECT_FALSE(MockStore::instance().wasCalled("mod", "fn"));
}
