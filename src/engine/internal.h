#ifndef CXPR_ENGINE_INTERNAL_H
#define CXPR_ENGINE_INTERNAL_H

#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <cxpr/typecheck.h>
#include "context/internal.h"
#include "limits.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define ENGINE_TLS __declspec(thread)
#else
#define ENGINE_TLS _Thread_local
#endif

/* -------------------------------------------------------------------------- */
/* Internal representations                                                    */
/* -------------------------------------------------------------------------- */

typedef enum {
    ENGINE_SRC_PULL = 0,
    ENGINE_SRC_VIEW = 1,
    ENGINE_SRC_COLUMN = 2,
} engine_source_kind;

typedef struct {
    char* name;
    engine_source_kind kind;
    cxpr_engine_pull_fn pull_fn; /* PULL */
    cxpr_engine_view_fn view_fn; /* VIEW */
    cxpr_engine_view_index_map_fn map_index_fn; /* VIEW, optional */
    void* userdata;              /* PULL/VIEW binding (default; overridable per session) */
    const void* base;            /* COLUMN binding (default; overridable per session) */
    size_t stride;               /* COLUMN: structural, fixed */
    size_t count;                /* COLUMN binding (default; overridable per session) */

    /* Computed at program build (D5/D16). */
    bool referenced;             /* appears in the expression set (bare or via lookback) */
    bool hydrate_bare;           /* needs current value written under `name` in context */
    size_t max_lookback;         /* deepest literal subscript on this source, 0 if none */

    /* Session-only pull ring (NULL in the program template). */
    double* ring;
    size_t ring_cap;             /* == max_lookback + 1 when allocated */
    size_t ring_head;            /* index of the newest sample */
    size_t ring_count;           /* samples held so far (<= ring_cap) */

    /* Session-only hot-loop write slot: avoids re-hashing the name each tick. */
    cxpr_context_slot slot;
    bool slot_bound;
} engine_source;

typedef struct {
    char* expr_name;
    cxpr_engine_edge edge;
} engine_watch;

typedef struct {
    char* name;
    double* members;
    size_t count;
    size_t bound_count;
} engine_role;

/* A named expression that is looked back (`expr[n]`); gets a per-session result
 * ring sized to max_depth (D16). */
typedef struct {
    char* name;
    size_t max_depth;
} engine_tracked_expr;

/* Simple circular buffer of expression values (newest at head). */
typedef struct {
    cxpr_value* buf;
    size_t cap;
    size_t head;
    size_t count;
} engine_ring;

typedef struct {
    const engine_source* src;
    double args[CXPR_MAX_CALL_ARGS];
    size_t argc;
    size_t offset;
    double value;
    bool valid;
} engine_source_memo_entry;

typedef struct {
    const engine_source* src;
    double args[CXPR_MAX_CALL_ARGS];
    size_t argc;
    double* ring;
    size_t ring_cap;
    size_t ring_head;
    size_t ring_count;
    int64_t last_append_cursor;
    bool valid;
} engine_arg_ring_entry;

struct cxpr_engine_program {
    const cxpr_registry* registry;
    bool owns_registry;

    cxpr_expression_def* exprs; /* names + sources owned */
    size_t expr_count;

    engine_source* sources;
    size_t source_count;

    engine_watch* watches;
    size_t watch_count;

    cxpr_context_entry* params; /* names owned */
    size_t param_count;

    char** external_params; /* $params referenced by expressions, names owned */
    size_t external_param_count;

    engine_role* roles;
    size_t role_count;

    engine_tracked_expr* tracked; /* expressions referenced via lookback (D16) */
    size_t tracked_count;

    /* Resolver previously installed on an injected registry. The engine owns
     * lookback for its sources + tracked expressions; any other target falls
     * through here so the host can keep serving lookbacks the engine does not
     * own. NULL for the engine-owned default registry (no prior resolver). */
    cxpr_lookback_resolver_ptr delegate_lookback_fn;
    void* delegate_lookback_ud;
    cxpr_engine_inline_lookback_fn inline_lookback_fn;
    void* inline_lookback_ud;
};

