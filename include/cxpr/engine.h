/**
 * @file engine.h
 * @brief Stateful rule-engine layer built on top of the cxpr evaluator.
 *
 * The engine turns cxpr from "an expression evaluator you embed" into "a rule
 * engine you drive". It is an **additive, opt-in layer**: the stateless
 * @ref cxpr_evaluator, @ref cxpr_context, and registry APIs are unchanged, and
 * a host that does not include this header pays nothing for it.
 *
 * The engine owns the boilerplate a host would otherwise hand-roll on every
 * step: lazy input hydration, lookback history, per-step evaluation, and
 * transition detection. A host describes everything once in a
 * @ref cxpr_engine_config, then loops: feed data, get the events that fired.
 *
 * ## Model
 *
 * Two objects, mirroring the kernel's immutable/mutable split:
 *
 * - @ref cxpr_engine_program — immutable: the registry, the expression set, the
 *   source backings, the watch declarations, and the buffer layout derived from
 *   compile-time lookback analysis. Build it once; safe to share across threads.
 * - @ref cxpr_engine_session — mutable per-run state: one isolated execution of
 *   the program, with its own context, lookback history, previous-value
 *   snapshots, tick cursor, and `$param` values. Create **one per worker
 *   thread**; the program behind it stays shared and read-only.
 *
 * The expression set comes from the config's `expressions` and/or from
 * expression-defined functions already present in the registry (see
 * @ref cxpr_registry_define_fn). Watches and result reads resolve names against
 * both, so a host whose rules already live in the registry need not restate
 * them here.
 *
 * A *tick* (@ref cxpr_engine_tick) is one evaluation epoch: advance the cursor,
 * lazily pull the inputs the compiled rules actually reference, evaluate the
 * rule set, then collect the watched expressions whose state changed. A tick is
 * deliberately abstract — not a "bar" and not a wall-clock instant; the host
 * decides what one tick means.
 *
 * ## Data flow boundary
 *
 * - **Callbacks pull data in.** Source backings (@ref cxpr_engine_pull_fn,
 *   @ref cxpr_engine_view_fn) are the only host code the engine invokes
 *   mid-tick, and they only *provide values* — they never take action.
 * - **Events carry actions out.** Everything the host must *do* is returned
 *   from @ref cxpr_engine_tick as a borrowed array of plain events, after the
 *   tick has finished. All effects happen in host code, post-tick.
 *
 * The engine is domain-neutral: it knows nothing of host records or actions.
 * It speaks sources, expressions, ticks, and events. Policy — what an expression
 * *means* and what to do when it fires — stays in the host.
 *
 * See `plans/cxpr_engine_layer_decisions.md` for the rationale behind this API.
 */

#ifndef CXPR_ENGINE_H
#define CXPR_ENGINE_H

#include <cxpr/snapshot.h>
#include <cxpr/types.h>
#include <cxpr/registry.h>
#include <cxpr/expression.h>
#include <cxpr/context.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque immutable engine program (build once, share across threads). */
typedef struct cxpr_engine_program cxpr_engine_program;
/** @brief Opaque mutable engine session — one isolated run, one per thread. */
typedef struct cxpr_engine_session cxpr_engine_session;

/** @brief Context key used by cxpr_engine while evaluating an inline lookback target. */
extern const char* const CXPR_ENGINE_LOOKBACK_OFFSET_KEY;

/**
 * @brief Read the current inline lookback offset from a context.
 *
 * Hosts that opt AST targets into @ref cxpr_engine_inline_lookback_fn can use
 * this helper inside their callbacks to adjust any host-owned scoped/source
 * reads. Returns false when no engine inline lookback is active.
 */
bool cxpr_engine_context_lookback_offset(const cxpr_context* ctx, size_t* out_offset);

/**
 * @brief Transition kind that causes a watched expression to fire.
 *
 * The consumer chooses the edge per watch; the engine assumes nothing. For
 * "act the moment a signal appears" semantics, use @ref CXPR_EDGE_RISING; for
 * "act every step the condition holds", use @ref CXPR_EDGE_LEVEL.
 */
typedef enum {
    CXPR_EDGE_RISING = 0,  /**< Fires on a false→true transition. */
    CXPR_EDGE_FALLING = 1, /**< Fires on a true→false transition. */
    CXPR_EDGE_LEVEL = 2,   /**< Fires every tick while the expression is true. */
    CXPR_EDGE_CHANGED = 3, /**< Fires when the value differs from the previous tick. */
} cxpr_engine_edge;

