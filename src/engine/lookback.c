/** @file engine/lookback.c @brief Lookback discovery and resolution. */
#include "engine/internal.h"

typedef struct engine_registry_resolver_ref {
    const cxpr_registry* registry;
    cxpr_lookback_resolver_ptr prior_fn;
    void* prior_ud;
    size_t ref_count;
    struct engine_registry_resolver_ref* next;
} engine_registry_resolver_ref;

static engine_registry_resolver_ref* g_engine_registry_refs = NULL;

bool engine_registry_resolver_acquire(cxpr_engine_program* prog) {
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

void engine_registry_resolver_release(const cxpr_engine_program* prog) {
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

bool cxpr_engine_context_lookback_offset(const cxpr_context* ctx, size_t* out_offset) {
    return cxpr_context_history_offset(ctx, out_offset);
}

bool engine_is_expr_name(const cxpr_engine_program* prog, const char* name) {
    size_t i;
    if (!name) return false;
    for (i = 0; i < prog->expr_count; ++i) {
        if (prog->exprs[i].name && strcmp(prog->exprs[i].name, name) == 0) return true;
    }
    return false;
}

const cxpr_expression_def* engine_find_expr_def(const cxpr_engine_program* prog,
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

bool engine_track_external_param(cxpr_engine_program* prog, const char* name) {
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

void engine_scan_ast(const cxpr_expr_ast* ast, cxpr_engine_program* prog) {
    engine_scan_ast_with_offset(ast, prog, 0u);
}

int engine_tracked_index(const cxpr_engine_program* prog, const char* name) {
    size_t i;
    for (i = 0; i < prog->tracked_count; ++i) {
        if (strcmp(prog->tracked[i].name, name) == 0) return (int)i;
    }
    return -1;
}

bool engine_lookback_resolver(const cxpr_expr_ast* target, const cxpr_expr_ast* index,
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