struct cxpr_engine_session {
    const cxpr_engine_program* prog;
    cxpr_engine_program* owned_prog; /* non-NULL when created via session_create */

    cxpr_evaluator* eval; /* per-session: holds this run's results */
    cxpr_context* ctx;    /* persistent per-session store */
    int64_t cursor;       /* -1 before first tick */

    engine_source* sources; /* per-session copy: bindings overridable, names borrowed from prog */
    size_t source_count;

    engine_ring* expr_rings; /* parallel to prog->tracked: past results for expr[n] (D16) */
    size_t expr_ring_count;

    /* watch transition state, parallel to prog->watches */
    bool* prev_truthy;
    double* prev_value;
    bool* prev_valid;

    /* reused event batch */
    cxpr_engine_event* events;
    size_t event_cap;

    /* per-tick source-call memo, keyed by source + evaluated args + offset */
    engine_source_memo_entry* source_memo;
    size_t source_memo_count;
    size_t source_memo_cap;

    engine_arg_ring_entry* arg_rings;
    size_t arg_ring_count;
    size_t arg_ring_cap;
};

bool engine_lookback_resolver(const cxpr_expr_ast* target,
                              const cxpr_expr_ast* index,
                              const cxpr_context* ctx,
                              const cxpr_registry* reg,
                              void* userdata,
                              cxpr_value* out,
                              cxpr_error* err);

extern ENGINE_TLS cxpr_engine_session* g_engine_tls_session;
extern ENGINE_TLS size_t g_engine_tls_lookback_offset;

char* engine_strdup(const char* s);
void engine_set_err(cxpr_error* err, cxpr_error_code code, const char* msg);
double engine_value_to_double(cxpr_value value);
bool engine_seed_role(cxpr_context* ctx, const char* name,
                      const double* members, size_t count,
                      size_t bound_count);
cxpr_evaluator* engine_build_evaluator(const cxpr_engine_program* prog,
                                        cxpr_error* err);

bool engine_registry_resolver_acquire(cxpr_engine_program* prog);
void engine_registry_resolver_release(const cxpr_engine_program* prog);
bool engine_is_expr_name(const cxpr_engine_program* prog, const char* name);
const cxpr_expression_def* engine_find_expr_def(const cxpr_engine_program* prog,
                                                const char* name);
bool engine_track_external_param(cxpr_engine_program* prog, const char* name);
void engine_scan_ast(const cxpr_expr_ast* ast, cxpr_engine_program* prog);
int engine_tracked_index(const cxpr_engine_program* prog, const char* name);

engine_source* engine_find_source_in(engine_source* sources, size_t count,
                                     const char* name);
void engine_ring_append(engine_source* source, double value);
void engine_ringb_append(engine_ring* ring, cxpr_value value);
bool engine_struct_field_value(cxpr_value value, const char* field,
                               cxpr_value* out);
bool engine_struct_path_value(cxpr_value value, const cxpr_expr_ast* chain,
                              size_t first_segment, cxpr_value* out);
bool engine_tracked_expression_value(cxpr_engine_session* session,
                                     int tracked_index, size_t lookback,
                                     cxpr_value* out);
bool engine_eval_call_args(const cxpr_expr_ast* call_ast,
                           const cxpr_context* ctx,
                           const cxpr_registry* reg,
                           double* args, size_t* argc, cxpr_error* err);
bool engine_resolve_source_call(cxpr_engine_session* session,
                                const engine_source* source,
                                const double* args, size_t argc,
                                size_t offset, double* out, cxpr_error* err);
bool engine_eval_inline_lookback(cxpr_engine_session* session,
                                 const cxpr_expr_ast* target, size_t offset,
                                 const cxpr_context* ctx,
                                 const cxpr_registry* reg,
                                 cxpr_value* out, cxpr_error* err);
cxpr_value engine_source_call(const cxpr_expr_ast* call_ast,
                              const cxpr_context* ctx,
                              const cxpr_registry* reg,
                              void* userdata, cxpr_error* err);

#endif
