/**
 * @file engine.c
 * @brief Stateful rule-engine layer on top of the cxpr evaluator.
 *
 * Implements cxpr/engine.h. See plans/cxpr_engine_layer_decisions.md (D1-D25).
 *
 * Owns program/session lifecycle (D14/D4), registry DI incl. NULL-default
 * (D19), config-seeded params (D12) and roles (D25), per-session source binding
 * (D22), source hydration via pull/view/column backings, source/result lookback
 * rings (D7/D16/D17), watches with edge detection and borrowed event batches
 * (D9/D11), and result readers.
 */

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

static bool engine_lookback_resolver(const cxpr_expr_ast* target, const cxpr_expr_ast* index,
                                     const cxpr_context* ctx, const cxpr_registry* reg,
                                     void* userdata, cxpr_value* out, cxpr_error* err);

typedef struct engine_registry_resolver_ref {
    const cxpr_registry* registry;
    cxpr_lookback_resolver_ptr prior_fn;
    void* prior_ud;
    size_t ref_count;
    struct engine_registry_resolver_ref* next;
} engine_registry_resolver_ref;

static engine_registry_resolver_ref* g_engine_registry_refs = NULL;

static bool engine_registry_resolver_acquire(cxpr_engine_program* prog) {
    engine_registry_resolver_ref* it;
    cxpr_lookback_resolver_ptr prior = NULL;
    void* prior_ud = NULL;

    if (!prog || !prog->registry || prog->owns_registry) return true;
    for (it = g_engine_registry_refs; it; it = it->next) {
        if (it->registry == prog->registry) {
            it->ref_count++;
            prog->delegate_lookback_fn = it->prior_fn;
            prog->delegate_lookback_ud = it->prior_ud;
            return true;
        }
    }

    cxpr_registry_lookback_resolver(prog->registry, &prior, &prior_ud);
    it = (engine_registry_resolver_ref*)calloc(1u, sizeof(*it));
    if (!it) return false;
    it->registry = prog->registry;
    it->prior_fn = prior == engine_lookback_resolver ? NULL : prior;
    it->prior_ud = prior == engine_lookback_resolver ? NULL : prior_ud;
    it->ref_count = 1u;
    it->next = g_engine_registry_refs;
    g_engine_registry_refs = it;

    prog->delegate_lookback_fn = it->prior_fn;
    prog->delegate_lookback_ud = it->prior_ud;
    cxpr_registry_set_lookback_resolver(
        (cxpr_registry*)prog->registry, engine_lookback_resolver, NULL, NULL);
    return true;
}

static void engine_registry_resolver_release(const cxpr_engine_program* prog) {
    engine_registry_resolver_ref** link;
    if (!prog || !prog->registry || prog->owns_registry) return;
    for (link = &g_engine_registry_refs; *link; link = &(*link)->next) {
        engine_registry_resolver_ref* it = *link;
        if (it->registry != prog->registry) continue;
        if (it->ref_count > 1u) {
            it->ref_count--;
            return;
        }
        {
            cxpr_lookback_resolver_ptr cur = NULL;
            cxpr_registry_lookback_resolver(prog->registry, &cur, NULL);
            if (cur == engine_lookback_resolver) {
                cxpr_registry_set_lookback_resolver(
                    (cxpr_registry*)prog->registry,
                    it->prior_fn,
                    it->prior_ud,
                    NULL);
            }
        }
        *link = it->next;
        free(it);
        return;
    }
}

