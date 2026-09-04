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

### logos-protocol 0.10 — `available()` on every consumer wrapper

Reviewed, then regenerated. The diff is one declaration in the header and one
definition in the source, forwarding to `LpBridge::client().available()` —
nothing else moved, which is the property this fixture exists to check.

Emitted on EVERY wrapper, not only on the ones a module declares under
`optional_dependencies`. The generator is handed the same contract for both
dependency kinds and cannot tell them apart: optionality is a property of the
DECLARATION, not of the interface. A required dependency asking is merely always
true.


### logos-protocol 0.9 — per-MODULE subscription state

Reviewed, then regenerated. This entry REPLACES a short-lived one that recorded
a per-SUBSCRIPTION form of the same feature (a defaulted
`logos::SubscribeOptions` on every event accessor, routed through
`logos::qt::subscribeOpts`). That shape was wrong about the granularity —
losing a provider is a per-module event, since every subscription to a module
hangs off its single handle — and it was revised before leaving the tree. The
goldens now show the per-module form, and this note exists so a reader who finds
the old shape in the history knows it was withdrawn rather than lost.

The diff against the pre-0.9 goldens is exactly three things per binding, and
nothing else:

- `+#include "logos_lp_client.h"` in the header — the new accessors name
  `logos::SubStatus` and `logos::RestartPolicy`, so those have to be COMPLETE in
  the header, not merely in the .cpp where the bridge already arrived. Emitted
  only when the module has events.
- `onMoved(...)` loses the options parameter and goes back to its pre-0.9
  signature, with the body routing through `logos::qt::subscribe` again.
- four accessors appended ONCE per module (not once per event):
  `onSubscriptionStatus`, `subscriptionGeneration`, `setRestartPolicy`,
  `rearmSubscriptions`.

What did NOT change is the point of keeping this golden: every method, every
record codec, every async overload and the whole payload decode are
byte-identical. A module that never asks about subscription state behaves
exactly as before.

Captured first from the generator at `cde7d42` (pre-optionality). The
optional-parameters change left the **consumer** backend byte-identical — its
optionality handling fires only for an optional field, so a contract without one
is untouched by construction.

> A third golden, `plain_qt`, sat here until `--backend qt` was retired. It
> pinned the provider glue that emitter produced (`_qt_glue.h`, `_dispatch.cpp`,
> `_events.cpp`), and it went with the emitter: a module is a plain shared
> library, and hosting one in Qt is `logos-cpp-generator --backend cdylib`
> followed by `logos-qt-host-generator --backend cdylib`, whose own goldens live in
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

Refreshed for the derived module identity methods (`name()` / `version()`).
Both goldens grew by exactly the same 68 source lines and 6 header lines —
the two methods with their three standard overloads each, appended — and
nothing already there changed. Pure addition, zero deletions, which is what a
contract gaining two methods should look like here.

Refreshed once more for the widened provider-rejection detector. Both goldens
changed by exactly the same 4-line hunk in `plain_module_api.cpp`, and nothing
else moved — no header changed at all:

```
-    if (code->get<std::string>() != "dispatch_failed") return false;
-    out.code = code->get<std::string>();
+    const std::string _code = code->get<std::string>();
+    if (_code != "dispatch_failed"
+        && _code != "invalid_args"
+        && _code != "unknown_method") return false;
+    out.code = _code;
```

The detector folds a provider REFUSAL into `logos::CallError` instead of letting
the return decode erase it into a default. It matched one literal until
providers were found to be emitting `invalid_args` for an arity error with
nobody detecting it — measured as `logosctl call test_basic_module isPositive`
(argument missing) exiting 0 with status `"ok"` and the refusal object as its
result. `unknown_method` is in the set before any provider emits it, because a
detector can be widened compatibly on its own while a new provider code cannot.

The three shape guards above the compare are untouched, deliberately: the set is
CLOSED, so an unrecognised code, a 2- or 4-key object and a non-string value all
still come back as DATA. `CMakeLists.txt`'s
`consumer_rejection_detector_matches_closed_code_set` and
`consumer_rejection_detector_stays_narrow` state both halves as named
properties, so a future golden refresh cannot quietly widen the match.

Rebased again for the **lossless Qt type mapping** — the change that gave `[T]`,
`{tstr: V}` and `?T` their element types on the Qt surface instead of
QVariantList / QVariantMap / QVariant. Both goldens moved by the SAME hunks, and
in this whole contract exactly ONE slot is affected:

