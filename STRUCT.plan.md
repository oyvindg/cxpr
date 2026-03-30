# Native Struct Support Plan

Goal: add first-class typed values — `double`, `bool`, and nested `struct` —
to cxpr so that values carry their natural types end-to-end: through storage,
through the expression evaluator, and through the public API.  A `bool` stays
a `bool`, a `double` stays a `double`, and a `struct` is never silently
coerced.  Type mismatches (e.g. arithmetic on a bool, using a struct as a
scalar) are reported as errors.

## Motivation

- `cxpr_context` currently stores everything as `double`.  Hosts must cast
  `bool` fields to `0.0`/`1.0` and flatten structs to composite string keys
  (`"macd.histogram"`).  This is a leakage of internal representation.
- A standalone, domain-agnostic cxpr should accept the types users naturally
  have.  A robotics host has `vec3 {x,y,z}` with a `bool active`.  A trading
  host has `MACDOutput {line, signal, histogram}` alongside a
  `ZigZagOutput {…, active: bool, is_high: bool}`.
- Moving typed struct ownership into cxpr removes all cxpr-specific boilerplate
  (`FieldMap<T>`, `CXPR_FIELD_MAP_N`) from host indicator/struct headers.

## Working rules

- [ ] Each item is checked off only when it is actually done.
- [ ] No new implementation step starts before the test in the previous step
      has been run and passed.
- [ ] If a step proves too large to test meaningfully, split it before
      continuing.

---

## Phase 0 — Public typed value API

Everything else builds on this.  Define the types first, get them right, then
build storage and evaluation on top.

### 0.1  Types in `cxpr.h`

- [ ] Add `cxpr_field_type` enum:

  ```c
  typedef enum {
      CXPR_FIELD_DOUBLE = 0,  /**< 64-bit IEEE 754 double */
      CXPR_FIELD_BOOL   = 1,  /**< Boolean (true/false) */
      CXPR_FIELD_STRUCT = 2   /**< Nested struct */
  } cxpr_field_type;
  ```

- [ ] Forward-declare `cxpr_struct_value` and define `cxpr_field_value`:

  ```c
  typedef struct cxpr_struct_value cxpr_struct_value;

  /**
   * @brief A typed field value — double, bool, or nested struct.
   *
   * When type == CXPR_FIELD_STRUCT the `s` pointer is borrowed from the
   * owning context; callers must not free it.
   */
  typedef struct {
      cxpr_field_type type;
      union {
          double             d;
          bool               b;
          cxpr_struct_value* s;  /**< Borrowed from context; do not free */
      };
  } cxpr_field_value;
  ```

- [ ] Define `cxpr_struct_value`:

  ```c
  /**
   * @brief A named collection of typed field values.
   *
   * field_names and field_values are parallel arrays of length field_count.
   * All memory is owned by the struct value itself unless it was returned by
   * a get function (in which case it is owned by the context).
   */
  struct cxpr_struct_value {
      const char**       field_names;   /**< Owned array of field name strings */
      cxpr_field_value*  field_values;  /**< Owned array of typed field values */
      size_t             field_count;
  };
  ```

### 0.2  Convenience constructors and lifecycle

- [ ] Declare and implement inline convenience constructors in `cxpr.h`:

  ```c
  static inline cxpr_field_value cxpr_fv_double(double d)
      { return (cxpr_field_value){ .type = CXPR_FIELD_DOUBLE, .d = d }; }

  static inline cxpr_field_value cxpr_fv_bool(bool b)
      { return (cxpr_field_value){ .type = CXPR_FIELD_BOOL,   .b = b }; }

  static inline cxpr_field_value cxpr_fv_struct(cxpr_struct_value* s)
      { return (cxpr_field_value){ .type = CXPR_FIELD_STRUCT, .s = s }; }
  ```

- [ ] Declare `cxpr_struct_value_new` / `cxpr_struct_value_free` in `cxpr.h`:

  ```c
  /**
   * @brief Allocate and deep-copy a struct value.
   *
   * Copies field_names and field_values (including recursively nested structs).
   * Returns NULL on allocation failure.
   */
  cxpr_struct_value* cxpr_struct_value_new(const char* const*        field_names,
                                            const cxpr_field_value*   field_values,
                                            size_t                    field_count);

  /** @brief Free a struct value and all owned nested data. */
  void cxpr_struct_value_free(cxpr_struct_value* s);
  ```

