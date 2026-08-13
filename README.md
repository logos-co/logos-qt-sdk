# logos-qt-sdk

The **Qt developer layer** of the Logos SDK stack: `LogosAPI` (client cache +
provider + token manager), `LogosAPIProvider`, the developer-facing provider
base classes (`LogosProviderBase`, `LogosProviderPlugin`, the
`LOGOS_PROVIDER`/`LOGOS_METHOD` macros), and the legacy `QObject`/`Q_INVOKABLE`
provider glue (`QtProviderObject`).

> **Transitional:** that runtime now LIVES in `logos-qt-host`
> ([`logos-plugin-qt`](https://github.com/logos-co/logos-plugin-qt)), where a Qt
> plugin build already finds it. This repo keeps the surface — the same
> `find_package(logos-qt-sdk)`, the same `logos-qt-sdk::logos_qt_sdk` target,
> the same header names at the same paths — but `logos_qt_sdk` is an INTERFACE
> library that links `logos-qt-host::logos_qt_host`, and the headers here are
> generated forwarders (see `cpp/forward-header.h.in`). Nothing links two copies
> of `LogosAPI` at any point. Consumers get repointed at `logos-qt-host` in a
> later step, and the forwarders go with them.

Layered over [`logos-protocol`](https://github.com/logos-co/logos-protocol)
(transports, token exchange, consumer core, the `lp_*` C ABI). Qt-plugin and
Qt/QML UI modules build against this SDK; universal (pure-C++) module
implementations depend only on the Qt-free
[`logos-cpp-sdk`](https://github.com/logos-co/logos-cpp-sdk).

## logos-qt-generator

This repo also hosts **`logos-qt-generator`** (`qt-generator/`,
`packages.<system>.logos-qt-generator`) — ALL generated Qt glue comes from
here, per the Qt-confinement invariant (generated Qt code is the Qt layer's
product; `logos-cpp-generator` keeps the Qt-free outputs):

| Mode | Input | Emits |
|------|-------|-------|
| `--backend qt` | `--from-header` impl class | universal module glue (`_qt_glue.h`, `_dispatch.cpp`, `_events.cpp`) |
| `--backend cdylib` | `--lidl` contract (or `--from-header`) | the uniform Qt glue over the module-impl C ABI |
| `--backend ui` | `--metadata` + `--rep` | UI plugin glue: `*Interface.h` + `*Plugin.{h,cpp}` around the user-written `.rep` + `*Backend` class |

Both generators compile one shared LIDL frontend, distributed by
logos-cpp-sdk under `share/lidl-frontend/`, so the parsed surface can never
skew between them.

## Building

```bash
ws build logos-qt-sdk     # via workspace
nix build                 # standalone
nix build .#tests         # test suite
```
