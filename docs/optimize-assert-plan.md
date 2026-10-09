# CXPR Optimize & Assert — Implementation Plan

**Status:** Proposed implementation plan
**Source spec:** `cxpr-optimize-spec.md` (Optimize Specification)
**Author:** engineering plan mapping the spec onto the current codebase

---

## 0. Scope

The spec introduces five capabilities on top of the existing `.cxpr` model
language:

1. **`assert <expr> { description }`** — a model invariant over `$params`.
2. **`optimize <expr> { description }`** — an optimizer-only candidate
   constraint (does *not* affect normal `tick` execution).
3. **Search dimensions** — `optimize { min/max/step | values }` metadata on a
   `$param`.
4. **Objectives** — `optimize { minimize | maximize }` metadata on a named
   binding or `state`.
5. **Candidate execution** — an optimizer that generates candidates, filters by
   constraints, runs each with independent freshly-initialized state, and reads
   objectives after the final tick. CPU first, bulk/CUDA later.

**Key finding that shapes the whole plan:** the metadata block mechanism the
spec relies on *already exists and already parses*. Nested blocks
(`optimize { min = 5 }`) and bare flags (`maximize`) are supported today and are
introspectable through the public metadata API. The only genuinely new *syntax*
is the two statement forms `assert <expr>` and `optimize <expr>`. Everything
else is **semantics + a native optimize capability layer on top of existing
metadata**, not parser work.

---

## 1. Current-code map

### 1.1 Parse → lower → compile → execute pipeline

| Stage | Entry point | File |
| --- | --- | --- |
| Document parse (statement dispatch) | `cxpr_doc_ast_parse_statement` | `src/ast/document/parser.c:1100` |
| Metadata block parse | `cxpr_doc_ast_parse_host_fields` | `src/ast/document/parser.c:989` |
| Single-expression parse | `cxpr_doc_ast_parse_expr` | `src/ast/document/parser.c:412` |
| AST node kinds | `cxpr_doc_ast_kind` | `include/cxpr/doc/ast.h:34-101` |
| Lower AST → model | `cxpr_document_lower_node_to_model` | `src/document.c:1252` (switch) |
| Model struct | `struct cxpr_model` | `src/model/internal.h:90-125` |
| Compile model → program | `cxpr_model_compile` / `compile.c` | `src/model/compile/compile.c` |
| Compiled program struct | `struct cxpr_model_compiled` | `src/model/internal.h:224-269` |
| Seed constants ($params) | `cxpr_model_compiled_seed_defaults` | `src/model/program.c:136` |
| Seed state defaults + create session | `cxpr_model_session_new` | `src/model/session.c:~632` |
| Per-tick eval | `cxpr_model_compiled_eval` | `src/model/program.c:161` |
| Expression eval to bool | `cxpr_eval_ast_bool` | `include/cxpr/eval.h:46` |

### 1.2 What already exists (reuse, do not rebuild)

- **Nested metadata blocks on params and bindings.**
  `$fast = 12 { optimize { min = 5, max = 50, step = 10 } }` and
  `error = abs(x) { optimize { minimize } }` already parse. Proven by
  `tests/model.test.c:271-331` and `tests/parser_primary.test.c:28-29`, and
  verified against the current `cxpr_document_tooling` binary. Nested access uses
  dotted paths: `cxpr_model_metadata_field_number(model, i, "optimize.min", &n)`
  (`src/model/accessors.c:540`).
- **Output declarations accept metadata too.** `out profit { optimize { maximize } }`
  parses today (verified) and attaches metadata with `TARGET_OUTPUT`. This is the
  key enabler for **stateful public objectives without a parser change**: the
  objective annotation is just a label on a named value, and the value happens to
  be exposed via `out`.
- **Exception — state declarations do NOT accept a metadata block yet.**
  `state profit = 0 { optimize { maximize } }` fails to parse today: the `state`
  branch (`src/ast/document/parser.c:1197`) sees `{` and treats the whole thing
  as a `state { ... }` *block* of declarations, yielding "Invalid symbol name".
  Only `$param`, ordinary bindings, and outputs support trailing metadata.
  Because a public state objective can instead be annotated on its `out`
  declaration, the state-decl parser change (Phase 3a) is **optional** — needed
  only for objectives on state/bindings that are *not* exposed via `out`.