- [ ] Implement `cxpr_struct_value_new` and `cxpr_struct_value_free` in
      `context.c`.  Recurse into nested `CXPR_FIELD_STRUCT` entries on both
      copy and free.

### 0.3  New error codes

- [ ] Add to `cxpr_error_code` enum in `cxpr.h`:

  ```c
  CXPR_ERR_TYPE_MISMATCH  /**< Value type not accepted by the operator or context */
  ```

- [ ] Handle it in `cxpr_error_string`.

### 0.4  Bool literals and `CXPR_NODE_BOOL`

Bool fields in context are typed in Phase 1, but expressions must also accept
`true` and `false` as literals that carry the bool type.

- [ ] Add `CXPR_TOK_TRUE` and `CXPR_TOK_FALSE` to the token enum in `internal.h`.
- [ ] Recognise the keywords `true` and `false` in `cxpr_lexer_next` (`parser.c`).
- [ ] Add `CXPR_NODE_BOOL` to `cxpr_node_type` enum in `cxpr.h`.
- [ ] Add the corresponding union branch to `struct cxpr_ast` in `internal.h`:
  ```c
  struct { bool value; } boolean;  /* CXPR_NODE_BOOL */
  ```
- [ ] Add `cxpr_ast_new_bool(bool value)` in `ast.c`; declare in `internal.h`.
- [ ] In the primary-expression parser, emit `CXPR_NODE_BOOL` for `true`/`false`.
- [ ] Add to `tests/field_value.test.c`:
  - [ ] `"true"` evaluates to `cxpr_field_value{BOOL, true}`.
  - [ ] `"false"` evaluates to `cxpr_field_value{BOOL, false}`.
  - [ ] `"true == true"` evaluates to bool `true`.
  - [ ] `"true == 1"` gives `CXPR_ERR_TYPE_MISMATCH` (bool vs double).
  - [ ] `"sensor.active == true"` resolves correctly when `active` is a bool
        field (covered more fully in struct_ctx tests in Phase 1).

### 0.5  Typed evaluator API and operator type rules

This section changes the evaluation contract.  All subsequent phases build on it.

**Public API (`cxpr.h`)**

- [ ] Change `cxpr_ast_eval` to return `cxpr_field_value` instead of `double`.
- [ ] Change `cxpr_ir_eval` to return `cxpr_field_value` instead of `double`.
- [ ] Add convenience wrappers:
  ```c
  /** Returns the double value, or NAN + CXPR_ERR_TYPE_MISMATCH if not double. */
  double cxpr_ast_eval_double(const cxpr_ast*, const cxpr_context*,
                               const cxpr_registry*, cxpr_error*);
  double cxpr_ir_eval_double(const cxpr_program*, const cxpr_context*,
                              const cxpr_registry*, cxpr_error*);
  ```
- [ ] Update `cxpr_ast_eval_bool`: call `cxpr_ast_eval`; if result type is
      `CXPR_FIELD_BOOL` return its value; otherwise set `CXPR_ERR_TYPE_MISMATCH`
      and return `false`.
- [ ] Update `cxpr_ir_eval_bool` the same way: call `cxpr_ir_eval`; check for
      `CXPR_FIELD_BOOL`; otherwise `CXPR_ERR_TYPE_MISMATCH` + `false`.

**Operator type rules (enforced in `eval.c` and `ir.c`)**

| Operator | Operand types required | Result type |
|----------|----------------------|-------------|
| `+` `-` `*` `/` `%` `^` | both `double` | `double` |
| unary `-` | `double` | `double` |
| `<` `<=` `>` `>=` | both `double` | `bool` |
| `==` `!=` | both same type (`double` or `bool`) | `bool` |
| `&&` `\|\|` | both `bool` | `bool` |
| unary `!` | `bool` | `bool` |
| ternary `? :` | condition `bool`; branches same type | type of branches |

Any violation sets `CXPR_ERR_TYPE_MISMATCH` and returns
`cxpr_fv_double(NAN)`.

