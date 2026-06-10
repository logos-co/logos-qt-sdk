#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_api_client.h"

class LogosApiClientOverloadsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_mock = new LogosMockSetup();
        m_mock->when("mod", "fn").thenReturn(QVariant(42));
        m_api = new LogosAPI("origin");
        m_client = m_api->getClient("mod");
    }
    void TearDown() override
    {
        delete m_api;
        delete m_mock;
    }
    LogosMockSetup* m_mock = nullptr;
    LogosAPI* m_api = nullptr;
    LogosAPIClient* m_client = nullptr;
};

TEST_F(LogosApiClientOverloadsTest, ZeroArgs)
{
    QVariant r = m_client->invokeRemoteMethod("mod", "fn");
    EXPECT_EQ(r.toInt(), 42);
}

TEST_F(LogosApiClientOverloadsTest, OneArg)
{
    QVariant r = m_client->invokeRemoteMethod("mod", "fn", QVariant("a"));
    EXPECT_EQ(r.toInt(), 42);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    ASSERT_EQ(last.size(), 1);
    EXPECT_EQ(last[0].toString(), "a");
}

TEST_F(LogosApiClientOverloadsTest, TwoArgs)
{
    QVariant r = m_client->invokeRemoteMethod("mod", "fn", QVariant(1), QVariant(2));
    EXPECT_EQ(r.toInt(), 42);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    ASSERT_EQ(last.size(), 2);
    EXPECT_EQ(last[0].toInt(), 1);
    EXPECT_EQ(last[1].toInt(), 2);
}

TEST_F(LogosApiClientOverloadsTest, ThreeArgs)
{
    m_client->invokeRemoteMethod("mod", "fn", QVariant(1), QVariant(2), QVariant(3));
    QVariantList last = m_mock->lastArgs("mod", "fn");
    EXPECT_EQ(last.size(), 3);
}

TEST_F(LogosApiClientOverloadsTest, FourArgs)
{
    m_client->invokeRemoteMethod("mod", "fn", QVariant(1), QVariant(2), QVariant(3), QVariant(4));
    QVariantList last = m_mock->lastArgs("mod", "fn");
    EXPECT_EQ(last.size(), 4);
}

TEST_F(LogosApiClientOverloadsTest, FiveArgs)
{
    m_client->invokeRemoteMethod("mod", "fn", QVariant(1), QVariant(2), QVariant(3), QVariant(4), QVariant(5));
    QVariantList last = m_mock->lastArgs("mod", "fn");
    EXPECT_EQ(last.size(), 5);
}

TEST_F(LogosApiClientOverloadsTest, ArgsList)
{
    QVariantList args;
    args << QVariant("x") << QVariant("y") << QVariant("z");
    m_client->invokeRemoteMethod("mod", "fn", args);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    EXPECT_EQ(last.size(), 3);
}

TEST_F(LogosApiClientOverloadsTest, AsyncZeroArgs)
{
    bool called = false;
    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariantList(),
        [&](QVariant v) { called = true; received = v; });

    // Process event loop so QTimer::singleShot fires
    QCoreApplication::processEvents();

    EXPECT_TRUE(called);
    EXPECT_EQ(received.toInt(), 42);
}

TEST_F(LogosApiClientOverloadsTest, AsyncOneArg)
{
    bool called = false;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariant("a"),
        [&](QVariant) { called = true; });
    QCoreApplication::processEvents();
    EXPECT_TRUE(called);
}

TEST_F(LogosApiClientOverloadsTest, AsyncTwoArgs)
{
    bool called = false;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariant(1), QVariant(2),
        [&](QVariant) { called = true; });
    QCoreApplication::processEvents();
    EXPECT_TRUE(called);
}

TEST_F(LogosApiClientOverloadsTest, AsyncThreeArgs)
{
    bool called = false;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariant(1), QVariant(2), QVariant(3),
        [&](QVariant) { called = true; });
    QCoreApplication::processEvents();
    EXPECT_TRUE(called);
}

TEST_F(LogosApiClientOverloadsTest, AsyncFourArgs)
{
    bool called = false;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariant(1), QVariant(2), QVariant(3), QVariant(4),
        [&](QVariant) { called = true; });
    QCoreApplication::processEvents();
    EXPECT_TRUE(called);
}

TEST_F(LogosApiClientOverloadsTest, AsyncFiveArgs)
{
    bool called = false;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariant(1), QVariant(2), QVariant(3), QVariant(4), QVariant(5),
        [&](QVariant) { called = true; });
    QCoreApplication::processEvents();
    EXPECT_TRUE(called);
}

TEST_F(LogosApiClientOverloadsTest, AsyncNullCallbackIgnored)
{
    // Should not crash
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariantList(), nullptr);
    QCoreApplication::processEvents();
}

TEST_F(LogosApiClientOverloadsTest, GetTokenDelegatesToTokenManager)
{
    // Token was pre-seeded by mock.when()
    QString token = m_client->getToken("mod");
    EXPECT_FALSE(token.isEmpty());
}

TEST_F(LogosApiClientOverloadsTest, GetTokenReturnsEmptyForUnknownModule)
{
    QString token = m_client->getToken("unknown_module");
    EXPECT_TRUE(token.isEmpty());
}

TEST_F(LogosApiClientOverloadsTest, IsConnectedInMockMode)
{
    EXPECT_TRUE(m_client->isConnected());
}

TEST_F(LogosApiClientOverloadsTest, ReconnectInMockMode)
{
    EXPECT_TRUE(m_client->reconnect());
}

TEST_F(LogosApiClientOverloadsTest, GetTokenManagerReturnsNonNull)
{
    EXPECT_NE(m_client->getTokenManager(), nullptr);
}
