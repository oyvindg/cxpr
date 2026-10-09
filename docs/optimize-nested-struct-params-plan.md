# Plan: First-class nested struct-field metadata in cxpr (optimize as first consumer)

Status: **design / ready to implement**
Branch: `feature/optimize-nested-struct-params` (off `release/0.3.2`)
Owner: unassigned (hand-off to implementing agent)

## Goal

Make **metadata declared on fields nested inside struct `$params`** a first-class,
**generic** facility — any metadata key, not just `optimize` — with a **typed, validated**
representation. `optimize` sweeps/constraints are the first consumer.

Target syntax (optimize is just the first use):

```cxpr
$bb_params = {
    period = 20 { optimize = [30, 40, 50, 60] },
    stddev = 2.0 { optimize { min = 2.0, max = 2.4, step = 0.2 } }
}
$kst_params = {
    roc = { r1 = 8 { optimize { min = 3, max = 7, step = 1 } } },
    signal = 4 { optimize { min = 6, max = 10, step = 2 } }
}
optimize $kst_params.roc.r1 <= $kst_params.roc.r2 { description = "ROC ascending" }
assert $adx_range_max < $adx_trend_min { description = "range below trend" }
```

After this change:
- Any `{ key ... }` metadata on any nested struct field is captured and addressable by a
  **dotted/qualified name** (`bb_params.period`, `kst_params.roc.r1`).
- Metadata is parsed **once** into a typed tree with source spans; malformed blocks produce
  real diagnostics instead of silently-dropped fields.
- `optimize` discovery/constraints/apply work end-to-end on nested dims; `optimize = [list]`
  shorthand is accepted.
- Future nested metadata kinds (`ui`, `units`, `doc`, new constraints, …) need only a small
  consumer — no parser or capture work, and dyn can eventually drop its text scraper.

## Why this shape (established facts)

Verified on `release/0.3.2`:

