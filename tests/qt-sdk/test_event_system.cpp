#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QtTest/QSignalSpy>
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_api_provider.h"
#include "logos_api_consumer.h"
#include "logos_object.h"
#include "logos_provider_object.h"
#include "module_proxy.h"
#include "plugin_registry.h"
#include "local_transport.h"

// Provider that emits events on demand
class EventTestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "event_test"; }
    QString providerVersion() const override { return "1.0.0"; }

    QVariant callMethod(const QString& methodName, const QVariantList& args) override
    {
        Q_UNUSED(methodName) Q_UNUSED(args)
        return QVariant(true);
    }

    QJsonArray getMethods() override { return QJsonArray(); }

    void testEmitEvent(const QString& name, const QVariantList& data) { emitEvent(name, data); }
};

// ── LogosAPIClient::onEventResponse(LogosObject*) ─────────────────────────

class ClientEventResponseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_mock = new LogosMockSetup();
        m_api = new LogosAPI("origin");
        m_client = m_api->getClient("target");
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

TEST_F(ClientEventResponseTest, OnEventResponseNullObjectIsNoOp)
{
    // Should not crash
    m_client->onEventResponse(static_cast<LogosObject*>(nullptr), "evt", {QVariant(1)});
}

TEST_F(ClientEventResponseTest, OnEventResponseEmptyEventNameIsNoOp)
{
    // Get a mock object to test with
    m_mock->when("target", "fn").thenReturn(QVariant(1));
    LogosObject* obj = m_client->requestObject("target");
    ASSERT_NE(obj, nullptr);

    // Should not crash, empty event name is rejected
    m_client->onEventResponse(obj, "", {QVariant(1)});
    obj->release();
}

TEST_F(ClientEventResponseTest, OnEventResponseCallsEmitEventOnObject)
{
    // MockLogosObject::emitEvent is a no-op, so we just verify no crash
    m_mock->when("target", "fn").thenReturn(QVariant(1));
    LogosObject* obj = m_client->requestObject("target");
    ASSERT_NE(obj, nullptr);

    m_client->onEventResponse(obj, "my_event", {QVariant("data")});
    obj->release();
}

// ── LogosAPIClient::onEventResponse(QObject*) backward compat ──────────────

class LegacyQObjectEventTest : public QObject {
    Q_OBJECT
public:
    QString lastEventName;
    QVariantList lastEventData;

public slots:
    void eventResponse(const QString& eventName, const QVariantList& data)
    {
        lastEventName = eventName;
        lastEventData = data;
    }
};

TEST_F(ClientEventResponseTest, OnEventResponseQObjectCallsSlot)
{
    LegacyQObjectEventTest receiver;

    m_client->onEventResponse(&receiver, "test_event", {QVariant(42)});

    EXPECT_EQ(receiver.lastEventName, "test_event");
    ASSERT_EQ(receiver.lastEventData.size(), 1);
    EXPECT_EQ(receiver.lastEventData[0].toInt(), 42);
}

TEST_F(ClientEventResponseTest, OnEventResponseQObjectNullIsNoOp)
{
    m_client->onEventResponse(static_cast<QObject*>(nullptr), "evt", {QVariant(1)});
}

TEST_F(ClientEventResponseTest, OnEventResponseQObjectEmptyNameIsNoOp)
{
    LegacyQObjectEventTest receiver;
    m_client->onEventResponse(&receiver, "", {QVariant(1)});
    EXPECT_TRUE(receiver.lastEventName.isEmpty());
}

// ── LogosAPIClient::onEvent (LogosObject callback registration) ────────────

TEST_F(ClientEventResponseTest, OnEventNullObjectIsNoOp)
{
    // Should not crash
    m_client->onEvent(nullptr, "evt", [](const QString&, const QVariantList&) {});
}

// ── LogosAPIClient::informModuleToken ──────────────────────────────────────

TEST_F(ClientEventResponseTest, InformModuleTokenDelegatesToConsumer)
{
    m_mock->when("capability_module", "fn").thenReturn(QVariant(true));

    // informModuleToken routes through consumer -> capability_module -> informModuleToken
    bool result = m_client->informModuleToken("auth_tok", "target_mod", "secret");
    // MockLogosObject::informModuleToken always returns true
    EXPECT_TRUE(result);
}

// ── LogosAPIClient::informModuleToken_module ───────────────────────────────

TEST_F(ClientEventResponseTest, InformModuleTokenModuleDelegatesToConsumer)
{
    m_mock->when("origin_mod", "fn").thenReturn(QVariant(true));

    bool result = m_client->informModuleToken_module("auth_tok", "origin_mod", "target_mod", "secret");
    EXPECT_TRUE(result);
}

