// Tests for logos_qt_host_core.h — the Qt marshalling layer over
// logos::host::LogosCore.
//
// The substance (RAII, the pre-start ordering constraint, the shell binding,
// the stats parse) is tested in logos-cpp-sdk's test_logos_host_core.cpp. What
// is worth testing HERE is only what this layer adds: the std ⇄ Qt conversion at
// the boundary, and specifically the places where a conversion can quietly lose
// information.
//
// Same technique as the std suite: the C API and the shell binding are
// `extern "C"`, so this translation unit defines them, and core_service answers
// each method from a canned reply.

#include "logos_qt_host_core.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct CoreStub {
    // core_service over the shell binding: the calls made, one answer per method
    // (or per method and arguments, which wins).
    std::vector<std::pair<std::string, std::string>> coreServiceCalls;
    std::map<std::string, std::string> answers;
    std::map<std::pair<std::string, std::string>, std::string> answersFor;
};

CoreStub* g = nullptr;

char* dupC(const std::string& s)
{
    char* r = new char[s.size() + 1];
    std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

char* mallocCopy(const std::string& s)
{
    char* r = static_cast<char*>(std::malloc(s.size() + 1));
    std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
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
char* logos_core_process_module(const char*) { return dupC("ok"); }
int logos_core_set_bundled_modules_dirs(const char* const*) { return 0; }
int logos_core_set_placement_policy(const char*) { return 0; }
int logos_core_set_shell_identity(const char*) { return 0; }
int logos_core_set_package_config(const char*) { return 0; }

char gBindingTag;
logos_consumer* logos_core_take_shell_binding(void) { return reinterpret_cast<logos_consumer*>(&gBindingTag); }
const char* logos_consumer_name(const logos_consumer*) { return "basecamp"; }
char* logos_consumer_credential(const logos_consumer*) { return mallocCopy("shell-cr"); }
int logos_consumer_call(logos_consumer*, const char*, const char* method, const char* args, int,
                        char** out, char** err)
{
    g->coreServiceCalls.emplace_back(method, args);
    auto exact = g->answersFor.find({method, args});
    const std::string* answer = exact != g->answersFor.end() ? &exact->second : nullptr;
    if (!answer) {
        const auto it = g->answers.find(method);
        if (it == g->answers.end()) return -1;
        answer = &it->second;
    }
    *out = mallocCopy(*answer);
    *err = nullptr;
    return 0;
}
logos_consumer_subscription* logos_consumer_subscribe(logos_consumer*, const char*, const char*,
                                                      logos_consumer_event_cb, void*) { return nullptr; }
void logos_consumer_unsubscribe(logos_consumer_subscription*) {}
void logos_consumer_string_free(char* value) { std::free(value); }
void logos_consumer_release(logos_consumer*) {}
}

namespace {

using logos::qt::QtLogosCore;
using logos::host::LogosCore;

LogosCore::Config shellConfig()
{
    LogosCore::Config cfg;
    cfg.shellName = "basecamp";
    return cfg;
}

const char* kStats =
    R"([{"name":"alpha","cpu_percent":7.25,"cpu_time_seconds":1.5,)"
    R"("memory_mb":8192.0,"extra":"kept","flag":true}])";

TEST_F(QtHostCoreTest, AShellNameIsRequired)
{
    EXPECT_THROW(QtLogosCore(0, nullptr, LogosCore::Config{}), std::invalid_argument);
}

TEST_F(QtHostCoreTest, StringVectorsBecomeQStringLists)
{
    stub.answersFor = {
        {{"listModules", R"(["all"])"}, R"([{"name":"alpha"},{"name":"beta"}])"},
        {{"listModules", R"(["loaded"])"}, R"([{"name":"alpha"}])"},
    };
    stub.answers = {{"getModuleDependencies", R"(["d1"])"}, {"getModuleDependents", "[]"}};
    QtLogosCore core(0, nullptr, shellConfig());
    core.start();
    EXPECT_EQ(core.knownModules(), (QStringList{"alpha", "beta"}));
    EXPECT_EQ(core.loadedModules(), (QStringList{"alpha"}));
    EXPECT_TRUE(core.dependents("alpha").isEmpty());
    EXPECT_EQ(core.dependencies("alpha"), (QStringList{"d1"}));
}

TEST_F(QtHostCoreTest, QStringArgumentsReachCoreService)
{
    stub.answers = {
        {"loadModule", R"({"status":"ok","module":"alpha"})"},
        {"unloadModule", R"({"status":"ok","module":"alpha"})"},
        {"getOptionalLoadReport",
         R"([{"module":"extra","named_by":"alpha","reason":"not_installed"}])"},
    };
    QtLogosCore core(0, nullptr, shellConfig());
    core.start();
    EXPECT_TRUE(core.loadModule(QStringLiteral("alpha")));
    EXPECT_TRUE(core.loadModule(QStringLiteral("alpha"), LOGOS_LOAD_REQUIRED_AND_OPTIONAL));
    EXPECT_TRUE(core.optionalLoadReportJson(QStringLiteral("alpha")).contains("extra"));
    EXPECT_TRUE(core.unloadModule(QStringLiteral("alpha")));
    ASSERT_EQ(stub.coreServiceCalls.size(), 4u);
    // The Qt default stays what `withDependencies = true` meant: the required tree.
    EXPECT_EQ(stub.coreServiceCalls[0].second, R"(["alpha","required"])");
    EXPECT_EQ(stub.coreServiceCalls[1].second, R"(["alpha","required_and_optional"])");
    EXPECT_EQ(stub.coreServiceCalls[3].second, R"(["alpha",false])");
}

TEST_F(QtHostCoreTest, StatsCarryBothModelledAndUnmodelledFields)
{
    stub.answers = {{"getModuleStats", kStats}};
    QtLogosCore core(0, nullptr, shellConfig());
    core.start();
    const QVariantMap m = core.moduleStats(QStringLiteral("alpha"));

    // Modelled, under the struct's spelling.
    EXPECT_EQ(m.value("name").toString(), QStringLiteral("alpha"));
    EXPECT_DOUBLE_EQ(m.value("cpuPercent").toDouble(), 7.25);
    EXPECT_DOUBLE_EQ(m.value("memoryMb").toDouble(), 8192.0);

    // Unmodelled keys survive, so a field added to liblogos' stats JSON is
    // readable without this header growing first.
    EXPECT_EQ(m.value("extra").toString(), QStringLiteral("kept"));
    EXPECT_EQ(m.value("flag").toBool(), true);
}

TEST_F(QtHostCoreTest, StatsForAnUnloadedModuleIsAnEmptyMapNotAPartialOne)
{
    stub.answers = {{"getModuleStats", kStats}};
    QtLogosCore core(0, nullptr, shellConfig());
    core.start();
    EXPECT_TRUE(core.moduleStats(QStringLiteral("nope")).isEmpty());
}

TEST_F(QtHostCoreTest, AllStatsReturnsOneEntryPerModule)
{
    stub.answers = {{"getModuleStats", kStats}};
    QtLogosCore core(0, nullptr, shellConfig());
    core.start();
    EXPECT_EQ(core.allStats().size(), 1);
}

TEST_F(QtHostCoreTest, AbsentModulesInfoIsEmptyQStringAndTheDistinctionStaysReachable)
{
    QtLogosCore core(0, nullptr, shellConfig());
    core.start();
    EXPECT_TRUE(core.modulesInfoJson().isEmpty());
    // The Qt layer flattens nullopt to an empty QString, which is lossy. The
    // escape hatch to the std layer must keep the distinction available.
    EXPECT_FALSE(core.core().modulesInfoJson().has_value());

    stub.answers = {{"getModulesInfo", R"([{"name":"alpha"}])"}};
    EXPECT_TRUE(core.modulesInfoJson().contains("alpha"));
}

TEST_F(QtHostCoreTest, AShellAdmitsItsUiPluginsInQtTypes)
{
    stub.answers = {
        {"admitConsumer", R"({"status":"ok","name":"my_ui","credential":"cred-1"})"},
        {"retireConsumer", R"({"status":"ok","name":"my_ui"})"},
    };
    QtLogosCore core(0, nullptr, shellConfig());
    EXPECT_FALSE(core.shellBound()) << "the binding comes with start()";
    core.start();
    ASSERT_TRUE(core.shellBound());
    EXPECT_EQ(core.shellCredential(), QStringLiteral("shell-cr"));
    EXPECT_EQ(core.admitConsumer(QStringLiteral("my_ui")), QStringLiteral("cred-1"));
    EXPECT_TRUE(core.retireConsumer(QStringLiteral("my_ui")));
}

TEST_F(QtHostCoreTest, MalformedStatsDoesNotThrowThroughTheQtLayer)
{
    stub.answers = {{"getModuleStats", "{not json"}};
    QtLogosCore core(0, nullptr, shellConfig());
    core.start();
    EXPECT_TRUE(core.allStats().isEmpty());
    EXPECT_TRUE(core.moduleStats(QStringLiteral("alpha")).isEmpty());
}

} // namespace