/**
 * @brief Resolve the current value of a pull (streaming) source for this tick.
 *
 * Pull sources model point-in-time inputs that the host fetches on demand —
 * sensor reads, live quotes, or any IO-backed value. The engine calls this at
 * most once per tick per referenced source, appends the result to that
 * source's lookback ring, and memoizes it for the remainder of the tick.
 *
 * @param[in] name Provider-visible source name being requested.
 * @param[in] args Evaluated numeric source-plan arguments, or NULL when none.
 * @param[in] argc Number of entries in @p args.
 * @param[in] userdata Opaque pointer supplied at source registration.
 * @return Resolved value. Return `NAN` when the host cannot resolve it.
 */
typedef double (*cxpr_engine_pull_fn)(
    const char* name,
    const double* args,
    size_t argc,
    void* userdata);

/**
 * @brief Resolve a view (random-access) source at an absolute cursor index.
 *
 * View sources model inputs the host already holds as an indexed series — a
 * preloaded sample array, for example. The engine keeps no ring for them; it asks
 * for the value at the current cursor for live reads and at `cursor - n` for
 * `expr[n]` lookback, offsetting the index itself.
 *
 * @param[in] index Absolute series index requested (already lookback-adjusted).
 * @param[in] name Provider-visible source name being requested.
 * @param[in] args Evaluated numeric source-plan arguments, or NULL when none.
 * @param[in] argc Number of entries in @p args.
 * @param[out] out_value Receives the resolved value on success.
 * @param[in] userdata Opaque pointer supplied at source registration.
 * @return Non-zero on success, zero when @p index is out of range or unresolved.
 */
typedef bool (*cxpr_engine_view_fn)(
    int64_t index,
    const char* name,
    const double* args,
    size_t argc,
    double* out_value,
    void* userdata);

/**
 * @brief Optional host policy for inline lookback evaluation.
 *
 * The engine owns lookback for its declared sources and named expression rings.
 * For host-provided AST functions it normally delegates to the registry's prior
 * lookback resolver. A host that has offset-aware functions can opt specific
 * targets into engine inline evaluation without teaching cxpr any domain names.
 */
typedef bool (*cxpr_engine_inline_lookback_fn)(
    const cxpr_ast* target,
    void* userdata);

/**
 * @brief Optionally map the engine cursor to a view source's own index space.
 *
 * The engine calls this after applying expression lookback (`cursor - n`) and
 * before invoking @ref cxpr_engine_view_fn. Hosts with timestamp-aligned
 * secondary series can map a primary tick to the secondary index at or before
 * that tick. Return a negative value when no source row exists for the cursor.
 */
typedef int64_t (*cxpr_engine_view_index_map_fn)(
    int64_t cursor,
    void* userdata);

/** @brief One pull (streaming) source registration entry. */
typedef struct {
    const char* name;       /**< Provider-visible source name. */
    cxpr_engine_pull_fn fn;  /**< Per-tick value resolver. */
    void* userdata;         /**< Opaque pointer passed back to @ref fn. */
} cxpr_engine_pull_source_def;

/** @brief One callback-backed view (random-access) source registration entry. */
typedef struct {
    const char* name;       /**< Provider-visible source name. */
    cxpr_engine_view_fn fn;  /**< Indexed value resolver. */
    void* userdata;         /**< Opaque pointer passed back to @ref fn. */
    cxpr_engine_view_index_map_fn map_index; /**< Optional cursor → source-index mapper. */
} cxpr_engine_view_source_def;

/**
 * @brief One direct column-bound view source — a random-access source with no
 * callback.
 *
 * Binds a source straight to a `double` field of a host record array, so the
 * engine reads `*(const double*)((const char*)base + index * stride)` with no
 * per-read branching. This is the zero-boilerplate fast path for scalar columns
 * of an array-of-structs (e.g. `sample_record::value`): point @ref base at the
 * field of element 0 and set @ref stride to the element size. Lookback and
 * bounds behave exactly as for callback view sources (out-of-range → NaN/false).
 *
 * For non-`double` fields (e.g. an integer timestamp), derived values, scoped
 * sources, or anything needing computation, use the callback form
 * (@ref cxpr_engine_view_source_def) instead.
 */
typedef struct {
    const char* name;   /**< Provider-visible source name. */
    const void* base;   /**< Address of the `double` field in element 0 (e.g. `&records[0].value`). Borrowed. */
    size_t stride;      /**< Bytes between consecutive elements (e.g. `sizeof(sample_record)`). */
    size_t count;       /**< Number of elements, used for bounds checking. */
} cxpr_engine_column_source_def;

