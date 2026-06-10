#include <gtest/gtest.h>
#include <atomic>
#include <QCoreApplication>
#include "logos_mock.h"
#include "mock_store.h"
#include "logos_api.h"
#include "logos_api_client.h"

class AsyncCallsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_mock = new LogosMockSetup();
    }
    void TearDown() override
    {
        delete m_api;
        delete m_mock;
    }

    void createApi(const QString& targetModule = "mod")
    {
        m_api = new LogosAPI("origin");
        m_client = m_api->getClient(targetModule);
    }

    LogosMockSetup* m_mock = nullptr;
    LogosAPI* m_api = nullptr;
    LogosAPIClient* m_client = nullptr;
};

TEST_F(AsyncCallsTest, BasicAsyncCallReturnsCorrectResult)
{
    m_mock->when("mod", "getValue").thenReturn(QVariant(42));
    createApi();

    bool called = false;
    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "getValue", QVariantList(),
        [&](QVariant v) { called = true; received = v; });

    EXPECT_FALSE(called); // callback must not fire synchronously
    QCoreApplication::processEvents();
    EXPECT_TRUE(called);
    EXPECT_EQ(received.toInt(), 42);
}

TEST_F(AsyncCallsTest, AsyncCallWithStringResult)
{
    m_mock->when("mod", "getName").thenReturn(QVariant("hello"));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "getName", QVariantList(),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toString(), "hello");
}

TEST_F(AsyncCallsTest, AsyncCallWithBoolResult)
{
    m_mock->when("mod", "isReady").thenReturn(QVariant(true));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "isReady", QVariantList(),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_TRUE(received.toBool());
}

TEST_F(AsyncCallsTest, AsyncCallWithDoubleResult)
{
    m_mock->when("mod", "getPrice").thenReturn(QVariant(3.14));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "getPrice", QVariantList(),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_DOUBLE_EQ(received.toDouble(), 3.14);
}

TEST_F(AsyncCallsTest, AsyncCallWithVariantListResult)
{
    QVariantList expected = {1, 2, 3};
    m_mock->when("mod", "getItems").thenReturn(QVariant(expected));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "getItems", QVariantList(),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toList().size(), 3);
}

TEST_F(AsyncCallsTest, AsyncUserCallbackRunsBeforeMockLogosObjectRelease)
{
    // Regression: LogosAPIConsumer must invoke the user callback before plugin->release().
    // Releasing the mock/replica first can invalidate QVariant payloads (e.g. remote lists),
    // which manifested as crashes inside Qt when converting the async result.
    std::atomic<int> releaseCount{0};
    MockStore::instance().setMockObjectReleaseProbe(&releaseCount);

    QVariantList expected = {QStringLiteral("pkg_a"), QStringLiteral("pkg_b")};
    m_mock->when("mod", "getInstalledPackages").thenReturn(QVariant(expected));
    createApi();

    bool userCallbackRan = false;
    m_client->invokeRemoteMethodAsync("mod", "getInstalledPackages", QVariantList(),
        [&](QVariant v) {
            userCallbackRan = true;
            EXPECT_EQ(releaseCount.load(), 0)
                << "async user callback must run before MockLogosObject::release()";
            ASSERT_EQ(v.toList().size(), 2);
            EXPECT_EQ(v.toList().at(0).toString(), QStringLiteral("pkg_a"));
        });

    QCoreApplication::processEvents();
    EXPECT_TRUE(userCallbackRan);
    EXPECT_EQ(releaseCount.load(), 1);
}

TEST_F(AsyncCallsTest, AsyncCallWithVariantMapResult)
{
    QVariantMap expected;
    expected["key"] = "value";
    expected["count"] = 5;
    m_mock->when("mod", "getData").thenReturn(QVariant(expected));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "getData", QVariantList(),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    QVariantMap result = received.toMap();
    EXPECT_EQ(result["key"].toString(), "value");
    EXPECT_EQ(result["count"].toInt(), 5);
}

TEST_F(AsyncCallsTest, AsyncCallForwardsArguments)
{
    m_mock->when("mod", "add").thenReturn(QVariant(30));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "add",
        QVariantList() << 10 << 20,
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toInt(), 30);
    EXPECT_TRUE(m_mock->wasCalledWith("mod", "add", QVariantList() << 10 << 20));
}