**IR stack (`ir.c`, `internal.h`)**

- [ ] Change `double stack[64]` to `cxpr_field_value stack[64]` in the IR
      executor.
- [ ] Update all opcode handlers that push or pop from the stack.
- [ ] Add `CXPR_OP_PUSH_BOOL` opcode for `CXPR_NODE_BOOL`; emit it in the IR
      compiler alongside the existing `PUSH_CONST` for numbers.

**Evaluator (`eval.c`)**

- [ ] Add `CXPR_NODE_BOOL` case: return `cxpr_fv_bool(node->data.boolean.value)`.
- [ ] Update `CXPR_NODE_NUMBER` case: return `cxpr_fv_double(node->data.number.value)`.
- [ ] Update all binary/unary operator cases to check operand types per the
      table above before computing results.

**Migrate existing tests**

Changing the return type of `cxpr_ast_eval` / `cxpr_ir_eval` is a hard
breaking change for existing test files.  Do this as part of the same commit
that changes the signatures — do not leave the suite broken between steps.

- [ ] In `tests/eval.test.c`: replace all `cxpr_ast_eval(…)` call sites with
      `cxpr_ast_eval_double(…)`.  Update the `eval_ok` helper accordingly.
- [ ] In `tests/program.test.c`: replace `cxpr_ir_eval(…)` with
      `cxpr_ir_eval_double(…)`.
- [ ] In `tests/ir.test.c`: same.
- [ ] In `tests/formula_ir.test.c`: same where applicable.
- [ ] In `tests/precedence.test.c`, `tests/math.test.c`,
      `tests/simulation.test.c`: same.
- [ ] Verify zero failures across the full suite after migration.

### 0.6  Tests

- [ ] Add `tests/field_value.test.c`:
  - [ ] `cxpr_fv_double`, `cxpr_fv_bool`, `cxpr_fv_struct` set correct type and
        value.
  - [ ] `cxpr_struct_value_new` deep-copies field names and values.
  - [ ] Nested struct is deep-copied (mutating the copy does not affect the
        original).
  - [ ] `cxpr_struct_value_free` does not leak (verified by running under
        Valgrind / ASan).
  - [ ] `"1.0 + 2.0"` → `cxpr_field_value{DOUBLE, 3.0}`.
  - [ ] `"1.0 < 2.0"` → `cxpr_field_value{BOOL, true}`.
  - [ ] `"true && false"` → `cxpr_field_value{BOOL, false}`.
  - [ ] `"true + 1.0"` → `CXPR_ERR_TYPE_MISMATCH`.
  - [ ] `"1.0 && 0.0"` → `CXPR_ERR_TYPE_MISMATCH` (double where bool expected).
  - [ ] `"true == 1.0"` → `CXPR_ERR_TYPE_MISMATCH` (mixed types in `==`).
  - [ ] `cxpr_ast_eval_double` on a bool expression → NAN + `CXPR_ERR_TYPE_MISMATCH`.
  - [ ] `cxpr_ast_eval_bool` on a double expression → false + `CXPR_ERR_TYPE_MISMATCH`.
  - [ ] `cxpr_ir_eval` produces identical typed results to `cxpr_ast_eval` for
        all cases above.
- [ ] Run full test suite — zero failures before Phase 1.

---

## Phase 1 — Native struct storage in `cxpr_context`

### 1.1  Internal data structure (`internal.h`)

- [ ] Add `cxpr_struct_map_entry`:

  ```c
  typedef struct {
      char*              name;    /**< Owned struct name */
      cxpr_struct_value* value;   /**< Owned struct value */
  } cxpr_struct_map_entry;
  ```

- [ ] Add `cxpr_struct_map` (open-addressing, keyed on struct name):

  ```c
  typedef struct {
      cxpr_struct_map_entry* entries;
      size_t capacity;
      size_t count;
  } cxpr_struct_map;
  ```

- [ ] Add `cxpr_struct_map structs;` field to `struct cxpr_context`.

- [ ] Implement in `context.c`: `cxpr_struct_map_init`, `cxpr_struct_map_destroy`,
      `cxpr_struct_map_clear`, `cxpr_struct_map_clone` (deep-copy via
      `cxpr_struct_value_new`).

