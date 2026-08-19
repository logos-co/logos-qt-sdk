# `origin_umbrella` — a cross-repo artifact, checked in on purpose

`logos_sdk.h` and `logos_sdk.cpp` here are **not written by hand and not emitted
by this repo's generator**. They are the verbatim output of
**logos-cpp-sdk's** `logos-cpp-generator` for `metadata.json` beside them:

```
logos-cpp-generator --metadata metadata.json --general-only \
    --api-style qt --binding origin --output-dir .
```

## Why a copy lives here

The umbrella (`struct LogosModules`) and the per-dependency wrappers are emitted
by **two different tools in two different repos** — that split is deliberate
(see `lib/buildPlugin.nix` in logos-plugin-qt: re-implementing the umbrella in
the Qt generator would put two emitters back on the one artifact they agree
on). The pairing they have to agree on is a *constructor signature*:

| the umbrella writes | the wrapper must declare |
|---|---|
| `plain_module(QStringLiteral("origin_probe_module"))` | `PlainModule(const QString& origin)` |
| `OptionalModule(QStringLiteral("origin_probe_module"), moduleName)` | `OptionalModule(const QString& origin, const QString& target)` |

Nothing checks that agreement unless something compiles both halves together —
which is what `fixtures/origin_umbrella_tu.cpp` does.

Generating the umbrella *during* the test run would be the stronger test, and it
is deliberately not done: this repo's test derivation takes `logos-cpp-generator`
from its **locked** `logos-cpp-sdk` input, so the test would only pass while that
lock pointed at a revision carrying `--binding`, and `nix flake check` on this
repo alone would go red for a reason that has nothing to do with this repo. A
checked-in copy keeps the suite hermetic. The cost is that this file can drift
from the emitter; that is covered from the other side, by
`tests/generator/test_make_umbrella.cpp` in logos-cpp-sdk, which pins the same
lines as assertions.

**If the compile probe fails after a logos-cpp-sdk change, regenerate this file
with the command above and read the diff** — a changed constructor shape here is
a change to the contract between the two generators, not a formatting detail.