TEST_F(AsyncCallsTest, AsyncCallTracksCallCount)
{
    m_mock->when("mod", "ping").thenReturn(QVariant("pong"));
    createApi();

    int callCount = 0;
    auto cb = [&](QVariant) { callCount++; };

    m_client->invokeRemoteMethodAsync("mod", "ping", QVariantList(), cb);
    m_client->invokeRemoteMethodAsync("mod", "ping", QVariantList(), cb);
    m_client->invokeRemoteMethodAsync("mod", "ping", QVariantList(), cb);

    QCoreApplication::processEvents();
    EXPECT_EQ(callCount, 3);
    EXPECT_EQ(m_mock->callCount("mod", "ping"), 3);
}

TEST_F(AsyncCallsTest, AsyncNullCallbackDoesNotCrash)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(1));
    createApi();

    m_client->invokeRemoteMethodAsync("mod", "fn", QVariantList(), nullptr);
    QCoreApplication::processEvents();
    // no crash = pass
}

TEST_F(AsyncCallsTest, AsyncCallNoExpectationReturnsInvalidVariant)
{
    createApi("other");

    bool called = false;
    QVariant received(42); // pre-set to non-default
    m_client->invokeRemoteMethodAsync("other", "nonexistent", QVariantList(),
        [&](QVariant v) { called = true; received = v; });

    // Two-stage drain: with no token saved for "other" and no
    // capability_module expectation set, invokeRemoteMethodAsync
    // chains an async requestModule call before the real one — so
    // the outer callback only fires after both queued events have
    // run. processEvents() processes only events present at call
    // time, so a single call drains the requestModule callback but
    // leaves the chained invokeRemoteMethodAsync to a second
    // iteration. Bounded loop keeps a misbehaving test from
    // hanging forever.
    for (int i = 0; i < 10 && !called; ++i)
        QCoreApplication::processEvents();
    EXPECT_TRUE(called);
    EXPECT_FALSE(received.isValid());
}

TEST_F(AsyncCallsTest, MultipleConcurrentAsyncCalls)
{
    m_mock->when("mod", "a").thenReturn(QVariant(1));
    m_mock->when("mod", "b").thenReturn(QVariant(2));
    m_mock->when("mod", "c").thenReturn(QVariant(3));
    createApi();

    QVariant ra, rb, rc;
    m_client->invokeRemoteMethodAsync("mod", "a", QVariantList(), [&](QVariant v) { ra = v; });
    m_client->invokeRemoteMethodAsync("mod", "b", QVariantList(), [&](QVariant v) { rb = v; });
    m_client->invokeRemoteMethodAsync("mod", "c", QVariantList(), [&](QVariant v) { rc = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(ra.toInt(), 1);
    EXPECT_EQ(rb.toInt(), 2);
    EXPECT_EQ(rc.toInt(), 3);
}

TEST_F(AsyncCallsTest, AsyncCallbackIsNeverSynchronous)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(99));
    createApi();

    bool calledDuringInvoke = false;
    bool calledAfterProcessEvents = false;

    m_client->invokeRemoteMethodAsync("mod", "fn", QVariantList(),
        [&](QVariant) {
            calledAfterProcessEvents = true;
        });

    calledDuringInvoke = calledAfterProcessEvents;
    EXPECT_FALSE(calledDuringInvoke); // must not fire synchronously

    QCoreApplication::processEvents();
    EXPECT_TRUE(calledAfterProcessEvents);
}

TEST_F(AsyncCallsTest, AsyncOneArgOverload)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(10));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariant("arg1"),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toInt(), 10);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    ASSERT_EQ(last.size(), 1);
    EXPECT_EQ(last[0].toString(), "arg1");
}

TEST_F(AsyncCallsTest, AsyncTwoArgOverload)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(20));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "fn", QVariant(1), QVariant(2),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toInt(), 20);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    ASSERT_EQ(last.size(), 2);
}

TEST_F(AsyncCallsTest, AsyncThreeArgOverload)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(30));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "fn",
        QVariant(1), QVariant(2), QVariant(3),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toInt(), 30);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    ASSERT_EQ(last.size(), 3);
}

TEST_F(AsyncCallsTest, AsyncFourArgOverload)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(40));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "fn",
        QVariant(1), QVariant(2), QVariant(3), QVariant(4),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toInt(), 40);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    ASSERT_EQ(last.size(), 4);
}

TEST_F(AsyncCallsTest, AsyncFiveArgOverload)
{
    m_mock->when("mod", "fn").thenReturn(QVariant(50));
    createApi();

    QVariant received;
    m_client->invokeRemoteMethodAsync("mod", "fn",
        QVariant(1), QVariant(2), QVariant(3), QVariant(4), QVariant(5),
        [&](QVariant v) { received = v; });

    QCoreApplication::processEvents();
    EXPECT_EQ(received.toInt(), 50);
    QVariantList last = m_mock->lastArgs("mod", "fn");
    ASSERT_EQ(last.size(), 5);
}