/** @brief One transition-watch declaration. */
typedef struct {
    const char* expr_name;  /**< Expression to watch (from the config or the registry). */
    cxpr_engine_edge edge;   /**< Transition kind that fires the watch. */
} cxpr_engine_watch_def;

/**
 * @brief One basket role: a named set of opaque member ids.
 *
 * A role parameterizes a basket aggregate (`avg`/`any`/`all`/`min`/`max`/`count`)
 * over its members. The aggregate re-evaluates its argument once per member with
 * the role's `$name` bound to that member id, then folds. The member ids are
 * opaque `double`s the engine ascribes no meaning to — a host's source callbacks
 * interpret an id (passed as a source argument, e.g. `close($pair)`) to resolve
 * that member's data. This is the same mechanism cxpr's basket builtins use; the
 * engine only seeds the role binding into the session context.
 */
typedef struct {
    const char* name;        /**< Role variable name (used as `$name` in expressions). */
    const double* members;   /**< Opaque member ids. */
    size_t member_count;     /**< Number of members. */
    size_t bound_count;      /**< Logical bound universe size, or 0 to match member_count. */
} cxpr_engine_role_def;

/**
 * @brief One transition event produced by a tick.
 *
 * Events are plain data, returned in a borrowed array by @ref cxpr_engine_tick.
 * A watch is identified by its expression name plus the edge that fired; the
 * host dispatches on `(expr_name, edge)`. `expr_name` is borrowed from the
 * program and stable for the program's lifetime, so it is safe to compare and
 * to marshal across an FFI boundary (it is not a transient buffer).
 */
typedef struct {
    const char* expr_name;  /**< Name of the expression that fired (borrowed, program-stable). */
    cxpr_engine_edge edge;   /**< Edge kind that fired. */
    cxpr_value value;       /**< Current value of the watched expression this tick. */
} cxpr_engine_event;

/* -------------------------------------------------------------------------- */
/* Program: describe once, build once, share across threads.                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Declarative configuration for an engine program.
 *
 * Describes everything in one place: the registry, the expression set, the
 * source backings (pull / view / column), and the watch declarations. Arrays
 * are borrowed for the duration of the build call only; any array may be NULL
 * when its count is 0.
 *
 * `registry` is dependency-injected: it is the host's function vocabulary
 * (custom functions, providers, expression-defined rules), typically built once
 * and shared across many programs. Pass NULL for the trivial case to have the
 * engine create and own a registry populated with @ref cxpr_register_defaults.
 * The engine owns lookback itself (D7/D16), so the registry need not provide a
 * lookback resolver.
 *
 * `expressions` is this program's **program-local** rule set: the engine adds
 * them to the evaluator batch (private to this program), with dependency
 * ordering and one evaluation per tick. A watch may *also* target an
 * expression-defined function already in the **shared** registry (see
 * @ref cxpr_registry_define_fn), which the engine pulls into the evaluation set
 * automatically — use that for genuinely global rules, since program-specific
 * rules in a shared registry would collide across programs. `expressions` may
 * be empty only when every rule is such a shared registry rule. Static
 * constants are just expressions too — `{ "threshold", "30" }` — so there is no
 * separate "variable" concept; only `$param`s, the mutable per-session tuning
 * surface, stand apart.
 *
 * `params` seeds the per-session `$param` state. The values are **defaults**
 * baked into the immutable program: each new session starts from them and may
 * override via @ref cxpr_engine_set_param. Per D12 params stay
 * per-session-mutable, so optimizer workers still set their own — the config
 * just declares the common starting values once.
 *
 * Source entries split the same way: the **declaration** (name, kind, field /
 * stride / callback) is program-level and shared, but the **binding** — a pull/
 * view source's `userdata`, a column source's `base`/`count` — is per-session,
 * with the config value as the default. Data and host state are session-scoped:
 * a session points its sources at its own sample array / host state via
 * @ref cxpr_engine_bind_column and @ref cxpr_engine_bind_userdata. Sessions that
 * share data (e.g. an optimizer varying only params over one dataset) just keep
 * the defaults and never rebind.
 *
 * `roles` seed basket roles per session (defaults, like `params`); update a
 * role's membership at runtime with @ref cxpr_engine_set_role. Basket aggregates
 * require the basket builtins in the registry: the engine's NULL-default registry
 * includes them, but an **injected** registry must register them itself
 * (@ref cxpr_register_basket_builtins) if any expression uses a basket aggregate.
 */