// ── LogosAPIProvider::onEventResponse ──────────────────────────────────────

class ProviderEventResponseTest : public ::testing::Test {
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

TEST_F(ProviderEventResponseTest, OnEventResponseNullObjectIsNoOp)
{
    LogosAPI api("mod");
    LogosAPIProvider* prov = api.getProvider();
    // Should not crash
    prov->onEventResponse(nullptr, "evt", {QVariant(1)});
}

TEST_F(ProviderEventResponseTest, OnEventResponseEmptyNameIsNoOp)
{
    LogosAPI api("mod");
    LogosAPIProvider* prov = api.getProvider();

    m_mock->when("mod", "fn").thenReturn(QVariant(1));
    LogosAPIClient* client = api.getClient("mod");
    LogosObject* obj = client->requestObject("mod");
    ASSERT_NE(obj, nullptr);

    // Empty event name should be rejected
    prov->onEventResponse(obj, "", {QVariant(1)});
    obj->release();
}

TEST_F(ProviderEventResponseTest, OnEventResponseCallsEmitEvent)
{
    LogosAPI api("mod");
    LogosAPIProvider* prov = api.getProvider();

    m_mock->when("mod", "fn").thenReturn(QVariant(1));
    LogosAPIClient* client = api.getClient("mod");
    LogosObject* obj = client->requestObject("mod");
    ASSERT_NE(obj, nullptr);

    // MockLogosObject::emitEvent is no-op, just verify no crash
    prov->onEventResponse(obj, "test_event", {QVariant("data")});
    obj->release();
}

// ── Full local event pipeline: Provider -> ModuleProxy -> LocalLogosObject -> Client ──

class LocalEventPipelineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_savedMode = LogosModeConfig::getMode();
        LogosModeConfig::setMode(LogosMode::Local);
        TokenManager::instance().clearAllTokens();
        m_provider = new EventTestProvider();
        m_proxy = new ModuleProxy(m_provider);
    }
    void TearDown() override
    {
        PluginRegistry::unregisterPlugin("pipeline_mod");
        delete m_proxy;
        delete m_provider;
        LogosModeConfig::setMode(m_savedMode);
    }
    LogosMode m_savedMode;
    EventTestProvider* m_provider = nullptr;
    ModuleProxy* m_proxy = nullptr;
};

TEST_F(LocalEventPipelineTest, FullEventPipelineProviderToConsumerObject)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("pipeline_mod", 5000);
    ASSERT_NE(obj, nullptr);

    // Register event listener on the LogosObject
    QString receivedEvent;
    QVariantList receivedData;
    obj->onEvent("pipeline_evt", [&](const QString& name, const QVariantList& data) {
        receivedEvent = name;
        receivedData = data;
    });

    // Provider emits -> LogosProviderBase.emitEvent -> callback -> ModuleProxy.emit eventResponse
    // -> EventHelper.onEventResponse -> registered callback
    m_provider->testEmitEvent("pipeline_evt", {QVariant("payload"), QVariant(99)});

    EXPECT_EQ(receivedEvent, "pipeline_evt");
    ASSERT_EQ(receivedData.size(), 2);
    EXPECT_EQ(receivedData[0].toString(), "payload");
    EXPECT_EQ(receivedData[1].toInt(), 99);
    obj->release();
}

TEST_F(LocalEventPipelineTest, EventNotDeliveredAfterDisconnect)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("pipeline_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int count = 0;
    obj->onEvent("evt", [&](const QString&, const QVariantList&) { count++; });

    m_provider->testEmitEvent("evt", {});
    EXPECT_EQ(count, 1);

    obj->disconnectEvents();
    m_provider->testEmitEvent("evt", {});
    EXPECT_EQ(count, 1);

    // Re-register after disconnect
    obj->onEvent("evt", [&](const QString&, const QVariantList&) { count++; });
    m_provider->testEmitEvent("evt", {});
    EXPECT_EQ(count, 2);
    obj->release();
}

TEST_F(LocalEventPipelineTest, EmitEventOnLocalLogosObjectTriggersSignal)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("pipeline_mod", 5000);
    ASSERT_NE(obj, nullptr);

    // Use QSignalSpy on the ModuleProxy to verify emitEvent triggers the signal
    QSignalSpy spy(m_proxy, &ModuleProxy::eventResponse);

    obj->emitEvent("ext_event", {QVariant("from_outside")});

    // emitEvent uses QueuedConnection, so process events
    QCoreApplication::processEvents();

    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "ext_event");
    QVariantList data = spy.at(0).at(1).value<QVariantList>();
    ASSERT_EQ(data.size(), 1);
    EXPECT_EQ(data[0].toString(), "from_outside");
    obj->release();
}

