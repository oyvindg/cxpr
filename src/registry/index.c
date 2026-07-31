/**
 * @file index.c
 * @brief Neutral exact-target index capability registration.
 */

#include "internal.h"
#include <cxpr/expr/ast.h>

#include <stdlib.h>
#include <string.h>

static char* cxpr_index_strdup(const char* value) {
    const size_t size = value ? strlen(value) + 1u : 0u;
    char* copy;
    if (size == 0u) return NULL;
    copy = (char*)malloc(size);
    if (copy) memcpy(copy, value, size);
    return copy;
}

const cxpr_index_capability_entry* cxpr_registry_find_index_capability(
    const cxpr_registry* reg, const char* target_name) {
    if (!reg || !target_name) return NULL;
    for (size_t i = 0u; i < reg->index_capability_count; ++i) {
        if (strcmp(reg->index_capabilities[i].target_name, target_name) == 0) {
            return &reg->index_capabilities[i];
        }
    }
    return NULL;
}

const cxpr_index_capability_entry* cxpr_registry_select_index_capability(
    const cxpr_registry* reg, const cxpr_expr_ast* target,
    cxpr_error* err, bool* handled) {
    const cxpr_index_capability_entry* selected = NULL;
    const char** references = NULL;
    size_t reference_count;
    if (handled) *handled = false;
    if (!reg || !target) return NULL;
    if (cxpr_expr_ast_kind_of(target) == CXPR_NODE_IDENTIFIER) {
        selected = cxpr_registry_find_index_capability(
            reg, cxpr_expr_ast_identifier_name(target));
        if (handled) *handled = selected != NULL;
        return selected;
    }
    reference_count = cxpr_expr_ast_references(target, NULL, 0u);
    if (reference_count == 0u) return NULL;
    references = (const char**)malloc(reference_count * sizeof(*references));
    if (!references) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Failed to allocate index capability references";
        }
        if (handled) *handled = true;
        return NULL;
    }
    cxpr_expr_ast_references(target, references, reference_count);
    for (size_t i = 0u; i < reference_count; ++i) {
        const cxpr_index_capability_entry* candidate =
            cxpr_registry_find_index_capability(reg, references[i]);
        if (!candidate) continue;
        if (!selected) {
            selected = candidate;
        } else if (selected->resolve != candidate->resolve ||
                   selected->userdata != candidate->userdata) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Index target references multiple capabilities";
            }
            free(references);
            if (handled) *handled = true;
            return NULL;
        }
    }
    free(references);
    if (handled) *handled = selected != NULL;
    return selected;
}

bool cxpr_registry_add_index_capability(
    cxpr_registry* reg,
    const char* capability_name,
    const char* target_name,
    cxpr_value_type result_type,
    cxpr_index_capability_fn resolve,
    void* userdata,
    cxpr_userdata_free_fn free_userdata) {
    const char* targets[] = {target_name};
    return cxpr_registry_add_index_capability_targets(
        reg, capability_name, targets, 1u, result_type, resolve, userdata,
        free_userdata);
}

