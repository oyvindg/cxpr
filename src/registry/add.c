/**
 * @file add.c
 * @brief Registry mutation APIs for adding and replacing functions.
 */

#include "internal.h"
#include "core.h"

#include <stdlib.h>
#include <string.h>

static void cxpr_registry_clear_struct_metadata(cxpr_func_entry* entry) {
    if (!entry->struct_fields) return;
    for (size_t i = 0; i < entry->fields_per_arg; ++i) {
        free(entry->struct_fields[i]);
    }
    free(entry->struct_fields);
    entry->struct_fields = NULL;
    entry->fields_per_arg = 0;
    entry->struct_argc = 0;
}

static void cxpr_registry_replace_entry(cxpr_func_entry* entry) {
    cxpr_registry_clear_owned_entry(entry);
    entry->sync_func = NULL;
    entry->value_func = NULL;
    entry->typed_func = NULL;
    entry->ast_func = NULL;
    entry->struct_producer = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->param_names = NULL;
    entry->param_name_count = 0;
    entry->struct_fields = NULL;
    entry->fields_per_arg = 0;
    entry->struct_argc = 0;
    entry->defined_body = NULL;
    entry->defined_program = NULL;
    entry->defined_program_failed = false;
    entry->defined_param_names = NULL;
    entry->defined_param_count = 0;
    entry->defined_param_fields = NULL;
    entry->defined_param_field_counts = NULL;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = false;
    entry->userdata = NULL;
    entry->userdata_free = NULL;
}

