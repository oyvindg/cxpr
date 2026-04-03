# Typed Formula Engine

## Goal

Make formula results type-preserving so the caller does not need to know in advance
whether a formula yields `double`, `bool`, or `struct`.

The engine should keep the real runtime type all the way through evaluation,
dependency resolution, and retrieval.

## Previous Problem

The evaluator already produced `cxpr_field_value`, but the formula engine used to
throw that type information away:

- `cxpr_formula_eval_all(...)` evaluated each formula to `cxpr_field_value`
- `CXPR_FIELD_BOOL` was converted to `1.0` / `0.0`
- `CXPR_FIELD_DOUBLE` was stored as `double`
- the result was written back into context through `cxpr_context_set(...)`
- `cxpr_formula_get(...)` returned `double`

This means:

- callers must treat formula results as numeric even when they are semantically boolean
- dependent formulas only see prior formula results as `double`
- `struct` results cannot flow through the formula engine
- automatic typing is not possible at the API boundary

## Current Status

This has now been partially implemented:

- `cxpr_formula_entry.result` is `cxpr_field_value`
- `cxpr_formula_get(...)` returns `cxpr_field_value`
- `cxpr_formula_get_double(...)` and `cxpr_formula_get_bool(...)` exist as convenience wrappers

The remaining limitation is that dependent formulas still flow through
`cxpr_context_set(...)`, so inter-formula dependencies are still exposed to the
scalar context layer.

## Desired End State

The user should be able to write formulas naturally:

```text
trend    = close > ema_fast and ema_fast > ema_slow
pullback = close < ema_fast * 0.995
entry    = trend and pullback and rsi > 50
```

without caring whether `trend` and `pullback` are stored as bools or doubles.

Likewise, host code should be able to retrieve the real value type:

```c
cxpr_field_value entry = cxpr_formula_get_value(engine, "entry", &found);
```

## Minimal Correct Design

### 1. Store typed results in the formula engine

Change `cxpr_formula_entry` from:

```c
double result;
bool evaluated;
```

to:

```c
cxpr_field_value result;
bool evaluated;
```

This is the central change. Without this, every other typed API is fake.

### 2. Stop flattening results during evaluation

In `cxpr_formula_eval_all(...)`:

- keep the `cxpr_field_value` returned by AST/IR evaluation
- store it directly in the formula entry
- do not coerce bool to `1.0` / `0.0`
- only reject types the formula engine truly does not support

Recommended supported result types:

- `CXPR_FIELD_DOUBLE`
- `CXPR_FIELD_BOOL`
- `CXPR_FIELD_STRUCT`

If structs are not wanted in the first iteration, explicitly reject them and keep
the engine typed for `double` and `bool` only.

### 3. Add a typed retrieval API

Add:

```c
cxpr_field_value cxpr_formula_get_value(const cxpr_formula_engine* engine,
                                        const char* name,
                                        bool* found);
```

This should become the primary retrieval API.

### 4. Keep `cxpr_formula_get(...)` as a compatibility wrapper

Existing code likely depends on:

```c
double cxpr_formula_get(...);
```

Keep it, but redefine its behavior as:

- return `double` directly for `CXPR_FIELD_DOUBLE`
- return `1.0` / `0.0` for `CXPR_FIELD_BOOL`
- return `0.0` and `found=false`, or signal error, for unsupported types

This preserves compatibility while making typed access available.

## The Real Constraint: Formula Dependencies

The biggest issue is not `cxpr_formula_get(...)`. It is how one formula reads
another formula.

Today, dependent formulas work because evaluated results are written into the
context with `cxpr_context_set(...)`, which is scalar-only.

That forces this lossy pipeline:

```text
formula result -> cxpr_field_value -> double -> context variable -> dependent formula
```

To make typing automatic, dependent formulas must also see typed results.

## Two Possible Approaches

### Option A: Typed formula overlay inside evaluation

Introduce a formula-local typed lookup layer used only while evaluating formulas.

Behavior:

- after formula `A` evaluates, store its `cxpr_field_value` in engine state
- when formula `B` references `A`, identifier lookup first checks formula results
- if found there, return the typed value directly
- otherwise fall back to normal context lookup

Pros:

- smallest architectural change
- avoids redesigning the public context API immediately
- preserves typed dependency flow

Cons:

- formula identifiers behave slightly differently inside formula engine than in raw context
- requires evaluator/IR lookup hooks for formula-local values

### Option B: Add typed values to context

Extend `cxpr_context` so identifiers can store and retrieve `cxpr_field_value`,
not just `double`.

Pros:

- cleaner long-term runtime model
- typed values become a first-class concept everywhere
- formula engine becomes simpler because it can reuse context storage directly

Cons:

- much larger refactor
- affects context internals, APIs, caching, slot API expectations, and tests
- risks more breakage

## Recommendation

Implement in two stages.

### Stage 1: Typed formula engine with local overlay

Do this first:

- store `cxpr_field_value` in `cxpr_formula_entry`
- add `cxpr_formula_get_value(...)`
- keep `cxpr_formula_get(...)` as wrapper
- add formula-local typed lookup for dependencies

This gives automatic typing where it matters most, with limited API churn.

### Stage 2: Typed context if still needed

Only do this if there is a strong use case for general typed identifiers outside
the formula engine.

## Suggested API

```c
cxpr_field_value cxpr_formula_get_value(const cxpr_formula_engine* engine,
                                        const char* name,
                                        bool* found);

double cxpr_formula_get(const cxpr_formula_engine* engine,
                        const char* name,
                        bool* found);
```

Optional convenience helpers:

```c
double cxpr_formula_get_double(const cxpr_formula_engine* engine,
                               const char* name,
                               bool* found);

bool cxpr_formula_get_bool(const cxpr_formula_engine* engine,
                           const char* name,
                           bool* found,
                           bool* ok);
```

These are optional. `cxpr_formula_get_value(...)` is the important one.

## Internal Changes

At minimum, these areas need updates:

- `include/cxpr/cxpr.h`
- `src/internal.h`
- `src/formula.c`
- evaluator identifier lookup path
- IR identifier lookup path
- tests for formula engine and README examples

## Semantics Questions

These should be decided before implementation:

1. Should formula results be allowed to be `CXPR_FIELD_STRUCT`?
2. If yes, should dependent formulas be able to do `entry.signal` where `entry`
   is a formula result?
3. What should legacy `cxpr_formula_get(...)` do for non-scalar values?
4. Should formula results shadow context identifiers with the same name?

Recommended answers:

1. Yes eventually, but not required for stage 1.
2. Yes, if struct formula results are supported.
3. Preserve old scalar behavior; reject or mark not-found for non-scalar results.
4. Yes, during formula-engine evaluation, formula results should shadow context
   values of the same name.

## Migration Strategy

1. Add typed storage and `cxpr_formula_get_value(...)`
2. Keep old `cxpr_formula_get(...)`
3. Add tests for bool-preserving dependencies
4. Add tests for typed retrieval
5. Update README to show typed retrieval where useful

## Summary

Automatic typing is possible, but only if the formula engine stops collapsing
results to `double`.

The smallest correct change is:

- typed formula result storage
- typed retrieval API
- typed dependency lookup inside formula evaluation

Anything less will still leak `double` semantics into the user-facing model.