### 1.2  Public API additions (`cxpr.h`)

- [ ] Declare `cxpr_context_set_struct`:

  ```c
  /**
   * @brief Store a named struct in the context (deep-copied).
   *
   * Replaces any prior struct stored under the same name.
   * The caller retains ownership of `value` and may free it afterwards.
   */
  void cxpr_context_set_struct(cxpr_context*            ctx,
                                const char*              name,
                                const cxpr_struct_value* value);
  ```

- [ ] Declare `cxpr_context_get_struct`:

  ```c
  /**
   * @brief Retrieve a named struct from the context (borrowed).
   *
   * Returns a pointer owned by the context.  Valid until the next
   * cxpr_context_set_struct / cxpr_context_clear call on the same name.
   */
  const cxpr_struct_value* cxpr_context_get_struct(const cxpr_context* ctx,
                                                    const char*         name);
  ```

- [ ] Declare `cxpr_context_get_field`:

  ```c
  /**
   * @brief Retrieve a typed field value from a named struct (borrowed).
   *
   * When the field value is CXPR_FIELD_STRUCT the returned `s` pointer is
   * owned by the context.  Walks the parent chain if not found locally.
   *
   * @param ctx   Context
   * @param name  Struct name (e.g. "macd")
   * @param field Field name (e.g. "histogram")
   * @param found Set to true if both struct and field were found (may be NULL)
   */
  cxpr_field_value cxpr_context_get_field(const cxpr_context* ctx,
                                           const char*         name,
                                           const char*         field,
                                           bool*               found);
  ```

### 1.3  Implement in `context.c`

- [ ] `cxpr_context_set_struct` — upsert via `cxpr_struct_value_new` deep-copy.
- [ ] `cxpr_context_get_struct` — lookup struct by name, walk parent chain.
- [ ] `cxpr_context_get_field` — get struct, linear-scan field_names for match,
      return corresponding `field_values` entry; walk parent chain if struct not
      found locally.
- [ ] `cxpr_context_free` — call `cxpr_struct_map_destroy`.
- [ ] `cxpr_context_clone` — deep-copy `structs` map.
- [ ] `cxpr_context_clear` — call `cxpr_struct_map_clear`.
- [ ] `cxpr_context_overlay_new` — move declaration from `internal.h` to
      `cxpr.h` (make public); parent pointer already covers struct chain via
      `cxpr_context_get_struct` parent walk so no implementation change needed.

### 1.4  Update evaluator (`eval.c`) — `CXPR_NODE_FIELD_ACCESS`

- [ ] Replace current flat-key logic with `cxpr_context_get_field`.
- [ ] Return the `cxpr_field_value` directly — no type conversion:
  - `CXPR_FIELD_DOUBLE` → return as-is.
  - `CXPR_FIELD_BOOL`   → return as-is.
  - `CXPR_FIELD_STRUCT` → set `CXPR_ERR_TYPE_MISMATCH`, return `cxpr_fv_double(NAN)`.
    *(Chained access `a.b.c` is handled in Phase 2.)*
- [ ] Add flat-key fallback: if `cxpr_context_get_field` returns not-found,
      fall back to `cxpr_context_get(ctx, "name.field", &found)` and wrap result
      as `cxpr_fv_double(value)` for backward compatibility with callers still
      using `cxpr_context_set_fields`.
      Add a `/* deprecated: flat-key fallback, removed in Phase 4 */` comment
      at the fallback site so it is easy to locate and delete.

### 1.5  Update struct_fn expansion (`eval.c`)

- [ ] In the `entry->struct_fields` expansion loop: replace `snprintf` +
      `cxpr_context_get` with `cxpr_context_get_field`.
- [ ] Extract the double value: `CXPR_FIELD_DOUBLE` → use `d`; any other type
      → set `CXPR_ERR_TYPE_MISMATCH` and return `cxpr_fv_double(NAN)`.
      (Struct-fn arguments are inherently scalar — a bool or struct field used
      as a struct-fn argument is a type error.)

### 1.6  Update IR (`ir.c`) — `CXPR_OP_LOAD_FIELD`