typedef struct {
    const cxpr_registry* registry;                  /**< Function vocabulary (dependency-injected, borrowed, must outlive the program). NULL → the engine creates and owns a `cxpr_register_defaults` registry. */
    const cxpr_expression_def* expressions;         /**< Extra expressions to add, beyond any in the registry. */
    size_t expression_count;                        /**< Number of entries in @ref expressions. */
    const cxpr_engine_pull_source_def* pull_sources; /**< Pull (streaming) sources. */
    size_t pull_source_count;                       /**< Number of entries in @ref pull_sources. */
    const cxpr_engine_view_source_def* view_sources; /**< Callback-backed view sources. */
    size_t view_source_count;                       /**< Number of entries in @ref view_sources. */
    const cxpr_engine_column_source_def* column_sources; /**< Direct column-bound view sources. */
    size_t column_source_count;                     /**< Number of entries in @ref column_sources. */
    const cxpr_engine_watch_def* watches;           /**< Transition watches to declare. */
    size_t watch_count;                             /**< Number of entries in @ref watches. */
    const cxpr_context_entry* params;               /**< Default `$param` values (seeded per session). */
    size_t param_count;                             /**< Number of entries in @ref params. */
    const cxpr_engine_role_def* roles;              /**< Default basket roles (seeded per session). */
    size_t role_count;                              /**< Number of entries in @ref roles. */
    cxpr_engine_inline_lookback_fn inline_lookback; /**< Optional host policy for offset-aware AST targets. */
    void* inline_lookback_userdata;                 /**< Opaque pointer passed to @ref inline_lookback. */
} cxpr_engine_config;

/**
 * @brief Build and compile an engine program from one declarative config.
 *
 * The single construction entry point: registers the config's expressions and
 * sources, declares its watches, captures the default params, resolves
 * dependency order, and sizes the lookback buffers — returning a compiled
 * program ready for @ref cxpr_engine_session_new.
 *
 * Each watch name is resolved against both @ref cxpr_engine_config::expressions
 * and the expression-defined functions in the registry; a referenced registry
 * expression is pulled into the evaluation set automatically. The call fails if
 * a watch names an expression found in neither.
 *
 * @param config Declarative program configuration.
 * @param err Optional error output describing the first failing step.
 * @return Compiled program on success, or NULL on any failure (nothing leaks).
 */
cxpr_engine_program* cxpr_engine_program_new(const cxpr_engine_config* config,
                                             cxpr_error* err);

/**
 * @brief Free an engine program.
 *
 * The caller must ensure no @ref cxpr_engine_session still references this
 * program. If the program created its own registry (config `registry` was
 * NULL), that registry is freed too; an injected registry is left untouched.
 *
 * @param prog Program to free. May be NULL.
 */
void cxpr_engine_program_free(cxpr_engine_program* prog);

/* -------------------------------------------------------------------------- */
/* Session: one isolated run, one per worker thread.                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Open a fresh session for a compiled program.
 *
 * The session borrows @p prog, which must outlive it. Each session has its own
 * context, lookback history, cursor, and `$param` values — initialized from the
 * program's config defaults — so independent workers may share one program
 * safely (the "compile once, run many" pattern).
 *
 * @param prog Compiled engine program.
 * @return Newly allocated session, or NULL on allocation failure.
 */
cxpr_engine_session* cxpr_engine_session_new(const cxpr_engine_program* prog);

/**
 * @brief One-shot: build a program and open a single session from one config.
 *
 * Convenience for the **single-session** case where the program/session split
 * is pure ceremony. Equivalent to
 * @ref cxpr_engine_program_new followed by @ref cxpr_engine_session_new, except
 * the returned session **owns** its program: @ref cxpr_engine_session_free frees
 * both.
 *
 * Do **not** use this when one compiled program should back many sessions (the
 * optimizer's compile-once/run-many). There, keep @ref cxpr_engine_program_new
 * and @ref cxpr_engine_session_new separate so the program is shared across
 * threads.
 *
 * @param config Declarative program configuration.
 * @param err Optional error output describing the first failing step.
 * @return A session owning a freshly compiled program, or NULL on any failure.
 */
cxpr_engine_session* cxpr_engine_session_create(const cxpr_engine_config* config,
                                                cxpr_error* err);

