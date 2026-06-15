#ifndef LOGOS_UI_PLUGIN_CONTEXT_H
#define LOGOS_UI_PLUGIN_CONTEXT_H

#include <type_traits>

// ---------------------------------------------------------------------------
// LogosUiPluginContext — opt-in mixin for codegen-generated UI plugins
//
// A UI plugin (type: ui_qml, interface: universal) is NOT a module: it has no
// host-provisioned identity (no modulePath / instanceId / instancePersistencePath),
// it emits no events of its own, and it exposes no methods to other modules — it
// is a *view* whose backend derives a QtRO `<RepClass>SimpleSource` and pushes
// state to the QML replica through that. The one thing it legitimately needs from
// the framework is typed access to the modules/interfaces it declares as
// `dependencies`. This mixin confines the context to exactly that: `modules()`
// plus the `onContextReady()` lifecycle hook.
//
// This is the Qt-layer counterpart to logos-cpp-sdk's `LogosModuleContext`
// (which serves real, std-typed core/cdylib modules). A UI plugin lives entirely
// in Qt-land — its `.rep` slots are Qt-typed — so its `modules().<dep>` wrappers
// are generated Qt-typed too (api-style `qt`); no std<->Qt conversions at the
// view boundary.
//
// Usage from a UI backend:
//
//     class MyBackend : public MyRepSimpleSource,
//                       public LogosUiPluginContext {
//     public:
//         // ... .rep SLOT overrides ...
//     protected:
//         void onContextReady() override {
//             // modules() is now live — make typed calls or subscribe to a
//             // dependency's typed events here.
//             modules().some_dep.onSomething([this](int v) { /* ... */ });
//         }
//     };
//
// Backends that don't inherit from LogosUiPluginContext are unaffected — the
// generated glue's `maybeSetUiPluginModules` tag-dispatches to a no-op.
// ---------------------------------------------------------------------------

// Per-module aggregate of dependency wrappers. Each module's codegen emits
// `struct LogosModules { ... };` at global scope in its own
// `generated_code/logos_sdk.h` (one accessor per `metadata.json#dependencies`
// entry). Forward-declared here so this header stays decoupled from per-module
// codegen — the backend's translation unit makes the type complete via its own
// `#include "logos_sdk.h"`, at which point the inline `modules()` body compiles.
struct LogosModules;

class LogosUiPluginContext {
public:
    virtual ~LogosUiPluginContext() = default;

    // Typed access to this plugin's per-build `LogosModules` aggregate, which
    // the codegen emits in `generated_code/logos_sdk.h`. It owns one
    // strongly-typed (Qt-typed) client wrapper per entry in `metadata.json`'s
    // `dependencies` list — nothing else. The backend can call those declared
    // deps' methods, and subscribe to their typed events, without ever touching
    // the raw `LogosAPI`:
    //
    //     #include "logos_sdk.h"           // generated at build time
    //
    //     QString MyBackend::libVersion() {
    //         return modules().some_dep.version();
    //     }
    //
    // The pointer is set by the codegen-generated UI plugin's `initLogos`, which
    // constructs the `LogosModules` from the `LogosAPI`. Calling before that
    // (e.g. from a unit test bypassing the generated glue) is undefined.
    LogosModules& modules() const {
        return *static_cast<LogosModules*>(m_logosModulesPtr);
    }

    // True once the framework has wired `modules()`. Stays false when the
    // backend is constructed outside the generated glue (e.g. unit tests),
    // matching the null-pointer fallback above. Read this before using
    // `modules()` from helpers that may run earlier in the backend's life.
    bool isContextReady() const { return m_logosModulesPtr != nullptr; }

    // Framework-only entry point — invoked by the generated UI plugin's
    // `initLogos`. Sets the typed-deps pointer and fires `onContextReady()`
    // (by which point `modules()` is live). The leading/trailing underscores
    // signal "do not call from user code".
    void _logosCoreSetLogosModulesPtr_(void* ptr) {
        m_logosModulesPtr = ptr;
        onContextReady();
    }

protected:
    // Hook for derived backends. Fires exactly once, after `modules()` becomes
    // usable, before the view's first call. The default is a no-op; override to
    // make typed dependency calls or arm typed event subscriptions. Do NOT do
    // this work in the constructor — it runs before the framework hands the
    // dependencies over.
    virtual void onContextReady() {}

private:
    // Type-erased so this header doesn't need the per-module LogosModules
    // definition. Reinterpreted via the typed `modules()` accessor above.
    // Stays null when the backend is constructed outside the generated glue.
    void* m_logosModulesPtr = nullptr;
};

// ---------------------------------------------------------------------------
// _logos_codegen_::maybeSetUiPluginModules — codegen helper, do not call
// directly. The generated UI plugin's `initLogos` always wants to "wire the
// LogosModules aggregate if the backend inherits LogosUiPluginContext,
// otherwise do nothing." Tag-dispatching through two function templates makes
// the unused branch invisible to the compiler for non-inheriting backends
// (the discarded `static_cast` is never type-checked). Mirrors the
// `maybeSet*` helpers in logos-cpp-sdk's `logos_module_context.h`.
// ---------------------------------------------------------------------------
namespace _logos_codegen_ {

template<class T>
inline auto maybeSetUiPluginModules(T& backend, void* ptr)
    -> std::enable_if_t<std::is_base_of_v<LogosUiPluginContext, T>>
{
    static_cast<LogosUiPluginContext&>(backend)._logosCoreSetLogosModulesPtr_(ptr);
}

template<class T>
inline auto maybeSetUiPluginModules(T&, void*)
    -> std::enable_if_t<!std::is_base_of_v<LogosUiPluginContext, T>>
{
    // Backend didn't opt into LogosUiPluginContext; nothing to do.
}

} // namespace _logos_codegen_

#endif // LOGOS_UI_PLUGIN_CONTEXT_H