- [ ] Use `cxpr_context_get_field` and push the `cxpr_field_value` directly
      onto the typed stack (no conversion), with the same flat-key fallback
      as the AST evaluator.

### 1.7  Tests

- [ ] Add `tests/struct_ctx.test.c`:
  - [ ] `set_struct` then `get_field` returns correct `cxpr_field_value` for
        double, bool, and nested struct fields.
  - [ ] Bool field returned as `CXPR_FIELD_BOOL` in expression
        `"sensor.active && x > 0.0"` (both operands are bool; result is bool).
  - [ ] Nested struct field returns `CXPR_FIELD_STRUCT`; accessing it as a
        scalar (`"outer.inner > 0"`) gives `CXPR_ERR_TYPE_MISMATCH`.
  - [ ] Updating an existing struct replaces it entirely.
  - [ ] `context_clear` removes all structs.
  - [ ] `context_clone` deep-copies structs; mutating clone does not affect
        original.
  - [ ] Overlay context: field found in parent when not in child.
  - [ ] Flat-key fallback: expression using `cxpr_context_set_fields`
        (old API) still resolves correctly.
- [ ] All existing `struct_fn.test.c` tests still pass.
- [ ] Run full test suite — zero failures before Phase 2.

### 1.8  Formula engine policy

`cxpr_formula_engine` internally evaluates expressions and stores results.
`cxpr_formula_get` currently returns `double`.  Decision: **formula results
are always scalar (`double`)**.  A formula expression that evaluates to `bool`
or `struct` is a type error at evaluation time.

- [ ] In `cxpr_formula_eval_all`: use `cxpr_ast_eval_double` (or
      `cxpr_ir_eval_double`) internally; propagate `CXPR_ERR_TYPE_MISMATCH` if
      a formula expression returns a non-double value.
- [ ] `cxpr_formula_get` signature is unchanged — it continues to return
      `double`.
- [ ] Add to `tests/formula.test.c`:
  - [ ] A formula whose expression evaluates to bool (e.g. `"x > 0"`) triggers
        `CXPR_ERR_TYPE_MISMATCH` during `cxpr_formula_eval_all`.

---

## Phase 2 — Chained field access `a.b.c`

Currently the parser handles only one level for plain identifiers (`a.b`).
This phase extends it to arbitrary depth for nested struct traversal.

### 2.1  New AST node type

- [ ] Add `CXPR_NODE_CHAIN_ACCESS` to `cxpr_node_type` enum in `cxpr.h`:

  ```c
  CXPR_NODE_CHAIN_ACCESS  /**< Chained field access (e.g. "a.b.c") */
  ```

- [ ] Add the corresponding union branch to `struct cxpr_ast` in `internal.h`:

  ```c
  /* CXPR_NODE_CHAIN_ACCESS */
  struct {
      char** path;    /**< Owned array of path segments, e.g. {"a","b","c"} */
      size_t depth;   /**< Number of segments (>= 2) */
  } chain_access;
  ```

- [ ] Add `cxpr_ast_new_chain_access(const char* const* path, size_t depth)` to
      `ast.c` and declare it in `internal.h`.
- [ ] Free `chain_access.path` entries and array in `cxpr_ast_free`.

### 2.2  Public AST inspection API (`cxpr.h`)

- [ ] Declare accessor for codegen / host introspection:

  ```c
  /** @brief Get path depth of a CHAIN_ACCESS node (number of segments). */
  size_t cxpr_ast_chain_depth(const cxpr_ast* ast);

  /** @brief Get one segment of a CHAIN_ACCESS path (0-based). */
  const char* cxpr_ast_chain_segment(const cxpr_ast* ast, size_t index);
  ```

### 2.3  Parser update (`parser.c`)

- [ ] After parsing a plain identifier field-access `a.b`, loop while the next
      token is `CXPR_TOK_DOT`:
  - Consume `.` and the following identifier.
  - Accumulate segments into a path array.
  - If depth ends at 2, emit `CXPR_NODE_FIELD_ACCESS` as before (no
    regression).
  - If depth > 2, emit `CXPR_NODE_CHAIN_ACCESS`.

### 2.4  Update `cxpr_ast_references` (`ast.c`)