/**
 * @brief Close a session.
 *
 * If the session came from @ref cxpr_engine_session_create, its owned program is
 * freed too. Sessions from @ref cxpr_engine_session_new do not own their program.
 *
 * @param session Session to free. May be NULL.
 */
void cxpr_engine_session_free(cxpr_engine_session* session);

/**
 * @brief Reset a session to its initial state without reallocating.
 *
 * Clears the cursor, lookback history, previous-value snapshots, and pending
 * events, but **retains** the session's `$param` values and source bindings.
 * Lets a worker reuse one session across many series without per-run allocation
 * churn; rebind sources (@ref cxpr_engine_bind_column) to point at the next bar
 * set before re-running.
 *
 * @param session Session to reset.
 */
void cxpr_engine_session_reset(cxpr_engine_session* session);

/**
 * @brief Return the mutable context owned by a session.
 *
 * Hosts may populate ordinary variables, structs, strings, and params directly
 * before calling @ref cxpr_engine_tick. The returned pointer is borrowed and
 * remains owned by @p session.
 */
cxpr_context* cxpr_engine_session_context(cxpr_engine_session* session);

/**
 * @brief Advance the session at an explicit absolute cursor index.
 *
 * Equivalent to setting the next tick cursor to @p index and then calling
 * @ref cxpr_engine_tick. This is useful for hosts that already own an indexed
 * series and need an engine expression to align with an existing read index.
 */
bool cxpr_engine_tick_at(cxpr_engine_session* session,
                         int64_t index,
                         const cxpr_engine_event** out_events,
                         size_t* out_count,
                         cxpr_error* err);

/**
 * @brief Advance one tick using an external context as fallback input.
 *
 * The engine keeps its own session context for hydrated sources, seeded params,
 * roles, source slots, and result lookback, while temporarily using @p parent_ctx
 * as fallback input for ordinary host variables, structs, strings, and params.
 * This preserves existing host-owned context APIs while keeping source and
 * result lookback inside the engine session.
 */
bool cxpr_engine_tick_fallback(cxpr_engine_session* session,
                               const cxpr_context* parent_ctx,
                               const cxpr_engine_event** out_events,
                               size_t* out_count,
                               cxpr_error* err);

/**
 * @brief Advance at an explicit cursor index using an external parent context.
 */
bool cxpr_engine_tick_at_fallback(cxpr_engine_session* session,
                                  int64_t index,
                                  const cxpr_context* parent_ctx,
                                  const cxpr_engine_event** out_events,
                                  size_t* out_count,
                                  cxpr_error* err);

/**
 * @brief Set a numeric `$param` for a session.
 *
 * Params are fixed for the life of a session unless changed by an explicit call
 * to this setter; they are never re-hydrated per tick. Typically set once after
 * @ref cxpr_engine_session_new, before the first tick.
 *
 * @param session Destination session.
 * @param name Parameter name without `$`.
 * @param value Numeric value to store.
 */
void cxpr_engine_set_param(cxpr_engine_session* session, const char* name, double value);

/**
 * @brief Set a typed `$param` for a session.
 * @param session Destination session.
 * @param name Parameter name without `$`.
 * @param value Typed value to clone and store.
 */
void cxpr_engine_set_param_value(cxpr_engine_session* session, const char* name,
                                 const cxpr_value* value);

/**
 * @brief Point a column source at this session's data (per-session binding).
 *
 * Overrides the config default `base`/`count` for one column source. Data is
 * session-scoped: use this to run the same program over a different sample array
 * without rebuilding the program. The structural
 * `stride` is fixed at declaration and not changed here.
 *
 * @param session Destination session.
 * @param name Column source name.
 * @param base Address of the `double` field in element 0 (borrowed).
 * @param count Number of elements, for bounds checking.
 * @return True on success, false if @p name is not a column source.
 */
bool cxpr_engine_bind_column(cxpr_engine_session* session, const char* name,
                             const void* base, size_t count);

/**
 * @brief Point a pull or view source at this session's host state (per-session binding).
 *
 * Overrides the config default `userdata` for one callback source. Host state is
 * session-scoped — e.g. a `position_qty` pull source reads *this* session's
 * position — so each session binds its own. Sessions sharing state keep the
 * config default and never call this.
 *
 * @param session Destination session.
 * @param name Pull or view source name.
 * @param userdata Opaque pointer passed to that source's callback.
 * @return True on success, false if @p name is not a callback source.
 */