static bool engine_registry_resolver_delegate(const cxpr_registry* registry,
                                              cxpr_lookback_resolver_ptr* out_fn,
                                              void** out_ud) {
    engine_registry_resolver_ref* it;
    if (out_fn) *out_fn = NULL;
    if (out_ud) *out_ud = NULL;
    if (!registry) return false;
    for (it = g_engine_registry_refs; it; it = it->next) {
        if (it->registry == registry) {
            if (out_fn) *out_fn = it->prior_fn;
            if (out_ud) *out_ud = it->prior_ud;
            return it->prior_fn != NULL;
        }
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* Small helpers                                                               */
/* -------------------------------------------------------------------------- */

static char* engine_strdup(const char* s) {
    size_t n;
    char* p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void engine_set_err(cxpr_error* err, cxpr_error_code code, const char* msg) {
    if (!err) return;
    err->code = code;
    err->message = msg;
    err->position = 0;
    err->line = 0;
    err->column = 0;
}

static double engine_value_to_double(cxpr_value v) {
    if (v.type == CXPR_VALUE_NUMBER) return v.d;
    if (v.type == CXPR_VALUE_BOOL) return v.b ? 1.0 : 0.0;
    return NAN;
}

/* Build the basket role struct the cxpr basket builtins read (D25):
 * `__cxpr_basket_role_<name>` = { bound_count, value_count, v0..v{n-1} },
 * plus `$name` bound directly when there is a single member. */
static bool engine_seed_role(cxpr_context* ctx, const char* name,
                             const double* members, size_t count,
                             size_t bound_count) {
    char key[256];
    const char** fnames;
    cxpr_value* fvals;
    char (*vnames)[24];
    cxpr_struct_value* sv;
    size_t nf, i;

    if (!ctx || !name || name[0] == '\0') return false;

    snprintf(key, sizeof(key), "__cxpr_basket_role_%s", name);
    nf = count + 2u;
    fnames = (const char**)malloc(nf * sizeof(*fnames));
    fvals = (cxpr_value*)malloc(nf * sizeof(*fvals));
    vnames = (char (*)[24])malloc((count ? count : 1u) * sizeof(*vnames));
    if (!fnames || !fvals || !vnames) {
        free(fnames); free(fvals); free(vnames);
        return false;
    }

    fnames[0] = "bound_count";
    fvals[0] = cxpr_num((double)(bound_count ? bound_count : count));
    fnames[1] = "value_count";
    fvals[1] = cxpr_num((double)count);
    for (i = 0; i < count; ++i) {
        snprintf(vnames[i], sizeof(vnames[i]), "v%zu", i);
        fnames[i + 2u] = vnames[i];
        fvals[i + 2u] = cxpr_num(members[i]);
    }

    sv = cxpr_struct_value_new(fnames, fvals, nf);
    free(fnames);
    free(fvals);
    free(vnames);
    if (!sv) return false;
    cxpr_context_set_struct(ctx, key, sv);
    cxpr_struct_value_free(sv);

    if (count == 1u) cxpr_context_set_param(ctx, name, members[0]);
    return true;
}

/* The session being ticked on this thread; read by the lookback resolver.
 * Sessions are single-threaded (D23), so a thread-local pointer lets one
 * resolver installed on a shared registry reach per-session ring/cursor state
 * without per-session userdata on the registry. */
static ENGINE_TLS cxpr_engine_session* g_engine_tls_session = NULL;
static ENGINE_TLS size_t g_engine_tls_lookback_offset = 0;

const char* const CXPR_ENGINE_LOOKBACK_OFFSET_KEY = "__cxpr_engine_lookback_offset";

bool cxpr_engine_context_lookback_offset(const cxpr_context* ctx, size_t* out_offset) {
    return cxpr_context_history_offset(ctx, out_offset);
}

static engine_source* engine_find_source_in(engine_source* sources, size_t count,
                                            const char* name) {
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < count; ++i) {
        if (sources[i].name && strcmp(sources[i].name, name) == 0) return &sources[i];
    }
    return NULL;
}

/* Push a fresh sample onto a pull source's ring (newest at ring_head). */
static void engine_ring_append(engine_source* s, double v) {
    if (!s->ring || s->ring_cap == 0) return;
    s->ring_head = (s->ring_head + 1u) % s->ring_cap;
    s->ring[s->ring_head] = v;
    if (s->ring_count < s->ring_cap) s->ring_count++;
}

/* Read a pull source's value n samples back (depth 0 = newest). */
static double engine_ring_read(const engine_source* s, size_t depth) {
    size_t idx;
    if (!s->ring || depth >= s->ring_count) return NAN;
    idx = (s->ring_head + s->ring_cap - depth) % s->ring_cap;
    return s->ring[idx];
}

/* engine_ring (used for expression result rings). */
static void engine_ringb_append(engine_ring* r, cxpr_value v) {
    if (!r->buf || r->cap == 0) return;
    r->head = (r->head + 1u) % r->cap;
    cxpr_value_free(&r->buf[r->head]);
    r->buf[r->head] = cxpr_value_clone(&v);
    if (r->count < r->cap) r->count++;
}
static cxpr_value engine_ringb_read(const engine_ring* r, size_t depth, bool* found) {
    size_t idx;
    if (found) *found = false;
    if (!r->buf || depth >= r->count) return cxpr_num(NAN);
    idx = (r->head + r->cap - depth) % r->cap;
    if (found) *found = true;
    return r->buf[idx];
}

/* Is `name` one of the program's evaluated expressions (config or synthesized)? */
static bool engine_is_expr_name(const cxpr_engine_program* prog, const char* name) {
    size_t i;
    if (!name) return false;
    for (i = 0; i < prog->expr_count; ++i) {
        if (prog->exprs[i].name && strcmp(prog->exprs[i].name, name) == 0) return true;
    }
    return false;
}

static const cxpr_expression_def* engine_find_expr_def(const cxpr_engine_program* prog,
                                                       const char* name) {
    size_t i;
    if (!prog || !name) return NULL;
    for (i = 0; i < prog->expr_count; ++i) {
        if (prog->exprs[i].name && strcmp(prog->exprs[i].name, name) == 0) {
            return &prog->exprs[i];
        }
    }
    return NULL;
}

/* Record (or deepen) a tracked expression for `expr[n]` lookback. */
static bool engine_track_expr(cxpr_engine_program* prog, const char* name, size_t depth) {
    size_t i;
    engine_tracked_expr* grown;
    for (i = 0; i < prog->tracked_count; ++i) {
        if (strcmp(prog->tracked[i].name, name) == 0) {
            if (depth > prog->tracked[i].max_depth) prog->tracked[i].max_depth = depth;
            return true;
        }
    }
    grown = (engine_tracked_expr*)realloc(prog->tracked,
                                          (prog->tracked_count + 1u) * sizeof(*grown));
    if (!grown) return false;
    prog->tracked = grown;
    prog->tracked[prog->tracked_count].name = engine_strdup(name);
    if (!prog->tracked[prog->tracked_count].name) return false;
    prog->tracked[prog->tracked_count].max_depth = depth;
    prog->tracked_count++;
    return true;
}

static bool engine_track_external_param(cxpr_engine_program* prog, const char* name) {
    char** grown;
    size_t i;

    if (!prog || !name || name[0] == '\0') return true;
    for (i = 0u; i < prog->external_param_count; ++i) {
        if (strcmp(prog->external_params[i], name) == 0) return true;
    }
    grown = (char**)realloc(
        prog->external_params,
        (prog->external_param_count + 1u) * sizeof(*grown));
    if (!grown) return false;
    prog->external_params = grown;
    prog->external_params[prog->external_param_count] = engine_strdup(name);
    if (!prog->external_params[prog->external_param_count]) return false;
    prog->external_param_count++;
    return true;
}

/* Walk an AST, marking referenced sources (and their deepest literal lookback)
 * and tracked expressions referenced via lookback. Sources are matched by bare
 * identifier name or source-shaped calls (`name(args...)`). */
static void engine_scan_ast_with_offset(const cxpr_expr_ast* ast,
                                        cxpr_engine_program* prog,
                                        size_t inherited_lookback) {
    size_t i;
    if (!ast) return;
    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_IDENTIFIER: {
            const char* identifier = cxpr_expr_ast_identifier_name(ast);
            engine_source* s = engine_find_source_in(prog->sources, prog->source_count,
                                                     identifier);
            if (s) {
                s->referenced = true;
                s->hydrate_bare = true;
                if (inherited_lookback > s->max_lookback) s->max_lookback = inherited_lookback;
            } else if (inherited_lookback > 0u &&
                       engine_is_expr_name(prog, identifier)) {
                engine_track_expr(prog, identifier, inherited_lookback);
            }
            break;
        }
        case CXPR_NODE_INDEX: {
            const cxpr_expr_ast* target = cxpr_expr_ast_index_target(ast);
            const cxpr_expr_ast* index = cxpr_expr_ast_index_expression(ast);
            size_t lookback = inherited_lookback;
            if (target && cxpr_expr_ast_kind_of(target) == CXPR_NODE_IDENTIFIER &&
                index && cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER) {
                const char* tname = cxpr_expr_ast_identifier_name(target);
                engine_source* s = engine_find_source_in(prog->sources, prog->source_count, tname);
                double nd = cxpr_expr_ast_number_value(index);
                if (nd >= 0.0) {
                    size_t n = (size_t)nd;
                    lookback = inherited_lookback + n;
                    if (s) {
                        s->referenced = true;
                        s->hydrate_bare = true;
                        if (lookback > s->max_lookback) s->max_lookback = lookback;
                    } else if (engine_is_expr_name(prog, tname)) {
                        engine_track_expr(prog, tname, lookback); /* expr result-ring (D16) */
                    }
                }
            } else if (target && cxpr_expr_ast_kind_of(target) == CXPR_NODE_CHAIN_ACCESS &&
                       cxpr_expr_ast_chain_count(target) >= 2u &&
                       index && cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER) {
                const char* root = cxpr_expr_ast_chain_segment(target, 0u);
                double nd = cxpr_expr_ast_number_value(index);
                if (nd >= 0.0 && engine_is_expr_name(prog, root)) {
                    lookback = inherited_lookback + (size_t)nd;
                    engine_track_expr(prog, root, lookback);
                }
            } else if (target && cxpr_expr_ast_kind_of(target) == CXPR_NODE_FIELD_ACCESS &&
                       index && cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER) {
                const char* root = cxpr_expr_ast_field_object(target);
                double nd = cxpr_expr_ast_number_value(index);
                if (nd >= 0.0 && engine_is_expr_name(prog, root)) {
                    lookback = inherited_lookback + (size_t)nd;
                    engine_track_expr(prog, root, lookback);
                }
            } else if (index && cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER) {
                double nd = cxpr_expr_ast_number_value(index);
                if (nd >= 0.0) lookback = inherited_lookback + (size_t)nd;
            }
            engine_scan_ast_with_offset(target, prog, lookback);
            engine_scan_ast_with_offset(index, prog, inherited_lookback);
            break;
        }
        case CXPR_NODE_BINARY_OP:
            engine_scan_ast_with_offset(cxpr_expr_ast_binary_left(ast), prog, inherited_lookback);
            engine_scan_ast_with_offset(cxpr_expr_ast_binary_right(ast), prog, inherited_lookback);
            break;
        case CXPR_NODE_UNARY_OP:
            engine_scan_ast_with_offset(cxpr_expr_ast_unary_operand(ast), prog, inherited_lookback);
            break;
        case CXPR_NODE_TERNARY:
            engine_scan_ast_with_offset(cxpr_expr_ast_ternary_condition(ast), prog, inherited_lookback);
            engine_scan_ast_with_offset(cxpr_expr_ast_ternary_true(ast), prog, inherited_lookback);
            engine_scan_ast_with_offset(cxpr_expr_ast_ternary_false(ast), prog, inherited_lookback);
            break;
        case CXPR_NODE_FUNCTION_CALL: {
            const char* call_name = cxpr_expr_ast_call_name(ast);
            engine_source* s = engine_find_source_in(prog->sources, prog->source_count,
                                                     call_name);
            if (s) {
                s->referenced = true;
                if (inherited_lookback > s->max_lookback) s->max_lookback = inherited_lookback;
            }
            if (call_name && strcmp(call_name, "repeat") == 0 &&
                cxpr_expr_ast_call_arg_count(ast) == 2u) {
                const cxpr_expr_ast* condition = NULL;
                const cxpr_expr_ast* samples = NULL;
                for (i = 0u; i < 2u; ++i) {
                    const char* arg_name = cxpr_expr_ast_call_arg_name(ast, i);
                    if (!arg_name || strcmp(arg_name, "condition") == 0 ||
                        strcmp(arg_name, "value") == 0) {
                        if (!condition) condition = cxpr_expr_ast_call_arg(ast, i);
                    }
                    if ((arg_name && (strcmp(arg_name, "bars") == 0 ||
                                      strcmp(arg_name, "samples") == 0)) ||
                        (!arg_name && i == 1u)) {
                        samples = cxpr_expr_ast_call_arg(ast, i);
                    }
                }
                if (condition && samples &&
                    cxpr_expr_ast_kind_of(samples) == CXPR_NODE_NUMBER) {
                    const double count = cxpr_expr_ast_number_value(samples);
                    size_t depth = inherited_lookback;
                    if (count >= 1.0 && floor(count) == count &&
                        count - 1.0 <= (double)(SIZE_MAX - depth)) {
                        depth += (size_t)(count - 1.0);
                    }
                    engine_scan_ast_with_offset(condition, prog, depth);
                    engine_scan_ast_with_offset(samples, prog, inherited_lookback);
                    break;
                }
            }
            for (i = 0; i < cxpr_expr_ast_call_arg_count(ast); ++i) {
                engine_scan_ast_with_offset(
                    cxpr_expr_ast_call_arg(ast, i), prog, inherited_lookback);
            }
            break;
        }
        case CXPR_NODE_RECORD:
            for (i = 0; i < cxpr_expr_ast_record_field_count(ast); ++i) {
                engine_scan_ast_with_offset(cxpr_expr_ast_record_field_value(ast, i), prog, inherited_lookback);
            }
            break;
        case CXPR_NODE_FIELD_ACCESS: {
            const cxpr_expr_ast* base = cxpr_expr_ast_field_base(ast);
            const char* root = cxpr_expr_ast_field_object(ast);
            if (base) {
                engine_scan_ast_with_offset(base, prog, inherited_lookback);
            } else if (root && engine_is_expr_name(prog, root)) {
                engine_track_expr(prog, root, inherited_lookback);
            }
            break;
        }
        case CXPR_NODE_CHAIN_ACCESS: {
            const char* root = cxpr_expr_ast_chain_count(ast) > 0u ? cxpr_expr_ast_chain_segment(ast, 0u) : NULL;
            if (root && engine_is_expr_name(prog, root)) {
                engine_track_expr(prog, root, inherited_lookback);
            }
            break;
        }
        case CXPR_NODE_PRODUCER_ACCESS:
            for (i = 0; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
                engine_scan_ast_with_offset(cxpr_expr_ast_producer_arg(ast, i), prog, inherited_lookback);
            }
            break;
        default:
            break;
    }
}

static void engine_scan_ast(const cxpr_expr_ast* ast, cxpr_engine_program* prog) {
    engine_scan_ast_with_offset(ast, prog, 0u);
}

static int engine_tracked_index(const cxpr_engine_program* prog, const char* name) {
    size_t i;
    for (i = 0; i < prog->tracked_count; ++i) {
        if (strcmp(prog->tracked[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool engine_struct_field_value(cxpr_value value, const char* field, cxpr_value* out) {
    size_t i;
    if (!field || !out || value.type != CXPR_VALUE_STRUCT || !value.s) return false;
    for (i = 0; i < value.s->field_count; ++i) {
        if (value.s->field_names[i] && strcmp(value.s->field_names[i], field) == 0) {
            *out = value.s->field_values[i];
            return true;
        }
    }
    return false;
}

static bool engine_struct_path_value(cxpr_value value,
                                     const cxpr_expr_ast* chain,
                                     size_t first_segment,
                                     cxpr_value* out) {
    size_t i;
    size_t depth;
    cxpr_value cur = value;
    if (!chain || !out || cxpr_expr_ast_kind_of(chain) != CXPR_NODE_CHAIN_ACCESS) return false;
    depth = cxpr_expr_ast_chain_count(chain);
    if (first_segment >= depth) return false;
    for (i = first_segment; i < depth; ++i) {
        if (!engine_struct_field_value(cur, cxpr_expr_ast_chain_segment(chain, i), &cur)) {
            return false;
        }
    }
    *out = cur;
    return true;
}

static bool engine_tracked_expression_value(cxpr_engine_session* s,
                                            int tracked_index,
                                            size_t lookback,
                                            cxpr_value* out) {
    bool found = false;
    if (!s || tracked_index < 0 || (size_t)tracked_index >= s->expr_ring_count || !out) {
        return false;
    }
    if (lookback == 0u) {
        const char* name = s->prog->tracked[tracked_index].name;
        cxpr_value value = cxpr_expression_get(s->eval, name, &found);
        if (found) {
            *out = value;
            return true;
        }
    }
    *out = engine_ringb_read(&s->expr_rings[tracked_index], lookback, &found);
    return found;
}

static bool engine_eval_call_args(const cxpr_expr_ast* call_ast,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  double* args,
                                  size_t* argc,
                                  cxpr_error* err) {
    size_t i, n;
    if (!call_ast || !argc) return false;
    n = cxpr_expr_ast_call_arg_count(call_ast);
    if (n > CXPR_MAX_CALL_ARGS) {
        engine_set_err(err, CXPR_ERR_WRONG_ARITY, "engine: source call has too many arguments");
        return false;
    }
    for (i = 0; i < n; ++i) {
        if (!cxpr_eval_ast_number(cxpr_expr_ast_call_arg(call_ast, i), ctx, reg, &args[i], err)) {
            return false;
        }
    }
    *argc = n;
    return true;
}

static bool engine_source_memo_args_equal(const engine_source_memo_entry* entry,
                                          const double* args,
                                          size_t argc) {
    size_t i;
    if (!entry || entry->argc != argc) return false;
    for (i = 0; i < argc; ++i) {
        if (entry->args[i] != args[i]) return false;
    }
    return true;
}

static bool engine_source_memo_get(cxpr_engine_session* session,
                                   const engine_source* src,
                                   const double* args,
                                   size_t argc,
                                   size_t offset,
                                   double* out) {
    size_t i;
    if (!session || !src || !out) return false;
    for (i = 0; i < session->source_memo_count; ++i) {
        const engine_source_memo_entry* entry = &session->source_memo[i];
        if (entry->valid &&
            entry->src == src &&
            entry->offset == offset &&
            engine_source_memo_args_equal(entry, args, argc)) {
            *out = entry->value;
            return true;
        }
    }
    return false;
}

static void engine_source_memo_set(cxpr_engine_session* session,
                                   const engine_source* src,
                                   const double* args,
                                   size_t argc,
                                   size_t offset,
                                   double value) {
    engine_source_memo_entry* entry;
    size_t i;
    /* The memo is a fixed, preallocated table (sized at session_new, never grown
     * mid-tick) to honour D13's "no heap allocation per tick". When it is full we
     * deliberately stop caching: this is a graceful degradation, not an error —
     * the resolver simply re-invokes the source callback, so results stay correct;
     * only the "no redundant callback per tick" guarantee softens. A basket whose
     * member count × distinct call-sites × offsets exceeds the cap can hit this. */
    if (!session || !src || argc > CXPR_MAX_CALL_ARGS ||
        session->source_memo_count >= session->source_memo_cap) {
        return;
    }
    entry = &session->source_memo[session->source_memo_count++];
    memset(entry, 0, sizeof(*entry));
    entry->src = src;
    entry->argc = argc;
    entry->offset = offset;
    entry->value = value;
    entry->valid = true;
    for (i = 0; i < argc; ++i) entry->args[i] = args[i];
}

static bool engine_arg_tuple_equal(const double* lhs, const double* rhs, size_t argc) {
    size_t i;
    for (i = 0; i < argc; ++i) {
        if (lhs[i] != rhs[i]) return false;
    }
    return true;
}

static engine_arg_ring_entry* engine_arg_ring_find(cxpr_engine_session* session,
                                                   const engine_source* src,
                                                   const double* args,
                                                   size_t argc) {
    size_t i;
    if (!session || !src || !args || argc == 0u) return NULL;
    for (i = 0; i < session->arg_ring_count; ++i) {
        engine_arg_ring_entry* entry = &session->arg_rings[i];
        if (entry->valid && entry->src == src && entry->argc == argc &&
            engine_arg_tuple_equal(entry->args, args, argc)) {
            return entry;
        }
    }
    return NULL;
}

static engine_arg_ring_entry* engine_arg_ring_get_or_create(cxpr_engine_session* session,
                                                            const engine_source* src,
                                                            const double* args,
                                                            size_t argc) {
    engine_arg_ring_entry* entry;
    engine_arg_ring_entry* grown;
    size_t i;
    if (!session || !src || !args || argc == 0u || argc > CXPR_MAX_CALL_ARGS ||
        src->max_lookback == 0u) {
        return NULL;
    }
    entry = engine_arg_ring_find(session, src, args, argc);
    if (entry) return entry;
    if (session->arg_ring_count == session->arg_ring_cap) {
        size_t next_cap = session->arg_ring_cap ? session->arg_ring_cap * 2u : 8u;
        grown = (engine_arg_ring_entry*)realloc(
            session->arg_rings, next_cap * sizeof(*session->arg_rings));
        if (!grown) return NULL;
        memset(grown + session->arg_ring_cap, 0,
               (next_cap - session->arg_ring_cap) * sizeof(*grown));
        session->arg_rings = grown;
        session->arg_ring_cap = next_cap;
    }
    entry = &session->arg_rings[session->arg_ring_count++];
    memset(entry, 0, sizeof(*entry));
    entry->src = src;
    entry->argc = argc;
    entry->ring_cap = src->max_lookback + 1u;
    entry->last_append_cursor = -1;
    entry->ring = (double*)malloc(entry->ring_cap * sizeof(double));
    if (!entry->ring) {
        memset(entry, 0, sizeof(*entry));
        session->arg_ring_count--;
        return NULL;
    }
    for (i = 0; i < argc; ++i) entry->args[i] = args[i];
    entry->valid = true;
    return entry;
}

static void engine_arg_ring_append(engine_arg_ring_entry* entry, double value, int64_t cursor) {
    if (!entry || !entry->ring || entry->ring_cap == 0u ||
        entry->last_append_cursor == cursor) {
        return;
    }
    entry->ring_head = (entry->ring_head + 1u) % entry->ring_cap;
    entry->ring[entry->ring_head] = value;
    if (entry->ring_count < entry->ring_cap) entry->ring_count++;
    entry->last_append_cursor = cursor;
}

static double engine_arg_ring_read(const engine_arg_ring_entry* entry, size_t depth) {
    size_t idx;
    if (!entry || !entry->ring || depth >= entry->ring_count) return NAN;
    idx = (entry->ring_head + entry->ring_cap - depth) % entry->ring_cap;
    return entry->ring[idx];
}

static bool engine_resolve_source_call(cxpr_engine_session* session,
                                       const engine_source* src,
                                       const double* args,
                                       size_t argc,
                                       size_t offset,
                                       double* out,
                                       cxpr_error* err) {
    double v = NAN;
    int64_t cursor;
    if (!session || !src || !out) return false;
    if (!args) argc = 0u;
    if (engine_source_memo_get(session, src, args, argc, offset, out)) return true;
    cursor = session->cursor - (int64_t)offset;
    switch (src->kind) {
        case ENGINE_SRC_PULL:
            if (argc > 0u && src->ring_cap > 0u && src->pull_fn) {
                engine_arg_ring_entry* ring =
                    engine_arg_ring_get_or_create(session, src, args, argc);
                double current = src->pull_fn(src->name, args, argc, src->userdata);
                if (!ring) {
                    engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY,
                                   "engine: failed to allocate pull source argument ring");
                    return false;
                }
                engine_arg_ring_append(ring, current, session->cursor);
                v = engine_arg_ring_read(ring, offset);
                break;
            }
            if (argc == 0u && (offset > 0u || src->ring)) {
                v = engine_ring_read(src, offset);
            } else if (src->pull_fn) {
                if (argc == 0u) {
                    v = src->pull_fn(src->name, NULL, 0, src->userdata);
                } else {
                    v = src->pull_fn(src->name, args, argc, src->userdata);
                }
            }
            break;
        case ENGINE_SRC_VIEW:
            if (src->view_fn && cursor >= 0) {
                double out = 0.0;
                int64_t source_index = cursor;
                if (src->map_index_fn) source_index = src->map_index_fn(cursor, src->userdata);
                if (source_index >= 0 &&
                    src->view_fn(source_index, src->name, args, argc, &out, src->userdata)) {
                    v = out;
                }
            }
            break;
        case ENGINE_SRC_COLUMN:
            if (argc == 0u && src->base && cursor >= 0 && (size_t)cursor < src->count) {
                v = *(const double*)((const char*)src->base + (size_t)cursor * src->stride);
            }
            break;
    }
    *out = v;
    engine_source_memo_set(session, src, args, argc, offset, v);
    return true;
}

static bool engine_hydrate_sources_at_offset(cxpr_engine_session* s,
                                             cxpr_context* ctx,
                                             size_t offset) {
    size_t i;
    if (!s || !ctx) return false;
    for (i = 0; i < s->source_count; ++i) {
        engine_source* src = &s->sources[i];
        double v;
        if (!src->hydrate_bare) continue;
        if (!engine_resolve_source_call(s, src, NULL, 0u, offset, &v, NULL)) {
            v = NAN;
        }
        cxpr_context_set(ctx, src->name, v);
    }
    return true;
}

static bool engine_eval_inline_lookback(cxpr_engine_session* s,
                                        const cxpr_expr_ast* target,
                                        size_t offset,
                                        const cxpr_context* ctx,
                                        const cxpr_registry* reg,
                                        cxpr_value* out,
                                        cxpr_error* err) {
    cxpr_context* shifted;
    size_t saved_offset = g_engine_tls_lookback_offset;
    cxpr_value value = cxpr_num(NAN);
    bool ok;

    shifted = cxpr_context_clone(ctx);
    if (!shifted) {
        engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY, "engine: out of memory evaluating inline lookback");
        return true;
    }
    g_engine_tls_lookback_offset = saved_offset + offset;
    cxpr_context_set(shifted, CXPR_ENGINE_LOOKBACK_OFFSET_KEY,
                     (double)g_engine_tls_lookback_offset);
    engine_hydrate_sources_at_offset(s, shifted, g_engine_tls_lookback_offset);
    ok = cxpr_eval_ast(target, shifted, reg, &value, err);
    cxpr_context_free(shifted);
    g_engine_tls_lookback_offset = saved_offset;
    if (ok) *out = value;
    return true;
}

static cxpr_value engine_source_call(const cxpr_expr_ast* call_ast,
                                     const cxpr_context* ctx,
                                     const cxpr_registry* reg,
                                     void* userdata,
                                     cxpr_error* err) {
    cxpr_engine_session* s = g_engine_tls_session;
    const char* name = (const char*)userdata;
    engine_source* src;
    double args[CXPR_MAX_CALL_ARGS] = {0};
    size_t argc = 0;
    double value = NAN;

    if (!s || !name) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: source call outside active tick");
        return cxpr_num(NAN);
    }
    src = engine_find_source_in(s->sources, s->source_count, name);
    if (!src) {
        engine_set_err(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "engine: unknown source call");
        return cxpr_num(NAN);
    }
    if (!engine_eval_call_args(call_ast, ctx, reg, args, &argc, err)) return cxpr_num(NAN);
    if (!engine_resolve_source_call(s, src, args, argc, g_engine_tls_lookback_offset, &value, err)) {
        return cxpr_num(NAN);
    }
    return cxpr_num(value);
}

/* Lookback resolver for `target[n]` (D16). The engine serves the lookbacks it
 * owns — sources (column/view via cursor offset, pull via ring) and
 * named-expression result-rings. For anything else, a host-supplied inline
 * policy may opt offset-aware targets into engine re-evaluation; remaining
 * targets delegate to the resolver a host installed before the engine. This
 * keeps cxpr domain-neutral while allowing hosts to migrate lookback piecewise. */
static bool engine_lookback_resolver(const cxpr_expr_ast* target, const cxpr_expr_ast* index,
                                     const cxpr_context* ctx, const cxpr_registry* reg,
                                     void* userdata, cxpr_value* out, cxpr_error* err) {
    cxpr_engine_session* s = g_engine_tls_session;
    const char* name;
    engine_source* src;
    double nd, v = NAN;
    size_t n;
    int target_type;

    (void)userdata;
    if (!out) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: missing lookback output");
        return false;
    }
    if (!s) {
        cxpr_lookback_resolver_ptr delegate = NULL;
        void* delegate_ud = NULL;
        if (engine_registry_resolver_delegate(reg, &delegate, &delegate_ud)) {
            return delegate(target, index, ctx, reg, delegate_ud, out, err);
        }
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: no active session for lookback");
        return false;
    }
    if (!index || cxpr_expr_ast_kind_of(index) != CXPR_NODE_NUMBER) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: non-literal lookback index unsupported");
        return false;
    }
    nd = cxpr_expr_ast_number_value(index);
    if (nd < 0.0) { engine_set_err(err, CXPR_ERR_SYNTAX, "engine: negative lookback"); return false; }
    n = (size_t)nd;

    if (!target) return false;
    target_type = cxpr_expr_ast_kind_of(target);

    /* Engine-owned: a source bound as a function call, e.g. source(id)[2].
     * Column sources are excluded: `name(args)` on a column may be a host
     * function of the same name, so it must delegate rather than coerce
     * non-numeric host arguments to doubles. */
    if (target_type == CXPR_NODE_FUNCTION_CALL) {
        double args[CXPR_MAX_CALL_ARGS] = {0};
        size_t argc = 0;
        name = cxpr_expr_ast_call_name(target);
        src = engine_find_source_in(s->sources, s->source_count, name);
        if (src && src->kind != ENGINE_SRC_COLUMN) {
            if (!engine_eval_call_args(target, ctx, reg, args, &argc, err)) return true;
            if (!engine_resolve_source_call(
                    s, src, args, argc, g_engine_tls_lookback_offset + n, &v, err)) {
                return true;
            }
            *out = cxpr_num(v);
            return true;
        }
        if (src && src->kind == ENGINE_SRC_COLUMN) {
            return engine_eval_inline_lookback(
                s, target, n, ctx, reg, out, err);
        }
    } else if (target_type == CXPR_NODE_IDENTIFIER) {
        name = cxpr_expr_ast_identifier_name(target);
        src = engine_find_source_in(s->sources, s->source_count, name);
        if (src) {
            if (!engine_resolve_source_call(
                    s, src, NULL, 0u, g_engine_tls_lookback_offset + n, &v, err)) {
                return true;
            }
            *out = cxpr_num(v); /* warmup / OOB -> NaN, propagated (D18) */
            return true;
        }
        {
            /* Named-expression result ring (D16). */
            int ti = engine_tracked_index(s->prog, name);
            if (ti >= 0 && (size_t)ti < s->expr_ring_count) {
                cxpr_value value;
                bool found = engine_tracked_expression_value(s, ti, n, &value);
                *out = found ? value : cxpr_num(NAN);
                return true;
            }
        }
    } else if (target_type == CXPR_NODE_CHAIN_ACCESS &&
               cxpr_expr_ast_chain_count(target) >= 2u) {
        const char* root = cxpr_expr_ast_chain_segment(target, 0u);
        int ti = engine_tracked_index(s->prog, root);
        if (ti >= 0 && (size_t)ti < s->expr_ring_count) {
            cxpr_value value;
            bool found = engine_tracked_expression_value(s, ti, n, &value);
            if (!found) {
                *out = cxpr_num(NAN);
                return true;
            }
            if (!engine_struct_path_value(value, target, 1u, out)) {
                engine_set_err(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                               "engine: unknown tracked expression field path");
                return true;
            }
            return true;
        }
    } else if (target_type == CXPR_NODE_FIELD_ACCESS) {
        const char* root = cxpr_expr_ast_field_object(target);
        const char* field = cxpr_expr_ast_field_name(target);
        int ti = engine_tracked_index(s->prog, root);
        if (ti >= 0 && (size_t)ti < s->expr_ring_count) {
            cxpr_value value;
            bool found = engine_tracked_expression_value(s, ti, n, &value);
            if (!found) {
                *out = cxpr_num(NAN);
                return true;
            }
            if (!engine_struct_field_value(value, field, out)) {
                engine_set_err(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                               "engine: unknown tracked expression field");
                return true;
            }
            return true;
        }
    }

    /* Not engine-owned: hand off to the host's prior resolver if one exists. */
    if (s->prog->delegate_lookback_fn) {
        if (target_type != CXPR_NODE_IDENTIFIER &&
            s->prog->inline_lookback_fn &&
            s->prog->inline_lookback_fn(target, s->prog->inline_lookback_ud)) {
            return engine_eval_inline_lookback(
                s, target, n, ctx, reg, out, err);
        }
        return s->prog->delegate_lookback_fn(target, index, ctx, reg,
                                             s->prog->delegate_lookback_ud, out, err);
    }

    if (target_type != CXPR_NODE_IDENTIFIER) {
        /* Inline anonymous subexpression: re-evaluate with sources offset by n. */
        return engine_eval_inline_lookback(
            s, target, n, ctx, reg, out, err);
    }

    engine_set_err(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                   "engine: lookback target is neither a source nor a tracked expression");
    return false;
}

/* -------------------------------------------------------------------------- */
/* Program build / free                                                        */
/* -------------------------------------------------------------------------- */

static void engine_program_free_internals(cxpr_engine_program* prog) {
    size_t i;
    if (!prog) return;
    for (i = 0; i < prog->expr_count; ++i) {
        free((char*)prog->exprs[i].name);
        free((char*)prog->exprs[i].expression);
    }
    free(prog->exprs);
    for (i = 0; i < prog->source_count; ++i) free(prog->sources[i].name);
    free(prog->sources);
    for (i = 0; i < prog->watch_count; ++i) free(prog->watches[i].expr_name);
    free(prog->watches);
    for (i = 0; i < prog->param_count; ++i) free((char*)prog->params[i].name);
    free(prog->params);
    for (i = 0; i < prog->external_param_count; ++i) free(prog->external_params[i]);
    free(prog->external_params);
    for (i = 0; i < prog->role_count; ++i) {
        free(prog->roles[i].name);
        free(prog->roles[i].members);
    }
    free(prog->roles);
    for (i = 0; i < prog->tracked_count; ++i) free(prog->tracked[i].name);
    free(prog->tracked);
    if (prog->owns_registry) {
        cxpr_registry_free((cxpr_registry*)prog->registry);
    } else {
        engine_registry_resolver_release(prog);
    }
}

/* Build a fresh compiled evaluator from the program's expression set. Each
 * session owns one (the evaluator stores per-run results), so this runs per
 * session; program_new also calls it once to validate. */
static cxpr_evaluator* engine_build_evaluator(const cxpr_engine_program* prog,
                                              cxpr_error* err) {
    cxpr_evaluator* eval = cxpr_evaluator_new(prog->registry);
    if (!eval) {
        engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY, "engine: evaluator allocation failed");
        return NULL;
    }
    if (prog->expr_count > 0) {
        if (!cxpr_expressions_add(eval, prog->exprs, prog->expr_count, err)) {
            cxpr_evaluator_free(eval);
            return NULL;
        }
    }
    if (!cxpr_evaluator_compile(eval, err)) {
        cxpr_evaluator_free(eval);
        return NULL;
    }
    return eval;
}

