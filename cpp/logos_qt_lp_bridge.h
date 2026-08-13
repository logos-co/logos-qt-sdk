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

#include "logos_api.h"            // LogosAPI (logos-qt-host, via the forwarder
                                  // installed next to this header)

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

    // The error-carrying async: `cb` fires exactly once with the result JSON
    // AND the call error.
    //
    // It deliberately does NOT go through m_client, and that is the only reason
    // it is written out here rather than being one more forward. logos::LpClient
    // (logos-cpp-sdk) has no error-carrying async to borrow: its result
    // trampoline collapses the C ABI's failure form — `ok == 0` with `json` set
    // to the canonical {code, message, origin} object — into a bare JSON null,
    // which is also what a successful call returning nothing delivers. A
    // `<name>AsyncResult` built on that would report ok() for a call to a module
    // that is NOT LOADED, i.e. exactly the "error channel that lies" the cpp
    // generator refused to emit over this transport back when lp_invoke_async
    // itself hard-coded ok = 1. The transport reports it properly now (it binds
    // LogosAPIClient's CallError-aware overload); only the C++ wrapper in front
    // of it still drops it, so this goes straight to the C ABI.
    //
    // The cost is the second client below. A logos::LpClient::invokeAsync
    // overload taking std::function<void(nlohmann::json, const CallError&)>
    // retires this whole member, its trampoline and that second connection.
    void invokeAsyncResult(const std::string& method,
                           const nlohmann::json& args,
                           std::function<void(nlohmann::json, const logos::CallError&)> cb,
                           int timeoutMs)
    {
        lp_client* c = resultClient();
        if (!c) {
            cb(nlohmann::json(),
               logos::callErrorObjectUnavailable(m_target,
                                                 "could not create client for " + m_target));
            return;
        }
        auto* box = new ResultBox(std::move(cb));
        const std::string argsStr = args.dump();
        const int rc = lp_invoke_async(c, method.c_str(), argsStr.c_str(), timeoutMs,
                                       &LpBridge::resultTrampoline, box);
        if (rc != LP_OK) {
            // The C ABI documents that a synchronous LP_ERR_INVALID_ARG does
            // NOT call back, so the completion is this function's to make —
            // the callback still has to fire exactly once.
            ResultBox fn = std::move(*box);
            delete box;
            fn(nlohmann::json(),
               logos::callErrorCallFailed(m_target, "lp_invoke_async refused the call (rc="
                                                        + std::to_string(rc) + ")"));
        }
    }

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
    using ResultBox = std::function<void(nlohmann::json, const logos::CallError&)>;

    LpBridge(LogosAPI* api, std::string target, std::string origin)
        : m_api(api), m_target(std::move(target)), m_origin(std::move(origin)),
          m_client(m_target, m_origin) {}

    // The client invokeAsyncResult calls the C ABI on, created on first use and
    // never destroyed — same lifetime as m_client, and destroying it is what
    // would make a late callback a use-after-free.
    //
    // A SECOND connection to the same target for a wrapper that uses both async
    // flavours, which is the price of not being able to reach m_client's
    // lp_client (logos::LpClient owns it privately, as it must to destroy it).
    // It is a second transport, not a second behaviour: same target, same
    // origin, same process-default transport config, same token flow — tokens
    // live in the process-wide TokenManager, so the second client reuses
    // whatever the first one already minted.
    // Constructed under its OWN once_flag rather than under m_mutex, and that
    // is load-bearing rather than a style choice. On the QtRO transport —
    // the default inside a module process — lp_client_create ends in
    // runOnQtMainThread, i.e. a Qt::BlockingQueuedConnection when called off
    // the main thread. Holding m_mutex across that would let a worker thread
    // sit on the mutex waiting for the main thread while the main thread sits
    // in keep() — reached by every generated on<Event> subscription — waiting
    // for the same mutex. Neither ever runs the queued construction.
    //
    // m_subs is the only thing m_mutex protects; giving the lazy client a
    // separate flag means the two can never contend.
    lp_client* resultClient()
    {
        syncTokens();
        std::call_once(m_resultClientOnce, [this] {
            m_resultClient = lp_client_create(m_target.c_str(), m_origin.c_str(),
                                              nullptr, nullptr);
        });
        return m_resultClient;
    }

    // ok == 0 means `json` IS the canonical error object rather than a value —
    // the same one lp_invoke would have written to out_error_json — so it is
    // decoded into the CallError and the value stays null.
    static void resultTrampoline(int ok, const char* json, void* ud)
    {
        auto* fn = static_cast<ResultBox*>(ud);
        nlohmann::json parsed;  // null
        if (json) {
            auto p = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
            if (!p.is_discarded()) parsed = std::move(p);
        }
        logos::CallError err;
        if (!ok) {
            err = logos::callErrorCallFailed("", "lp_invoke_async failed");
            if (parsed.is_object()) {
                if (parsed.contains("code") && parsed["code"].is_string())
                    err.code = parsed["code"].get<std::string>();
                if (parsed.contains("message") && parsed["message"].is_string())
                    err.message = parsed["message"].get<std::string>();
                if (parsed.contains("origin") && parsed["origin"].is_string())
                    err.origin = parsed["origin"].get<std::string>();
            }
            parsed = nlohmann::json();
        }
        (*fn)(std::move(parsed), err);
        delete fn;  // result callback fires exactly once
    }

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
    std::string m_target;
    std::string m_origin;
    logos::LpClient m_client;
    lp_client* m_resultClient = nullptr;
    std::once_flag m_resultClientOnce;
    std::mutex m_mutex;   // guards m_subs only — see resultClient()
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

inline void invokeAsyncResult(LpBridge* bridge,
                              const std::string& method,
                              const nlohmann::json& args,
                              std::function<void(nlohmann::json, const logos::CallError&)> cb,
                              int timeoutMs = 0)
{
    if (!cb) return;
    if (!bridge) {
        // Reported rather than delivered as a silent default: this surface's
        // whole point is that the callback can tell.
        logos::CallError err;
        err.code = "object_unavailable";
        err.message = "no LogosAPI: consumer wrapper has no transport";
        cb(nlohmann::json(), err);
        return;
    }
    bridge->invokeAsyncResult(method, args, std::move(cb), timeoutMs);
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
