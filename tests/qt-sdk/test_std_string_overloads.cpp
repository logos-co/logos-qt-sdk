#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include "logos_mock.h"
#include "token_manager.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_api_provider.h"
#include "logos_provider_object.h"

// ============================================================
// TokenManager std::string overloads
// ============================================================

class TokenManagerStdStringTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        TokenManager::instance().clearAllTokens();
    }
};

TEST_F(TokenManagerStdStringTest, SaveAndGetWithStdString)
{
    TokenManager::instance().saveToken(std::string("mod_a"), std::string("token_a"));
    EXPECT_EQ(TokenManager::instance().getToken(std::string("mod_a")), "token_a");
}

TEST_F(TokenManagerStdStringTest, GetMissingKeyReturnsEmpty)
{
    EXPECT_TRUE(TokenManager::instance().getToken(std::string("missing")).empty());
}

TEST_F(TokenManagerStdStringTest, HasTokenWithStdString)
{
    EXPECT_FALSE(TokenManager::instance().hasToken(std::string("key")));
    TokenManager::instance().saveToken(std::string("key"), std::string("val"));
    EXPECT_TRUE(TokenManager::instance().hasToken(std::string("key")));
}

TEST_F(TokenManagerStdStringTest, RemoveTokenWithStdString)
{
    TokenManager::instance().saveToken(std::string("key"), std::string("val"));
    EXPECT_TRUE(TokenManager::instance().removeToken(std::string("key")));
    EXPECT_FALSE(TokenManager::instance().hasToken(std::string("key")));
}

TEST_F(TokenManagerStdStringTest, RemoveNonexistentKeyReturnsFalse)
{
    EXPECT_FALSE(TokenManager::instance().removeToken(std::string("nonexistent")));
}

TEST_F(TokenManagerStdStringTest, CrossTypeInteropSaveStdGetQString)
{
    TokenManager::instance().saveToken(std::string("key1"), std::string("val1"));
    EXPECT_EQ(TokenManager::instance().getToken(QString("key1")), QString("val1"));
}

TEST_F(TokenManagerStdStringTest, CrossTypeInteropSaveQStringGetStd)
{
    TokenManager::instance().saveToken(QString("key2"), QString("val2"));
    EXPECT_EQ(TokenManager::instance().getToken(std::string("key2")), "val2");
}

TEST_F(TokenManagerStdStringTest, OverwriteWithStdString)
{
    TokenManager::instance().saveToken(std::string("key"), std::string("old"));
    TokenManager::instance().saveToken(std::string("key"), std::string("new"));
    EXPECT_EQ(TokenManager::instance().getToken(std::string("key")), "new");
    EXPECT_EQ(TokenManager::instance().tokenCount(), 1);
}

// ============================================================
// LogosAPI std::string overloads
// ============================================================

class LogosApiStdStringTest : public ::testing::Test {
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

TEST_F(LogosApiStdStringTest, ConstructWithStdStringCreatesProvider)
{
    LogosAPI api(std::string("test_module"));
    EXPECT_NE(api.getProvider(), nullptr);
}

TEST_F(LogosApiStdStringTest, ConstructWithStdStringGetsTokenManager)
{
    LogosAPI api(std::string("test_module"));
    EXPECT_EQ(api.getTokenManager(), &TokenManager::instance());
}

TEST_F(LogosApiStdStringTest, GetClientWithStdStringReturnsNonNull)
{
    LogosAPI api(std::string("origin"));
    LogosAPIClient* client = api.getClient(std::string("target_module"));
    EXPECT_NE(client, nullptr);
}

TEST_F(LogosApiStdStringTest, GetClientWithStdStringCachesSameModule)
{
    LogosAPI api(std::string("origin"));
    LogosAPIClient* c1 = api.getClient(std::string("target"));
    LogosAPIClient* c2 = api.getClient(std::string("target"));
    EXPECT_EQ(c1, c2);
}

TEST_F(LogosApiStdStringTest, GetClientStdStringAndQStringSameModule)
{
    // Both overloads should resolve to the same cached client
    LogosAPI api(std::string("origin"));
    LogosAPIClient* cStd = api.getClient(std::string("target"));
    LogosAPIClient* cQt  = api.getClient(QString("target"));
    EXPECT_EQ(cStd, cQt);
}

TEST_F(LogosApiStdStringTest, SetPropertyWithStdString)
{
    LogosAPI api(std::string("prop_mod"));
    const std::string path = "/tmp/plugin_parent";
    api.setProperty("modulePath", path);
    QVariant stored = api.property("modulePath");
    ASSERT_TRUE(stored.isValid());
    EXPECT_EQ(stored.toString().toStdString(), path);
}

// ============================================================
// LogosAPIClient::informModuleToken std::string overload
// ============================================================

class LogosApiClientStdStringTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_mock = new LogosMockSetup();
        m_api = new LogosAPI("origin");
        TokenManager::instance().saveToken(QString("capability_module"), QString("cap-token"));
        m_client = m_api->getClient("capability_module");
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

TEST_F(LogosApiClientStdStringTest, InformModuleTokenStdStringCompilable)
{
    // Verifies the std::string overload compiles and delegates without crashing.
    // Return value is transport-dependent (false in mock); we only confirm no crash.
    bool result = m_client->informModuleToken(
        std::string("cap-token"),
        std::string("some_module"),
        std::string("some-token-value")
    );
    (void)result;
    SUCCEED();
}

// ============================================================
// LogosAPIProvider::registerObject std::string overload
// ============================================================

// Minimal LogosProviderObject for testing the LogosProviderObject* registration path (with QString name)
class StdStringTestProvider : public LogosProviderBase {
public:
    QString providerName()  const override { return "std_string_test"; }
    QString providerVersion() const override { return "1.0.0"; }
    QVariant callMethod(const QString&, const QVariantList&) override { return QVariant(); }
    QJsonArray getMethods() override { return QJsonArray(); }
};

// Minimal QObject to exercise the QObject* overload with std::string name - kept for future use


class LogosApiProviderStdStringTest : public ::testing::Test {
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

TEST_F(LogosApiProviderStdStringTest, RegisterQObjectWithStdStringNullFails)
{
    LogosAPI api(std::string("std_mod_null"));
    LogosAPIProvider* prov = api.getProvider();
    EXPECT_FALSE(prov->registerObject(std::string("std_mod_null"),
                                      static_cast<QObject*>(nullptr)));
}

TEST_F(LogosApiProviderStdStringTest, RegisterQObjectWithStdStringSucceeds)
{
    LogosAPI api(std::string("std_mod_qobj"));
    LogosAPIProvider* prov = api.getProvider();
    // Plain QObject* — the std::string name overload delegates to registerObject(QString, QObject*)
    QObject* obj = new QObject();
    EXPECT_TRUE(prov->registerObject(std::string("std_mod_qobj"), obj));
}

TEST_F(LogosApiProviderStdStringTest, RegisterQObjectWithStdStringDoubleRegisterFails)
{
    LogosAPI api(std::string("std_mod_dbl"));
    LogosAPIProvider* prov = api.getProvider();

    QObject* obj1 = new QObject();
    QObject* obj2 = new QObject();
    EXPECT_TRUE(prov->registerObject(std::string("std_mod_dbl"), obj1));
    EXPECT_FALSE(prov->registerObject(std::string("std_mod_dbl"), obj2));
    delete obj2;
}