void cxpr_registry_add(cxpr_registry* reg, const char* name,
                       cxpr_func_ptr func, size_t min_args, size_t max_args,
                       void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        cxpr_registry_replace_entry(existing);
        existing->sync_func = func;
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->return_type = CXPR_VALUE_NUMBER;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    cxpr_registry_prepare_entry(entry, name);
    entry->sync_func = func;
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_value(cxpr_registry* reg, const char* name,
                             cxpr_value_func_ptr func, size_t min_args, size_t max_args,
                             void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        cxpr_registry_replace_entry(existing);
        existing->value_func = func;
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->return_type = CXPR_VALUE_NUMBER;
        existing->has_return_type = false;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    cxpr_registry_prepare_entry(entry, name);
    entry->value_func = func;
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = false;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_typed(cxpr_registry* reg, const char* name,
                             cxpr_typed_func_ptr func, size_t min_args, size_t max_args,
                             const cxpr_value_type* arg_types, cxpr_value_type return_type,
                             void* userdata, cxpr_userdata_free_fn free_userdata) {
    cxpr_value_type* owned_arg_types;
    if (!reg || !name || !func) return;
    owned_arg_types = cxpr_registry_clone_arg_types(arg_types, max_args);
    if (arg_types && max_args > 0 && !owned_arg_types) return;

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        cxpr_registry_replace_entry(existing);
        existing->typed_func = func;
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->arg_types = owned_arg_types;
        existing->arg_type_count = max_args;
        existing->return_type = return_type;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    cxpr_registry_prepare_entry(entry, name);
    entry->typed_func = func;
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->arg_types = owned_arg_types;
    entry->arg_type_count = max_args;
    entry->return_type = return_type;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_ast(cxpr_registry* reg, const char* name,
                           cxpr_ast_func_ptr func, size_t min_args, size_t max_args,
                           cxpr_value_type return_type,
                           void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        cxpr_registry_replace_entry(existing);
        existing->ast_func = func;
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->return_type = return_type;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    cxpr_registry_prepare_entry(entry, name);
    entry->ast_func = func;
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->return_type = return_type;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_ast_overlay(cxpr_registry* reg, const char* name,
                                   cxpr_ast_func_ptr func,
                                   size_t min_args, size_t max_args,
                                   void* userdata,
                                   cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    cxpr_func_entry* entry = cxpr_registry_find(reg, name);
    if (entry) {
        if (entry->ast_func_overlay_userdata_free) {
            entry->ast_func_overlay_userdata_free(entry->ast_func_overlay_userdata);
        }
        entry->ast_func_overlay = func;
        entry->ast_func_overlay_userdata = userdata;
        entry->ast_func_overlay_userdata_free = free_userdata;
        if (min_args < entry->min_args) entry->min_args = min_args;
        if (max_args > entry->max_args) entry->max_args = max_args;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    entry = &reg->entries[reg->count++];
    cxpr_registry_prepare_entry(entry, name);
    entry->ast_func_overlay = func;
    entry->ast_func_overlay_userdata = userdata;
    entry->ast_func_overlay_userdata_free = free_userdata;
    entry->min_args = min_args;
    entry->max_args = max_args;
    reg->version++;
}

void cxpr_registry_add_timeseries(cxpr_registry* reg, const char* name,
                                  cxpr_timeseries_func_ptr func,
                                  size_t min_args, size_t max_args,
                                  cxpr_value_type return_type,
                                  void* userdata,
                                  cxpr_userdata_free_fn free_userdata) {
    cxpr_registry_add_ast(
        reg,
        name,
        (cxpr_ast_func_ptr)func,
        min_args,
        max_args,
        return_type,
        userdata,
        free_userdata);
}

void cxpr_registry_add_unary(cxpr_registry* reg, const char* name,
                             double (*func)(double)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_unary_userdata* ud = (cxpr_unary_userdata*)malloc(sizeof(cxpr_unary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_unary_adapter, 1, 1, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_UNARY;
        entry->native_scalar.unary = func;
    }
}

void cxpr_registry_add_binary(cxpr_registry* reg, const char* name,
                              double (*func)(double, double)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_binary_userdata* ud = (cxpr_binary_userdata*)malloc(sizeof(cxpr_binary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_binary_adapter, 2, 2, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_BINARY;
        entry->native_scalar.binary = func;
    }
}

void cxpr_registry_add_nullary(cxpr_registry* reg, const char* name,
                               double (*func)(void)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_nullary_userdata* ud = (cxpr_nullary_userdata*)malloc(sizeof(cxpr_nullary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_nullary_adapter, 0, 0, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_NULLARY;
        entry->native_scalar.nullary = func;
    }
}

void cxpr_registry_add_ternary(cxpr_registry* reg, const char* name,
                               double (*func)(double, double, double)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_ternary_userdata* ud = (cxpr_ternary_userdata*)malloc(sizeof(cxpr_ternary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_ternary_adapter, 3, 3, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_TERNARY;
        entry->native_scalar.ternary = func;
    }
}

void cxpr_registry_add_fn(cxpr_registry* reg, const char* name,
                          cxpr_func_ptr func,
                          const char* const* fields, size_t fields_per_arg,
                          size_t struct_argc,
                          void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func || !fields || fields_per_arg == 0 || struct_argc == 0) return;

    char** owned_fields = (char**)calloc(fields_per_arg, sizeof(char*));
    if (!owned_fields) return;
    for (size_t f = 0; f < fields_per_arg; f++) {
        owned_fields[f] = cxpr_strdup(fields[f]);
        if (!owned_fields[f]) {
            for (size_t k = 0; k < f; k++) free(owned_fields[k]);
            free(owned_fields);
            return;
        }
    }

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        cxpr_registry_replace_entry(existing);
        existing->sync_func = func;
        existing->min_args = struct_argc;
        existing->max_args = struct_argc;
        existing->return_type = CXPR_VALUE_NUMBER;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        existing->struct_fields = owned_fields;
        existing->fields_per_arg = fields_per_arg;
        existing->struct_argc = struct_argc;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    cxpr_registry_prepare_entry(entry, name);
    entry->sync_func = func;
    entry->min_args = struct_argc;
    entry->max_args = struct_argc;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    entry->struct_fields = owned_fields;
    entry->fields_per_arg = fields_per_arg;
    entry->struct_argc = struct_argc;
    reg->version++;
}

void cxpr_registry_add_struct(cxpr_registry* reg, const char* name,
                              cxpr_struct_producer_ptr func,
                              size_t min_args, size_t max_args,
                              const char* const* fields, size_t field_count,
                              void* userdata,
                              cxpr_userdata_free_fn free_userdata) {
    char** owned_fields;
    cxpr_func_entry* entry;

    if (!reg || !name || !func || !fields || field_count == 0) return;

    owned_fields = (char**)calloc(field_count, sizeof(char*));
    if (!owned_fields) return;
    for (size_t i = 0; i < field_count; i++) {
        owned_fields[i] = cxpr_strdup(fields[i]);
        if (!owned_fields[i]) {
            for (size_t j = 0; j < i; j++) free(owned_fields[j]);
            free(owned_fields);
            return;
        }
    }

    entry = cxpr_registry_find(reg, name);
    if (entry) {
        cxpr_registry_clear_struct_metadata(entry);
        entry->struct_producer = func;
        if (!entry->sync_func && !entry->value_func && !entry->typed_func) {
            entry->min_args = min_args;
            entry->max_args = max_args;
            entry->return_type = CXPR_VALUE_STRUCT;
            entry->has_return_type = true;
            entry->userdata = userdata;
            entry->userdata_free = free_userdata;
        }
        entry->struct_fields = owned_fields;
        entry->fields_per_arg = field_count;
        entry->struct_argc = 0;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    entry = &reg->entries[reg->count++];
    cxpr_registry_prepare_entry(entry, name);
    entry->struct_producer = func;
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->return_type = CXPR_VALUE_STRUCT;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    entry->struct_fields = owned_fields;
    entry->fields_per_arg = field_count;
    reg->version++;
}
