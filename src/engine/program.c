/** @file engine/program.c @brief Engine program construction and lifecycle. */
#include "engine/internal.h"

char* engine_strdup(const char* s) {
    size_t n;
    char* p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

void engine_set_err(cxpr_error* err, cxpr_error_code code, const char* msg) {
    if (!err) return;
    err->code = code;
    err->message = msg;
    err->position = 0;
    err->line = 0;
    err->column = 0;
}

double engine_value_to_double(cxpr_value v) {
    if (v.type == CXPR_VALUE_NUMBER) return v.d;
    if (v.type == CXPR_VALUE_BOOL) return v.b ? 1.0 : 0.0;
    return NAN;
}

/* Build the basket role struct the cxpr basket builtins read (D25):
 * `__cxpr_basket_role_<name>` = { bound_count, value_count, v0..v{n-1} },
 * plus `$name` bound directly when there is a single member. */
bool engine_seed_role(cxpr_context* ctx, const char* name,
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
ENGINE_TLS cxpr_engine_session* g_engine_tls_session = NULL;
ENGINE_TLS size_t g_engine_tls_lookback_offset = 0;

const char* const CXPR_ENGINE_LOOKBACK_OFFSET_KEY = "__cxpr_engine_lookback_offset";

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
cxpr_evaluator* engine_build_evaluator(const cxpr_engine_program* prog,
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
