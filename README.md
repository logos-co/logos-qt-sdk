# logos-qt-sdk

The **Qt developer layer** of the Logos SDK stack: `LogosAPI` (client cache +
provider + token manager), `LogosAPIProvider`, the developer-facing provider
base classes (`LogosProviderBase`, `LogosProviderPlugin`, the
`LOGOS_PROVIDER`/`LOGOS_METHOD` macros), and the legacy `QObject`/`Q_INVOKABLE`
provider glue (`QtProviderObject`).

Layered over [`logos-protocol`](https://github.com/logos-co/logos-protocol)
(transports, token exchange, consumer core, the `lp_*` C ABI). Qt-plugin and
Qt/QML UI modules build against this SDK; universal (pure-C++) module
implementations depend only on the Qt-free
[`logos-cpp-sdk`](https://github.com/logos-co/logos-cpp-sdk).

## Building

```bash
ws build logos-qt-sdk     # via workspace
nix build                 # standalone
nix build .#tests         # test suite
```
