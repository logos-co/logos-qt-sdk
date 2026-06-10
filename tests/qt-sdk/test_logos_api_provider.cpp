#include <gtest/gtest.h>
#include "logos_mock.h"
#include "logos_api.h"
#include "logos_api_provider.h"
#include "logos_provider_object.h"

// Minimal provider for registration testing
class RegistrationTestProvider : public LogosProviderBase {
public:
    QString providerName() const override { return "reg_test"; }
    QString providerVersion() const override { return "1.0.0"; }
    QVariant callMethod(const QString&, const QVariantList&) override { return QVariant(); }
    QJsonArray getMethods() override { return QJsonArray(); }
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