bool cxpr_engine_bind_userdata(cxpr_engine_session* session, const char* name,
                               void* userdata);

/**
 * @brief Set or update a basket role's membership for this session.
 *
 * Seeds the role binding the basket builtins read (and binds `$name` to the sole
 * member when @p count is 1). Call once for a static basket, or per tick for a
 * dynamic membership (e.g. a changing universe) — membership is session state.
 * Members are opaque ids the engine does not interpret; source callbacks resolve
 * a member's data from the id passed as a source argument.
 *
 * @param session Destination session.
 * @param name Role name (matches a `config.roles` entry or introduces a new one).
 * @param members Opaque member ids.
 * @param count Number of members.
 * @return True on success, false on allocation failure.
 */
bool cxpr_engine_set_role(cxpr_engine_session* session, const char* name,
                          const double* members, size_t count);

/* -------------------------------------------------------------------------- */
/* Tick and results.                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Advance the session by one tick and collect the events that fired.
 *
 * One tick advances the cursor, lazily pulls only the sources the compiled
 * rules reference (memoized for the tick), evaluates the rule set in dependency
 * order, then compares each watched expression against its previous value and
 * collects the transitions that match their declared edge.
 *
 * On success, @p out_events points to a borrowed array of @p out_count events
 * owned by @p session. The array is reused across ticks: it is valid only until
 * the next call that mutates @p session (another tick, a reset, or free). Copy
 * any event you need to retain. The common case of zero fired events performs no
 * allocation and yields @p out_count of 0.
 *
 * The engine takes no action on the events — draining and acting on them is the
 * host's job, after this call returns (e.g. `for (i = 0; i < n; ++i) ...`).
 *
 * @param session Session to advance.
 * @param out_events Receives a borrowed pointer to this tick's events. Pass NULL
 *                   to ignore events.
 * @param out_count Receives the number of events. Pass NULL to ignore the count.
 * @param err Optional error output.
 * @return True on success, false on a source-binding or evaluation failure.
 */
bool cxpr_engine_tick(cxpr_engine_session* session,
                      const cxpr_engine_event** out_events, size_t* out_count,
                      cxpr_error* err);

/**
 * @brief Return the current tick cursor index.
 * @param session Session to query.
 * @return Zero-based index of the most recently evaluated tick, or -1 before
 *         the first tick.
 */
int64_t cxpr_engine_tick_index(const cxpr_engine_session* session);

/**
 * @brief Read one expression result from the last tick as a typed value.
 *
 * Resolves @p name against both the config expressions and registry expressions,
 * the same name space watches use.
 *
 * @param session Session to query.
 * @param name Expression name.
 * @param found Optional success flag output.
 * @return Expression result, or a zero-like value on miss.
 */
cxpr_value cxpr_engine_get(const cxpr_engine_session* session, const char* name, bool* found);

/**
 * @brief Read one expression result from the last tick as a number.
 * @param session Session to query.
 * @param name Expression name.
 * @param found Optional success flag output.
 * @return Numeric result, or `0.0` on miss or type mismatch.
 */
double cxpr_engine_get_double(const cxpr_engine_session* session, const char* name, bool* found);

/**
 * @brief Read one expression result from the last tick as a boolean.
 * @param session Session to query.
 * @param name Expression name.
 * @param found Optional success flag output.
 * @return Boolean result, or `false` on miss or type mismatch.
 */
bool cxpr_engine_get_bool(const cxpr_engine_session* session, const char* name, bool* found);

/**
 * @brief Return the compiled IR instruction count for one engine expression.
 */
size_t cxpr_engine_expression_instruction_count(const cxpr_engine_session* session,
                                                const char* name,
                                                bool* found);

/**
 * @brief Return compiled IR instructions for one expression plus dependencies.
 */
size_t cxpr_engine_expression_dependency_instruction_count(
    const cxpr_engine_session* session,
    const char* name,
    bool* found);

/**
 * @brief Return total compiled IR instructions for the engine expression batch.
 */
size_t cxpr_engine_expression_total_instruction_count(const cxpr_engine_session* session);

/**
 * @brief Build a diagnostic flow snapshot for all expressions at the current tick.
 *
 * The flow contains expression-level dependency edges plus one AST snapshot per
 * expression for drilldown. This is intended for debugging/visualization and is
 * not part of the hot tick path.
 */
bool cxpr_engine_snapshot_flow(const cxpr_engine_session* session,
                               cxpr_eval_snapshot_flow* out_flow,
                               cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_ENGINE_H */