| change | why |
|---|---|
| `echo_ints`: `QVariantList` → `QList<qlonglong>` on all three overloads | `[int]` is a typed array, and the Qt surface now says so. It is the only widened shape in `plain_module.lidl` |
| its argument encode becomes an element loop | `QVariant::fromValue(QList<qlonglong>)` is a value `qvariantToNlohmann` answers **null** for — it matches a closed `userType()` set. The loop hands it one ELEMENT at a time, each of which is in that set |
| its return decode becomes an element loop through `logos::qt::tryFromWire` | the decode direction fails just as silently (`qvariant_cast<QList<qlonglong>>` of a QVariantList is EMPTY), and the per-element form is also what closes the no-element-type-checking gap: `["x", 5]` is now REJECTED with the codec's own sentence instead of arriving as `[0, 5]` |

**Everything else in the contract is byte-identical, and that is the assertion
worth reading.** `echo_strings` (`[tstr]` → QStringList), `attributes`
(`{tstr: any}` → QVariantMap), `describe` (`any` → QVariant), `bounds`
(`[Point]` → QList<Point>), `fetch` (`result`), `reset` (`void`) and every
scalar did not move — QStringList is already in that closed set, and every
`any`-bottomed shape deliberately keeps the QVariant spelling because QVariant
is the only Qt type that holds bytes AND an exact uint64 AND arbitrary nesting.

Rebased once more, for ONE emitter fix. Both goldens moved by the SAME hunk, and
the emitted HEADERS did not move at all — the fix changes no public signature.

| change | why |
|---|---|
| `bounds`: the `[Point]` argument encode takes its source as a lambda ARGUMENT (`}(points)`) instead of inlining it (`for (const auto& __e : points)`) | `QList<Record>` and `QMap<QString, Record>` had their own hand-written loops beside the generic ones, and those two INLINED their source. At depth that emitted code which does not compile — `{tstr: {tstr: Point}}` produced `for (auto __i = __i.value().cbegin(); …)`, `__i` in its own initialiser, and `?{tstr: Point}` produced `*x.cbegin()`, which parses as `*(x.cbegin())`. The special cases are gone; the generic loops already produce identical code for both, because they recurse onto the scalar record case |

**Nothing else moved.** `translate` and `bounds` still call `recToWire_PlainModule_Point` /
`recFromWire_PlainModule_Point` exactly where they did, and the three shapes a record can
appear in produce the same JSON they always did — the fix is about the SPELLING
of the loop, not about what it encodes.

Rebased again for the DECODE ERROR CHANNEL. Both goldens moved by the same
hunks, and the emitted headers still did not move — the sink is an
implementation detail of the .cpp, not a signature.

| change | why |
|---|---|
| every container decode gains `logos::qt::tryRequireArray` / `tryRequireObject` in place of `if (!__s.is_array())` | a wrong-shaped response used to answer an empty container and say nothing. The check is the CODEC's own, so the sentence a Qt consumer reports is the one every std consumer of the same contract reports |
| `recFromWire_PlainModule_Point` takes a `std::string*`, and every call site passes one | the sink itself. A rejected element used to leave an empty container with `err.ok()` TRUE — indistinguishable from a container the provider legitimately sent empty. The parameter is UNNAMED here because `Point` has no field that can reject; a record that does gets it named |
| `echo_ints`, `translate` and `bounds` decode into a named `_out`, then `logosNoteDecodeFailure(...)` | the fold onto `logos::CallError`. It cannot be done before the decode — the rejection is only known once the decode has walked the value — and it is guarded on `err`, because the error channel is opt-in and a caller who passed nothing already gets the qWarning |
| `<name>AsyncResult` captures `_target = m_moduleName.toStdString()` | the callback runs after the method returned, and the wrapper is a copyable handle that may not outlive the call |
| the value-only `<name>Async` and the typed event accessor declare `std::string* __derr = nullptr` | neither has anywhere to put an error, so the qWarning stays their only report. The sink still has to be NAMED, because the decode expression names it |

**Byte-identical: every method the element rule does not reach.**
`echo_strings` (`[tstr]` → QStringList), `attributes` (`{tstr: any}`),
`describe` (`any`), `fetch` (`result`), `reset` (`void`) and every scalar return
cross whole through the lenient `logos::qt::fromWire<T>`. That leniency is the
shipped scalar contract and is deliberately untouched — this is the ELEMENT
rule, and only the element rule. (As an ELEMENT, `[tstr]` *is* checked:
`[[tstr]]` decodes each QStringList through `tryFromWire`.)

