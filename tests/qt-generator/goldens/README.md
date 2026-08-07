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

Captured from the generator at `cde7d42`, before optionality. Any diff against
these is a change to a path that carries no optionals at all.
