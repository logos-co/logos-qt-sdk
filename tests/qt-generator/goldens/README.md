# Goldens — provenance, and what a diff here means

These files are the generator's output for `fixtures/plain_module.lidl`, a
contract that deliberately contains **no optional anywhere**. They exist to
catch changes leaking into paths that were supposed to be untouched.

**A red golden test is a review prompt, not a refresh prompt.** Read the diff
and decide whether the change was intended before regenerating.

## Regenerating (only after reviewing the diff)

```
logos-qt-generator --lidl fixtures/plain_module.lidl --backend qt \
    --impl-header fixture_impl.h --output-dir goldens/plain_qt
logos-qt-generator --lidl fixtures/plain_module.lidl --backend consumer \
    --output-dir goldens/plain_consumer
```

`--impl-header` is pinned so the emitted `#include` does not vary by machine.

## History

Captured first from the generator at `cde7d42` (pre-optionality), then rebased
once onto the optional-parameters change. That rebase was accepted only after
classifying **every** differing line; the complete set was:

| change | why it touches a contract with no optionals |
|---|---|
| an arity gate (`args.size() < N` -> `dispatch_failed`) | the optionality mechanism itself — a trailing `?T` must be omittable, so the dispatch can no longer assume every declared slot was sent |
| `args.at(i)` -> `args.value(i)` | same mechanism, and it closes a pre-existing unchecked `QList::at` read past the end when a caller sent too few arguments |
| `static_cast<int>` -> `static_cast<qlonglong>` / `<qulonglong>` on `int`/`uint` returns | an unrelated pre-existing bug fixed in passing: `int` is `int64_t` and `uint` is `uint64_t`, so the 32-bit cast silently truncated every value past 2^31 |

Nothing else differed. The **consumer** backend was byte-identical across that
rebase — its optionality changes fire only for an optional field, so a contract
without one is untouched by construction.

`plain_consumer` was then rebased once, onto the change that brought the
consumer surface up to parity with the emitter it replaces
(logos-cpp-sdk's `cpp-generator`, `ApiStyle::Qt`). Classified the same way; the
complete set was:

| change | why |
|---|---|
| a trailing `Timeout timeout = Timeout()` on every SYNC method, threaded into `logos::qt::invoke`'s `timeoutMs` | the surface being matched has always had it, and it was the one entry point here with no way to say how long it would wait. Trailing and defaulted, so no call site moves |
| a new `<name>AsyncResult(...)` per method, over a new `logos::qt::invokeAsyncResult` seam | the error-carrying async. `<name>Async` hands the callback a bare value, so a failed call is indistinguishable from a provider that legitimately returned `0` / `""` / `false` — and live callers (`installPluginAsyncResult`) branch on `r.ok()` |
| `#include "logos_async_result.h"` in the emitted header | `logos::AsyncResult<T>` is named in those signatures |

Nothing else differed, and `plain_qt` was untouched: both changes are on the
consumer surface only.
