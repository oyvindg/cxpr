/**
 * @file helpers.c
 * @brief Internal registry allocation and entry lifecycle helpers.
 */

#include "internal.h"

#include "../ir/internal.h"

static void cxpr_registry_free_struct_fields(cxpr_func_entry* entry) {
    if (!entry->struct_fields) return;
    for (size_t f = 0; f < entry->fields_per_arg; f++) {
        free(entry->struct_fields[f]);
    }
    free(entry->struct_fields);
    entry->struct_fields = NULL;
    entry->fields_per_arg = 0;
    entry->struct_argc = 0;
}

static void cxpr_registry_free_arg_types(cxpr_func_entry* entry) {
    free(entry->arg_types);
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->has_return_type = false;
    entry->return_type = CXPR_VALUE_NUMBER;
}

static void cxpr_registry_free_param_names(cxpr_func_entry* entry) {
    if (!entry->param_names) return;
    for (size_t i = 0; i < entry->param_name_count; ++i) {
        free(entry->param_names[i]);
    }
    free(entry->param_names);
    entry->param_names = NULL;
    entry->param_name_count = 0;
}

static void cxpr_registry_free_defined_fn(cxpr_func_entry* entry) {
    if (!entry->defined_body) return;
    cxpr_program_free(entry->defined_program);
    entry->defined_program = NULL;
    entry->defined_program_failed = false;
    cxpr_ast_free(entry->defined_body);
    entry->defined_body = NULL;
    if (entry->defined_param_fields) {
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            if (entry->defined_param_fields[i]) {
                for (size_t f = 0; f < entry->defined_param_field_counts[i]; f++) {
                    free(entry->defined_param_fields[i][f]);
                }
                free(entry->defined_param_fields[i]);
            }
        }
        free(entry->defined_param_fields);
        entry->defined_param_fields = NULL;
    }
    if (entry->defined_param_names) {
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            free(entry->defined_param_names[i]);
        }
        free(entry->defined_param_names);
        entry->defined_param_names = NULL;
    }
    free(entry->defined_param_field_counts);
    entry->defined_param_field_counts = NULL;
    entry->defined_param_count = 0;
}

bool cxpr_registry_grow(cxpr_registry* reg) {
    if (reg->capacity > SIZE_MAX / 2) return false;
    size_t new_capacity = reg->capacity * 2;
    cxpr_func_entry* new_entries = (cxpr_func_entry*)calloc(new_capacity, sizeof(cxpr_func_entry));
    if (!new_entries) return false;
    memcpy(new_entries, reg->entries, reg->count * sizeof(cxpr_func_entry));
    free(reg->entries);
    reg->entries = new_entries;
    reg->capacity = new_capacity;
    return true;
}

cxpr_func_entry* cxpr_registry_find(const cxpr_registry* reg, const char* name) {
    if (!reg || !name) return NULL;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->entries[i].name && strcmp(reg->entries[i].name, name) == 0) {
            return &((cxpr_registry*)reg)->entries[i];
        }
    }
    return NULL;
}

const char* const* cxpr_registry_entry_param_names(const cxpr_func_entry* entry,
                                                   size_t* count) {
    if (count) *count = 0;
    if (!entry) return NULL;
    if (entry->defined_param_names && entry->defined_param_count > 0) {
        if (count) *count = entry->defined_param_count;
        return (const char* const*)entry->defined_param_names;
    }
    if (entry->param_names && entry->param_name_count > 0) {
        if (count) *count = entry->param_name_count;
        return (const char* const*)entry->param_names;
    }
    return NULL;
}

char** cxpr_registry_clone_param_names(const char* const* param_names, size_t param_count) {
    char** out;
    if (!param_names || param_count == 0) return NULL;
    out = (char**)calloc(param_count, sizeof(char*));
    if (!out) return NULL;
    for (size_t i = 0; i < param_count; ++i) {
        if (!param_names[i]) {
            for (size_t j = 0; j < param_count; ++j) free(out[j]);
            free(out);
            return NULL;
        }
        out[i] = cxpr_strdup(param_names[i]);
        if (!out[i]) {
            for (size_t j = 0; j <= i; ++j) free(out[j]);
            free(out);
            return NULL;
        }
    }
    return out;
}

cxpr_value_type* cxpr_registry_clone_arg_types(const cxpr_value_type* arg_types, size_t arg_count) {
    cxpr_value_type* out;
    if (!arg_types || arg_count == 0) return NULL;
    out = (cxpr_value_type*)malloc(sizeof(cxpr_value_type) * arg_count);
    if (!out) return NULL;
    memcpy(out, arg_types, sizeof(cxpr_value_type) * arg_count);
    return out;
}