cxpr_engine_program* cxpr_engine_program_new(const cxpr_engine_config* config,
                                             cxpr_error* err) {
    cxpr_engine_program* prog;
    cxpr_evaluator* validate;
    size_t i;

    if (!config) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: NULL config");
        return NULL;
    }

    prog = (cxpr_engine_program*)calloc(1, sizeof(*prog));
    if (!prog) {
        engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY, "engine: program allocation failed");
        return NULL;
    }

    /* Registry: dependency-injected, or engine-owned default (D19). */
    if (config->registry) {
        prog->registry = config->registry;
        prog->owns_registry = false;
    } else {
        cxpr_registry* reg = cxpr_registry_new();
        if (!reg) {
            engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY, "engine: registry allocation failed");
            free(prog);
            return NULL;
        }
        cxpr_register_defaults(reg);
        cxpr_register_basket_builtins(reg);
        prog->registry = reg;
        prog->owns_registry = true;
    }
    prog->inline_lookback_fn = config->inline_lookback;
    prog->inline_lookback_ud = config->inline_lookback_userdata;

    /* Copy expressions. */
    if (config->expression_count > 0) {
        prog->exprs = (cxpr_expression_def*)calloc(config->expression_count, sizeof(*prog->exprs));
        if (!prog->exprs) goto oom;
        for (i = 0; i < config->expression_count; ++i) {
            prog->exprs[i].name = engine_strdup(config->expressions[i].name);
            prog->exprs[i].expression = engine_strdup(config->expressions[i].expression);
            if (!prog->exprs[i].name || !prog->exprs[i].expression) {
                prog->expr_count = i + 1; /* free what we have */
                goto oom;
            }
        }
        prog->expr_count = config->expression_count;
    }

    /* Copy source declarations + default bindings. */
    {
        size_t total = config->pull_source_count + config->view_source_count +
                       config->column_source_count;
        if (total > 0) {
            size_t k = 0;
            prog->sources = (engine_source*)calloc(total, sizeof(*prog->sources));
            if (!prog->sources) goto oom;
            for (i = 0; i < config->pull_source_count; ++i, ++k) {
                prog->sources[k].kind = ENGINE_SRC_PULL;
                prog->sources[k].name = engine_strdup(config->pull_sources[i].name);
                prog->sources[k].pull_fn = config->pull_sources[i].fn;
                prog->sources[k].userdata = config->pull_sources[i].userdata;
                if (!prog->sources[k].name) { prog->source_count = k + 1; goto oom; }
            }
            for (i = 0; i < config->view_source_count; ++i, ++k) {
                prog->sources[k].kind = ENGINE_SRC_VIEW;
                prog->sources[k].name = engine_strdup(config->view_sources[i].name);
                prog->sources[k].view_fn = config->view_sources[i].fn;
                prog->sources[k].map_index_fn = config->view_sources[i].map_index;
                prog->sources[k].userdata = config->view_sources[i].userdata;
                if (!prog->sources[k].name) { prog->source_count = k + 1; goto oom; }
            }
            for (i = 0; i < config->column_source_count; ++i, ++k) {
                prog->sources[k].kind = ENGINE_SRC_COLUMN;
                prog->sources[k].name = engine_strdup(config->column_sources[i].name);
                prog->sources[k].base = config->column_sources[i].base;
                prog->sources[k].stride = config->column_sources[i].stride;
                prog->sources[k].count = config->column_sources[i].count;
                if (!prog->sources[k].name) { prog->source_count = k + 1; goto oom; }
            }
            prog->source_count = total;
        }
    }

    /* Source names can also be called with numeric args, e.g. `metric($item)`.
     * The handler reads the active session via TLS so bindings remain per-session.
     * Column sources are scalar per-step values with no numeric-arg call semantics
     * (engine_resolve_source_call yields NaN for argc>0), so skip them: registering
     * a column as a callable would clobber a host function of the same name on a
     * shared registry, and that host function may accept non-numeric arguments. */
    for (i = 0; i < prog->source_count; ++i) {
        if (prog->sources[i].kind == ENGINE_SRC_COLUMN) continue;
        cxpr_registry_add_ast(
            (cxpr_registry*)prog->registry,
            prog->sources[i].name,
            engine_source_call,
            0,
            CXPR_MAX_CALL_ARGS,
            CXPR_VALUE_NUMBER,
            prog->sources[i].name,
            NULL);
    }

    /* Copy watches. */
    if (config->watch_count > 0) {
        prog->watches = (engine_watch*)calloc(config->watch_count, sizeof(*prog->watches));
        if (!prog->watches) goto oom;
        for (i = 0; i < config->watch_count; ++i) {
            prog->watches[i].expr_name = engine_strdup(config->watches[i].expr_name);
            prog->watches[i].edge = config->watches[i].edge;
            if (!prog->watches[i].expr_name) { prog->watch_count = i + 1; goto oom; }
        }
        prog->watch_count = config->watch_count;
    }

    /* Copy param defaults. */
    if (config->param_count > 0) {
        prog->params = (cxpr_context_entry*)calloc(config->param_count, sizeof(*prog->params));
        if (!prog->params) goto oom;
        for (i = 0; i < config->param_count; ++i) {
            prog->params[i].name = engine_strdup(config->params[i].name);
            prog->params[i].value = config->params[i].value;
            if (!prog->params[i].name) { prog->param_count = i + 1; goto oom; }
        }
        prog->param_count = config->param_count;
    }

    /* Copy role defaults. */
    if (config->role_count > 0) {
        prog->roles = (engine_role*)calloc(config->role_count, sizeof(*prog->roles));
        if (!prog->roles) goto oom;
        for (i = 0; i < config->role_count; ++i) {
            size_t mc = config->roles[i].member_count;
            prog->roles[i].name = engine_strdup(config->roles[i].name);
            prog->roles[i].count = mc;
            prog->roles[i].bound_count = config->roles[i].bound_count;
            if (mc > 0) {
                prog->roles[i].members = (double*)malloc(mc * sizeof(double));
                if (!prog->roles[i].members) { prog->role_count = i + 1; goto oom; }
                memcpy(prog->roles[i].members, config->roles[i].members, mc * sizeof(double));
            }
            if (!prog->roles[i].name) { prog->role_count = i + 1; goto oom; }
        }
        prog->role_count = config->role_count;
    }

    /* D15: a watch may target a registry-defined expression (`name() => ...`).
     * Synthesize an evaluator entry `{name, "name()"}` for each such watch so it
     * is evaluated and retrievable, and validate that every watch resolves to a
     * config or registry expression. */
    {
        bool synthesized_any = false;
        for (i = 0; i < prog->watch_count; ++i) {
            const char* wn = prog->watches[i].expr_name;
            size_t min_args = 0, max_args = 0;
            char body[256];
            cxpr_expression_def* grown;
            if (engine_is_expr_name(prog, wn)) continue; /* config or already synthesized */
            if (!cxpr_registry_lookup(prog->registry, wn, &min_args, &max_args) || min_args != 0) {
                engine_set_err(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                               "engine: watch names an expression that is neither in the config "
                               "nor a nullary registry-defined expression");
                engine_program_free_internals(prog);
                free(prog);
                return NULL;
            }
            snprintf(body, sizeof(body), "%s()", wn);
            grown = (cxpr_expression_def*)realloc(prog->exprs,
                                                  (prog->expr_count + 1u) * sizeof(*grown));
            if (!grown) goto oom;
            prog->exprs = grown;
            prog->exprs[prog->expr_count].name = engine_strdup(wn);
            prog->exprs[prog->expr_count].expression = engine_strdup(body);
            if (!prog->exprs[prog->expr_count].name || !prog->exprs[prog->expr_count].expression) {
                prog->expr_count++;
                goto oom;
            }
            prog->expr_count++;
            synthesized_any = true;
        }
        /* Registry expression bodies are opaque to the engine's AST scan, so it
         * cannot tell which sources they read. Conservatively hydrate all declared
         * sources when any registry expression is in play (lazy hydration applies
         * only when every expression is config-visible). */
        if (synthesized_any) {
            for (i = 0; i < prog->source_count; ++i) {
                prog->sources[i].referenced = true;
                prog->sources[i].hydrate_bare = true;
            }
        }
    }

    for (i = 0; i < prog->watch_count; ++i) {
        const cxpr_expression_def* def = engine_find_expr_def(prog, prog->watches[i].expr_name);
        cxpr_expr_parser* parser;
        cxpr_expr_ast* ast;

        if (!def) continue;
        parser = cxpr_expr_parser_new();
        if (!parser) goto oom;
        ast = cxpr_expr_ast_parse(parser, def->expression, err);
        cxpr_expr_parser_free(parser);
        if (!ast) {
            engine_program_free_internals(prog);
            free(prog);
            return NULL;
        }
        if (!cxpr_typecheck_bool_root(ast, prog->registry, err)) {
            cxpr_expr_ast_free(ast);
            engine_program_free_internals(prog);
            free(prog);
            return NULL;
        }
        cxpr_expr_ast_free(ast);
    }

    /* Discover referenced sources + per-source lookback depth (D5/D16). */
    {
        cxpr_expr_parser* parser = cxpr_expr_parser_new();
        if (parser) {
            for (i = 0; i < prog->expr_count; ++i) {
                cxpr_error perr = {0};
                cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, prog->exprs[i].expression, &perr);
                if (ast) {
                    const char* params[256];
                    size_t param_count;
                    size_t pi;
                    engine_scan_ast(ast, prog);
                    param_count = cxpr_expr_ast_variables_used(
                        ast, params, sizeof(params) / sizeof(params[0]));
                    for (pi = 0u;
                         pi < param_count && pi < sizeof(params) / sizeof(params[0]);
                         ++pi) {
                        if (!engine_track_external_param(prog, params[pi])) {
                            cxpr_expr_ast_free(ast);
                            cxpr_expr_parser_free(parser);
                            goto oom;
                        }
                    }
                    cxpr_expr_ast_free(ast);
                }
            }
            cxpr_expr_parser_free(parser);
        }
    }

    /* Install the engine's lookback resolver (D7/D16). Injected registries can
     * be shared by several engine programs, so refcount the install and restore
     * the host resolver only when the last program is freed. */
    if (prog->owns_registry) {
        cxpr_registry_set_lookback_resolver((cxpr_registry*)prog->registry,
                                            engine_lookback_resolver, NULL, NULL);
    } else if (!engine_registry_resolver_acquire(prog)) {
        engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY,
                       "engine: failed to install lookback resolver");
        goto oom;
    }

    /* Validate by compiling once; surfaces parse/dependency errors early. */
    validate = engine_build_evaluator(prog, err);
    if (!validate) {
        engine_program_free_internals(prog);
        free(prog);
        return NULL;
    }
    cxpr_evaluator_free(validate);

    if (err) err->code = CXPR_OK;
    return prog;