- [ ] Handle `CXPR_NODE_CHAIN_ACCESS`: emit the full dotted path joined by `.`
      as a single reference string (consistent with existing codegen consumers).

### 2.5  Evaluator (`eval.c`)

- [ ] Add `CXPR_NODE_CHAIN_ACCESS` case: walk the struct chain segment by
      segment using `cxpr_context_get_struct` / `cxpr_context_get_field`:
  - Look up `path[0]` as a struct in context.
  - For segments `1 … depth-2`: look up each as a field expecting
    `CXPR_FIELD_STRUCT`; error with `CXPR_ERR_TYPE_MISMATCH` if not.
  - For the final segment: look up as a field and return the
    `cxpr_field_value` directly (same rules as Phase 1 field access — no
    type conversion).

### 2.6  IR (`ir.c`)

- [ ] Add `CXPR_OP_LOAD_CHAIN` to `cxpr_opcode` in `internal.h`.  The
      instruction carries a `const char**` path pointer and a `size_t depth`
      stored in the `cxpr_ir_instr` (reuse the existing `name` field for a
      joined `"a.b.c"` string and reconstruct segments by splitting on `.` at
      runtime — no extra fields needed in the instr struct).
- [ ] In the IR compiler (`ir.c`), emit `CXPR_OP_LOAD_CHAIN` for
      `CXPR_NODE_CHAIN_ACCESS` nodes by joining the path segments with `.` into
      an owned string stored in `instr.name`.
- [ ] In the IR executor, handle `CXPR_OP_LOAD_CHAIN`: split `instr.name` on
      `.`, walk the struct chain via `cxpr_context_get_struct` /
      `cxpr_context_get_field`, push the final `cxpr_field_value` onto the
      typed stack without conversion (same rules as the AST evaluator in 2.5).

### 2.7  Tests

- [ ] Add `tests/chain_access.test.c`:
  - [ ] `"outer.inner.value"` resolves correctly when `inner` is a nested
        struct field and `value` is a double.
  - [ ] `"outer.inner.flag"` returns `cxpr_field_value{BOOL, …}` unchanged.
  - [ ] Three-level nesting `"a.b.c.d"` works.
  - [ ] Non-struct intermediate field gives `CXPR_ERR_TYPE_MISMATCH`.
  - [ ] Unknown intermediate struct gives `CXPR_ERR_UNKNOWN_IDENTIFIER`.
  - [ ] `cxpr_ast_references` returns the full path string for chain nodes.
  - [ ] Two-segment access `a.b` still emits `CXPR_NODE_FIELD_ACCESS`
        (regression test).
- [ ] Run full test suite — zero failures before Phase 3.

---

## Phase 3 — Struct-producing functions

A **struct producer** is a registered function whose output is a
`cxpr_struct_value` rather than a scalar.  The producer is called with
evaluated scalar arguments; its output is cached in the context under `name`
so that subsequent field accesses within the same evaluation are free.

Two call forms are supported:

- Zero-argument: `macd.line` — no call-site arguments; producer is triggered
  on the first field access of an unknown identifier that matches a registered
  producer name.
- Argument-bearing: `macd(14, 3).line` — scalar arguments are evaluated and
  passed to the producer; the result is cached under `name` for the duration
  of the evaluation.  If the same name is called with different arguments
  within one evaluation the second call shadows the first in the context.

### 3.1  Producer function pointer type (`cxpr.h`)

- [ ] Declare:

  ```c
  /**
   * @brief Function pointer for struct-producing functions.
   *
   * Must populate `out` with exactly `field_count` entries (parallel to the
   * registered field names).  The values in `out` are copied by cxpr after
   * the call.
   *
   * For zero-argument producers, args is NULL and argc is 0.
   *
   * @param args        Evaluated scalar arguments (NULL for zero-arg producers)
   * @param argc        Number of arguments (0 for zero-arg producers)
   * @param out         Output array — write field_count cxpr_field_value here
   * @param field_count Number of output fields (matches registration)
   * @param userdata    User-provided context pointer
   */
  typedef void (*cxpr_producer_fn)(const double*      args,   size_t argc,
                                    cxpr_field_value*  out,    size_t field_count,
                                    void*              userdata);
  ```

### 3.2  Parser/AST support for `name(args).field` (`parser.c`, `internal.h`, `ast.c`)