**Objective annotation sites (all resolve to "read this named value after the
final tick"):**

| Annotation site | Example | Parses today? |
| --- | --- | --- |
| Binding | `error = abs(p - m) { optimize { minimize } }` | Yes |
| Output (incl. exposed state) | `out profit { optimize { maximize } }` | Yes |
| State decl | `state profit = 0 { optimize { maximize } }` | No — Phase 3a (optional) |
- **Bare boolean flags.** A metadata field with no `=` is stored as `text="true"`
  (`src/ast/document/parser.c:1080`), so `optimize { maximize }` already parses
  and reads back as `"true"`.
- **Metadata introspection API** (`include/cxpr/model/model.h:694-715`):
  `cxpr_model_metadata_count/name/body/target_kind_at/target_name/`
  `field_value/field_number/field_number_list`.
- **Metadata target kinds** (`include/cxpr/model/model.h:25-34`):
  `MODEL/USE/INPUT/PARAM/BINDING/STATE/FUNCTION/OUTPUT`. Metadata is already
  attached to params, bindings, state and outputs during lowering
  (`cxpr_document_lower_metadata_children` in `src/document.c`).
- **State declaration + default seeding.** State bindings compile into
  `program->state_defaults[]` (`src/model/compile/compile.c:804`) and are
  evaluated into the context at session creation (`src/model/session.c:~636`).
- **Bulk execution** (`include/cxpr/bulk.h`, `src/bulk.c`): SoA host storage,
  one **per-element state block** (`state_stride`), per-element reset
  (`cxpr_bulk_reset_range`, `src/bulk.c:95`), driven by the generated-C scalar
  descriptor (`include/cxpr/generated.h:118`).

### 1.3 What is missing (the actual work)

- No `assert` or `optimize` **statement** forms in the parser dispatch.
- No model/compiled storage for asserts or optimize-constraints.
- No `CXPR_ERR_ASSERTION_FAILED` error code.
- No load-time assert evaluation hook.
- No **semantic validation** of optimize metadata (min<max, step>0, `values`
  xor `min/max/step`, `minimize` xor `maximize`, objectives must be named).
- No native **optimize introspection API** (enumerate search dimensions,
  constraints, objectives) — today a host would have to hand-walk metadata.
- No **candidate runner** and no **per-candidate fresh-state** guarantee. There
  is no session reset today; a fresh run means a new session
  (`cxpr_model_session_new` / `_free`).
- **Bulk shares one `params` pointer across all elements**
  (`cxpr_bulk_view.params`, `include/cxpr/bulk.h:45`) — candidates need
  *per-element* params. This is the largest execution gap for CUDA.

---

## 2. Syntax surface — what needs parser work

| Spec form | Example | Parser change? |
| --- | --- | --- |
| Assert statement | `assert $fast > 0 { description = "..." }` | **Yes** — new statement |
| Optimize constraint statement | `optimize $fast < $slow { description = "..." }` | **Yes** — new statement |
| Search dimension (range) | `$fast = 12 { optimize { min=5 max=50 step=5 } }` | No — metadata exists |
| Search dimension (values) | `$slow = 26 { optimize { values = [20,30] } }` | No — metadata exists |
| Objective (binding) | `error = expr { optimize { minimize } }` | No — metadata exists |
| Objective (output / exposed state) | `out profit { optimize { maximize } }` | No — metadata exists |
| Objective (state decl, internal only) | `state profit = 0 { optimize { maximize } }` | **Yes (optional)** — collides with `state { decls }` |

Only the two statement forms are *required* parser work. The `state <name> =
<init> { metadata }` form (Phase 3a) is optional — public state objectives can be
annotated on their `out` declaration instead, which parses today. The remaining
metadata forms are validation + runtime only.

---

## 3. Phased implementation

Each phase is independently shippable and testable. Phases 1–4 are pure
front-end / metadata work with low risk. Phase 5 is the first real optimizer.
Phase 6 is the CUDA/bulk scaling story.

### Phase 1 — `assert` statement (model invariant)

Smallest, self-contained, useful on its own. A load-time invariant over
`$params`, hard-failing session creation.

**Parser / AST**
- Add `CXPR_DOC_AST_ASSERT` to `include/cxpr/doc/ast.h` enum.
- Add an `assert` branch in `cxpr_doc_ast_parse_statement`
  (`src/ast/document/parser.c:1100`): split the condition text at the first
  `{`, parse it via `cxpr_doc_ast_parse_expr`, and (if present) parse the
  trailing `{ ... }` into a `CXPR_DOC_AST_METADATA` child via
  `cxpr_doc_ast_parse_host_fields` (mirror the `model` decl at
  `parser.c:1125-1156`). Store the raw condition text on `node->text` for
  diagnostics.
- Reserve `assert` in `cxpr_doc_ast_reserved_host_kind`
  (`src/ast/document/parser.c:340`) so it is never mistaken for a host block.

**Model + lowering**
- New `cxpr_model_assert { char* source; cxpr_expr_ast* expr; char* description;
  cxpr_source_span span; bool has_span; }` in `src/model/internal.h`; add
  `asserts` / `assert_count` to `struct cxpr_model`.
- New compiled form `cxpr_model_compiled_assert { cxpr_expr_ast* ast; char*
  description; char* source; cxpr_source_span span; bool has_span; }`; add
  `asserts` / `assert_count` to `struct cxpr_model_compiled`.
- Add `case CXPR_DOC_AST_ASSERT:` to `cxpr_document_lower_node_to_model`
  (`src/document.c:1256`) calling a new `cxpr_doc_model_append_assert` (mirror
  `cxpr_doc_model_append_constant` at `src/document.c:559`); pull `description`
  from the metadata child's `HOST_FIELD` children (strip quotes).
- Free in `cxpr_model_free` (`src/model/accessors.c:141`) and
  `cxpr_model_compiled_free` (`src/model/program.c:~70`).

**Compile + evaluate**
- In `compile.c`, after the constants block (`src/model/compile/compile.c:793`):
  inline defined calls + `cxpr_typecheck` each assert (mirror constants at
  `compile.c:755-758`); **enforce param-only** via
  `cxpr_expr_ast_references(ast, buf, N) == 0` (no plain identifiers = only
  `$params` / literals; identifiers would be inputs/bindings not known at load
  time). On violation → `CXPR_ERR_SYNTAX` naming the offending identifier.
- New `cxpr_model_compiled_check_asserts(program, ctx, reg, err)` in
  `program.c`: for each assert, `cxpr_eval_ast_bool`; on `false` →
  `CXPR_ERR_ASSERTION_FAILED` with `description` (fallback `"Assertion failed"`).
- Call it in `cxpr_model_session_new` immediately after
  `cxpr_model_compiled_seed_defaults` (`src/model/session.c:632`); on failure
  free the session and return NULL.

**Errors**
- Add `CXPR_ERR_ASSERTION_FAILED` to `include/cxpr/types.h:44` and to
  `cxpr_error_string` (`src/expression/expression.c:229`).

**Acceptance:** a model with `assert $fast < $slow { description = "..." }` fails
`cxpr_model_session_new` with `CXPR_ERR_ASSERTION_FAILED` and the description as
the message when params violate it; passes otherwise. An assert referencing an
input fails at compile with a clear message.

**Checklist**
- [ ] Add `CXPR_ERR_ASSERTION_FAILED` to `cxpr_error_code` (`include/cxpr/types.h:44`) and a case in `cxpr_error_string` (`src/expression/expression.c:229`).
- [ ] Add `CXPR_DOC_AST_ASSERT` to the enum in `include/cxpr/doc/ast.h`.
- [ ] Add `"assert"` to `cxpr_doc_ast_reserved_host_kind` (`src/ast/document/parser.c:340`).
- [ ] Add the `assert` branch in `cxpr_doc_ast_parse_statement` (`src/ast/document/parser.c:1100`): parse condition via `cxpr_doc_ast_parse_expr`, parse optional `{ … }` into a `CXPR_DOC_AST_METADATA` child via `cxpr_doc_ast_parse_host_fields`, store raw condition on `node->text`.
- [ ] Define `cxpr_model_assert` + add `asserts`/`assert_count` to `struct cxpr_model` (`src/model/internal.h`).
- [ ] Define `cxpr_model_compiled_assert` + add `asserts`/`assert_count` to `struct cxpr_model_compiled` (`src/model/internal.h`).
- [ ] Implement `cxpr_doc_model_append_assert` (mirror `cxpr_doc_model_append_constant`, `src/document.c:559`); extract `description` from the metadata child (strip quotes).
- [ ] Add `case CXPR_DOC_AST_ASSERT:` to `cxpr_document_lower_node_to_model` (`src/document.c:1256`).
- [ ] Compile asserts after the constants block (`src/model/compile/compile.c:793`): inline defined calls + `cxpr_typecheck`; enforce param-only via `cxpr_expr_ast_references(ast, buf, N) == 0`, else `CXPR_ERR_SYNTAX` naming the identifier.
- [ ] Free asserts in `cxpr_model_free` (`src/model/accessors.c:141`) and `cxpr_model_compiled_free` (`src/model/program.c:~70`).
- [ ] Implement `cxpr_model_compiled_check_asserts` in `src/model/program.c` (eval each via `cxpr_eval_ast_bool`; on false → `CXPR_ERR_ASSERTION_FAILED` with description) and declare it in `include/cxpr/model/model.h` next to `seed_defaults`.
- [ ] Call `check_asserts` in `cxpr_model_session_new` right after `seed_defaults` (`src/model/session.c:632`); free session + return NULL on failure.
- [ ] Tests: parse (assert node + metadata), pass, fail-with-description, param-only rejection. Wire `assert_basic.cxpr` into a test.
- [ ] Build + run: `cmake --build build && ctest --test-dir build --output-on-failure`. Re-parse `assert_basic.cxpr` with `build/cxpr_document_tooling` → `ok:true`.

### Phase 2 — `optimize <expr>` constraint statement

Same statement machinery as Phase 1, but the constraint is stored separately and
is **inert during normal execution** — it is only consumed by the optimizer
(Phase 5). Per spec §4: a config with `$fast >= $slow` is still valid for
ordinary `tick`; it is merely excluded from optimization.

- Add `CXPR_DOC_AST_OPTIMIZE_CONSTRAINT` node kind + parser branch (identical
  shape to `assert`, keyword `optimize` **followed by an expression**; note the
  disambiguation below).
- **Disambiguation:** `optimize` appears both as a statement keyword
  (`optimize <expr>`) and as a metadata block name (`optimize { ... }` inside a
  declaration's braces). At statement level the two are distinguished by what
  follows the keyword: a `{` immediately after `optimize` is invalid at
  statement scope (metadata only appears attached to a declaration), whereas
  `optimize $x < $y` / `optimize $x < $y { description }` is the constraint
  statement. The metadata form never reaches statement dispatch because it lives
  inside another declaration's `{ ... }` body.
- Store as `cxpr_model_optimize_constraint` (same shape as `cxpr_model_assert`)
  on the model and compiled program. Param-only check identical to asserts.
- **No execution hook in `cxpr_model_session_new`** — constraints are read by
  the optimizer, not enforced on normal runs.

**Acceptance:** constraint parses, compiles, and is retrievable via the Phase 4
API; a normal `tick()` run is unaffected by a violated optimize-constraint.

**Checklist**
- [ ] Add `CXPR_DOC_AST_OPTIMIZE_CONSTRAINT` to `include/cxpr/doc/ast.h`.
- [ ] Add the `optimize <expr>` statement branch in `cxpr_doc_ast_parse_statement` (mirror `assert`); ensure the metadata-block form (`optimize {` inside a decl) still routes through the declaration path, not statement dispatch. Add `"optimize"` to `cxpr_doc_ast_reserved_host_kind` if needed.
- [ ] Define `cxpr_model_optimize_constraint` (same shape as `cxpr_model_assert`) on `struct cxpr_model` and `struct cxpr_model_compiled` (`src/model/internal.h`).
- [ ] Implement `cxpr_doc_model_append_optimize_constraint` + `case CXPR_DOC_AST_OPTIMIZE_CONSTRAINT:` in `cxpr_document_lower_node_to_model` (`src/document.c:1256`).
- [ ] Compile constraints (inline + typecheck + param-only check) alongside asserts in `compile.c`; free in both free paths.
- [ ] Confirm NO hook is added to `cxpr_model_session_new` (constraints are inert on normal runs).
- [ ] Tests: constraint parses into the right node; a normal `tick()` with a violated constraint still runs. Wire `optimize_constraint.cxpr` into a test.
- [ ] Build + run tests; re-parse `optimize_constraint.cxpr` → `ok:true`.

### Phase 3 — State-decl metadata + semantic validation of optimize metadata

**3a. Parser: allow `state <name> = <init> { metadata }` (OPTIONAL).**
Public state objectives can be annotated on the `out` declaration
(`out profit { optimize { maximize } }`, parses today), so this is only needed
for objectives on state that is *not* exposed via `out`. If implemented: in the
`state` branch (`src/ast/document/parser.c:1197`) the presence of `{` currently
forces the `state { decls }` block path. Change the discriminator: if there is a
top-level `=` *before* the first `{` (i.e. `state NAME = EXPR { ... }`) parse it
as a `CXPR_DOC_AST_STATE_DECL` with a trailing metadata child (reuse
`cxpr_doc_ast_parse_host_fields`, exactly like the `$param` decl path); only fall
to the block path when `{` immediately follows `state`. Use the existing
`cxpr_doc_ast_top_level_char(rest, '=')` helper. State metadata already lowers
(`CXPR_DOC_AST_STATE_DECL` → `TARGET_STATE`, `src/document.c:1307`), so no
lowering change is needed — this is parser-only. Add a regression test for both
forms.

**3b. Semantic validation.**
A validation pass over already-parsed metadata. Runs during compile (earliest
failure) via a new `cxpr_model_validate_optimize` invoked from
`cxpr_model_compile`.

Rules (spec §3, §5):
- Search dimension on a `$param` (`TARGET_PARAM`): `optimize.values` **xor**
  (`optimize.min` / `optimize.max` / `optimize.step`); reject `step <= 0`,
  `min > max`; reject a `min/max/step` grid with no integer step count.
- Objective on a binding/output/state (`TARGET_BINDING` / `TARGET_OUTPUT` /
  `TARGET_STATE`): `optimize.minimize` **xor** `optimize.maximize`; reject both
  present. Deduplicate when the same named value carries the annotation in two
  places (e.g. both a state decl and its `out`).
- Objectives must be **named** (they already are, being metadata on a named
  declaration); reject any objective metadata on an anonymous output.
- Search-space metadata is only valid on `$params`; warn/error if `min/max/step/
  values` appears on a non-param.

Use `cxpr_model_metadata_field_value` / `_field_number` / `_field_number_list`
(`src/model/accessors.c`) to read. Emit `CXPR_ERR_SYNTAX` (or a new
`CXPR_ERR_INVALID_METADATA`) with the metadata span.

**Acceptance:** invalid grids/objectives fail compile with precise spans; valid
ones compile.

**Checklist**
- [ ] (3a, optional) Change the `state` branch discriminator in `src/ast/document/parser.c:1197` to route `state NAME = EXPR { … }` to `CXPR_DOC_AST_STATE_DECL` + metadata child (top-level `=` before `{`), keeping `state { … }` as block. Add regression tests for both forms.
- [ ] Add `cxpr_model_validate_optimize` (new file e.g. `src/model/validate_optimize.c`) invoked from `cxpr_model_compile`.
- [ ] Validate search dims (`TARGET_PARAM`): `values` xor `min/max/step`; reject `step <= 0`, `min > max`, non-integer step count.
- [ ] Validate objectives (`TARGET_BINDING`/`TARGET_OUTPUT`/`TARGET_STATE`): `minimize` xor `maximize`; dedupe same-name; reject on anonymous outputs.
- [ ] Reject search-space metadata on non-params.
- [ ] Emit `CXPR_ERR_SYNTAX` (or add `CXPR_ERR_INVALID_METADATA`) with the metadata span.
- [ ] Tests: each rejection case + a valid model; keep `tests/model.test.c:271-331` passing.

### Phase 4 — Native optimize introspection API

A thin public API so hosts (and Phase 5) enumerate the optimization model
without hand-walking metadata. Declared in a new
`include/cxpr/model/optimize.h`, implemented over the compiled program +
metadata API.

Sketch:
```c
size_t                       cxpr_model_optimize_dimension_count(const cxpr_model_compiled*);
cxpr_model_optimize_dimension cxpr_model_optimize_dimension_at(const cxpr_model_compiled*, size_t);
/* dimension: name, kind (RANGE|VALUES), min/max/step or explicit values[]     */

size_t                       cxpr_model_optimize_constraint_count(const cxpr_model_compiled*);
const cxpr_expr_ast*         cxpr_model_optimize_constraint_at(const cxpr_model_compiled*, size_t);

size_t                       cxpr_model_optimize_objective_count(const cxpr_model_compiled*);
cxpr_model_optimize_objective cxpr_model_optimize_objective_at(const cxpr_model_compiled*, size_t);
/* objective: name, direction (MINIMIZE|MAXIMIZE), source (binding|output|state) */
```
Objectives and dimensions are derived from metadata at compile time and cached
on `cxpr_model_compiled` so the optimizer does not re-parse per candidate.
Objective derivation scans `TARGET_BINDING`, `TARGET_OUTPUT` and `TARGET_STATE`
metadata for `optimize.minimize` / `optimize.maximize`, keyed by the value name
(so `out profit { optimize { maximize } }` and a hypothetical state-decl
annotation collapse to one objective on `profit`).

**Multiple metrics per candidate.** A candidate result is a *vector* of named
values, not a single scalar. Real hosts collect several metrics per combination
(e.g. a primary objective plus secondary quality/robustness measures). The API
must therefore let a candidate report *any* named output value as a readable
metric (not only the declared objectives), so hosts can rank/filter on secondary
metrics and apply their own guards. Objectives declare *direction*; metrics are
just readable outputs.

**Acceptance:** for the full spec §13 example, the API returns dimensions
`$fast`/`$slow`, constraint `$fast < $slow`, objective `profit`
(maximize).

**Checklist**
- [ ] Create `include/cxpr/model/optimize.h` with `cxpr_model_optimize_dimension` / `_objective` structs + enums (`CXPR_OPT_DIM_RANGE|VALUES`, `CXPR_OPT_MINIMIZE|MAXIMIZE`) and the count/at accessors.
- [ ] Add cached `dimensions` / `objectives` arrays to `struct cxpr_model_compiled` (`src/model/internal.h`); populate during `cxpr_model_compile` from metadata.
- [ ] Implement accessors (new `src/model/optimize.c`) reading cached data; constraint accessors return the compiled constraint ASTs from Phase 2.
- [ ] Export the header in the public umbrella header / install rules; add to `CMakeLists.txt` sources.
- [ ] Test: enumerate dims/constraints/objectives from `ma_cross_optimize.cxpr`.

### Phase 5 — Candidate runner (CPU, serial)

The first working optimizer. Domain-agnostic (spec §1, §17).

- **Candidate generation:** cartesian product of dimension values
  (expand `min/max/step` and `values`).
- **Constraint filter:** evaluate each param-only optimize-constraint against a
  candidate's params *before* execution (spec §4 “apply before expensive
  candidate execution”). Reuse `cxpr_eval_ast_bool` with a context holding just
  that candidate's params.
- **Per-candidate fresh state (spec §8, the central invariant):** the cleanest
  path today is one `cxpr_model_session` per candidate — `session_new` already
  seeds state defaults from initializers, giving true isolation for free — then
  `session_free`. Optimization: add `cxpr_model_session_reset(session)` that
  re-seeds constants + state defaults into the existing context (re-run of
  `seed_defaults` + the state-default loop from `session.c`) to avoid
  alloc/free churn across many candidates.
- **Param binding per candidate:** need a public
  `cxpr_model_session_set_param(session, name, &value)` that overrides a seeded
  `$param` before replay (today only generic `cxpr_context_set*` exists,
  `include/cxpr/context.h`). Set candidate params, then re-derive dependent
  constants.
- **Replay + objective read (spec §7):** feed the identical input sequence,
  `tick` to the end, then read each objective's final value from the context by
  name. Assemble `{candidate params → objective values}`.
- **Result selection (spec §11):** single objective → best candidate; multiple
  → return the full scored set (Pareto handling left to host policy — the model
  only declares direction).

Host entry point (new): `cxpr_model_optimize(program, inputs, options, &result,
err)`.

**Acceptance:** the spec §13 MA-cross example, run over a price series, returns
the `$fast`/`$slow` pair maximizing final `profit`, with `$fast < $slow`
candidates only, each candidate isolated.

**Checklist**
- [ ] Add `cxpr_model_session_set_param(session, name, &value)` (`src/model/session.c` + declare in `include/cxpr/model/model.h`).
- [ ] Add `cxpr_model_session_reset(session)` re-seeding constants + state defaults (factor the seeding loop out of `cxpr_model_session_new`, `src/model/session.c:632+`).
- [ ] Implement candidate expansion (cartesian product of dimension values) in a new `src/model/optimize_run.c`.
- [ ] Implement constraint filtering pre-execution using `cxpr_eval_ast_bool` over candidate params.
- [ ] Implement replay: for each surviving candidate, set params → reset → tick over the input sequence → read objective final values by name.
- [ ] Implement `cxpr_model_optimize(program, inputs, options, &result, err)` + a result struct (candidate params → objective values, best/selected).
- [ ] Tests: deterministic best candidate over a fixed series; state-isolation regression (two candidates must not leak state); constraint exclusion honored.

### Phase 6 — Bulk / CUDA candidate execution

Scale Phase 5 across candidates (spec §9). One batch slot = one candidate.

- **Per-candidate params:** extend the bulk contract so params can vary per
  element. Add a strided `params` column (like inputs) or a `param_stride` to
  `cxpr_bulk_view` (`include/cxpr/bulk.h:45`), keeping stride-0 = shared for
  backward compatibility. This is the core enabler and the main API change.
- **Per-candidate state:** already supported — one state block per element with
  per-element `reset` (`src/bulk.c:95`). Candidate isolation (spec §8) maps
  directly onto `state_stride`.
- **Objective collection:** expose each objective as an output column; read the
  final-tick value per slot after replaying the full sequence
  (spec §9 chunking: physical batch boundaries must not change results).
- **Driver:** a helper that expands candidates → fills a `cxpr_bulk_view`
  (per-slot params + fresh state) → replays inputs across all slots → gathers
  objective columns → selects best. CUDA reuses the same generated-C descriptor
  in a one-thread-per-candidate kernel (spec §9), device-resident state and
  accumulation, results copied back at the end (spec §10).

**Acceptance:** bulk optimizer reproduces Phase 5 results bit-for-bit on CPU;
CUDA parity test matches within tolerance; **and CUDA throughput is within an
agreed factor of any existing host-side grid optimizer being replaced** (see §9 —
for a replacement, performance parity is a first-class acceptance criterion, not
just numeric parity).

**Checklist**
- [ ] Extend `cxpr_bulk_view` (`include/cxpr/bulk.h:45`) with per-element params (a strided `params` column or `param_stride`), stride-0 = shared for backward compat; update `cxpr_bulk_validate`.
- [ ] Thread per-element params through `cxpr_bulk_run_range` (`src/bulk.c:57`) into `descriptor->tick`.
- [ ] Add a candidate→bulk driver: expand candidates → fill per-slot params + fresh state (`cxpr_bulk_reset_range`) → replay sequence → gather objective output columns → select best. The reference pattern is a host CPU grid runner (`grid_batch.c`) mirrored on CUDA (`grid_batch_cuda.c`).
- [ ] CUDA: one-thread-per-candidate kernel over the generated-C descriptor (cf. the `grid_batch_synthetic.cu` dispatch shape); device-resident state/accumulation; copy back final objectives (spec §9, §10). The per-model device evaluator is produced by cxpr codegen rather than hand-written kernels.
- [ ] Benchmark: CUDA candidate throughput on a representative grid; record in `benchmarks/`. When replacing an existing host optimizer, compare against it and fail the milestone if generated code is materially slower (§9 risk).
- [ ] Tests: CPU bulk vs Phase 5 serial parity; CUDA parity under `CXPR_BUILD_CUDA_TESTS`; chunking invariance (batch boundaries don't change results).

### Phase 7 — Editor tooling: syntax highlighting for `assert` / `optimize`

The VS Code extension (`tools/vscode-cxpr/`) and the language server
(`tools/language-server/`) must colour the two new keywords. Today:
- The TextMate grammar `tools/vscode-cxpr/syntaxes/cxpr.tmLanguage.json` has a
  keyword-control list (the `#keywords` repository, ~line 164) and a
  negative-lookahead block-keyword list (~line 61:
  `name|model|use|in|fn|update|out|state|meta`). It already contains an
  `optimize` rule (~line 224) scoped `variable.other.named-argument.cxpr` for the
  **metadata** form `optimize = …` / `optimize {`.
- The language server core `tools/language-server/cxpr-language-core.js` has a
  `CXPR_KEYWORDS` array (~line 45) and a `keyword` colour (`#C586C0`, ~line 40).

**Design:** colour `assert` and the **statement** form of `optimize` as
`keyword.control` (existing keyword colour). Keep the existing metadata-`optimize`
rule (named-argument scope) so `optimize { … }` inside a declaration stays
distinct from the `optimize <expr>` statement. Optionally introduce a dedicated
scope/colour for constraint keywords if a visual distinction from other keywords
is wanted (the user asked for "the new colours" — default to the keyword colour,
offer a distinct one as a follow-up).

**Checklist**
- [ ] tmLanguage: add a `keyword.control.cxpr` match for `\bassert\b` in the `#keywords` repository.
- [ ] tmLanguage: add a `keyword.control.cxpr` match for the statement `optimize` (e.g. `\boptimize\b(?!\s*(?:=|\{))`), ordered AFTER the existing metadata `optimize` rule so `optimize {`/`optimize =` keep the named-argument scope.
- [ ] tmLanguage: add `assert\b|optimize\b` to the negative-lookahead block list (~line 61) so the constraint statements aren't captured as generic block/binding names.
- [ ] language-server: add `"assert"` and `"optimize"` to `CXPR_KEYWORDS` (`tools/language-server/cxpr-language-core.js:45`) so LSP semantic tokens colour them with the keyword colour.
- [ ] (optional) Add a distinct colour/scope for `assert`/`optimize` if a visual distinction is desired; wire it in both the grammar `tokenColors`/theme and the language-core colour map (~line 40).
- [ ] Bump the extension version in `tools/vscode-cxpr/package.json` and rebuild the vsix (`tools/vscode-cxpr/scripts/build-vsix.js`); the new `.vsix` lands in `tools/vscode-cxpr/dist/`.
- [ ] Update language-server tests (`tools/language-server/test/server.test.js`) to assert `assert`/`optimize` tokenize as keywords; run that test suite.
- [ ] Manual check: open a fixture (`tests/fixtures/optimize/ma_cross_optimize.cxpr`) in VS Code with the rebuilt extension and confirm `assert` and `optimize <expr>` are highlighted while `optimize { … }` metadata retains its distinct colour.

---

## 4. New data structures (summary)

```c
/* src/model/internal.h */
typedef struct { char* source; cxpr_expr_ast* expr; char* description;
                 cxpr_source_span span; bool has_span; } cxpr_model_assert;
typedef cxpr_model_assert cxpr_model_optimize_constraint; /* same shape */

/* cached on cxpr_model_compiled (Phase 4) */
typedef enum { CXPR_OPT_DIM_RANGE, CXPR_OPT_DIM_VALUES } cxpr_model_optimize_dim_kind;
typedef enum { CXPR_OPT_MINIMIZE, CXPR_OPT_MAXIMIZE }    cxpr_model_optimize_direction;
```

`struct cxpr_model` and `struct cxpr_model_compiled` each gain
`asserts/assert_count` and `optimize_constraints/optimize_constraint_count`;
the compiled program additionally caches derived `dimensions` and `objectives`.

---

## 5. Errors & diagnostics

- `CXPR_ERR_ASSERTION_FAILED` (Phase 1) — runtime invariant violation at session
  creation; message = assert description.
- Reuse `CXPR_ERR_SYNTAX` for parser/semantic failures, or add
  `CXPR_ERR_INVALID_METADATA` (Phase 3) for optimize-metadata validation to give
  hosts a distinct code. All carry the offending source span.

---

## 6. Testing strategy

- **Parser/lowering** (`tests/document.test.c`, `tests/parser_primary.test.c`):
  assert + optimize-constraint parse into the right nodes; metadata attaches.
- **Assert execution** (new `tests/model_assert.test.c` or extend
  `tests/model.test.c`): pass, fail-with-description, param-only rejection.
- **Optimize metadata validation** (extend `tests/model.test.c`): grid/objective
  rejection cases; the existing `tests/model.test.c:271-331` already covers
  metadata read-back and must keep passing.
- **Introspection API** (Phase 4): enumerate dims/constraints/objectives from
  the spec example.
- **Candidate runner** (Phase 5): deterministic best-candidate over a fixed
  series; state-isolation regression (two candidates must not leak state).
- **Bulk/CUDA** (Phase 6): CPU-vs-bulk parity, CUDA parity (guarded by
  `CXPR_BUILD_CUDA_TESTS`).
- Register new fixtures in `tests/CMakeLists.txt` as needed.

---

## 7. Fixtures

Created under `tests/fixtures/optimize/` (see that directory). Parse status
below was verified against the current `build/cxpr_document_tooling` binary.

| Fixture | Demonstrates | Parses today? |
| --- | --- | --- |
| `search_dimensions.cxpr` | range + explicit-values search dims (`$p { optimize { … } }`) | **Yes** |
| `objectives.cxpr` | binding objective + output objective (exposed state) | **Yes** (uses `out … { optimize }`) |
| `assert_basic.cxpr` | assert invariants | After Phase 1 |
| `optimize_constraint.cxpr` | optimize constraint + asserts | After Phase 1–2 |
| `ma_cross_optimize.cxpr` | complete spec §13 example (all forms) | After Phases 1–3 |

These fixtures intentionally show the *target* spec syntax so they double as
acceptance targets: each becomes parseable/valid as its phase lands, and each is
wired into `tests/` at that point.

**Convention:** fixtures use the flat comma-separated `in`/`out` form
(`in close, trend` / `out signal`), not the braced block form (`in { … }`). A
single output that carries an objective still uses the trailing metadata block:
`out profit { optimize { maximize } }`.

---

## 8. Risks & open questions

- **`optimize` keyword overload** (statement vs metadata block) — resolved by
  scope (metadata only inside declaration braces) but must be covered by tests.
- **Per-candidate params in bulk** (§9) is a public-ABI change to
  `cxpr_bulk_view`; design it stride-compatible so existing callers are
  unaffected.
- **Objective evaluation point** (§7): "final tick" must be defined precisely
  for models with no state (instantaneous value) vs accumulators — the runner
  always reads the post-final-tick context value; the model owns aggregation.
- **Counterfactual objectives** (§10): out of scope for the engine — documented
  as a modelling requirement, not enforced.
- **Multi-objective selection** (§11): engine returns the scored candidate set;
  Pareto/weighting is host policy, not model semantics.

---

## 9. Replacing an existing host optimizer & reuse

A concrete driver for this work is **replacing a host's existing bespoke
optimizer** with the cxpr-native capability, while exposing the same capability
via public API to any other host. This section captures the general
replacement pattern; it applies whenever a host has already grown its own
parameter-search machinery on top of cxpr models.

### 9.1 The typical existing optimizer

Such host optimizers usually have two layers in one subsystem:

1. **Generic search + execution** — grid search over `$params` whose search space
   is declared in the host's own config format (e.g. `[28, 32, 35]` or
   `{min, max, step}` — the *same shape* as cxpr `optimize { values | min max
   step }`), plus, for performance, a CUDA subsystem of hand-written per-operator
   kernels + descriptors (`cuda/indicators/*.cu` + `*_descriptor.c`), a
   `fragment_registry`, strategy code generation, and a grid-batch runner: a CPU
   runner (`grid_batch.c`) mirrored by a CUDA path (`grid_batch_cuda.c` +
   `grid_batch_synthetic.cu`) of the form "one thread evaluates one parameter
   combination across the whole input sequence; host owns grid construction and
   result ranking".
2. **Domain orchestration** — caching (content-addressed on model + data +
   settings), best-params storage, data feeds, domain accounting (`portfolio_score.c`),
   job control, and any overfit / robustness policy.

Frequently the host *already* uses a cxpr model as its objective/fitness
function (e.g. a `robust_optimizer`-style model computing a score from `$params`
and metrics). When so, the objective side is low-risk; **search + accelerated
execution is the heavy part.**

### 9.2 Replace / keep / reuse

| Existing host mechanism | Fate under cxpr optimize |
| --- | --- |
| Host-format grid declaration | **Replace** → cxpr metadata (parses today) |
| Candidate generation + grid batch | **Replace** → Phase 5 runner + Phase 6 bulk/CUDA |
| Hand-written per-operator CUDA kernels + descriptors + registry (`cuda/indicators/*.cu`, `*_descriptor.c`, `fragment_registry`) | **Replace with codegen** → cxpr generates the device evaluator from the model |
| `min/max/step` validation, "search key must exist in params" | **Replace** → Phase 3 validation |
| Grid-batch CUDA dispatch/ranking harness (`grid_batch_cuda.c`, `grid_batch_synthetic.cu`) | **Reuse the pattern** → Phase 6 driver (one thread per candidate) |
| CPU grid runner (`grid_batch.c`) | **Reuse as reference** → Phase 5 numeric-parity target |
| Scoring + multi-metric result struct (`portfolio_score.c`) | **Reuse concepts** → Phase 4 multi-metric results |
| Caching, best-params, feeds, domain accounting, overfit guard | **Keep in the host** → policy on top of the cxpr API |

### 9.3 The biggest win, and the biggest risk

- **Win — codegen replaces hand-maintained CUDA.** A host that hand-writes a
  kernel + descriptor per operator (`cuda/indicators/*.cu` + `*_descriptor.c`)
  carries ongoing maintenance cost. cxpr already has model→C/CUDA codegen
  (`generated.h` descriptor + bulk), so the device evaluator is generated from the
  model. This is a durable maintenance reduction, not just consolidation — and it
  is what makes the capability truly generic (any model, not a fixed operator
  set).
- **Risk — performance parity.** A mature host's kernels are hand-tuned.
  Generated code must match them in *throughput*, not only produce the same
  numbers. This is the crux of whether replacement is worthwhile and is a
  first-class Phase 6 acceptance criterion (§Phase 6).
- **Risk — feature parity beyond the spec.** Caching, secondary-metric ranking,
  robustness guards. Most is host policy that stays in the host, but the API must
  be rich enough to carry it — hence multi-metric candidate results (Phase 4) and
  a scored candidate *set* rather than a single winner.

### 9.4 Genericity — "optimize anything"

The engine is domain-agnostic by construction: it only knows *dimensions*
(`$params` with `optimize` metadata), *constraints* (`optimize <expr>`),
*objectives* (`optimize { min/max }` on named values) and *metrics* (any named
output). No domain-specific concept enters the cxpr layer. Every host uses the
identical API:

```c
/* domain-agnostic: the model defines the search; the host supplies inputs */
cxpr_model_optimize(program, inputs, options, &result, err);
```

Domain concerns (feeds, accounting, caching, policy) live entirely in the host
above this call, so a calibration/physics/controller host reuses the same engine
unchanged. See §10 for how far the genericity goal reaches and where a host must
still supply semantics.

### 9.5 Migration order (when replacing an existing optimizer)

1. Ship Phases 1–4 (assert, optimize-constraint, validation, introspection) —
   low risk; the host can start reading search space/objectives from the model.
2. Build Phase 5 CPU runner; verify numeric parity against the host's existing
   CPU grid runner (`grid_batch.c`).
3. Build Phase 6 bulk/CUDA; verify numeric **and throughput** parity.
4. Run both optimizers in parallel behind a flag until parity holds; then remove
   the host's bespoke engine, keeping only its orchestration layer on top of the
   API.

---

## 10. Genericity — "optimize anything"

**Goal:** anything expressible as a cxpr model should be optimizable through the
same API, with zero domain knowledge in the engine.

### 10.1 Why it is generic by construction

Optimization needs exactly four things, and cxpr already models all four
generically — none of them are trading concepts:

1. **Dimensions** — `$params` carrying `optimize { min/max/step | values }`.
2. **Constraints** — `optimize <expr>` (and `assert <expr>` invariants).
3. **Objective(s)** — `optimize { minimize | maximize }` on any named value.
4. **Metrics** — any named output the host wants to read/rank on.

The engine's whole job is: *expand candidates → for each, bind params + fresh
state → replay the host's input sequence → read named metrics → select.* It never
inspects what the numbers *mean*. The same `cxpr_model_optimize(...)` call
optimizes a trading strategy, a PID controller, an orbital burn, or a curve fit —
only the model and the input data differ.

### 10.2 What a host must still supply (the honest limits)

"Optimize anything" holds **only for problems that fit this contract**:

- **Inputs (the "world") come from the host.** The model must be runnable against
  a host-supplied input sequence — price bars, sensor measurements, boundary
  conditions. The engine replays inputs; it does not invent them.
- **The objective must be computable inside the model** (or from its outputs). If
  the true objective lives outside cxpr (human judgement, a separate simulator),
  cxpr can only optimize a *proxy* the model computes.
- **Counterfactual correctness (spec §10).** If a result depends on
  candidate-specific decisions, the model must *recompute* it per candidate —
  you cannot feed observed historical outcomes as the objective. This is the
  single most common way "optimize anything" goes wrong.
- **Determinism.** Same params + same inputs ⇒ same metrics, or ranking is
  meaningless. Nondeterministic models are out of scope.
- **Search space must be enumerable/finite** in this design (grid/values). Search
  strategies beyond exhaustive grid (random, Bayesian, evolutionary, gradient)
  are a *future* pluggable layer above the same candidate/metric contract — the
  API should be shaped so they can be added without changing model semantics.

### 10.3 Design implications to keep the door open

- Keep the engine free of any domain vocabulary — dimensions/constraints/
  objectives/metrics only.
- Return a **scored candidate set** plus per-candidate **metric vectors**, not a
  single scalar winner (multi-objective, host ranking, Pareto).
- Treat the **search strategy as pluggable** (grid now; the runner takes a
  candidate *source*, so random/Bayesian/evolutionary can be dropped in later).
- Keep inputs, accounting, caching, and selection *policy* in the host, above the
  API — so a non-trading host reuses the engine unchanged.

**Bottom line:** yes — this can and should be fully generic. The boundary is not
the domain but the *contract*: a deterministic model with `$param` dimensions,
host-supplied inputs, and a model-computed objective. Everything inside that
contract optimizes through one API; everything outside it (data, policy, exotic
search) stays in the host or arrives as a later pluggable layer.
