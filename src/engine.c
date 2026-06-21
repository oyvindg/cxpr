/**
 * @file engine.c
 * @brief Stateful rule-engine layer on top of the cxpr evaluator.
 *
 * Implements cxpr/engine.h. See plans/cxpr_engine_layer_decisions.md (D1-D25).
 *
 * Increment 1 scope: program/session lifecycle (D14/D4), registry DI incl.
 * NULL-default (D19), config-seeded params (D12) and roles (D25), per-session
 * source binding (D22), source hydration via pull/view/column backings, watches
 * with edge detection and the borrowed event batch (D9/D11), and the result
 * readers. Engine-owned lookback rings / result-rings (D7/D16/D17) and the
 * lookback-resolver wiring are a later increment; until then expressions see the
 * current value of each referenced source.
 *
 * Built only on cxpr's public API so the layer stays decoupled from the kernel.
 */

#include <cxpr/engine.h>
#include <cxpr/cxpr.h>

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
    void* userdata;              /* PULL/VIEW binding (default; overridable per session) */
    const void* base;            /* COLUMN binding (default; overridable per session) */
    size_t stride;               /* COLUMN: structural, fixed */
    size_t count;                /* COLUMN binding (default; overridable per session) */

    /* Computed at program build (D5/D16). */
    bool referenced;             /* appears in the expression set (bare or via lookback) */
    size_t max_lookback;         /* deepest literal subscript on this source, 0 if none */

    /* Session-only pull ring (NULL in the program template). */
    double* ring;
    size_t ring_cap;             /* == max_lookback + 1 when allocated */
    size_t ring_head;            /* index of the newest sample */
    size_t ring_count;           /* samples held so far (<= ring_cap) */
} engine_source;

typedef struct {
    char* expr_name;
    cxpr_engine_edge edge;
} engine_watch;

typedef struct {
    char* name;
    double* members;
    size_t count;
} engine_role;

/* A named expression that is looked back (`expr[n]`); gets a per-session result
 * ring sized to max_depth (D16). */
typedef struct {
    char* name;
    size_t max_depth;
} engine_tracked_expr;

/* Simple circular buffer of doubles (newest at head). */
typedef struct {
    double* buf;
    size_t cap;
    size_t head;
    size_t count;
} engine_ring;

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

    engine_role* roles;
    size_t role_count;

    engine_tracked_expr* tracked; /* expressions referenced via lookback (D16) */
    size_t tracked_count;
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
};

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

static bool engine_value_truthy(cxpr_value v) {
    if (v.type == CXPR_VALUE_BOOL) return v.b;
    if (v.type == CXPR_VALUE_NUMBER) return v.d != 0.0 && !isnan(v.d);
    return false;
}

/* Build the basket role struct the cxpr basket builtins read (D25). Mirrors
 * dyn's binding: `__cxpr_basket_role_<name>` = { bound_count, value_count,
 * v0..v{n-1} }, plus `$name` bound directly when there is a single member. */