oom:
    engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY, "engine: out of memory building program");
    engine_program_free_internals(prog);
    free(prog);
    return NULL;
}

void cxpr_engine_program_free(cxpr_engine_program* prog) {
    if (!prog) return;
    engine_program_free_internals(prog);
    free(prog);
}

/* -------------------------------------------------------------------------- */
/* Session lifecycle                                                           */
/* -------------------------------------------------------------------------- */

cxpr_engine_session* cxpr_engine_session_new(const cxpr_engine_program* prog) {
    cxpr_engine_session* s;
    cxpr_error err = {0};
    size_t i;

    if (!prog) return NULL;

    s = (cxpr_engine_session*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->prog = prog;
    s->cursor = -1;

    s->eval = engine_build_evaluator(prog, &err);
    if (!s->eval) goto fail;

    s->ctx = cxpr_context_new();
    if (!s->ctx) goto fail;

    /* Per-session source copy (bindings overridable; names borrowed). */
    if (prog->source_count > 0) {
        s->sources = (engine_source*)malloc(prog->source_count * sizeof(*s->sources));
        if (!s->sources) goto fail;
        memcpy(s->sources, prog->sources, prog->source_count * sizeof(*s->sources));
        s->source_count = prog->source_count;
        /* Allocate pull rings for referenced pull sources that need lookback (D7). */
        for (i = 0; i < s->source_count; ++i) {
            engine_source* src = &s->sources[i];
            src->ring = NULL;
            src->ring_cap = src->ring_head = src->ring_count = 0;
            src->slot_bound = false;
            if (src->kind == ENGINE_SRC_PULL && src->referenced && src->max_lookback > 0) {
                src->ring_cap = src->max_lookback + 1u;
                src->ring = (double*)malloc(src->ring_cap * sizeof(double));
                if (!src->ring) goto fail;
            }
        }
    }

    /* Per-session expression result rings (D16), sized to each tracked depth. */
    if (prog->tracked_count > 0) {
        s->expr_rings = (engine_ring*)calloc(prog->tracked_count, sizeof(*s->expr_rings));
        if (!s->expr_rings) goto fail;
        s->expr_ring_count = prog->tracked_count;
        for (i = 0; i < prog->tracked_count; ++i) {
            size_t cap = prog->tracked[i].max_depth + 1u;
            s->expr_rings[i].buf = (cxpr_value*)calloc(cap, sizeof(cxpr_value));
            if (!s->expr_rings[i].buf) goto fail;
            s->expr_rings[i].cap = cap;
        }
    }

    /* Watch transition state. */
    if (prog->watch_count > 0) {
        s->prev_truthy = (bool*)calloc(prog->watch_count, sizeof(bool));
        s->prev_value = (double*)calloc(prog->watch_count, sizeof(double));
        s->prev_valid = (bool*)calloc(prog->watch_count, sizeof(bool));
        if (!s->prev_truthy || !s->prev_value || !s->prev_valid) goto fail;
        s->events = (cxpr_engine_event*)malloc(prog->watch_count * sizeof(*s->events));
        if (!s->events) goto fail;
        s->event_cap = prog->watch_count;
    }

    /* Heuristic: ~16 distinct (args, offset) keys per source plus slack. Generous
     * for scalar sources; a large basket (members × call-sites × offsets) can still
     * exceed it, in which case engine_source_memo_set degrades gracefully (see there).
     * Revisit the sizing — and a hashed lookup over the current linear scan — if
     * real workloads push past this. */
    s->source_memo_cap = (prog->source_count * 16u) + 32u;
    if (s->source_memo_cap > 0u) {
        s->source_memo = (engine_source_memo_entry*)calloc(
            s->source_memo_cap,
            sizeof(*s->source_memo));
        if (!s->source_memo) goto fail;
    }

    /* Seed param + role defaults (D12/D25). */
    for (i = 0; i < prog->param_count; ++i) {
        cxpr_context_set_param(s->ctx, prog->params[i].name, prog->params[i].value);
    }
    for (i = 0; i < prog->role_count; ++i) {
        engine_seed_role(s->ctx, prog->roles[i].name, prog->roles[i].members,
                         prog->roles[i].count, prog->roles[i].bound_count);
    }

    return s;

fail:
    cxpr_engine_session_free(s);
    return NULL;
}

cxpr_engine_session* cxpr_engine_session_create(const cxpr_engine_config* config,
                                                cxpr_error* err) {
    cxpr_engine_program* prog = cxpr_engine_program_new(config, err);
    cxpr_engine_session* s;
    if (!prog) return NULL;
    s = cxpr_engine_session_new(prog);
    if (!s) {
        engine_set_err(err, CXPR_ERR_OUT_OF_MEMORY, "engine: session allocation failed");
        cxpr_engine_program_free(prog);
        return NULL;
    }
    s->owned_prog = prog; /* session owns it; freed in session_free */
    return s;
}

void cxpr_engine_session_free(cxpr_engine_session* session) {
    size_t i;
    if (!session) return;
    if (session->eval) cxpr_evaluator_free(session->eval);
    if (session->ctx) cxpr_context_free(session->ctx);
    for (i = 0; i < session->source_count; ++i) free(session->sources[i].ring);
    free(session->sources);
    for (i = 0; i < session->expr_ring_count; ++i) {
        size_t j;
        for (j = 0; j < session->expr_rings[i].cap; ++j) {
            cxpr_value_free(&session->expr_rings[i].buf[j]);
        }
        free(session->expr_rings[i].buf);
    }
    free(session->expr_rings);
    free(session->prev_truthy);
    free(session->prev_value);
    free(session->prev_valid);
    free(session->events);
    free(session->source_memo);
    for (i = 0; i < session->arg_ring_count; ++i) free(session->arg_rings[i].ring);
    free(session->arg_rings);
    if (session->owned_prog) cxpr_engine_program_free(session->owned_prog);
    free(session);
}

void cxpr_engine_session_reset(cxpr_engine_session* session) {
    size_t i;
    if (!session) return;
    session->cursor = -1;
    for (i = 0; i < session->prog->watch_count; ++i) {
        session->prev_truthy[i] = false;
        session->prev_value[i] = 0.0;
        session->prev_valid[i] = false;
    }
    for (i = 0; i < session->source_count; ++i) {
        session->sources[i].ring_head = 0;
        session->sources[i].ring_count = 0;
        session->sources[i].slot_bound = false; /* rebind lazily next tick */
    }
    for (i = 0; i < session->expr_ring_count; ++i) {
        size_t j;
        for (j = 0; j < session->expr_rings[i].cap; ++j) {
            cxpr_value_free(&session->expr_rings[i].buf[j]);
        }
        session->expr_rings[i].head = 0;
        session->expr_rings[i].count = 0;
    }
    for (i = 0; i < session->arg_ring_count; ++i) {
        session->arg_rings[i].ring_head = 0u;
        session->arg_rings[i].ring_count = 0u;
        session->arg_rings[i].last_append_cursor = -1;
    }
    session->source_memo_count = 0u;
    cxpr_context_clear_cached_structs(session->ctx);
    /* Params, role structs and source bindings are retained (D12/D22). */
}

cxpr_context* cxpr_engine_session_context(cxpr_engine_session* session) {
    return session ? session->ctx : NULL;
}

/* -------------------------------------------------------------------------- */
/* Per-session setters                                                         */
/* -------------------------------------------------------------------------- */

void cxpr_engine_set_param(cxpr_engine_session* session, const char* name, double value) {
    if (!session || !name) return;
    cxpr_context_set_param(session->ctx, name, value);
}

void cxpr_engine_set_param_value(cxpr_engine_session* session, const char* name,
                                 const cxpr_value* value) {
    if (!session || !name || !value) return;
    cxpr_context_set_param_value(session->ctx, name, value);
}

static engine_source* engine_find_source(cxpr_engine_session* session, const char* name) {
    size_t i;
    if (!session || !name) return NULL;
    for (i = 0; i < session->source_count; ++i) {
        if (session->sources[i].name && strcmp(session->sources[i].name, name) == 0) {
            return &session->sources[i];
        }
    }
    return NULL;
}

bool cxpr_engine_bind_column(cxpr_engine_session* session, const char* name,
                             const void* base, size_t count) {
    engine_source* s = engine_find_source(session, name);
    if (!s || s->kind != ENGINE_SRC_COLUMN) return false;
    s->base = base;
    s->count = count;
    return true;
}

bool cxpr_engine_bind_userdata(cxpr_engine_session* session, const char* name,
                               void* userdata) {
    engine_source* s = engine_find_source(session, name);
    if (!s || (s->kind != ENGINE_SRC_PULL && s->kind != ENGINE_SRC_VIEW)) return false;
    s->userdata = userdata;
    return true;
}

bool cxpr_engine_set_role(cxpr_engine_session* session, const char* name,
                          const double* members, size_t count) {
    if (!session || !name) return false;
    return engine_seed_role(session->ctx, name, members, count, 0u);
}

/* -------------------------------------------------------------------------- */
/* Tick + results                                                              */
/* -------------------------------------------------------------------------- */

static double engine_resolve_current_source(cxpr_engine_session* session, engine_source* s) {
    double value = NAN;
    if (!s) return NAN;
    if (s->kind == ENGINE_SRC_PULL && s->pull_fn) {
        return s->pull_fn(s->name, NULL, 0, s->userdata);
    }
    if (!engine_resolve_source_call(session, s, NULL, 0u, 0u, &value, NULL)) return NAN;
    return value;
}

bool cxpr_engine_tick(cxpr_engine_session* session,
                      const cxpr_engine_event** out_events, size_t* out_count,
                      cxpr_error* err) {
    cxpr_error eval_err = {0};
    size_t i, n = 0;
    const cxpr_engine_program* prog;

    if (out_events) *out_events = NULL;
    if (out_count) *out_count = 0;
    if (!session) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: NULL session");
        return false;
    }
    prog = session->prog;

    session->cursor++;
    session->source_memo_count = 0u;

    /* Hydrate only referenced sources for this tick (D5); pull sources also feed
     * their lookback ring (D7). Current value goes into the context for bare reads;
     * lagged reads are served by the lookback resolver. */
    for (i = 0; i < session->source_count; ++i) {
        engine_source* src = &session->sources[i];
        double v;
        if (!src->hydrate_bare && !(src->kind == ENGINE_SRC_PULL && src->ring)) continue;
        v = engine_resolve_current_source(session, src);
        if (src->kind == ENGINE_SRC_PULL && src->ring) engine_ring_append(src, v);
        /* Hot path: write through a pre-bound slot (no per-tick name hash); fall
         * back to a keyed set on the first write or after a rehash. */
        if (src->slot_bound && cxpr_context_slot_valid(session->ctx, &src->slot)) {
            cxpr_context_slot_set(&src->slot, v);
        } else {
            cxpr_context_set(session->ctx, src->name, v);
            src->slot_bound = cxpr_context_slot_bind(session->ctx, src->name, &src->slot);
        }
    }

    /* Reserve this tick's slot (depth 0) in each expression result ring so that
     * `expr[n]` reads depth n uniformly with sources; filled after eval (D16). */
    for (i = 0; i < session->expr_ring_count; ++i) {
        engine_ringb_append(&session->expr_rings[i], cxpr_num(NAN));
    }

    /* Evaluate the rule set. Data misses are NaN and do not abort (D18).
     * The thread-local session lets the lookback resolver reach ring/cursor state. */
    g_engine_tls_session = session;
    g_engine_tls_lookback_offset = 0u;
    cxpr_evaluator_eval(session->eval, session->ctx, &eval_err);
    g_engine_tls_lookback_offset = 0u;
    g_engine_tls_session = NULL;
    if (eval_err.code != CXPR_OK) {
        if (err) *err = eval_err;
        return false;
    }

    /* Fill each tracked expression's current result into its reserved slot. */
    for (i = 0; i < session->expr_ring_count; ++i) {
        bool f = false;
        cxpr_value v = cxpr_expression_get(session->eval, prog->tracked[i].name, &f);
        engine_ring* r = &session->expr_rings[i];
        if (r->buf) {
            cxpr_value_free(&r->buf[r->head]);
            r->buf[r->head] = f ? cxpr_value_clone(&v) : cxpr_num(NAN);
        }
    }

    /* Edge detection over watches (D9/D11). */
    for (i = 0; i < prog->watch_count; ++i) {
        const engine_watch* w = &prog->watches[i];
        bool found = false;
        cxpr_value val = cxpr_expression_get(session->eval, w->expr_name, &found);
        bool truthy = false;
        double num = found ? engine_value_to_double(val) : NAN;
        bool fire = false;

        if (found) {
            if (val.type != CXPR_VALUE_BOOL) {
                engine_set_err(err, CXPR_ERR_TYPE_MISMATCH,
                               "engine watch expression must evaluate to bool");
                return false;
            }
            truthy = val.b;
        }

        switch (w->edge) {
            case CXPR_EDGE_RISING:
                fire = truthy && !session->prev_truthy[i];
                break;
            case CXPR_EDGE_FALLING:
                fire = !truthy && session->prev_truthy[i];
                break;
            case CXPR_EDGE_LEVEL:
                fire = truthy;
                break;
            case CXPR_EDGE_CHANGED:
                if (session->prev_valid[i]) {
                    double prev = session->prev_value[i];
                    bool both_nan = isnan(num) && isnan(prev);
                    fire = !both_nan && (num != prev);
                }
                break;
        }

        if (fire && n < session->event_cap) {
            session->events[n].expr_name = w->expr_name;
            session->events[n].edge = w->edge;
            session->events[n].value = found ? val : cxpr_num(NAN);
            ++n;
        }

        session->prev_truthy[i] = truthy;
        session->prev_value[i] = num;
        session->prev_valid[i] = true;
    }

    if (out_events) *out_events = session->events;
    if (out_count) *out_count = n;
    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_engine_tick_at(cxpr_engine_session* session,
                         int64_t index,
                         const cxpr_engine_event** out_events,
                         size_t* out_count,
                         cxpr_error* err) {
    if (!session) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: NULL session");
        return false;
    }
    if (index < 0) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: negative tick index");
        return false;
    }
    session->cursor = index - 1;
    return cxpr_engine_tick(session, out_events, out_count, err);
}

