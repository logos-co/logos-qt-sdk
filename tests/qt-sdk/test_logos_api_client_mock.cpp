#include <gtest/gtest.h>
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_api_client.h"

class LogosApiClientMockTest : public ::testing::Test {
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

TEST_F(LogosApiClientMockTest, InvokeRemoteMethodReturnsMockedValue)
{
    m_mock->when("test_module", "getValue").thenReturn(QVariant(42));

    LogosAPI api("client_module");
    LogosAPIClient* client = api.getClient("test_module");
    ASSERT_NE(client, nullptr);

    QVariant result = client->invokeRemoteMethod("test_module", "getValue");
    EXPECT_EQ(result.toInt(), 42);
    EXPECT_TRUE(m_mock->wasCalled("test_module", "getValue"));
}

TEST_F(LogosApiClientMockTest, InvokeWithSingleArg)
{
    m_mock->when("mod", "echo")
        .withArgs({QVariant("hello")})
        .thenReturn(QVariant("hello back"));

    LogosAPI api("origin");
    LogosAPIClient* client = api.getClient("mod");
    ASSERT_NE(client, nullptr);

    QVariant result = client->invokeRemoteMethod("mod", "echo", QVariant("hello"));
    EXPECT_EQ(result.toString(), "hello back");
}

TEST_F(LogosApiClientMockTest, InvokeWithMultipleArgs)
{
    m_mock->when("math", "add").thenReturn(QVariant(30));

    LogosAPI api("origin");
    LogosAPIClient* client = api.getClient("math");
    ASSERT_NE(client, nullptr);

    QVariant result = client->invokeRemoteMethod("math", "add", QVariant(10), QVariant(20));
    EXPECT_EQ(result.toInt(), 30);
    EXPECT_TRUE(m_mock->wasCalled("math", "add"));
}

TEST_F(LogosApiClientMockTest, InvokeWithArgsList)
{
    m_mock->when("mod", "multi").thenReturn(QVariant("done"));

    LogosAPI api("origin");
    LogosAPIClient* client = api.getClient("mod");
    ASSERT_NE(client, nullptr);

    QVariantList args;
    args << QVariant(1) << QVariant(2) << QVariant(3);
    QVariant result = client->invokeRemoteMethod("mod", "multi", args);
    EXPECT_EQ(result.toString(), "done");
}

TEST_F(LogosApiClientMockTest, NoExpectationReturnsInvalid)
{
    // Token still pre-seeded by when(), but no expectation for this method
    m_mock->when("mod", "other").thenReturn(QVariant(1));

    LogosAPI api("origin");
    LogosAPIClient* client = api.getClient("mod");
    ASSERT_NE(client, nullptr);

    QVariant result = client->invokeRemoteMethod("mod", "unregistered");
    EXPECT_FALSE(result.isValid());
}

TEST_F(LogosApiClientMockTest, CallCountTracking)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(true));

    LogosAPI api("origin");
    LogosAPIClient* client = api.getClient("mod");
    ASSERT_NE(client, nullptr);

    client->invokeRemoteMethod("mod", "fn");
    client->invokeRemoteMethod("mod", "fn");
    client->invokeRemoteMethod("mod", "fn");

    EXPECT_EQ(m_mock->callCount("mod", "fn"), 3);
}
