/**
 * @file lookback.c
 * @brief Ready-made column-backed lookback resolver (cxpr_register_column_lookback).
 *
 * A reusable lookback resolver so a host can evaluate `name[n]` over its own
 * array-of-structs columns without hand-writing one. Built only on the public
 * registry/AST/types API.
 */

#include <cxpr/registry.h>
#include <cxpr/ast.h>
#include <cxpr/types.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cxpr_lookback_column* cols;
    size_t count;
    const int64_t* cursor;
} cxpr_column_lookback_state;

static bool cxpr_column_lookback_resolve(const cxpr_ast* target, const cxpr_ast* index,
                                         const cxpr_context* ctx, const cxpr_registry* reg,
                                         void* userdata, cxpr_value* out, cxpr_error* err) {
    const cxpr_column_lookback_state* st = (const cxpr_column_lookback_state*)userdata;
    const char* name;
    double nd;
    int64_t n, idx;
    size_t i;

    (void)ctx; (void)reg; (void)err;
    if (!st || !out || !target || !index) return false;
    if (cxpr_ast_type(index) != CXPR_NODE_NUMBER) return false;     /* literal index only */
    if (cxpr_ast_type(target) != CXPR_NODE_IDENTIFIER) return false; /* bare source name only */
    nd = cxpr_ast_number_value(index);
    if (nd < 0.0) return false;
    n = (int64_t)nd;
    name = cxpr_ast_identifier_name(target);
    if (!name) return false;

    for (i = 0; i < st->count; ++i) {
        const cxpr_lookback_column* c = &st->cols[i];
        if (!c->name || strcmp(c->name, name) != 0) continue;
        idx = (st->cursor ? *st->cursor : 0) - n;
        if (c->base && idx >= 0 && (size_t)idx < c->count) {
            *out = cxpr_num(*(const double*)((const char*)c->base + (size_t)idx * c->stride));
        } else {
            *out = cxpr_num(NAN); /* warmup / out of range */
        }
        return true;
    }
    return false; /* name not in the column table */
}

static void cxpr_column_lookback_free(void* userdata) {
    cxpr_column_lookback_state* st = (cxpr_column_lookback_state*)userdata;
    if (!st) return;
    free(st->cols);
    free(st);
}

bool cxpr_register_column_lookback(cxpr_registry* reg,
                                   const cxpr_lookback_column* columns, size_t count,
                                   const int64_t* cursor) {
    cxpr_column_lookback_state* st;

    if (!reg) return false;
    if (count > 0 && !columns) return false;

    st = (cxpr_column_lookback_state*)calloc(1, sizeof(*st));
    if (!st) return false;
    if (count > 0) {
        st->cols = (cxpr_lookback_column*)malloc(count * sizeof(*st->cols));
        if (!st->cols) {
            free(st);
            return false;
        }
        memcpy(st->cols, columns, count * sizeof(*st->cols));
    }
    st->count = count;
    st->cursor = cursor;

    cxpr_registry_set_lookback_resolver(reg, cxpr_column_lookback_resolve, st,
                                        cxpr_column_lookback_free);
    return true;
}