- [ ] Add `CXPR_NODE_PRODUCER_ACCESS` to `cxpr_node_type` in `cxpr.h`:

  ```c
  CXPR_NODE_PRODUCER_ACCESS  /**< Struct-producer call with field access: name(args).field */
  ```

- [ ] Add the corresponding union branch to `struct cxpr_ast` in `internal.h`:

  ```c
  /* CXPR_NODE_PRODUCER_ACCESS */
  struct {
      char*       name;    /**< Producer name */
      cxpr_ast**  args;    /**< Owned evaluated-scalar argument nodes */
      size_t      argc;
      char*       field;   /**< Field name after the dot */
  } producer_access;
  ```

- [ ] Add `cxpr_ast_new_producer_access` in `ast.c`; free args array and
      strings in `cxpr_ast_free`.
- [ ] In the parser: after parsing a primary `IDENTIFIER`, if the next token
      is `(` and the following tokens form an argument list followed by `)` and
      then `.` and another `IDENTIFIER`, emit `CXPR_NODE_PRODUCER_ACCESS`.
  - The lookahead must not conflict with plain function calls; the `.field`
    suffix is the discriminator — ordinary function calls never end with `.field`.
- [ ] In `cxpr_ast_references`: emit `"name.field"` for producer access nodes
      (consistent with FIELD_ACCESS references; the args are not part of the
      reference string).

### 3.3  Registration API (`cxpr.h`)

- [ ] Declare `cxpr_registry_add_struct_producer`:

  ```c
  /**
   * @brief Register a struct-producing function.
   *
   * Zero-argument form: when `name.field` is accessed and no struct named
   * `name` is present in the context, the producer is called with no
   * arguments, its output is stored under `name`, and the field is returned.
   *
   * Argument-bearing form: `name(args).field` evaluates the scalar arguments,
   * calls the producer, stores the output under `name` for the duration of the
   * evaluation, and returns the field.
   *
   * In both forms, if the context already contains a struct named `name`
   * (set explicitly by the host before evaluation) the producer is NOT
   * called — host-set values take priority.
   *
   * @param reg          Registry
   * @param name         Producer name (e.g. "macd")
   * @param func         Producer function pointer
   * @param min_args     Minimum number of scalar arguments (0 for zero-arg)
   * @param max_args     Maximum number of scalar arguments (0 for zero-arg)
   * @param field_names  Output field names
   * @param field_count  Number of output fields
   * @param userdata     User data passed to func (can be NULL)
   * @param free_ud      Optional cleanup callback (can be NULL)
   */
  void cxpr_registry_add_struct_producer(cxpr_registry*        reg,
                                          const char*           name,
                                          cxpr_producer_fn      func,
                                          size_t                min_args,
                                          size_t                max_args,
                                          const char* const*    field_names,
                                          size_t                field_count,
                                          void*                 userdata,
                                          cxpr_userdata_free_fn free_ud);
  ```

### 3.4  Internal storage (`internal.h`, `registry.c`)

- [ ] Add producer fields to `cxpr_func_entry`:

  ```c
  cxpr_producer_fn   producer_func;         /* NULL when not a producer */
  char**             producer_field_names;  /* owned */
  size_t             producer_field_count;
  size_t             producer_min_args;
  size_t             producer_max_args;
  ```

- [ ] Implement `cxpr_registry_add_struct_producer` in `registry.c`.
- [ ] Free producer fields in `cxpr_func_entry` cleanup path.

### 3.5  Evaluator integration (`eval.c`)

- [ ] Add `CXPR_NODE_PRODUCER_ACCESS` case:
  1. Evaluate each argument node to a `double`; collect into a stack array.
  2. Validate argc against `producer_min_args`/`producer_max_args`; error with
     `CXPR_ERR_WRONG_ARITY` if out of range.
  3. If context does not already have a struct for `name`: call the producer
     with the evaluated args, build a `cxpr_struct_value` via
     `cxpr_struct_value_new`, store via `cxpr_context_set_struct`.
  4. Look up the field via `cxpr_context_get_field`; return `cxpr_field_value`
     directly (no type conversion).

