# Goldens — provenance, and what a diff here means

These files are the generator's output for `fixtures/plain_module.lidl`, a
contract that deliberately contains **no optional anywhere**. They exist to
catch changes leaking into paths that were supposed to be untouched.

**A red golden test is a review prompt, not a refresh prompt.** Read the diff
and decide whether the change was intended before regenerating.

## Regenerating (only after reviewing the diff)

```
logos-qt-generator --lidl fixtures/plain_module.lidl --backend consumer \
    --output-dir goldens/plain_consumer
logos-qt-generator --lidl fixtures/plain_module.lidl --backend consumer \
    --binding origin --output-dir goldens/plain_consumer_origin
```

## History

Captured first from the generator at `cde7d42` (pre-optionality). The
optional-parameters change left the **consumer** backend byte-identical — its
optionality handling fires only for an optional field, so a contract without one
is untouched by construction.

> A third golden, `plain_qt`, sat here until `--backend qt` was retired. It
> pinned the provider glue that emitter produced (`_qt_glue.h`, `_dispatch.cpp`,
> `_events.cpp`), and it went with the emitter: a module is a plain shared
> library, and hosting one in Qt is `logos-cpp-generator --backend cdylib`
> followed by `logos-qt-host-generator`, whose own goldens live in
> logos-plugin-qt. Nothing in the two goldens below was captured from it.

`plain_consumer` was rebased once, onto the change that brought the
consumer surface up to parity with the emitter it replaces
(logos-cpp-sdk's `cpp-generator`, `ApiStyle::Qt`). Classified the same way; the
complete set was:

| change | why |
|---|---|
| a trailing `Timeout timeout = Timeout()` on every SYNC method, threaded into `logos::qt::invoke`'s `timeoutMs` | the surface being matched has always had it, and it was the one entry point here with no way to say how long it would wait. Trailing and defaulted, so no call site moves |
| a new `<name>AsyncResult(...)` per method, over a new `logos::qt::invokeAsyncResult` seam | the error-carrying async. `<name>Async` hands the callback a bare value, so a failed call is indistinguishable from a provider that legitimately returned `0` / `""` / `false` — and live callers (`installPluginAsyncResult`) branch on `r.ok()` |
| `#include "logos_async_result.h"` in the emitted header | `logos::AsyncResult<T>` is named in those signatures |

Nothing else differed: both changes are on the consumer surface only.

## `plain_consumer_origin` — the LogosAPI-free binding

Captured when `--binding origin` was added: the same contract through the same
backend, differing only in how the wrapper reaches a transport. It is here so
that the *difference between the two bindings* is a reviewable diff rather than
a claim —

```
diff -u goldens/plain_consumer/plain_module_api.h goldens/plain_consumer_origin/plain_module_api.h
diff -u goldens/plain_consumer/plain_module_api.cpp goldens/plain_consumer_origin/plain_module_api.cpp
```

— and the complete delta is four hunks:

| change | why |
|---|---|
| `#include "logos_api.h"` + `#include "logos_api_client.h"` → `#include "logos_mode.h"` | the flavour names neither `LogosAPI` nor `LogosAPIClient`, so it includes neither. `Timeout` — on every method — lives in `logos_mode.h` and had only ever been reached transitively through `logos_api_client.h` |
| `explicit PlainModule(LogosAPI* api);` → `explicit PlainModule(const QString& origin);` | the constructor is handed the consuming module's own name instead of an identity object to read one off |
| the `LogosAPI* m_api;` member disappears | nothing in the class holds one; it was already write-only |
| `LpBridge::forTarget(api, …)` → `LpBridge::forOrigin(origin, …)` | `forTarget` DERIVES the origin from `api->moduleName()`; `forOrigin` is handed it. The ctor parameter is threaded through verbatim — see `CMakeLists.txt`, `origin_consumer_asserts_its_own_origin` |

**Every method, event and record signature is byte-identical between the two.**
That is the point of the pair: the type surface is decoupled from the transport
binding, so a module can change how it binds without any call site moving.

`plain_consumer` was unchanged by that work, and must stay so — the
LogosAPI-taking path is additive-only.