void cxpr_registry_reset_entry(cxpr_func_entry* entry) {
    free(entry->name);
    entry->name = NULL;
    entry->sync_func = NULL;
    entry->value_func = NULL;
    entry->typed_func = NULL;
    entry->ast_func = NULL;
    entry->struct_producer = NULL;
    entry->ast_func_overlay = NULL;
    entry->ast_func_overlay_userdata = NULL;
    entry->ast_func_overlay_userdata_free = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = 0;
    entry->max_args = 0;
    entry->param_names = NULL;
    entry->param_name_count = 0;
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = false;
    entry->userdata = NULL;
    entry->userdata_free = NULL;
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
}

void cxpr_registry_clear_owned_entry(cxpr_func_entry* entry) {
    if (entry->userdata_free) {
        entry->userdata_free(entry->userdata);
    }
    if (entry->ast_func_overlay_userdata_free) {
        entry->ast_func_overlay_userdata_free(entry->ast_func_overlay_userdata);
    }
    cxpr_registry_free_struct_fields(entry);
    cxpr_registry_free_param_names(entry);
    cxpr_registry_free_arg_types(entry);
    cxpr_registry_free_defined_fn(entry);
    entry->userdata = NULL;
    entry->userdata_free = NULL;
    entry->ast_func_overlay = NULL;
    entry->ast_func_overlay_userdata = NULL;
    entry->ast_func_overlay_userdata_free = NULL;
}

void cxpr_registry_prepare_entry(cxpr_func_entry* entry, const char* name) {
    memset(entry, 0, sizeof(*entry));
    entry->name = cxpr_strdup(name);
}

cxpr_registry* cxpr_registry_new(void) {
    cxpr_registry* reg = (cxpr_registry*)calloc(1, sizeof(cxpr_registry));
    if (!reg) return NULL;
    reg->capacity = CXPR_REGISTRY_INITIAL_CAPACITY;
    reg->count = 0;
    reg->version = 1;
    reg->entries = (cxpr_func_entry*)calloc(reg->capacity, sizeof(cxpr_func_entry));
    if (!reg->entries) {
        free(reg);
        return NULL;
    }
    return reg;
}

void cxpr_registry_free(cxpr_registry* reg) {
    if (!reg) return;
    if (reg->free_lookback_userdata && reg->lookback_userdata) {
        reg->free_lookback_userdata(reg->lookback_userdata);
    }
    for (size_t i = 0; i < reg->count; i++) {
        free(reg->entries[i].name);
        cxpr_registry_clear_owned_entry(&reg->entries[i]);
    }
    free(reg->entries);
    free(reg);
}

void cxpr_registry_set_lookback_resolver(cxpr_registry* reg,
                                         cxpr_lookback_resolver_ptr resolver,
                                         void* userdata,
                                         cxpr_userdata_free_fn free_userdata) {
    if (!reg) return;
    if (reg->free_lookback_userdata && reg->lookback_userdata &&
        reg->lookback_userdata != userdata) {
        reg->free_lookback_userdata(reg->lookback_userdata);
    }
    reg->lookback_resolver = resolver;
    reg->lookback_userdata = userdata;
    reg->free_lookback_userdata = free_userdata;
}

bool cxpr_registry_set_param_names(cxpr_registry* reg, const char* name,
                                   const char* const* param_names, size_t param_count) {
    cxpr_func_entry* entry;
    char** owned;
    if (!reg || !name) return false;
    entry = cxpr_registry_find(reg, name);
    if (!entry) return false;
    if (!param_names || param_count == 0) {
        cxpr_registry_free_param_names(entry);
        reg->version++;
        return true;
    }
    if (param_count < entry->max_args) return false;
    owned = cxpr_registry_clone_param_names(param_names, param_count);
    if (!owned) return false;
    cxpr_registry_free_param_names(entry);
    entry->param_names = owned;
    entry->param_name_count = param_count;
    reg->version++;
    return true;
}

bool cxpr_registry_lookup(const cxpr_registry* reg, const char* name,
                          size_t* min_args, size_t* max_args) {
    cxpr_func_entry* entry = cxpr_registry_find(reg, name);
    if (!entry) return false;
    if (min_args) *min_args = entry->min_args;
    if (max_args) *max_args = entry->max_args;
    return true;
}