1. **Record field metadata is parsed then discarded.** `src/parser/primary.c:196`
   `cxpr_skip_record_field_metadata()` throws away the trailing `{ ... }`; called from
   `cxpr_parse_record_literal()` at `:277` (comment at 274-276 defers extraction to "the
   host", i.e. dyn's scraper). The `record` expr node has no metadata slot
   (`src/ast/internal.h:39-43`).

2. **Metadata is already a GENERIC key→value facility, not optimize-specific.** Accessors
   take an arbitrary `const char* key` (`src/model/accessors.c:523,557,630,678,…`); cxpr
   already reads non-optimize keys (`min`, `max`, `source_arg` in window/lookback), dyn
   reads `type`, etc. So nested-generic capture unlocks *all* keys at nested level at once.

3. **Metadata has NO typed representation today — it is a raw `body` string + lazy text
   scanner.** `cxpr_model_metadata.body` is a string (`src/model/parse.c:364`,
   `src/document.c:1143`); `cxpr_model_metadata_field_value` (`accessors.c:555-582`) scans
   it, supporting dotted keys by descending nested `{ }` blocks. The body string is consumed
   ONLY by the accessors (`accessors.c:413` getter, `:564` scanner) — not serialized or used
   in codegen. Blast radius of changing the internals: 33 accessor call-sites across 5 cxpr
   files, 3 dyn files — all of which keep working if accessor **signatures** are preserved.

4. **Dimension discovery keys on the metadata list + PARAM target.**
   `src/model/validate_optimize.c` `cxpr_model_prepare_optimize()` requires
   `CXPR_MODEL_METADATA_TARGET_PARAM`; `dim->name = target_name` (a string — dotted is fine).

5. **Reads of `$a.b` / `$a.b.c` work** via one-dot shortcut (`src/context/structs.c:194-212`)
   and recursive field access (`src/eval/tree.c:109-205`), both reading the struct in the
   context struct-map. **There is no path-aware setter** — `cxpr_context_set_param`
   (`src/context/values.c:429-431`) is flat.

6. **Candidate application happens at two sites in `src/model/optimize_run.c`:** `:66`
   (`cxpr_context_set_param`, constraint eval in `prepare_grid`) and `:260`
   (`cxpr_model_session_set_param`, CPU replay).

## Scope decision (grad 1, not grad 2)

- **IN scope:** parse metadata into a typed tree internally (once, with diagnostics) and
  reimplement the existing accessors over it, **keeping their signatures** → zero call-site
  churn. Generic nested-field capture + qualified names. optimize consumer + apply path.
- **OUT of scope (separate follow-up):** migrating the *public* metadata API and all cxpr/dyn
  call-sites to a typed API, and deleting dyn's scraper. That is "grad 2" — larger, tracked
  separately (see Future work).

## Architecture: native, not a plugin

**Nested optimize (and the metadata facility it rides on) is first-class NATIVE cxpr — not a
plugin.** cxpr already draws the plugin line, and semantics sit on the native side of it:

- `cxpr_model_plugin_*` (`include/cxpr/model/plugin.h`) plugins are **downstream artifact
  emitters** — they receive an already-compiled model and emit output (C, CUDA, graph, meta,
  debug_map). They cannot add grammar or discover dimensions.
- `cxpr_model_optimize_backend` (`optimize.h`) backends only **accelerate execution**
  (CPU/host/CUDA/cxcu). The header states it outright: *"CXPR retains optimization
  semantics."*

Therefore: parsing, the metadata facility, nested-field capture, dimension discovery,
constraints/asserts, and the path-aware apply are **native core**, exactly where `optimize`/
`assert` already live (grammar in the parser, semantics in `validate_optimize.c` /
`optimize_run.c`). The only legitimate plugin interactions are: execution backends (already a
plugin) and codegen artifact plugins (`plugins/meta.c`, `plugins/c.c`, `plugins/cuda.c`),
which must be updated to **consume** the native nested metadata — they are consumers, never
owners.

Extensibility is served by the generic native facility (below) plus, if the number of
semantic consumers grows, an internal consumer-registry seam (`{ key, validate_fn, lower_fn }`
the core iterates) — still native, just tidier. Pure artifact/output consumers of metadata
may be `plugin.h` plugins.

## Diagnostics: generic retrieval API (verified)

Add a **generic, typed metadata-retrieval API** (not optimize-specific) so every consumer —
`optimize`, `window`, `lookback`, `description`, and future keys — gets uniform diagnostics.

Verified current behavior (why this is needed):
- The accessors (`field_value`/`field_number`/`field_number_list`, `accessors.c:555/676/628`)
  return `NULL`/`false` for **both missing and malformed** — silently, with no way to
  distinguish the two and no source span.
- Consumers that do set errors (window `window.c`, lookback `collect.c`, and even
  `validate_optimize.c`) emit them with position `0, 0` — **no source location**, because the
  accessor layer carries no span.
- Non-optimize metadata beyond window/lookback gets **no validation at all**.

What the generic API must do to actually deliver diagnostics (not repeat silent `false`):
- Return a result that distinguishes **absent vs present-but-malformed vs typed-value**.
- Carry the **source span** of the field (so a consumer can report "`…optimize.min` at line N
  is not a number").
- Back onto the Stage-0 typed tree so **structural** errors (malformed `{ }`) surface once,
  with position.

Scope of "full diagnostics" this unlocks: **structural + type/shape + uniform across all
keys** (a large improvement over today's fully-silent path). **Unknown-key / typo detection**
(`optimze`) is NOT provided by a generic API alone — it additionally needs a known-key schema
(the consumer-registry seam above). Note this limitation explicitly so it is not oversold.

## Implementation stages

Verify after each stage: `cmake --preset default && cmake --build build && (cd build &&
ctest --output-on-failure)`. Also run `cmake --preset asan` for the memory-sensitive stages.
CI builds Release/NDEBUG — do not rely on `assert()`-wrapped calls in tests
(see `cxgn-tests-assert-ndebug-trap`).

### Stage 0 — Typed metadata representation (foundation)

Files: new `src/model/metadata/*.c` (or alongside `src/model/accessors.c`),
`src/model/internal.h`, `src/model/parse.c`, `src/document.c:1143`.

- Define a typed metadata node: a tree of entries where each entry is
  `{ key, span, value }` and value is one of `{ number, string, bool, number-list,
  nested-block (list of entries) }`.
- Write ONE recursive-descent parser `body-string → typed tree` with source spans and real
  error messages (position of the offending token). This is the single canonical metadata
  parser; both top-level and nested metadata feed it.
- Parse the body into the tree **once**, where the body is attached
  (`cxpr_model_attach_metadatas` in `parse.c:349`, and `document.c:1143`). Keep the raw
  `body` string too (cheap; `cxpr_model_metadata_body` at `accessors.c:413` stays valid).
- Reimplement `cxpr_model_metadata_field_value` / `field_number` / `field_number_list`
  (`accessors.c:555/676/628`) as lookups over the typed tree, **preserving signatures and
  return semantics** (e.g. `field_value` returns a stable `const char*`; store trimmed value
  strings in the tree so the pointer stays valid). Result: the 33 cxpr call-sites and 3 dyn
  files are untouched (back-compat).
- **Add a new generic, typed retrieval API** (addition, not a replacement) that any consumer
  — optimize, window, lookback, description, future keys — can use for real diagnostics.
  Shape (illustrative): `cxpr_model_metadata_get(model, idx, key, &out_value, &err)` returning
  a tri-state **absent / malformed / present** and filling `out_value` (typed:
  number/string/bool/list/block) plus the field's **source span** on both success and the
  malformed case. This is what delivers diagnostics per the "Diagnostics" section — it must
  NOT collapse absent and malformed into one silent result like the legacy accessors.
- Tests: parser unit tests for scalars / lists / nested blocks / dotted lookups; malformed
  inputs asserting diagnostics **with source positions**; tri-state behavior (absent vs
  malformed vs present) of the new API. Regression: existing optimize / window / lookback
  behavior unchanged.

### Stage 1 — Generic nested-field metadata capture

Files: `src/ast/internal.h`, `src/ast/construct.c`, `src/ast/free.c`, clone path
(`src/ast/alias.c`), `src/parser/primary.c`, `src/parser/helpers.c:65`, `src/ast/inspect.c`,
`src/document.c` (constant lowering :559 / PARAM_DECL case :1352).

- Extend the `record` node (`internal.h:39-43`) with a parallel `char** field_metadata`
  (metadata-body **source substring** per field, `NULL` if none). This is **key-agnostic** —
  capture the whole `{ ... }` regardless of key (`optimize`, `description`, future).
- Replace `cxpr_skip_record_field_metadata` (`primary.c:196`) with a capture that records the
  **source substring** between the braces (use lexer source offsets; do not re-serialize
  tokens). Store into `metadata[count]` in `cxpr_parse_record_literal` (~`:277`).
- Update `cxpr_expr_ast_record_new` (`construct.c:70`) + both callers (`helpers.c:65`,
  `primary.c:284`) to carry the metadata array. **Critical:** `cxpr_expr_ast_clone` must
  deep-copy it (`append_constant` clones the param expr at `document.c:574`). Update free.
  Add `cxpr_expr_ast_record_field_metadata(ast, i)` accessor in `inspect.c`.
- In document lowering, after appending a PARAM constant whose expr is a record, walk it
  **recursively**; for each field carrying metadata, append a `cxpr_model_metadata` entry
  with `target_kind = CXPR_MODEL_METADATA_TARGET_PARAM`, `target_name` = dotted path
  (`param` + nested field path), and body fed to the Stage 0 parser. Generic: emits entries
  for every metadata key, not just optimize.

Verify: a probe prints dotted dimensions for `bb_params.period`, `kst_params.roc.r1`, and a
non-optimize key (e.g. `description`) on a nested field is retrievable via
`cxpr_model_metadata_field_value(model, idx, "description")`.

### Stage 2 — optimize consumer: dotted dims + list shorthand

File: `src/model/validate_optimize.c` (`cxpr_optimize_add_dimension`, lines 11-73).

- Accept the `optimize = [list]` shorthand: a bare `optimize` number list (via
  `cxpr_model_metadata_field_number_list(model, idx, "optimize", …)`) → `CXPR_OPT_DIM_VALUES`,
  in addition to `optimize.values` and `optimize.min|max|step`. Keep list-XOR-range checks.
- `dim->name` already copies `target_name`; dotted names flow through unchanged.

Verify: nested `optimize = [30,40,50,60]` yields a VALUES dim with 4 values; nested
`optimize { min, max, step }` yields a RANGE dim.

### Stage 3 — Apply: path-aware param set (arbitrary depth)

Files: `src/context/values.c` + `include/cxpr/context.h`, `src/context/structs.c`,
`src/model/optimize_run.c` (`:66`), `src/model/session*.c`
(`cxpr_model_session_set_param`, used at `:260`).

- Add `cxpr_context_set_param_path(ctx, "a.b.c", value)`: no dot → delegate to
  `cxpr_context_set_param`; else look up the root struct in the struct-map, clone/descend
  intermediate struct fields, set the leaf, store back via `cxpr_context_store_struct`.
  Handle depth ≥ 2. Clone-on-write; do not mutate shared/const structs in place.
- `optimize_run.c:66` → `cxpr_context_set_param_path(context, dim->name, value)`.
- Make `cxpr_model_session_set_param` path-aware (route dotted names to the new setter) so
  CPU replay (`:260`) applies nested overrides.
- Reads already work after the struct-map struct is updated (fact 5) — verify the one-dot
  shortcut (`structs.c:194`) returns the updated field.

Verify: `prepare_grid` prunes nested-constraint candidates (`active_count < candidate_count`)
and `cxpr_model_optimize` (CPU) selects the expected best candidate.

### Stage 4 — Tests

File: `tests/optimize_assert.test.c` (+ a small metadata-parser test file for Stage 0).

- Stage 0: typed parser valid/malformed/diagnostic cases.
- Nested dimension discovery: dotted names + counts (2-level struct param).
- **Generic extensibility test:** a non-optimize metadata key on a nested field is
  retrievable via the new generic API (proves the facility is not optimize-specific).
- **Diagnostics test:** a malformed metadata block (e.g. `optimize { min = abc }`) on a nested
  field surfaces a diagnostic with a **source position**, and the generic API reports
  `malformed` (distinct from `absent`) — not a silent miss.
- `optimize = [list]` shorthand → VALUES dim.
- Constraint over nested dotted params prunes the grid.
- Full `cxpr_model_optimize` over nested dims picks the correct best candidate.
- `assert` over a nested `$param` path fails at `cxpr_model_session_new`
  (`CXPR_ERR_ASSERTION_FAILED`, correct description).
- Regression: existing optimize/assert/window/lookback tests still pass; `ctest` green under
  `default` and `asan`.

## Future work (separate — do NOT bundle)

- **Grad 2: typed public metadata API.** Expose the Stage-0 typed tree through a typed public
  API and migrate all cxpr/dyn call-sites off the string accessors. Lets **dyn delete its
  scraper** (`libs/dyn/src/config/strategy_source.c:1191-1477`) and switch to cxpr for nested
  sweeps; add a dyn parity test (identical grids before/after). Separate effort.

## Risks & edge cases

- **Accessor signature/semantics preservation (Stage 0)** is the highest-risk item — a
  behavior change there ripples to 33 sites. Pin current behavior with characterization tests
  BEFORE refactoring internals.
- **Clone correctness (Stage 1):** if `field_metadata` isn't cloned, lowering sees nothing —
  add a focused clone test.
- **Memory ownership** of the new string arrays / typed tree across new/clone/free — run asan.
- **Struct mutation (Stage 3):** rebuild-and-store, no in-place mutation of shared structs.
- **Depth > 2:** implement generically; test at least depth 2 (`roc.r1`).
- **Mixed top-level + nested dims** must both appear with stable ordering (grid indexing
  depends on dimension order — document: declaration order, outer-to-inner).
- **Grid size:** ~13 swept params here; honor `options.max_candidates` (`optimize_run.c:233`).
- The real strategy
  (`tests/configs/strategies/bkadx_breakaway_ltf_base_cxpr_constraints.cxpr`) needs dyn's
  indicator registry to compile — use it for an end-to-end check in the dyn follow-up, not in
  cxpr unit tests.

## Done criteria

- Nested optimize is **native** (in parser/model/runtime), not a plugin; codegen/backends
  only consume it.
- Stage 0 typed parser lands with zero legacy accessor-call-site churn, plus a **new generic
  typed retrieval API** with tri-state (absent/malformed/present) results and source spans.
- Metadata diagnostics are **uniform across all keys** (structural + type), with source
  positions; a malformed nested block is reported, not silently dropped. (Unknown-key
  detection is explicitly deferred to a key schema.)
- Nested metadata is generic (a non-optimize key test passes), with dotted addressing.
- The nested example at the top yields the expected dimensions, constraint pruning, and
  best-candidate selection with no dyn involvement.
- No regression in existing tests; green under `default` and `asan`.
