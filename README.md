# logos-qt-sdk

The **Qt developer layer** of the Logos SDK stack: `LogosAPI` (client cache +
provider + token manager), `LogosAPIProvider`, the provider base classes
(`LogosProviderBase`, `LogosProviderPlugin`, the `LOGOS_PROVIDER`/`LOGOS_METHOD`
macros), and the legacy `QObject`/`Q_INVOKABLE` provider glue
(`QtProviderObject`).

> **None of that is how a module is WRITTEN any more.** `LogosProviderBase` and
> the `LOGOS_METHOD` marker were the `interface: "provider"` authoring path, and
> the generator behind them (`logos-cpp-generator --provider-header`) was
> removed — the macros still compile, but nothing emits the
> `callMethod`/`getMethods` dispatch `LOGOS_METHOD` used to mark. A module is
> now `interface: "universal"`: a Qt-free impl class deriving logos-cpp-sdk's
> `LogosModuleContext`, whose contract is DERIVED from the impl header named by
> `codegen.impl_class` / `codegen.impl_header` — its plain public methods ARE
> its API, with no marker. The names above survive for the legacy
> provider-style and `Q_INVOKABLE` plugins that still load.

> **That runtime does not live here.** It moved to `logos-qt-host`
> ([`logos-plugin-qt`](https://github.com/logos-co/logos-plugin-qt)), and every
> consumer now names that package directly. This repo keeps the surface — the
> same `find_package(logos-qt-sdk)` and the same `logos-qt-sdk::logos_qt_sdk`
> target — but `logos_qt_sdk` is an INTERFACE library: it compiles nothing, and
> its link interface is `logos-qt-host::logos_qt_host` plus this repo's own
> capability targets. Those are exported too, and a consumer should link the
> narrow one it actually is rather than the umbrella:
> `logos-qt-sdk::logos_qt_consumer` (calling other modules — the Qt↔lp seam
> headers, deliberately NO Qt host runtime), `::logos_qt_provider`
> (implementing a view plugin — `logos_ui_plugin_context.h`),
> `::logos_qt_host_core` (standing up a core — Qt marshalling over
> `logos::host::LogosCore`) and `::logos_qt_common` under all three. The
> transitional forwarding headers that once
> re-exported `logos_api.h` & co. from this prefix are gone; the only copy of
> those headers in any closure is logos-qt-host's.
>
> What this repo still OWNS is `logos_qt_lp_bridge.h`, `logos_qt_wire.h`,
> `logos_qt_host_core.h` and the `logos-qt-generator` binary.
>
> `logos_ui_plugin_context.h` is still installed here, but logos-view-module
> now ships it too and is its owner: it is one half of a matched pair with the
> view glue emitter, which lives there. logos-module-builder puts
> logos-view-module's copy on the include path AHEAD of this one. The copy here
> is a leftover to be removed once every ui build is confirmed to reach the
> header through logos-module-builder.

Layered over [`logos-protocol`](https://github.com/logos-co/logos-protocol)
(transports, token exchange, consumer core, the `lp_*` C ABI). Qt-plugin and
Qt/QML UI modules build against this SDK; universal (pure-C++) module
implementations depend only on the Qt-free
[`logos-cpp-sdk`](https://github.com/logos-co/logos-cpp-sdk).

## logos-qt-generator

This repo also hosts **`logos-qt-generator`** (`qt-generator/`,
`packages.<system>.logos-qt-generator`) — the CONSUMER-side Qt glue, per the
Qt-confinement invariant (generated Qt code is the Qt layer's product;
`logos-cpp-generator` keeps the Qt-free outputs). The PROVIDER-side Qt glue is
not here: hosting a module in Qt is logos-plugin-qt's `logos-qt-host-generator`.

| Mode | Input | Emits |
|------|-------|-------|
| `--backend consumer` | `--lidl` contract (or `--from-header`) | the Qt-typed CONSUMER wrapper for a dependency / interface: `<name>_api.{h,cpp}` |

`--backend ui` was **removed**. The view plugin glue is emitted by
[`logos-view-module`](https://github.com/logos-co/logos-view-module)'s
`logos-view-generator`, which sits with the `LogosView*.in` templates its output
is compiled against and with `logos_ui_plugin_context.h`, which its output calls
into. Those three are one authoring surface, and splitting them is not
theoretical tidiness: while the emitter lived here as well, the two copies drifted
— this one gained the module teardown hook and the other did not — and nothing
caught it, because a view plugin missing that hook builds, loads and runs. It is
simply never asked to finish. Invoking `--backend ui` now fails with a message
naming the replacement.

There is deliberately **no backend that wraps a module implementation directly in
a Qt provider object**. A module is a plain shared library; turning one into a Qt
plugin is a downstream hosting step over the language-neutral module-impl C ABI:

```
plain std impl
  -> logos-cpp-sdk   lidl_gen_cdylib      -> logos_module_* C ABI
  -> logos-plugin-qt qt-host-generator    -> <name>CdylibProvider
```

`--backend qt` used to short-circuit that seam by consuming an impl class
directly, which was the one thing that made a module *not* language-neutral.
`--backend cdylib` respected the seam but emitted its *hosting* half, which
belongs with the host. Both were removed; asking for either now fails with a
pointer to the two tools above rather than emitting nothing.

Both generators compile one shared LIDL frontend, distributed by
logos-cpp-sdk under `share/lidl-frontend/`, so the parsed surface can never
skew between them.

## Building

```bash
ws build logos-qt-sdk     # via workspace
nix build                 # standalone
nix build .#tests         # test suite
```
