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

#include <atomic>
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
    // How a bridge mirrors the host's bootstrap tokens into the plugin's own
    // TokenManager, when it has a LogosAPI to read them from. A raw function
    // pointer rather than a std::function so it is trivially atomic (and so the
    // one implementation stays visible in this header).
    using SyncFn = void (*)(LpBridge*);

public:
    logos::LpClient& client() { syncTokens(); return m_client; }

    // The error-carrying async: `cb` fires exactly once with the result JSON
    // AND the call error.
    //
    // A straight forward to logos::LpClient, as it always should have been.
    // This used to be written out here over a SECOND, privately-owned
    // lp_client, because LpClient had no error-carrying async to borrow: its
    // result trampoline collapsed the C ABI's failure form — `ok == 0` with
    // `json` set to the canonical {code, message, origin} object — into a bare
    // JSON null, which is also what a successful call returning nothing
    // delivers. logos-cpp-sdk#142 added LpClient::invokeAsyncResult with
    // byte-identical decoding, so the duplicate is gone, and with it the second
    // connection per (origin, target) pair and the deliberate leak that came
    // with it — that client was created on first use and never destroyed.
    //
    // syncTokens() still runs first: client() does it, exactly as the retired
    // private accessor did.
    void invokeAsyncResult(const std::string& method,
                           const nlohmann::json& args,
                           std::function<void(nlohmann::json, const logos::CallError&)> cb,
                           int timeoutMs)
    {
        client().invokeAsyncResult(method, args, std::move(cb), timeoutMs);
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

    // The bridge for (this module, `target`), where "this module" is the one
    // the LogosAPI speaks for. Stable address: entries are never erased and are
    // held by unique_ptr, so a wrapper may keep the pointer.
    static LpBridge* forTarget(LogosAPI* api, const QString& target)
    {
        if (!api) return nullptr;
        // The one place an origin is DERIVED rather than stated. Correct here,
        // because `api` IS this module's own identity object — and the reason
        // forOrigin below exists rather than a wrapper reusing this: a consumer
        // handed someone else's LogosAPI would silently call out under the
        // LENDER's name, which is a privilege escalation the transport cannot
        // see. Anything without its own LogosAPI must state its origin.
        // `&syncFromApi` is taken HERE and nowhere else, and that placement is
        // load-bearing rather than tidy — see syncFromApi's own comment.
        return lookup(api->moduleName().toStdString(), target.toStdString(), api,
                      &LpBridge::syncFromApi);
    }

    // The bridge for (`origin`, `target`) with NO LogosAPI: the binding a
    // generated consumer wrapper uses when the module it belongs to has no
    // identity object — a cdylib module, whose provider surface is the std
    // `logos_module_impl.h` C ABI.
    //
    // `origin` is ASSERTED, never derived. It is the consuming module's own
    // name, baked into the generated umbrella from `metadata.json#name` and
    // threaded through the wrapper's constructor verbatim. There is deliberately
    // no overload that infers it from anything: inference is what let a reused
    // bridge inherit its caller's identity.
    //
    // The absent LogosAPI costs nothing HERE, and that is a property of the
    // caller rather than luck. `syncTokens` exists to mirror the host's
    // bootstrap tokens into the plugin's own TokenManager, because a Qt PLUGIN
    // links its own copy of the protocol library and the host writes to a
    // different instance. A cdylib module has no such gap: its glue already
    // forwards tokens across the C ABI (`logos_module_accept_token` ->
    // `lp_token_save`) into the very TokenManager this bridge's lp client
    // reads. So a null api means "tokens arrive by the other route", not
    // "tokens are missing" — and `syncTokens` correctly does nothing.
    static LpBridge* forOrigin(const QString& origin, const QString& target)
    {
        return lookup(origin.toStdString(), target.toStdString(), nullptr, nullptr);
    }

private:
    // One bridge per (origin, target) for BOTH factories — the invariant this
    // class documents is about the pair, not about how the pair was spelled.
    //
    // `api` is adopted rather than overwritten: null -> non-null only, never the
    // reverse. Sharing the registry otherwise has one failure mode — an
    // api-less bridge created first would leave a later Qt-plugin caller
    // without `syncTokens`, i.e. an empty auth token and a call that fails
    // silently, the exact defect syncTokens was added for. Adoption removes it.
    // (Two LogosAPIs answering the same moduleName would be pathological; the
    // first one wins.)
    static LpBridge* lookup(const std::string& origin, const std::string& target,
                            LogosAPI* api, SyncFn sync)
    {
        const std::string key = origin + "\x1f" + target;
        static std::mutex s_mutex;
        static std::map<std::string, std::unique_ptr<LpBridge>> s_bridges;
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_bridges.find(key);
        if (it == s_bridges.end()) {
            it = s_bridges.emplace(key, std::unique_ptr<LpBridge>(
                                            new LpBridge(api, sync, target, origin)))
                     .first;
        } else if (sync && !it->second->m_sync.load(std::memory_order_relaxed)) {
            // Publish the api BEFORE the hook that reads it: a concurrent
            // syncTokens acquire-loads m_sync, so seeing a non-null hook implies
            // seeing the api it was installed for.
            it->second->m_api.store(api, std::memory_order_relaxed);
            it->second->m_sync.store(sync, std::memory_order_release);
        }
        return it->second.get();
    }

    LpBridge(LogosAPI* api, SyncFn sync, std::string target, std::string origin)
        : m_api(api), m_sync(sync), m_target(std::move(target)),
          m_origin(std::move(origin)), m_client(m_target, m_origin) {}

    // Mirror the bootstrap tokens the host delivered to the LogosAPI's
    // TokenManager into the one the plugin-side lp client reads. Cheap (two
    // hash lookups) and idempotent, so it runs before every use rather than
    // once — a token that rotates later must not leave the lp side stale.
    // Per-target tokens are NOT mirrored: the lp client mints its own through
    // the same capability flow once it can authenticate.
    void syncTokens()
    {
        SyncFn fn = m_sync.load(std::memory_order_acquire);
        if (fn) fn(this);
    }

    // The ONLY code in this class that touches LogosAPI's or TokenManager's
    // out-of-line members, and it is reached exclusively through a pointer
    // installed by forTarget.
    //
    // That indirection is the point, and it is not style. This is a header, so
    // every function in it is emitted into a translation unit only if that TU
    // ODR-uses it — and ODR-use is STATIC, not a runtime branch. When syncTokens
    // named `m_api->getTokenManager()` directly, syncTokens was reached from
    // `client()`, which every generated call goes through; so ANY TU using this
    // seam emitted a reference to `LogosAPI::getTokenManager()` and
    // `TokenManager::getToken()` — including one whose bridge is api-less by
    // construction and could never call them. Measured on the origin-bound
    // compile probe: two undefined Qt-host symbols in a translation unit whose
    // whole premise is that it needs no Qt host identity object.
    //
    // Taking `&syncFromApi` inside forTarget — and passing it down, rather than
    // branching on `api` inside lookup, which both factories call — moves that
    // ODR-use to the one place that genuinely means it. A module that only ever
    // calls forOrigin links none of it.
    static void syncFromApi(LpBridge* self)
    {
        LogosAPI* api = self->m_api.load(std::memory_order_relaxed);
        if (!api) return;
        TokenManager* tm = api->getTokenManager();
        if (!tm) return;
        static const char* const kBootstrapKeys[] = { "capability_module", "core" };
        for (const char* key : kBootstrapKeys) {
            const QString token = tm->getToken(QString::fromLatin1(key));
            if (!token.isEmpty()) lp_token_save(key, token.toUtf8().constData());
        }
    }

    // Atomic only so `lookup` may adopt one into an already-published bridge
    // without racing a concurrent `syncTokens`; the pointer itself never
    // changes twice. Read ONLY by syncFromApi.
    std::atomic<LogosAPI*> m_api{nullptr};
    // Null on the forOrigin path, and left null: an api-less bridge has nothing
    // to mirror (a cdylib's tokens arrive through the C ABI instead).
    std::atomic<SyncFn> m_sync{nullptr};
    std::string m_target;
    std::string m_origin;
    logos::LpClient m_client;
    std::mutex m_mutex;   // guards m_subs only
    std::vector<logos::LpSubscription> m_subs;
};