static bool engine_seed_role(cxpr_context* ctx, const char* name,
                             const double* members, size_t count) {
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
    fvals[0] = cxpr_num((double)count);
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
static void engine_ringb_append(engine_ring* r, double v) {
    if (!r->buf || r->cap == 0) return;
    r->head = (r->head + 1u) % r->cap;
    r->buf[r->head] = v;
    if (r->count < r->cap) r->count++;
}
static double engine_ringb_read(const engine_ring* r, size_t depth) {
    size_t idx;
    if (!r->buf || depth >= r->count) return NAN;
    idx = (r->head + r->cap - depth) % r->cap;
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

/* Walk an AST, marking referenced sources (and their deepest literal lookback)
 * and tracked expressions referenced via lookback. Sources are matched by bare
 * identifier name; lookback on an expression name records a tracked expr (D16). */
static void engine_scan_ast(const cxpr_ast* ast, cxpr_engine_program* prog) {
    size_t i;
    if (!ast) return;
    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_IDENTIFIER: {
            engine_source* s = engine_find_source_in(prog->sources, prog->source_count,
                                                     cxpr_ast_identifier_name(ast));
            if (s) s->referenced = true;
            break;
        }
        case CXPR_NODE_LOOKBACK: {
            const cxpr_ast* target = cxpr_ast_lookback_target(ast);
            const cxpr_ast* index = cxpr_ast_lookback_index(ast);
            if (target && cxpr_ast_type(target) == CXPR_NODE_IDENTIFIER &&
                index && cxpr_ast_type(index) == CXPR_NODE_NUMBER) {
                const char* tname = cxpr_ast_identifier_name(target);
                engine_source* s = engine_find_source_in(prog->sources, prog->source_count, tname);
                double nd = cxpr_ast_number_value(index);
                if (nd >= 0.0) {
                    size_t n = (size_t)nd;
                    if (s) {
                        s->referenced = true;
                        if (n > s->max_lookback) s->max_lookback = n;
                    } else if (engine_is_expr_name(prog, tname)) {
                        engine_track_expr(prog, tname, n); /* expr result-ring (D16) */
                    }
                }
            }
            engine_scan_ast(target, prog);
            engine_scan_ast(index, prog);
            break;
        }
        case CXPR_NODE_BINARY_OP:
            engine_scan_ast(cxpr_ast_left(ast), prog);
            engine_scan_ast(cxpr_ast_right(ast), prog);
            break;
        case CXPR_NODE_UNARY_OP:
            engine_scan_ast(cxpr_ast_operand(ast), prog);
            break;
        case CXPR_NODE_TERNARY:
            engine_scan_ast(cxpr_ast_ternary_condition(ast), prog);
            engine_scan_ast(cxpr_ast_ternary_true_branch(ast), prog);
            engine_scan_ast(cxpr_ast_ternary_false_branch(ast), prog);
            break;
        case CXPR_NODE_FUNCTION_CALL:
            for (i = 0; i < cxpr_ast_function_argc(ast); ++i) {
                engine_scan_ast(cxpr_ast_function_arg(ast, i), prog);
            }
            break;
        case CXPR_NODE_PRODUCER_ACCESS:
            for (i = 0; i < cxpr_ast_producer_argc(ast); ++i) {
                engine_scan_ast(cxpr_ast_producer_arg(ast, i), prog);
            }
            break;
        default:
            break;
    }
}

static int engine_tracked_index(const cxpr_engine_program* prog, const char* name) {
    size_t i;
    for (i = 0; i < prog->tracked_count; ++i) {
        if (strcmp(prog->tracked[i].name, name) == 0) return (int)i;
    }
    return -1;
}

/* Lookback resolver for `target[n]` (D16). Increment 2a serves source
 * identifiers (column/view via cursor offset, pull via ring); other targets
 * (named-expression result-rings, inline expressions) are increment 2b. */
static bool engine_lookback_resolver(const cxpr_ast* target, const cxpr_ast* index,
                                     const cxpr_context* ctx, const cxpr_registry* reg,
                                     void* userdata, cxpr_value* out, cxpr_error* err) {
    cxpr_engine_session* s = g_engine_tls_session;
    const char* name;
    engine_source* src;
    double nd, v = NAN;
    int64_t idx;
    size_t n;

    (void)ctx; (void)reg; (void)userdata;
    if (!s || !out) { engine_set_err(err, CXPR_ERR_SYNTAX, "engine: no active session for lookback"); return false; }
    if (!index || cxpr_ast_type(index) != CXPR_NODE_NUMBER) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: non-literal lookback index unsupported");
        return false;
    }
    nd = cxpr_ast_number_value(index);
    if (nd < 0.0) { engine_set_err(err, CXPR_ERR_SYNTAX, "engine: negative lookback"); return false; }
    n = (size_t)nd;

    if (!target || cxpr_ast_type(target) != CXPR_NODE_IDENTIFIER) {
        engine_set_err(err, CXPR_ERR_SYNTAX, "engine: unsupported lookback target (increment 2b)");
        return false;
    }
    name = cxpr_ast_identifier_name(target);
    src = engine_find_source_in(s->sources, s->source_count, name);
    if (!src) {
        /* Named-expression result ring (D16). */
        int ti = engine_tracked_index(s->prog, name);
        if (ti >= 0 && (size_t)ti < s->expr_ring_count) {
            *out = cxpr_num(engine_ringb_read(&s->expr_rings[ti], n));
            return true;
        }
        engine_set_err(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                       "engine: lookback target is neither a source nor a tracked expression "
                       "(inline (expr)[n] re-eval is increment 2b)");
        return false;
    }

    idx = s->cursor - (int64_t)n;
    switch (src->kind) {
        case ENGINE_SRC_COLUMN:
            if (src->base && idx >= 0 && (size_t)idx < src->count) {
                v = *(const double*)((const char*)src->base + (size_t)idx * src->stride);
            }
            break;
        case ENGINE_SRC_VIEW:
            if (src->view_fn && idx >= 0) {
                double o = 0.0;
                if (src->view_fn(idx, name, NULL, 0, &o, src->userdata)) v = o;
            }
            break;
        case ENGINE_SRC_PULL:
            v = engine_ring_read(src, n);
            break;
    }
    *out = cxpr_num(v); /* warmup / OOB -> NaN, propagated (D18) */
    return true;
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
    for (i = 0; i < prog->role_count; ++i) {
        free(prog->roles[i].name);
        free(prog->roles[i].members);
    }
    free(prog->roles);
    for (i = 0; i < prog->tracked_count; ++i) free(prog->tracked[i].name);
    free(prog->tracked);
    if (prog->owns_registry) cxpr_registry_free((cxpr_registry*)prog->registry);
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
            for (i = 0; i < prog->source_count; ++i) prog->sources[i].referenced = true;
        }
    }

    /* Discover referenced sources + per-source lookback depth (D5/D16). */
    {
        cxpr_parser* parser = cxpr_parser_new();
        if (parser) {
            for (i = 0; i < prog->expr_count; ++i) {
                cxpr_error perr = {0};
                cxpr_ast* ast = cxpr_parse(parser, prog->exprs[i].expression, &perr);
                if (ast) {
                    engine_scan_ast(ast, prog);
                    cxpr_ast_free(ast);
                }
            }
            cxpr_parser_free(parser);
        }
    }

    /* Install the engine's lookback resolver (D7/D16). The engine owns lookback
     * (D19), so it installs its resolver on the registry it uses — the one
     * registry write it performs, done here at single-threaded setup time. An
     * injected registry must therefore not rely on a different lookback resolver. */
    cxpr_registry_set_lookback_resolver((cxpr_registry*)prog->registry,
                                        engine_lookback_resolver, NULL, NULL);

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
            s->expr_rings[i].buf = (double*)malloc(cap * sizeof(double));
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

    /* Seed param + role defaults (D12/D25). */
    for (i = 0; i < prog->param_count; ++i) {
        cxpr_context_set_param(s->ctx, prog->params[i].name, prog->params[i].value);
    }
    for (i = 0; i < prog->role_count; ++i) {
        engine_seed_role(s->ctx, prog->roles[i].name, prog->roles[i].members, prog->roles[i].count);
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
    for (i = 0; i < session->expr_ring_count; ++i) free(session->expr_rings[i].buf);
    free(session->expr_rings);
    free(session->prev_truthy);
    free(session->prev_value);
    free(session->prev_valid);
    free(session->events);
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
    }
    for (i = 0; i < session->expr_ring_count; ++i) {
        session->expr_rings[i].head = 0;
        session->expr_rings[i].count = 0;
    }
    cxpr_context_clear_cached_structs(session->ctx);
    /* Params, role structs and source bindings are retained (D12/D22). */
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
    return engine_seed_role(session->ctx, name, members, count);
}

