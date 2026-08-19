// Tests for logos_qt_host_core.h — the Qt marshalling layer over
// logos::host::LogosCore.
//
// The substance (RAII, char** ownership, the pre-start ordering constraint,
// the stats-blob parse) is tested in logos-cpp-sdk's test_logos_host_core.cpp.
// What is worth testing HERE is only what this layer adds: the std ⇄ Qt
// conversion at the boundary, and specifically the places where a conversion
// can quietly lose information.
//
// Same technique as the std suite: the logos_core_* ABI is `extern "C"`, so
// this translation unit defines it, allocating exactly as liblogos does.

#include "logos_qt_host_core.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct CoreStub {
    std::vector<std::string> known{"alpha", "beta"};
    std::vector<std::string> loaded{"alpha"};
    std::string statsJson =
        R"([{"name":"alpha","cpu":7.25,"memory":8192,"extra":"kept","flag":true}])";
    bool tokenPresent = true;
};

CoreStub* g = nullptr;

char* dupC(const std::string& s)
{
    char* r = new char[s.size() + 1];
    std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

char** dupCArray(const std::vector<std::string>& xs)
{
    char** a = new char*[xs.size() + 1];
    for (std::size_t i = 0; i < xs.size(); ++i) a[i] = dupC(xs[i]);
    a[xs.size()] = nullptr;
    return a;
}

class QtHostCoreTest : public ::testing::Test {
protected:
    void SetUp() override { stub = CoreStub{}; g = &stub; }
    void TearDown() override { g = nullptr; }
    CoreStub stub;
};

} // namespace

extern "C" {
void logos_core_init(int, char**) {}
void logos_core_start() {}
void logos_core_cleanup() {}
void logos_core_add_modules_dir(const char*) {}
void logos_core_set_persistence_base_path(const char*) {}
void logos_core_set_access_policy(const char*) {}
void logos_core_set_module_transports(const char*, const char*) {}
void logos_core_refresh_modules() {}
char** logos_core_get_known_modules()  { return dupCArray(g->known); }
char** logos_core_get_loaded_modules() { return dupCArray(g->loaded); }
char** logos_core_get_module_dependencies(const char*, bool) { return dupCArray({"d1"}); }
char** logos_core_get_module_dependents(const char*, bool)   { return dupCArray({}); }
int  logos_core_load_module(const char*, bool)   { return 1; }
int  logos_core_unload_module(const char*, bool) { return 1; }
char* logos_core_get_modules_info()          { return dupC("[]"); }
char* logos_core_process_module(const char*) { return dupC("ok"); }
char* logos_core_get_token(const char*)      { return g->tokenPresent ? dupC("tok") : nullptr; }
char* logos_core_get_module_stats()          { return dupC(g->statsJson); }
}

namespace {

using logos::qt::QtLogosCore;
using logos::host::LogosCore;

TEST_F(QtHostCoreTest, StringVectorsBecomeQStringLists)
{
    QtLogosCore core(0, nullptr, LogosCore::Config{});
    EXPECT_EQ(core.knownModules(), (QStringList{"alpha", "beta"}));
    EXPECT_EQ(core.loadedModules(), (QStringList{"alpha"}));
    EXPECT_TRUE(core.dependents("alpha").isEmpty());
    EXPECT_EQ(core.dependencies("alpha"), (QStringList{"d1"}));
}

TEST_F(QtHostCoreTest, QStringArgumentsReachTheStdLayer)
{
    QtLogosCore core(0, nullptr, LogosCore::Config{});
    EXPECT_TRUE(core.loadModule(QStringLiteral("alpha")));
    EXPECT_TRUE(core.unloadModule(QStringLiteral("alpha")));
}

TEST_F(QtHostCoreTest, StatsCarryBothModelledAndUnmodelledFields)
{
    QtLogosCore core(0, nullptr, LogosCore::Config{});
    const QVariantMap m = core.moduleStats(QStringLiteral("alpha"));

    // Modelled, under the struct's spelling.
    EXPECT_EQ(m.value("name").toString(), QStringLiteral("alpha"));
    EXPECT_DOUBLE_EQ(m.value("cpuPercent").toDouble(), 7.25);
    EXPECT_EQ(m.value("memoryBytes").toLongLong(), 8192);

    // Unmodelled keys survive, so a field added to liblogos' stats JSON is
    // readable without this header growing first.
    EXPECT_EQ(m.value("extra").toString(), QStringLiteral("kept"));
    EXPECT_EQ(m.value("flag").toBool(), true);
}

TEST_F(QtHostCoreTest, StatsForAnUnloadedModuleIsAnEmptyMapNotAPartialOne)
{
    QtLogosCore core(0, nullptr, LogosCore::Config{});
    EXPECT_TRUE(core.moduleStats(QStringLiteral("nope")).isEmpty());
}

TEST_F(QtHostCoreTest, AllStatsReturnsOneEntryPerModule)
{
    QtLogosCore core(0, nullptr, LogosCore::Config{});
    EXPECT_EQ(core.allStats().size(), 1);
}

TEST_F(QtHostCoreTest, AbsentTokenIsEmptyQStringAndTheDistinctionStaysReachable)
{
    QtLogosCore core(0, nullptr, LogosCore::Config{});
    EXPECT_EQ(core.token(QStringLiteral("core")), QStringLiteral("tok"));

    stub.tokenPresent = false;
    EXPECT_TRUE(core.token(QStringLiteral("core")).isEmpty());
    // The Qt layer flattens nullopt to an empty QString, which is lossy. The
    // escape hatch to the std layer must keep the distinction available.
    EXPECT_FALSE(core.core().token("core").has_value());
}

TEST_F(QtHostCoreTest, MalformedStatsDoesNotThrowThroughTheQtLayer)
{
    stub.statsJson = "{not json";
    QtLogosCore core(0, nullptr, LogosCore::Config{});
    EXPECT_TRUE(core.allStats().isEmpty());
    EXPECT_TRUE(core.moduleStats(QStringLiteral("alpha")).isEmpty());
}

} // namespace