bool cxpr_engine_tick_fallback(cxpr_engine_session* session,
                               const cxpr_context* parent_ctx,
                               const cxpr_engine_event** out_events,
                               size_t* out_count,
                               cxpr_error* err) {
    const cxpr_context* previous_parent;
    size_t i;
    bool ok;

    if (!session) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: NULL session");
        return false;
    }
    if (!parent_ctx) return cxpr_engine_tick(session, out_events, out_count, err);

    for (i = 0u; i < session->prog->external_param_count; ++i) {
        bool found = false;
        const char* name = session->prog->external_params[i];
        cxpr_value value = cxpr_context_get_param_typed(parent_ctx, name, &found);
        if (found) cxpr_context_set_param_value(session->ctx, name, &value);
    }
    previous_parent = session->ctx->parent;
    session->ctx->parent = parent_ctx;
    ok = cxpr_engine_tick(session, out_events, out_count, err);
    session->ctx->parent = previous_parent;
    return ok;
}

bool cxpr_engine_tick_at_fallback(cxpr_engine_session* session,
                                  int64_t index,
                                  const cxpr_context* parent_ctx,
                                  const cxpr_engine_event** out_events,
                                  size_t* out_count,
                                  cxpr_error* err) {
    if (!session) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: NULL session");
        return false;
    }
    if (index < 0) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: negative tick index");
        return false;
    }
    session->cursor = index - 1;
    return cxpr_engine_tick_fallback(session, parent_ctx, out_events, out_count, err);
}