// ── Wildcard subscriptions (empty event name = match any event) ────────────
//
// An empty event name passed to LogosObject::onEvent means "receive every
// event regardless of name". Without this, wrapper layers that subscribe
// without knowing the specific event names up front (e.g. logoscore's
// `watch` with no --event filter) silently drop every event.

TEST_F(LocalEventPipelineTest, WildcardSubscriptionReceivesEveryEvent)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("pipeline_mod", 5000);
    ASSERT_NE(obj, nullptr);

    QList<QPair<QString, QVariantList>> received;
    obj->onEvent("", [&](const QString& name, const QVariantList& data) {
        received.append({name, data});
    });

    m_provider->testEmitEvent("alpha", {QVariant(1)});
    m_provider->testEmitEvent("beta",  {QVariant("two"), QVariant(2)});
    m_provider->testEmitEvent("gamma", {});

    ASSERT_EQ(received.size(), 3);
    EXPECT_EQ(received[0].first, "alpha");
    EXPECT_EQ(received[0].second.size(), 1);
    EXPECT_EQ(received[0].second[0].toInt(), 1);
    EXPECT_EQ(received[1].first, "beta");
    EXPECT_EQ(received[1].second.size(), 2);
    EXPECT_EQ(received[2].first, "gamma");
    EXPECT_EQ(received[2].second.size(), 0);
    obj->release();
}

TEST_F(LocalEventPipelineTest, WildcardAndNamedSubscriptionsCoexist)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("pipeline_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int wildcardCount = 0;
    int alphaCount = 0;
    QStringList wildcardNames;
    obj->onEvent("", [&](const QString& name, const QVariantList&) {
        wildcardCount++;
        wildcardNames.append(name);
    });
    obj->onEvent("alpha", [&](const QString&, const QVariantList&) {
        alphaCount++;
    });

    m_provider->testEmitEvent("alpha", {});
    m_provider->testEmitEvent("beta",  {});
    m_provider->testEmitEvent("alpha", {});

    // Wildcard sees all 3, named-"alpha" sees 2.
    EXPECT_EQ(wildcardCount, 3);
    EXPECT_EQ(alphaCount, 2);
    EXPECT_EQ(wildcardNames, (QStringList{"alpha", "beta", "alpha"}));
    obj->release();
}

TEST_F(LocalEventPipelineTest, MultipleWildcardSubscribersAllReceive)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("pipeline_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int countA = 0, countB = 0;
    obj->onEvent("", [&](const QString&, const QVariantList&) { countA++; });
    obj->onEvent("", [&](const QString&, const QVariantList&) { countB++; });

    m_provider->testEmitEvent("evt1", {});
    m_provider->testEmitEvent("evt2", {});

    EXPECT_EQ(countA, 2);
    EXPECT_EQ(countB, 2);
    obj->release();
}

TEST_F(LocalEventPipelineTest, NamedSubscriberDoesNotReceiveOtherEvents)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    LocalTransportConnection conn;
    LogosObject* obj = conn.requestObject("pipeline_mod", 5000);
    ASSERT_NE(obj, nullptr);

    int alphaCount = 0;
    obj->onEvent("alpha", [&](const QString&, const QVariantList&) { alphaCount++; });

    m_provider->testEmitEvent("beta",  {});
    m_provider->testEmitEvent("alpha", {});
    m_provider->testEmitEvent("gamma", {});

    EXPECT_EQ(alphaCount, 1);
    obj->release();
}

// ── Consumer onEvent integration ───────────────────────────────────────────

TEST_F(LocalEventPipelineTest, ConsumerOnEventRegistersCallback)
{
    LocalTransportHost host;
    host.publishObject("pipeline_mod", m_proxy);

    // Seed a token so client doesn't try capability_module
    TokenManager::instance().saveToken("pipeline_mod", "tok");

    LogosAPI api("test_origin");
    LogosAPIClient* client = api.getClient("pipeline_mod");

    // Get object handle
    LogosObject* obj = client->requestObject("pipeline_mod");
    ASSERT_NE(obj, nullptr);

    // Register via client.onEvent (delegates to consumer.onEvent -> obj.onEvent)
    QString receivedName;
    QVariantList receivedData;
    client->onEvent(obj, "client_evt", [&](const QString& name, const QVariantList& data) {
        receivedName = name;
        receivedData = data;
    });

    // Provider emits event
    m_provider->testEmitEvent("client_evt", {QVariant("via_client")});

    EXPECT_EQ(receivedName, "client_evt");
    ASSERT_EQ(receivedData.size(), 1);
    EXPECT_EQ(receivedData[0].toString(), "via_client");
    obj->release();
}

#include "test_event_system.moc"
