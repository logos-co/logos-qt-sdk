#ifndef LOGOS_QT_LP_BRIDGE_H
#define LOGOS_QT_LP_BRIDGE_H

// ---------------------------------------------------------------------------
// The Qt <-> lp seam: everything a generated Qt-typed consumer wrapper needs in
// order to be a VENEER over the one transport path instead of a second
// implementation of it.
//
// Background. There used to be two consumer implementations for calling one
// module from another — a Qt-typed one (its own conversion table, its own
// LogosAPIClient calls) and an std-typed `lp` one over the logos-protocol C
// ABI. Two implementations of one behaviour, and the divergences were real:
// the Qt sync and async return tables converted the same type differently, and
// the Qt path had its own idea of how a `result` or a byte string crossed the
// wire. This header exists so the Qt surface can keep its types while its BODY
// is the lp path.
//
// This header is the TRANSPORT half of the seam: one lp client per
// (origin, target) plus the token bridge a Qt plugin needs. The CONVERSION half
// — the only conversion vocabulary a generated wrapper may use — is
// logos_qt_wire.h, included below.
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logos_protocol.h"       // lp_token_save
#include "token_manager.h"        // TokenManager (logos-protocol)
#include "logos_qt_wire.h"        // logos::qt::toWire / fromWire<T> — the edge

#include "logos_lp_client.h"      // logos::LpClient / LpSubscription (logos-cpp-sdk)

#include "logos_api.h"            // LogosAPI

namespace logos {
namespace qt {

// ── the transport seam ──────────────────────────────────────────────────────

// One lp client per (origin, target), for the life of the process, plus the
// subscriptions taken out through it.
//
// Two things force this shape, and both are about matching what the Qt wrapper
// already promised:
//
//   LIFETIME. `modules().bind_<iface>(name)` returns a wrapper BY VALUE, and
//   call sites subscribe on that temporary. That worked because the Qt
//   subscription lived on the LogosAPI-owned LogosAPIClient. An lp subscription
//   is an RAII handle that unsubscribes when it dies, so a wrapper owning one
//   by value would stop delivering the moment the temporary went out of scope.
//   Parking client + subscriptions here keeps the wrapper a thin, copyable
//   handle and preserves the existing lifetime contract exactly — a
//   LogosAPIClient obtained from `api->getClient()` is likewise cached for the
//   module's lifetime and never destroyed.
//
//   TOKENS. A Qt module is a plugin, and it links its own copy of the protocol
//   library — so `TokenManager::instance()` inside the plugin is NOT the
//   instance the LogosAPI (constructed in the host image) hands to its clients.
//   The lp client reads the plugin's; the host writes the host's. Without a
//   bridge, every lp call out of a Qt plugin presents an empty auth token,
//   capability_module refuses to mint a per-target token, and the call returns
//   a default value with NO error surfaced. (The cdylib backend never hits this
//   because its glue forwards tokens across the C ABI through
//   `logos_module_accept_token` -> `lp_token_save`; the Qt backend had no such
//   hook.) `syncTokens` is that hook, and it is a hard prerequisite for this
//   whole design, not an optimisation.
class LpBridge {
public:
    logos::LpClient& client() { syncTokens(); return m_client; }

    // Keep an lp subscription alive for the process, mirroring the Qt
    // channel's "subscribe once, delivered forever" behaviour.
    bool keep(logos::LpSubscription sub)
    {
        if (!sub.valid()) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subs.push_back(std::move(sub));
        return true;
    }

    // The bridge for (this module, `target`). Stable address: entries are
    // never erased and are held by unique_ptr, so a wrapper may keep the
    // pointer.
    static LpBridge* forTarget(LogosAPI* api, const QString& target)
    {
        if (!api) return nullptr;
        const std::string origin = api->moduleName().toStdString();
        const std::string key = origin + "\x1f" + target.toStdString();
        static std::mutex s_mutex;
        static std::map<std::string, std::unique_ptr<LpBridge>> s_bridges;
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_bridges.find(key);
        if (it == s_bridges.end()) {
            it = s_bridges.emplace(key, std::unique_ptr<LpBridge>(
                                            new LpBridge(api, target.toStdString(), origin)))
                     .first;
        }
        return it->second.get();
    }

private:
    LpBridge(LogosAPI* api, std::string target, std::string origin)
        : m_api(api), m_client(std::move(target), std::move(origin)) {}

    // Mirror the bootstrap tokens the host delivered to the LogosAPI's
    // TokenManager into the one the plugin-side lp client reads. Cheap (two
    // hash lookups) and idempotent, so it runs before every use rather than
    // once — a token that rotates later must not leave the lp side stale.
    // Per-target tokens are NOT mirrored: the lp client mints its own through
    // the same capability flow once it can authenticate.
    void syncTokens()
    {
        if (!m_api) return;
        TokenManager* tm = m_api->getTokenManager();
        if (!tm) return;
        static const char* const kBootstrapKeys[] = { "capability_module", "core" };
        for (const char* key : kBootstrapKeys) {
            const QString token = tm->getToken(QString::fromLatin1(key));
            if (!token.isEmpty()) lp_token_save(key, token.toUtf8().constData());
        }
    }

    LogosAPI* m_api = nullptr;
    logos::LpClient m_client;
    std::mutex m_mutex;
    std::vector<logos::LpSubscription> m_subs;
};

// ── what a generated wrapper actually calls ─────────────────────────────────
//
// Free functions taking a possibly-null bridge, so the emitted bodies stay one
// line each and no generated code has to know what "no bridge" means. A null
// bridge only happens when the wrapper was constructed with a null LogosAPI —
// which already could not work — and it yields a JSON null, which every
// `fromWire<T>` turns into a default-constructed T. One expression, no
// per-type default table.

inline nlohmann::json invoke(LpBridge* bridge,
                             const std::string& method,
                             const nlohmann::json& args,
                             logos::CallError* err,
                             int timeoutMs = 0)
{
    if (!bridge) {
        if (err) {
            err->code = "object_unavailable";
            err->message = "no LogosAPI: consumer wrapper has no transport";
        }
        return nlohmann::json();
    }
    return bridge->client().invoke(method, args, err, timeoutMs);
}

inline void invokeAsync(LpBridge* bridge,
                        const std::string& method,
                        const nlohmann::json& args,
                        std::function<void(nlohmann::json)> cb,
                        int timeoutMs = 0)
{
    if (!bridge) { if (cb) cb(nlohmann::json()); return; }
    bridge->client().invokeAsync(method, args, std::move(cb), timeoutMs);
}

inline bool subscribe(LpBridge* bridge,
                      const std::string& event,
                      std::function<void(nlohmann::json)> cb)
{
    if (!bridge) return false;
    return bridge->keep(bridge->client().subscribe(event, std::move(cb)));
}

}  // namespace qt
}  // namespace logos

#endif  // LOGOS_QT_LP_BRIDGE_H
