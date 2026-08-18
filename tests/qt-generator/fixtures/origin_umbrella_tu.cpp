// THE PROPERTY THE WHOLE `--binding origin` CHANGE EXISTS FOR.
//
// A module whose provider surface is the std `logos_module_impl.h` C ABI — a
// cdylib — has no `LogosAPI` object anywhere in it. Until now that also cost it
// the Qt-typed consumer surface: the umbrella's constructor took a `LogosAPI*`,
// every dependency wrapper's did too, and the cdylib glue emits an
// unconditional `new LogosModules()`. So a module that wanted Qt-typed
// dependency wrappers had to keep a `LogosAPI`, which meant keeping a legacy
// `interface: "provider"` module alive for no other reason.
//
// This translation unit is that module, in miniature. It compiles:
//
//   * the umbrella logos-cpp-generator emits under
//     `--api-style qt --binding origin` (fixtures/origin_umbrella/logos_sdk.h,
//     checked in — see the README beside it for why), and
//   * the wrappers logos-qt-generator emits under
//     `--backend consumer --binding origin`, one Static and one Bound,
//
// and then CONSTRUCTS them. No `LogosAPI` is created, named, or passed. If any
// part of this surface still required one, this file would not compile — which
// is the entire assertion. Nothing is executed.
//
// It is a second, sharper reading of the same claim the FORBID_TEXT tests make
// on the generator's output: those say the text does not mention LogosAPI, this
// says a compiler agrees the type is not needed.

// --- 1. the umbrella, and the wrapper headers it pulls in --------------------
#include "logos_sdk.h"

// The consumer's own headers must be self-sufficient. `logos_api.h` declares
// LogosAPI, and this surface is supposed to have stopped needing it: not merely
// "does not pass one", but "does not even require the declaration". A wrapper
// header that quietly kept the include would satisfy every text assertion and
// still drag the Qt host's identity object into a module that has none.
//
// (The include is checked, not the type: a `LogosAPI` typo-name would compile.
// LOGOS_API_H is that header's own guard macro.)
#ifdef LOGOS_API_H
#error "the origin-bound consumer surface pulled in logos_api.h; it must not need the LogosAPI declaration at all"
#endif

// --- 2. the wrapper bodies ---------------------------------------------------
//
// Included, not linked, exactly as the generated `logos_sdk.cpp` does it: the
// umbrella is one `#include` per dependency wrapper, so a two-dependency module
// compiles both into ONE translation unit. (That is also what caught the
// file-scope rejection-helper redefinition — see umbrella_tu.cpp.)
//
// These DO reach the transport seam (logos_qt_lp_bridge.h), which includes
// logos_api.h for its OWN LogosAPI-taking factory. That is the transport's
// business and not the consumer's: the guard above is what pins the consumer
// surface. Placing these after it is deliberate.
#include "plain_module_api.cpp"
#include "optional_module_api.cpp"

namespace {

// Never called. Constructing is the test.
void logosOriginBoundProbe()
{
    // Default-constructible, with no LogosAPI in existence anywhere in this TU.
    // This is precisely what the cdylib glue's `new LogosModules()` needs.
    LogosModules modules;

    // A name-baked dependency: reached as a member, its origin already baked in
    // by the umbrella.
    logos::CallError err;
    const QString echoed = modules.plain_module.echo_text(QStringLiteral("hello"), &err);
    (void)echoed;

    // A runtime-bound interface dependency, through both bind overloads. The
    // returned handle is a TEMPORARY by design; the bridge behind it is
    // process-lifetime and keyed by (origin, target), so a subscription taken
    // out here outlives the handle — the same contract the LogosAPI-taking path
    // has always had.
    OptionalModule boundQ = modules.bind_optional_module(QStringLiteral("provider_a"));
    OptionalModule boundS = modules.bind_optional_module(std::string("provider_b"));
    (void)boundQ;
    (void)boundS;

    // And the wrappers are constructible directly, with an origin stated at the
    // call site — the shape a hand-written module or a test would use.
    PlainModule direct(QStringLiteral("origin_probe_module"));
    OptionalModule directBound(QStringLiteral("origin_probe_module"),
                               QStringLiteral("provider_c"));
    (void)direct;
    (void)directBound;
}

}  // namespace
