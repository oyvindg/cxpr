/**
 * @file history.c
 * @brief Host-neutral historical numeric source adapter.
 */

#include <cxpr/history.h>
#include <cxpr/context.h>
#include <cxpr/eval.h>
#include <cxpr/expr/ast.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cxpr_history_numeric_source* sources;
    size_t source_count;
    const int64_t* cursor;
    cxpr_history_numeric_view_fn view;
    void* view_userdata;
    cxpr_userdata_free_fn free_view_userdata;
    cxpr_history_bounds_policy policy;
} cxpr_history_numeric_state;

static bool cxpr_history_error(cxpr_error* err, cxpr_error_code code,
                               const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return true;
}

static bool cxpr_history_find_source(
    const cxpr_history_numeric_state* state, const char* name,
    cxpr_history_numeric_source* out, int64_t* cursor) {
    for (size_t i = 0u; i < state->source_count; ++i) {
        if (state->sources[i].name && name &&
            strcmp(state->sources[i].name, name) == 0) {
            if (state->view) {
                return state->view(state->view_userdata, name, out, cursor);
            }
            *out = state->sources[i];
            *cursor = *state->cursor;
            return true;
        }
    }
    return false;
}

static bool cxpr_history_value_at(const cxpr_history_numeric_state* state,
                                  const cxpr_history_numeric_source* source,
                                  int64_t cursor, int64_t offset,
                                  cxpr_value* out, cxpr_error* err) {
    int64_t position;

    if (cursor < 0 || (uint64_t)cursor >= source->count) {
        if (state->policy == CXPR_HISTORY_BOUNDS_LEGACY_NAN) {
            *out = cxpr_num(NAN);
            return true;
        }
        cxpr_history_error(err, CXPR_ERR_INDEX_OUT_OF_RANGE,
                           "History cursor is out of range");
        return false;
    }
    position = cursor - offset;
    if (position < 0) {
        if (state->policy == CXPR_HISTORY_BOUNDS_CLAMP_FIRST) {
            position = 0;
        } else if (state->policy == CXPR_HISTORY_BOUNDS_LEGACY_NAN) {
            *out = cxpr_num(NAN);
            return true;
        } else {
            cxpr_history_error(err, CXPR_ERR_INDEX_OUT_OF_RANGE,
                               "History offset is out of range");
            return false;
        }
    }
    {
        double number;
        const size_t stride = source->stride ? source->stride : sizeof(double);
        memcpy(&number, (const char*)source->base + (size_t)position * stride,
               sizeof(number));
        *out = cxpr_num(number);
    }
    return true;
}

static bool cxpr_history_numeric_resolve(const cxpr_expr_ast* target,
                                         const cxpr_expr_ast* index_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_value* out,
                                         cxpr_error* err) {
    const cxpr_history_numeric_state* state =
        (const cxpr_history_numeric_state*)userdata;
    const char** references = NULL;
    size_t reference_count;
    bool has_history_source = false;
    cxpr_value index_value = cxpr_null();
    int64_t offset;
    int64_t cursor;
    cxpr_context* shifted = NULL;

    if (!state || !target || !index_ast || !out ||
        (!state->cursor && !state->view)) {
        return false;
    }
    reference_count = cxpr_expr_ast_references(target, NULL, 0u);
    if (reference_count == 0u) return false;
    references = (const char**)malloc(reference_count * sizeof(*references));
    if (!references) {
        return cxpr_history_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                  "Failed to allocate history references");
    }
    cxpr_expr_ast_references(target, references, reference_count);
    for (size_t i = 0u; i < reference_count; ++i) {
        cxpr_history_numeric_source source;
        int64_t source_cursor;
        if (cxpr_history_find_source(
                state, references[i], &source, &source_cursor)) {
            has_history_source = true;
            break;
        }
    }
    if (!has_history_source) {
        free(references);
        return false;
    }
    if (!cxpr_eval_ast(index_ast, ctx, reg, &index_value, err)) {
        free(references);
        return true;
    }
    if (index_value.type != CXPR_VALUE_NUMBER || !isfinite(index_value.d) ||
        index_value.d < 0.0 || floor(index_value.d) != index_value.d ||
        index_value.d > (double)INT64_MAX) {
        cxpr_value_free(&index_value);
        free(references);
        return cxpr_history_error(
            err, CXPR_ERR_INVALID_INDEX,
            "History offset must be a finite non-negative integer");
    }
    offset = (int64_t)index_value.d;
    cxpr_value_free(&index_value);
    cursor = state->cursor ? *state->cursor : 0;
    shifted = cxpr_context_overlay_new(ctx);
    if (!shifted) {
        free(references);
        return cxpr_history_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                  "Failed to allocate shifted history context");
    }
    cxpr_context_set_history_offset(shifted, (size_t)offset);
    for (size_t i = 0u; i < reference_count; ++i) {
        cxpr_history_numeric_source source;
        int64_t source_cursor = cursor;
        cxpr_value value;
        if (!cxpr_history_find_source(
                state, references[i], &source, &source_cursor)) continue;
        if (!cxpr_history_value_at(
                state, &source, source_cursor, offset, &value, err)) {
            cxpr_context_free(shifted);
            free(references);
            return true;
        }
        cxpr_context_set(shifted, source.name, value.d);
    }
    free(references);
    if (!cxpr_eval_ast(target, shifted, reg, out, err)) {
        cxpr_context_free(shifted);
        return true;
    }
    cxpr_context_free(shifted);
    return true;
}