- [ ] In `CXPR_NODE_FIELD_ACCESS` (and chain access from Phase 2): after
      `cxpr_context_get_field` returns not-found and before the flat-key
      fallback, handle the zero-argument producer form:
  1. Check registry for a producer with `producer_min_args == 0`.
  2. If found and context does not already have a struct for that name: call
     the producer with NULL/0, build `cxpr_struct_value`, store in context.
  3. Retry `cxpr_context_get_field`.

### 3.6  IR integration (`ir.c`)

- [ ] Add `CXPR_OP_CALL_PRODUCER` opcode to `cxpr_opcode` in `internal.h`.
      The instruction carries: `func` pointer to the `cxpr_func_entry`, `name`
      (producer name for context storage), `index` (argc on stack).
- [ ] In the IR compiler: emit argument nodes then `CXPR_OP_CALL_PRODUCER` for
      `CXPR_NODE_PRODUCER_ACCESS`; follow with `CXPR_OP_LOAD_FIELD` for the
      field name.
- [ ] In the IR executor, handle `CXPR_OP_CALL_PRODUCER`: pop `argc` doubles,
      check context for existing struct, call producer if absent, store result
      in context.  Do not push anything onto the stack (the following
      `LOAD_FIELD` reads from context).
- [ ] Handle zero-arg producers in `CXPR_OP_LOAD_FIELD` / `CXPR_OP_LOAD_CHAIN`
      the same way as the AST evaluator in 3.5 (check registry, call if absent,
      retry field lookup).

### 3.7  Tests

- [ ] Add `tests/struct_producer.test.c`:
  - [ ] Zero-arg producer: `"macd.line > 0 and macd.histogram > 0"` calls
        producer once.
  - [ ] Argument-bearing producer: `"macd(14, 3).line > 0"` calls producer
        with args `{14.0, 3.0}`.
  - [ ] Argument-bearing producer called once when two fields referenced:
        `"macd(14, 3).line + macd(14, 3).histogram"`.
  - [ ] Bool output field from producer returned as `CXPR_FIELD_BOOL` (no conversion).
  - [ ] Nested struct output field from producer accessible via chain access.
  - [ ] Explicit `cxpr_context_set_struct` takes priority — producer not called.
  - [ ] `context_clear` removes cached output; next evaluation calls producer
        again.
  - [ ] Wrong arity gives `CXPR_ERR_WRONG_ARITY`.
  - [ ] Unknown field on a known producer gives `CXPR_ERR_UNKNOWN_IDENTIFIER`.
- [ ] `cxpr_ir_eval` produces identical results to `cxpr_ast_eval` for all
      producer test cases above (parity test).
- [ ] Run full test suite — zero failures before Phase 4.

---

## Phase 4 — Dynasty integration (separate repo)

Acceptance criteria proving that the cxpr changes achieved the architectural
goal of making host indicator types cxpr-agnostic.

- [ ] Remove all `CXPR_FIELD_MAP_N` macro calls from indicator headers
      (`MACD.hpp`, `BollingerBands.hpp`, `ADX.hpp`, `Supertrend.hpp`,
      `ZigZag.hpp`, and all others).
- [ ] Remove all `#include <dynasty/expression/Context.hpp>` from indicator
      headers.
- [ ] Remove `FieldMap<T>`, `CXPR_FIELD_MAP_N` macros, and the
      `setFields(prefix, const T&)` template overload from `Context.hpp`.
- [ ] Add `Context::setStruct(name, field_names, field_values, count)` thin
      C++ wrapper over `cxpr_context_set_struct`.
- [ ] Replace all `ctx.setFields("macd", out)` and equivalent call sites in
      `IndicatorEvaluator` with `ctx.setStruct(...)`.
- [ ] Remove the flat-key fallback from `eval.c` and `ir.c` (the sites marked
      `/* deprecated: flat-key fallback, removed in Phase 4 */`).
- [ ] Remove `cxpr_context_set_fields` and `cxpr_context_set_field` from
      `cxpr.h` and `context.c`.
- [ ] Verify no indicator header transitively includes any cxpr or expression
      header (CI check via `include-what-you-use` or equivalent grep).
- [ ] All expression unit tests pass.
- [ ] All indicator unit tests pass.