bool cxpr_registry_add_index_capability_targets(
    cxpr_registry* reg, const char* capability_name,
    const char* const* target_names, size_t target_count,
    cxpr_value_type result_type, cxpr_index_capability_fn resolve,
    void* userdata, cxpr_userdata_free_fn free_userdata) {
    cxpr_index_capability_entry* pending;
    size_t required;
    if (!reg || !capability_name || capability_name[0] == '\0' ||
        !target_names || target_count == 0u || !resolve ||
        target_count > SIZE_MAX / sizeof(*pending)) {
        return false;
    }
    pending = (cxpr_index_capability_entry*)calloc(target_count, sizeof(*pending));
    if (!pending) return false;
    for (size_t i = 0u; i < target_count; ++i) {
        if (!target_names[i] || target_names[i][0] == '\0' ||
            cxpr_registry_find_index_capability(reg, target_names[i])) {
            goto fail;
        }
        for (size_t j = 0u; j < i; ++j) {
            if (strcmp(target_names[i], target_names[j]) == 0) goto fail;
        }
        pending[i].capability_name = cxpr_index_strdup(capability_name);
        pending[i].target_name = cxpr_index_strdup(target_names[i]);
        if (!pending[i].capability_name || !pending[i].target_name) goto fail;
        pending[i].result_type = result_type;
        pending[i].resolve = resolve;
        pending[i].userdata = userdata;
        pending[i].free_userdata = i == 0u ? free_userdata : NULL;
    }
    if (reg->index_capability_count > SIZE_MAX - target_count) goto fail;
    required = reg->index_capability_count + target_count;
    if (required > reg->index_capability_capacity) {
        size_t next = reg->index_capability_capacity
                          ? reg->index_capability_capacity
                          : 4u;
        while (next < required) {
            if (next > SIZE_MAX / 2u) {
                next = required;
                break;
            }
            next *= 2u;
        }
        {
            cxpr_index_capability_entry* grown =
                (cxpr_index_capability_entry*)realloc(
                    reg->index_capabilities, next * sizeof(*grown));
            if (!grown) goto fail;
            reg->index_capabilities = grown;
            reg->index_capability_capacity = next;
        }
    }
    memcpy(&reg->index_capabilities[reg->index_capability_count], pending,
           target_count * sizeof(*pending));
    reg->index_capability_count = required;
    free(pending);
    return true;

fail:
    for (size_t i = 0u; i < target_count; ++i) {
        free(pending[i].capability_name);
        free(pending[i].target_name);
    }
    free(pending);
    return false;
}

size_t cxpr_registry_index_capability_count(const cxpr_registry* reg) {
    return reg ? reg->index_capability_count : 0u;
}

bool cxpr_registry_index_capability_at(
    const cxpr_registry* reg, size_t index,
    const char** target_name, const char** capability_name,
    cxpr_value_type* result_type) {
    const cxpr_index_capability_entry* capability;
    if (!reg || index >= reg->index_capability_count) return false;
    capability = &reg->index_capabilities[index];
    if (target_name) *target_name = capability->target_name;
    if (capability_name) *capability_name = capability->capability_name;
    if (result_type) *result_type = capability->result_type;
    return true;
}

bool cxpr_registry_index_capability_info(
    const cxpr_registry* reg, const char* target_name,
    const char** capability_name, cxpr_value_type* result_type) {
    const cxpr_index_capability_entry* capability =
        cxpr_registry_find_index_capability(reg, target_name);
    if (capability_name) *capability_name = capability
                                                ? capability->capability_name
                                                : NULL;
    if (!capability) return false;
    if (result_type) *result_type = capability->result_type;
    return true;
}

bool cxpr_registry_index_target_info(
    const cxpr_registry* reg, const cxpr_expr_ast* target,
    const char** capability_name, cxpr_value_type* result_type,
    cxpr_error* err) {
    bool handled = false;
    const cxpr_index_capability_entry* capability =
        cxpr_registry_select_index_capability(reg, target, err, &handled);
    if (capability_name) {
        *capability_name = capability ? capability->capability_name : NULL;
    }
    if (!capability) return false;
    if (result_type) *result_type = capability->result_type;
    return true;
}

bool cxpr_registry_resolve_index_capability(
    const cxpr_registry* reg, const cxpr_expr_ast* target, int64_t index,
    const cxpr_context* ctx, cxpr_value* out, cxpr_error* err,
    bool* handled) {
    const cxpr_index_capability_entry* capability = NULL;
    if (handled) *handled = false;
    if (!reg || !target || !out) return false;
    capability = cxpr_registry_select_index_capability(reg, target, err, handled);
    if (!capability) return false;
    if (capability->resolve(target, index, ctx, reg, capability->userdata,
                            out, err)) {
        return !(err && err->code != CXPR_OK);
    }
    if (err && err->code == CXPR_OK) {
        err->code = CXPR_ERR_TYPE_MISMATCH;
        err->message = "Index capability failed to resolve its owned target";
    }
    return false;
}