/* -------------------------------------------------------------------------- */
/* Tick + results                                                              */
/* -------------------------------------------------------------------------- */

static double engine_resolve_source(const engine_source* s, int64_t cursor) {
    double v = NAN;
    switch (s->kind) {
        case ENGINE_SRC_PULL:
            if (s->pull_fn) v = s->pull_fn(s->name, NULL, 0, s->userdata);
            break;
        case ENGINE_SRC_VIEW:
            if (s->view_fn) {
                double out = 0.0;
                if (s->view_fn(cursor, s->name, NULL, 0, &out, s->userdata)) v = out;
            }
            break;
        case ENGINE_SRC_COLUMN:
            if (s->base && cursor >= 0 && (size_t)cursor < s->count) {
                v = *(const double*)((const char*)s->base + (size_t)cursor * s->stride);
            }
            break;
    }
    return v;
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

    /* Hydrate only referenced sources for this tick (D5); pull sources also feed
     * their lookback ring (D7). Current value goes into the context for bare reads;
     * lagged reads are served by the lookback resolver. */
    for (i = 0; i < session->source_count; ++i) {
        engine_source* src = &session->sources[i];
        double v;
        if (!src->referenced) continue;
        v = engine_resolve_source(src, session->cursor);
        if (src->kind == ENGINE_SRC_PULL && src->ring) engine_ring_append(src, v);
        cxpr_context_set(session->ctx, src->name, v);
    }

    /* Reserve this tick's slot (depth 0) in each expression result ring so that
     * `expr[n]` reads depth n uniformly with sources; filled after eval (D16). */
    for (i = 0; i < session->expr_ring_count; ++i) {
        engine_ringb_append(&session->expr_rings[i], NAN);
    }

    /* Evaluate the rule set. Data misses are NaN and do not abort (D18).
     * The thread-local session lets the lookback resolver reach ring/cursor state. */
    g_engine_tls_session = session;
    cxpr_evaluator_eval(session->eval, session->ctx, &eval_err);
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
        if (r->buf) r->buf[r->head] = f ? engine_value_to_double(v) : NAN;
    }

    /* Edge detection over watches (D9/D11). */
    for (i = 0; i < prog->watch_count; ++i) {
        const engine_watch* w = &prog->watches[i];
        bool found = false;
        cxpr_value val = cxpr_expression_get(session->eval, w->expr_name, &found);
        bool truthy = found ? engine_value_truthy(val) : false;
        double num = found ? engine_value_to_double(val) : NAN;
        bool fire = false;

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