Rebased once more, for the THREE SLOTS THE ELEMENT RULE DID NOT REACH. Both
goldens moved by the same seven hunks, and the emitted **headers still did not
move** — none of this changes a public signature.

The previous entry above said, of the slots that cross the QVariant boundary
whole: *"That leniency is the shipped scalar contract and is deliberately
untouched."* Calling `[tstr]` and `{tstr: any}` **scalar** is what this entry
corrects. They are CONTAINERS: they declare a shape, and `[tstr]` declares an
element type too. The reason they had no element loop is an ENCODE property —
QStringList / QVariantList / QVariantMap are in `qvariantToNlohmann`'s closed
`userType()` set, so they cross whole — and it had been carried over into the
DECODE direction, where it decides nothing. The tell that this was an accident
rather than a contract: the very same types are already checked one level down,
because as an ELEMENT `[tstr]` goes through `tryFromWire`. So `?[tstr]` was
strict and `[tstr]` was not, for the same payload, in the same wrapper.

| change | why |
|---|---|
| `echo_strings` (`[tstr]`) and `attributes` (`{tstr: any}`) decode through `logos::qt::tryFromWire` on all three overloads, and fold into the error channel exactly as `echo_ints` already did | `["a", 5, true, {}]` read as `[tstr]` arrived as four strings, three of which the provider never sent, while the std codec rejected the identical input. `{tstr: any}` declares no element type but does declare a SHAPE, and a wrong-shaped one answered an empty map and said nothing |
| `recFromWire_PlainModule_Point` checks its SHAPE (`tryRequireObject`) instead of `if (!w.is_object()) return __out;` | a non-object answered a whole default-constructed struct with `err.ok()` — not an empty value the provider might really have sent, a record of fabricated members. `Codec<Record>::from`, the std twin, raises `typeError(path, "object", j)` for this input |
| every scalar FIELD decodes through `tryFromWire` and reports a mismatch | inside ONE record a rejected `[uint]` field was reported while a mistyped `float64` field beside it was silently zeroed, from the same wire object, on the same channel. The granularity matches the element rule: the rejected slot keeps its default, the rest of the record still decodes, and `err.ok()` is no longer true |
| `recFromWire_PlainModule_Point`'s sink parameter is now NAMED | every record's decode can reject, if only on its shape. The predicate that decided this per record (`recordDecodeUsesSink`) is gone with it |

**Byte-identical: every bare SCALAR slot.** `echo_text`, `echo_bytes`,
`echo_int`, `echo_uint`, `echo_bool`, `echo_float`, `describe` (`any`), `fetch`
(`result`) and `reset` (`void`) still cross through the lenient
`logos::qt::fromWire<T>`. That is the line, and it is deliberate: the leniency
is the shipped contract of BOTH consumer surfaces — logos_qt_wire.h documents it
here, and logos-cpp-sdk's lp emitter (`lpFromJsonExpr`) independently answers
0 / "" / false for the same mismatch — so tightening it is a decision about
every scalar return of every module, to be taken together with that emitter or
the two surfaces diverge. `tests/qt-generator/roundtrip_tu.cpp` pins it with
`ABareScalarSlotIsLenientOnPurpose`, so moving the line means deleting a test
that says not to.

Rebased once more, for a NAME and nothing else. Both goldens moved by the same
hunk, in the .cpp only; the emitted headers did not move, because these two
functions are file-scope `static` and appear in no signature.

| change | why |
|---|---|
| `recToWire_Point` / `recFromWire_Point` are now `recToWire_PlainModule_Point` / `recFromWire_PlainModule_Point` — qualified by the wrapper class | the umbrella (logos-cpp-sdk `generator_lib.cpp`) amalgamates every generated `<name>_api.cpp` into ONE translation unit by `#include`-ing them from `logos_sdk.cpp`. Two contracts in one module that each declare a record called `Blob` therefore emitted two `recFromWire_Blob` differing only in return type — *ambiguating new declaration*, and the module does not compile. That is not a hypothetical shape, it is what a PROXY is: a module that binds an interface and also declares that interface's providers as dependencies gets three wrappers for one contract, so any record at all collides three ways. It went unnoticed because the only Qt-consumer fixture with a record — this one — has a single contract in it |

**This diff is provably rename-only.** The golden was refreshed by applying the
two-name substitution to the committed file and re-running the check, not by
copying the generator's output over it: the byte-comparison then passed, which
is the assertion that nothing else moved. `ownerOf(qual)` was already in scope
at every emission site (it names the wrapper in the decode diagnostics), so the
qualification adds no plumbing and no state.
