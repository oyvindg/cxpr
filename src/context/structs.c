/**
 * @file structs.c
 * @brief Struct and typed context bindings.
 */

#include "internal.h"
#include "expression/internal.h"

#include <stdio.h>

void cxpr_context_set_struct(cxpr_context* ctx, const char* name,
                             const cxpr_struct_value* value) {
    if (!ctx) return;
    cxpr_context_store_struct(&ctx->structs, name, value);
}

void cxpr_context_set_cached_struct(cxpr_context* ctx, const char* name,
                                    const cxpr_struct_value* value) {
    if (!ctx) return;
    cxpr_context_store_struct(&ctx->cached_structs, name, value);
}

const cxpr_struct_value* cxpr_context_get_struct(const cxpr_context* ctx,
                                                 const char* name) {
    const cxpr_struct_value* value;

    if (!ctx) return NULL;
    value = cxpr_context_lookup_struct_map(&ctx->structs, name);
    if (value) return value;
    return ctx->parent ? cxpr_context_get_struct(ctx->parent, name) : NULL;
}

const cxpr_struct_value* cxpr_context_get_cached_struct(const cxpr_context* ctx,
                                                        const char* name) {
    const cxpr_struct_value* value;

    if (!ctx) return NULL;
    value = cxpr_context_lookup_struct_map(&ctx->cached_structs, name);
    if (value) return value;
    return ctx->parent ? cxpr_context_get_cached_struct(ctx->parent, name) : NULL;
}

cxpr_value cxpr_context_get_typed(const cxpr_context* ctx, const char* name, bool* found) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;
    cxpr_value value;
    bool local_found = false;

    if (!ctx || !name) {
        if (found) *found = false;
        return cxpr_fv_double(0.0);
    }

    if (ctx->expression_scope) {
        value = cxpr_expression_lookup_typed_result(ctx->expression_scope, name, &local_found);
        if (local_found) {
            if (found) *found = true;
            return value;
        }
    }

    {
        bool bool_value = cxpr_context_get_local_bool(ctx, name, &local_found);
        if (local_found) {
            if (found) *found = true;
            return cxpr_fv_bool(bool_value);
        }
    }

    entry = cxpr_context_lookup_pointer_cached_entry((cxpr_hashmap*)&ctx->variables,
                                                     ((cxpr_context*)ctx)->variable_ptr_cache,
                                                     name);
    if (entry) {
        if (found) *found = true;
        return cxpr_fv_double(entry->value);
    }

    hash = cxpr_hash_string(name);
    entry = cxpr_context_lookup_cached_entry((cxpr_hashmap*)&ctx->variables,
                                             ((cxpr_context*)ctx)->variable_cache, name, hash);
    if (entry) {
        cxpr_context_refresh_pointer_cache((cxpr_hashmap*)&ctx->variables,
                                           ((cxpr_context*)ctx)->variable_ptr_cache, name, entry);
        if (found) *found = true;
        return cxpr_fv_double(entry->value);
    }

    {
        const cxpr_struct_value* struct_value = cxpr_context_lookup_struct_map(&ctx->structs, name);
        if (!struct_value) {
            struct_value = cxpr_context_lookup_struct_map(&ctx->cached_structs, name);
        }
        if (struct_value) {
            if (found) *found = true;
            return cxpr_fv_struct((cxpr_struct_value*)struct_value);
        }
    }

    if (ctx->parent) return cxpr_context_get_typed(ctx->parent, name, found);
    if (found) *found = false;
    return cxpr_fv_double(0.0);
}

cxpr_value cxpr_context_get_field(const cxpr_context* ctx, const char* name,
                                  const char* field, bool* found) {
    const cxpr_struct_value* s = cxpr_context_get_struct(ctx, name);
    cxpr_value root;
    bool root_found = false;
    size_t i;

    if (!s) {
        root = cxpr_context_get_typed(ctx, name, &root_found);
        if (root_found && root.type == CXPR_VALUE_STRUCT) {
            s = root.s;
        }
    }

    if (!s) {
        if (found) *found = false;
        return cxpr_fv_double(0.0);
    }

    for (i = 0; i < s->field_count; i++) {
        if (strcmp(s->field_names[i], field) == 0) {
            if (found) *found = true;
            return s->field_values[i];
        }
    }

    if (found) *found = false;
    return cxpr_fv_double(0.0);
}

void cxpr_context_set_fields(cxpr_context* ctx, const char* prefix,
                             const char* const* fields, const double* values,
                             size_t count) {
    char key[256];
    cxpr_value* typed_values;
    cxpr_struct_value* struct_value;
    size_t i;

    if (!ctx || !prefix || !fields || !values) return;

    for (i = 0; i < count; i++) {
        if (!fields[i]) continue;
        snprintf(key, sizeof(key), "%s.%s", prefix, fields[i]);
        cxpr_context_set(ctx, key, values[i]);
    }

    typed_values = (cxpr_value*)calloc(count, sizeof(cxpr_value));
    if (!typed_values) return;

    for (i = 0; i < count; i++) {
        typed_values[i] = cxpr_fv_double(values[i]);
    }

    struct_value = cxpr_struct_value_new(fields, typed_values, count);
    free(typed_values);
    if (!struct_value) return;

    cxpr_context_set_struct(ctx, prefix, struct_value);
    cxpr_struct_value_free(struct_value);
}
