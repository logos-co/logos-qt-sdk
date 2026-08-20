#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// logos_qt_host_core.h — Qt marshalling over logos::host::LogosCore.
//
// The HOST capability, Qt-typed. logos-cpp-sdk's `logos_host_core.h` owns the
// substance — RAII around logos_core_init/cleanup, the `delete[]`-not-`free()`
// ownership rules, the pre-start ordering constraint, and the module-stats blob
// parse. This adds exactly one thing: std ⇄ Qt conversion at the boundary, so a
// Qt host writes `QStringList` and `QVariantMap` instead of converting at every
// call site.
//
// ── Why there is no Q_OBJECT, no signal and no timer here ───────────────────
// logos-qt-sdk is an INTERFACE library — header-only by design since the host
// split (see cpp/CMakeLists.txt). A Q_OBJECT living in an external include
// directory is not reliably processed by a consumer's AUTOMOC, which scans the
// consumer's own sources; making it work would mean shipping a compiled source
// from this package again and undoing that split.
//
// It is also the right split on the merits. Change notification and stats
// polling are APPLICATION policy: what interval to poll on, which thread the
// timer lives on, and when "the module set changed" should be announced are
// decisions this package has no basis to make. logos-basecamp's
// CoreModuleManager stays the QObject that owns `coreModulesChanged()` and the
// poll timer; it just holds one of these instead of calling the C API itself.
//
// ── Which target to link ────────────────────────────────────────────────────
//     logos-qt-sdk::logos_qt_host_core
// which carries logos-cpp-sdk::logos_host transitively. NOT
// logos-qt-host::logos_qt_host — that is the Qt PLUGIN runtime (LogosAPI,
// providers) and is a different capability with an unfortunately similar name.
// A program that only manages a core needs none of it.
// ─────────────────────────────────────────────────────────────────────────────

#include "logos_host_core.h"

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <string>
#include <utility>
#include <vector>

namespace logos {
namespace qt {

namespace detail {

inline QStringList toQStringList(const std::vector<std::string>& xs)
{
    QStringList out;
    out.reserve(static_cast<int>(xs.size()));
    for (const std::string& x : xs) out.append(QString::fromStdString(x));
    return out;
}

// nlohmann → QVariant for the stats entries. Deliberately local and minimal:
// stats values are numbers, strings and bools, so this does not pull in the
// general converter (logos_json_convert.h, logos-protocol) that the CONSUMER
// capability uses — the host target must not acquire a protocol dependency for
// four scalar cases.
inline QVariant toQVariant(const nlohmann::json& v)
{
    if (v.is_boolean())        return QVariant(v.get<bool>());
    if (v.is_number_integer()) return QVariant(static_cast<qlonglong>(v.get<long long>()));
    if (v.is_number_float())   return QVariant(v.get<double>());
    if (v.is_string())         return QVariant(QString::fromStdString(v.get<std::string>()));
    if (v.is_null())           return QVariant();
    // Anything structured is handed back as its serialised form rather than
    // silently dropped, so a field this SDK does not model is still readable.
    return QVariant(QString::fromStdString(v.dump()));
}

inline QVariantMap toQVariantMap(const host::ModuleStats& s)
{
    QVariantMap m;
    // The modelled fields, named as the struct names them.
    m.insert(QStringLiteral("name"),        QString::fromStdString(s.name));
    m.insert(QStringLiteral("cpuPercent"),     s.cpuPercent);
    m.insert(QStringLiteral("cpuTimeSeconds"), s.cpuTimeSeconds);
    // MEGABYTES. This was `memoryBytes` as a qlonglong, mirroring a struct
    // member that both misnamed the unit and read a JSON key nothing emits.
    m.insert(QStringLiteral("memoryMb"),       s.memoryMb);
    // Plus every raw key, so a consumer sees fields added to liblogos' stats
    // JSON without waiting for this header to grow them. Modelled keys above
    // win on collision, since they are the documented spelling.
    if (s.raw.is_object()) {
        for (auto it = s.raw.begin(); it != s.raw.end(); ++it) {
            const QString key = QString::fromStdString(it.key());
            if (!m.contains(key)) m.insert(key, toQVariant(it.value()));
        }
    }
    return m;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// QtLogosCore — the same object as logos::host::LogosCore, in Qt types.
//
// Owns the underlying core, so the same rule applies: construct exactly one,
// keep it for the process. Non-copyable and non-movable for the same reason
// (the wrapped type is).
//
//     logos::host::LogosCore::Config cfg;
//     cfg.modulesDirs = { modulesDir.toStdString() };
//
//     logos::qt::QtLogosCore core(argc, argv, std::move(cfg));
//     core.start();
//     for (const QString& m : core.loadedModules()) { ... }
// ─────────────────────────────────────────────────────────────────────────────
class QtLogosCore {
public:
    // Config is taken in std form deliberately. It is set once, at startup,
    // from paths the host already has — inventing a Qt-typed mirror of it would
    // be a second definition of the same thing for one call.
    QtLogosCore(int argc, char* argv[], host::LogosCore::Config config)
        : m_core(argc, argv, std::move(config)) {}

    QtLogosCore(const QtLogosCore&) = delete;
    QtLogosCore& operator=(const QtLogosCore&) = delete;
    QtLogosCore(QtLogosCore&&) = delete;
    QtLogosCore& operator=(QtLogosCore&&) = delete;

    void start()          { m_core.start(); }
    bool isStarted() const { return m_core.isStarted(); }

    bool loadModule(const QString& name, bool withDependencies = true)
    {
        return m_core.loadModule(name.toStdString(), withDependencies);
    }

    bool unloadModule(const QString& name, bool withDependents = false)
    {
        return m_core.unloadModule(name.toStdString(), withDependents);
    }

    void refreshModules() { m_core.refreshModules(); }

    QStringList knownModules() const  { return detail::toQStringList(m_core.knownModules()); }
    QStringList loadedModules() const { return detail::toQStringList(m_core.loadedModules()); }

    QStringList dependencies(const QString& name, bool recursive = false) const
    {
        return detail::toQStringList(m_core.dependencies(name.toStdString(), recursive));
    }

    QStringList dependents(const QString& name, bool recursive = false) const
    {
        return detail::toQStringList(m_core.dependents(name.toStdString(), recursive));
    }

    // Empty QString when absent — distinct from the std layer's nullopt, which
    // stays available through core() for a caller that needs the distinction.
    QString modulesInfoJson() const
    {
        const auto v = m_core.modulesInfoJson();
        return v.has_value() ? QString::fromStdString(*v) : QString();
    }

    QString token(const QString& key) const
    {
        const auto v = m_core.token(key.toStdString());
        return v.has_value() ? QString::fromStdString(*v) : QString();
    }

    // Empty map when the module is not loaded. One C call + one parse; for a
    // whole list use allStats() rather than looping this.
    QVariantMap moduleStats(const QString& moduleName) const
    {
        const auto s = m_core.stats(moduleName.toStdString());
        return s.has_value() ? detail::toQVariantMap(*s) : QVariantMap();
    }

    QVariantList allStats() const
    {
        QVariantList out;
        for (const host::ModuleStats& s : m_core.allStats())
            out.append(detail::toQVariantMap(s));
        return out;
    }

    // Escape hatch to the std layer, for anything this veneer does not mirror
    // (notably the nullopt/empty distinction above).
    host::LogosCore&       core()       { return m_core; }
    const host::LogosCore& core() const { return m_core; }

private:
    host::LogosCore m_core;
};

} // namespace qt
} // namespace logos
