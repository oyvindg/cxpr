/** @file engine/source.c @brief Runtime source resolution and hydration. */
#include "engine/internal.h"

engine_source* engine_find_source_in(engine_source* sources, size_t count,
                                            const char* name) {
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < count; ++i) {
        if (sources[i].name && strcmp(sources[i].name, name) == 0) return &sources[i];
    }
    return NULL;
}

/* Push a fresh sample onto a pull source's ring (newest at ring_head). */
void engine_ring_append(engine_source* s, double v) {
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
void engine_ringb_append(engine_ring* r, cxpr_value v) {
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
bool engine_struct_field_value(cxpr_value value, const char* field, cxpr_value* out) {
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

bool engine_struct_path_value(cxpr_value value,
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

bool engine_tracked_expression_value(cxpr_engine_session* s,
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

bool engine_eval_call_args(const cxpr_expr_ast* call_ast,
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

bool engine_resolve_source_call(cxpr_engine_session* session,
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

bool engine_eval_inline_lookback(cxpr_engine_session* s,
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

cxpr_value engine_source_call(const cxpr_expr_ast* call_ast,
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
