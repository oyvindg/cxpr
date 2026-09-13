/** @file engine/session.c @brief Engine session lifecycle and execution. */
#include "engine/internal.h"

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