static bool cxpr_history_numeric_index(const cxpr_expr_ast* target,
                                       int64_t index,
                                       const cxpr_context* ctx,
                                       const cxpr_registry* reg,
                                       void* userdata,
                                       cxpr_value* out,
                                       cxpr_error* err) {
    cxpr_expr_ast* index_ast = cxpr_expr_ast_number_new((double)index);
    bool resolved;
    if (!index_ast) {
        return cxpr_history_error(
            err, CXPR_ERR_OUT_OF_MEMORY,
            "Failed to allocate history index expression");
    }
    resolved = cxpr_history_numeric_resolve(
        target, index_ast, ctx, reg, userdata, out, err);
    cxpr_expr_ast_free(index_ast);
    return resolved;
}

static void cxpr_history_numeric_free(void* userdata) {
    cxpr_history_numeric_state* state = (cxpr_history_numeric_state*)userdata;
    if (!state) return;
    if (state->free_view_userdata) {
        state->free_view_userdata(state->view_userdata);
    }
    free(state->sources);
    free(state);
}

bool cxpr_register_history_numeric_sources(
    cxpr_registry* reg,
    const cxpr_history_numeric_source* sources,
    size_t source_count,
    const int64_t* cursor,
    cxpr_history_bounds_policy policy) {
    cxpr_history_numeric_state* state;
    const char** target_names;

    if (!reg || !cursor || !sources || source_count == 0u ||
        policy < CXPR_HISTORY_BOUNDS_ERROR ||
        policy > CXPR_HISTORY_BOUNDS_LEGACY_NAN) {
        return false;
    }
    for (size_t i = 0u; i < source_count; ++i) {
        const size_t stride = sources[i].stride ? sources[i].stride : sizeof(double);
        if (!sources[i].name || sources[i].name[0] == '\0' || !sources[i].base ||
            sources[i].count == 0u ||
            sources[i].count - 1u > (SIZE_MAX - sizeof(double)) / stride) {
            return false;
        }
        for (size_t j = 0u; j < i; ++j) {
            if (strcmp(sources[i].name, sources[j].name) == 0) return false;
        }
    }
    state = (cxpr_history_numeric_state*)calloc(1u, sizeof(*state));
    if (!state) return false;
    if (source_count > 0u) {
        state->sources = (cxpr_history_numeric_source*)malloc(
            source_count * sizeof(*state->sources));
        if (!state->sources) {
            free(state);
            return false;
        }
        memcpy(state->sources, sources, source_count * sizeof(*state->sources));
    }
    state->source_count = source_count;
    state->cursor = cursor;
    state->policy = policy;
    target_names = (const char**)malloc(source_count * sizeof(*target_names));
    if (!target_names) {
        cxpr_history_numeric_free(state);
        return false;
    }
    for (size_t i = 0u; i < source_count; ++i) {
        target_names[i] = state->sources[i].name;
    }
    if (!cxpr_registry_add_index_capability_targets(
            reg, "history", target_names, source_count, CXPR_VALUE_NUMBER,
            cxpr_history_numeric_index, state, cxpr_history_numeric_free)) {
        free(target_names);
        cxpr_history_numeric_free(state);
        return false;
    }
    free(target_names);
    return true;
}

bool cxpr_register_history_contiguous_numbers(
    cxpr_registry* reg,
    const char* name,
    const double* values,
    size_t count,
    const int64_t* cursor,
    cxpr_history_bounds_policy policy) {
    const cxpr_history_numeric_source source = {
        .name = name,
        .base = values,
        .stride = sizeof(double),
        .count = count,
    };
    return cxpr_register_history_numeric_sources(reg, &source, 1u, cursor, policy);
}


bool cxpr_register_history_numeric_provider(
    cxpr_registry* reg, const char* const* source_names, size_t source_count,
    cxpr_history_numeric_view_fn view, void* userdata,
    cxpr_userdata_free_fn free_userdata,
    cxpr_history_bounds_policy policy) {
    cxpr_history_numeric_state* state;
    if (!reg || !source_names || source_count == 0u || !view ||
        policy < CXPR_HISTORY_BOUNDS_ERROR ||
        policy > CXPR_HISTORY_BOUNDS_LEGACY_NAN) return false;
    state = (cxpr_history_numeric_state*)calloc(1u, sizeof(*state));
    if (!state) return false;
    state->sources = (cxpr_history_numeric_source*)calloc(
        source_count, sizeof(*state->sources));
    if (!state->sources) {
        free(state);
        return false;
    }
    for (size_t i = 0u; i < source_count; ++i) {
        if (!source_names[i] || source_names[i][0] == '\0') goto fail;
        for (size_t j = 0u; j < i; ++j) {
            if (strcmp(source_names[i], source_names[j]) == 0) goto fail;
        }
        state->sources[i].name = source_names[i];
    }
    state->source_count = source_count;
    state->view = view;
    state->view_userdata = userdata;
    state->free_view_userdata = free_userdata;
    state->policy = policy;
    if (!cxpr_registry_add_index_capability_targets(
            reg, "history", source_names, source_count, CXPR_VALUE_NUMBER,
            cxpr_history_numeric_index, state, cxpr_history_numeric_free)) {
        goto fail;
    }
    return true;
fail:
    state->free_view_userdata = NULL;
    cxpr_history_numeric_free(state);
    return false;
}