// ── what a generated wrapper actually calls ─────────────────────────────────
//
// Free functions taking a possibly-null bridge, so the emitted bodies stay one
// line each and no generated code has to know what "no bridge" means. A null
// bridge only happens on the forTarget path, when the wrapper was constructed
// with a null LogosAPI — which already could not work — and it yields a JSON
// null, which every `fromWire<T>` turns into a default-constructed T. One
// expression, no per-type default table. (forOrigin cannot return null: an
// asserted origin is always enough to key a bridge.)

inline nlohmann::json invoke(LpBridge* bridge,
                             const std::string& method,
                             const nlohmann::json& args,
                             logos::CallError* err,
                             int timeoutMs = 0)
{
    if (!bridge) {
        if (err) {
            err->code = "object_unavailable";
            err->message = "consumer wrapper has no transport (null bridge)";
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
        err.message = "consumer wrapper has no transport (null bridge)";
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

// The target's subscription state, forwarded to the bridge's client.
//
// Per MODULE rather than per subscription, because that is the granularity the
// runtime has: an LpBridge is (this module, one target), every subscribe()
// through it attaches to that target's single handle, and a provider that dies
// takes all of them down together.
//
// keep() still parks each subscription handle for the process lifetime, exactly
// as before — the Qt consumer never owned it and still does not. These give the
// author the state that handle would otherwise have carried.

inline void onSubscriptionStatus(LpBridge* bridge,
                                 std::function<void(logos::SubStatus, std::uint64_t)> cb)
{
    if (!bridge) return;
    bridge->client().onSubscriptionStatus(std::move(cb));
}

inline std::uint64_t subscriptionGeneration(LpBridge* bridge)
{
    return bridge ? bridge->client().subscriptionGeneration() : 0;
}

inline void setRestartPolicy(LpBridge* bridge, logos::RestartPolicy policy)
{
    if (!bridge) return;
    bridge->client().setRestartPolicy(policy);
}

inline bool rearmSubscriptions(LpBridge* bridge)
{
    return bridge && bridge->client().rearmSubscriptions();
}

}  // namespace qt
}  // namespace logos

#endif  // LOGOS_QT_LP_BRIDGE_H