int64_t cxpr_engine_tick_index(const cxpr_engine_session* session) {
    return session ? session->cursor : -1;
}

cxpr_value cxpr_engine_get(const cxpr_engine_session* session, const char* name, bool* found) {
    if (found) *found = false;
    if (!session || !name) return cxpr_num(0.0);
    return cxpr_expression_get(session->eval, name, found);
}

double cxpr_engine_get_double(const cxpr_engine_session* session, const char* name, bool* found) {
    if (found) *found = false;
    if (!session || !name) return 0.0;
    return cxpr_expression_get_double(session->eval, name, found);
}

bool cxpr_engine_get_bool(const cxpr_engine_session* session, const char* name, bool* found) {
    if (found) *found = false;
    if (!session || !name) return false;
    return cxpr_expression_get_bool(session->eval, name, found);
}

size_t cxpr_engine_expression_instruction_count(const cxpr_engine_session* session,
                                                const char* name,
                                                bool* found) {
    if (found) *found = false;
    if (!session || !name) return 0u;
    return cxpr_expression_instruction_count(session->eval, name, found);
}

size_t cxpr_engine_expression_dependency_instruction_count(
    const cxpr_engine_session* session,
    const char* name,
    bool* found) {
    if (found) *found = false;
    if (!session || !name) return 0u;
    return cxpr_expression_dependency_instruction_count(session->eval, name, found);
}

