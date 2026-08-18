# logos-qt-sdk

The **Qt developer layer** of the Logos SDK stack: `LogosAPI` (client cache +
provider + token manager), `LogosAPIProvider`, the developer-facing provider
base classes (`LogosProviderBase`, `LogosProviderPlugin`, the
`LOGOS_PROVIDER`/`LOGOS_METHOD` macros), and the legacy `QObject`/`Q_INVOKABLE`
provider glue (`QtProviderObject`).

> **That runtime does not live here.** It moved to `logos-qt-host`
> ([`logos-plugin-qt`](https://github.com/logos-co/logos-plugin-qt)), and every
> consumer now names that package directly. This repo keeps the surface — the
> same `find_package(logos-qt-sdk)` and the same `logos-qt-sdk::logos_qt_sdk`
> target — but `logos_qt_sdk` is an INTERFACE library whose only job is to link
> `logos-qt-host::logos_qt_host`. The transitional forwarding headers that once
> re-exported `logos_api.h` & co. from this prefix are gone; the only copy of
> those headers in any closure is logos-qt-host's.
>
> What this repo still OWNS is `logos_ui_plugin_context.h`,
> `logos_qt_lp_bridge.h`, `logos_qt_wire.h` and the `logos-qt-generator` binary.

Layered over [`logos-protocol`](https://github.com/logos-co/logos-protocol)
(transports, token exchange, consumer core, the `lp_*` C ABI). Qt-plugin and
Qt/QML UI modules build against this SDK; universal (pure-C++) module
implementations depend only on the Qt-free
[`logos-cpp-sdk`](https://github.com/logos-co/logos-cpp-sdk).

## logos-qt-generator

This repo also hosts **`logos-qt-generator`** (`qt-generator/`,
`packages.<system>.logos-qt-generator`) — the CONSUMER-side Qt glue and the ui
plugin backend, per the Qt-confinement invariant (generated Qt code is the Qt
layer's product; `logos-cpp-generator` keeps the Qt-free outputs). The
PROVIDER-side Qt glue is not here: hosting a module in Qt is logos-plugin-qt's
`logos-qt-host-generator`.

| Mode | Input | Emits |
|------|-------|-------|
| `--backend consumer` | `--lidl` contract (or `--from-header`) | the Qt-typed CONSUMER wrapper for a dependency / interface: `<name>_api.{h,cpp}` |
| `--backend ui` | `--metadata` + `--rep` | UI plugin glue: `*Interface.h` + `*Plugin.{h,cpp}` around the user-written `.rep` + `*Backend` class |

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