size_t cxpr_engine_expression_total_instruction_count(const cxpr_engine_session* session) {
    if (!session) return 0u;
    return cxpr_expression_total_instruction_count(session->eval);
}

bool cxpr_engine_snapshot_flow(const cxpr_engine_session* session,
                               cxpr_eval_snapshot_flow* out_flow,
                               cxpr_error* err) {
    cxpr_engine_session* previous_session;
    size_t previous_offset;
    bool ok;

    if (!out_flow) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "engine: snapshot output is NULL";
        }
        return false;
    }
    if (!session || !session->eval || !session->ctx) {
        memset(out_flow, 0, sizeof(*out_flow));
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_SYNTAX;
            err->message = "engine: snapshot requires a live session";
        }
        return false;
    }

    previous_session = g_engine_tls_session;
    previous_offset = g_engine_tls_lookback_offset;
    g_engine_tls_session = (cxpr_engine_session*)session;
    g_engine_tls_lookback_offset = 0u;
    ok = cxpr_eval_snapshot_build_flow(
        session->eval,
        session->ctx,
        session->prog ? session->prog->registry : NULL,
        out_flow,
        err);
    g_engine_tls_lookback_offset = previous_offset;
    g_engine_tls_session = previous_session;
    return ok;
}

bool cxpr_engine_snapshot_flow_fallback(const cxpr_engine_session* session,
                                        const cxpr_context* parent_ctx,
                                        cxpr_eval_snapshot_flow* out_flow,
                                        cxpr_error* err) {
    const cxpr_context* previous_parent;
    bool ok;

    if (!session || !session->ctx || !parent_ctx) {
        return cxpr_engine_snapshot_flow(session, out_flow, err);
    }
    previous_parent = session->ctx->parent;
    session->ctx->parent = parent_ctx;
    ok = cxpr_engine_snapshot_flow(session, out_flow, err);
    session->ctx->parent = previous_parent;
    return ok;
}
