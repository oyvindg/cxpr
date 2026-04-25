/**
 * @file define.c
 * @brief Registry support for expression-defined functions.
 */

#include "internal.h"
#include "../core.h"
#include "../ast/internal.h"
#include <stdlib.h>
#include <string.h>

#define CXPR_DEF_MAX_PARAMS 16

typedef struct {
    const char** fields;
    size_t count;
    size_t capacity;
} cxpr_def_field_set;

static void cxpr_def_field_set_destroy(cxpr_def_field_set* set) {
    if (!set) return;
    free(set->fields);
    set->fields = NULL;
    set->count = 0;
    set->capacity = 0;
}

static bool cxpr_def_field_set_add(cxpr_def_field_set* set, const char* field) {
    bool dup = false;
    const char** grown;
    size_t new_capacity;

    if (!set || !field) return true;

    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->fields[i], field) == 0) {
            dup = true;
            break;
        }
    }

    if (dup) return true;

    if (set->count == set->capacity) {
        new_capacity = set->capacity ? (set->capacity * 2) : 8;
        grown = (const char**)realloc((void*)set->fields, sizeof(const char*) * new_capacity);
        if (!grown) return false;
        set->fields = grown;
        set->capacity = new_capacity;
    }

    set->fields[set->count++] = field;
    return true;
}

static bool collect_fields_in_ast(const cxpr_ast* node,
                                  const char* const* param_names, size_t param_count,
                                  cxpr_def_field_set* sets) {
    if (!node) return true;

    if (node->type == CXPR_NODE_FIELD_ACCESS) {
        const char* obj = node->data.field_access.object;
        const char* fld = node->data.field_access.field;
        for (size_t i = 0; i < param_count; i++) {
            if (strcmp(obj, param_names[i]) != 0) continue;
            if (!cxpr_def_field_set_add(&sets[i], fld)) return false;
            break;
        }
        return true;
    }

    switch (node->type) {
    case CXPR_NODE_BINARY_OP:
        return collect_fields_in_ast(node->data.binary_op.left, param_names, param_count, sets) &&
               collect_fields_in_ast(node->data.binary_op.right, param_names, param_count, sets);
    case CXPR_NODE_UNARY_OP:
        return collect_fields_in_ast(node->data.unary_op.operand, param_names, param_count, sets);
    case CXPR_NODE_LOOKBACK:
        return collect_fields_in_ast(node->data.lookback.target, param_names, param_count, sets) &&
               collect_fields_in_ast(node->data.lookback.index, param_names, param_count, sets);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0; i < node->data.function_call.argc; i++) {
            if (!collect_fields_in_ast(node->data.function_call.args[i],
                                       param_names, param_count, sets)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_TERNARY:
        return collect_fields_in_ast(node->data.ternary.condition, param_names, param_count, sets) &&
               collect_fields_in_ast(node->data.ternary.true_branch, param_names, param_count, sets) &&
               collect_fields_in_ast(node->data.ternary.false_branch, param_names, param_count, sets);
    default:
        return true;
    }
}

static bool collect_transitive_fields_in_ast(const cxpr_ast* node, const cxpr_registry* reg,
                                             const char* const* param_names, size_t param_count,
                                             cxpr_def_field_set* sets) {
    if (!node || !reg) return true;

    switch (node->type) {
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, node->data.function_call.name);
        if (entry && entry->defined_param_fields &&
            entry->defined_param_count == node->data.function_call.argc) {
            for (size_t arg_index = 0; arg_index < node->data.function_call.argc; arg_index++) {
                const cxpr_ast* arg = node->data.function_call.args[arg_index];

                if (arg->type != CXPR_NODE_IDENTIFIER) continue;

                for (size_t param_index = 0; param_index < param_count; param_index++) {
                    if (strcmp(arg->data.identifier.name, param_names[param_index]) != 0) continue;

                    for (size_t field_index = 0;
                         field_index < entry->defined_param_field_counts[arg_index];
                         field_index++) {
                        if (!cxpr_def_field_set_add(&sets[param_index],
                                                    entry->defined_param_fields[arg_index][field_index])) {
                            return false;
                        }
                    }
                    break;
                }
            }
        }

        for (size_t i = 0; i < node->data.function_call.argc; i++) {
            if (!collect_transitive_fields_in_ast(node->data.function_call.args[i], reg,
                                                  param_names, param_count, sets)) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_BINARY_OP:
        return collect_transitive_fields_in_ast(node->data.binary_op.left, reg,
                                                param_names, param_count, sets) &&
               collect_transitive_fields_in_ast(node->data.binary_op.right, reg,
                                                param_names, param_count, sets);
    case CXPR_NODE_UNARY_OP:
        return collect_transitive_fields_in_ast(node->data.unary_op.operand, reg,
                                                param_names, param_count, sets);
    case CXPR_NODE_LOOKBACK:
        return collect_transitive_fields_in_ast(node->data.lookback.target, reg,
                                                param_names, param_count, sets) &&
               collect_transitive_fields_in_ast(node->data.lookback.index, reg,
                                                param_names, param_count, sets);
    case CXPR_NODE_TERNARY:
        return collect_transitive_fields_in_ast(node->data.ternary.condition, reg,
                                                param_names, param_count, sets) &&
               collect_transitive_fields_in_ast(node->data.ternary.true_branch, reg,
                                                param_names, param_count, sets) &&
               collect_transitive_fields_in_ast(node->data.ternary.false_branch, reg,
                                                param_names, param_count, sets);
    default:
        return true;
    }
}

static bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

cxpr_error cxpr_registry_define_fn(cxpr_registry* reg, const char* def) {
    cxpr_error err = {0};
    char** owned_names = NULL;
    char*** owned_fields = NULL;
    size_t* owned_counts = NULL;

    if (!reg || !def) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "NULL registry or definition";
        return err;
    }

    const char* p = def;
    while (*p == ' ' || *p == '\t') p++;

    const char* name_start = p;
    while (is_ident_char(*p)) p++;
    size_t name_len = (size_t)(p - name_start);
    if (name_len == 0) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Expected function name";
        return err;
    }
    char fname[256];
    if (name_len >= sizeof(fname)) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Function name too long";
        return err;
    }
    memcpy(fname, name_start, name_len);
    fname[name_len] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '(') {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Expected '(' after function name";
        return err;
    }
    p++;

    char param_buf[CXPR_DEF_MAX_PARAMS][64];
    size_t param_count = 0;

    while (true) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ')') {
            p++;
            break;
        }
        if (param_count > 0) {
            if (*p != ',') {
                err.code = CXPR_ERR_SYNTAX;
                err.message = "Expected ',' or ')' in parameter list";
                return err;
            }
            p++;
            while (*p == ' ' || *p == '\t') p++;
        }
        const char* pstart = p;
        while (is_ident_char(*p)) p++;
        size_t plen = (size_t)(p - pstart);
        if (plen == 0) {
            err.code = CXPR_ERR_SYNTAX;
            err.message = "Expected parameter name";
            return err;
        }
        if (param_count >= CXPR_DEF_MAX_PARAMS) {
            err.code = CXPR_ERR_SYNTAX;
            err.message = "Too many parameters (max 16)";
            return err;
        }
        if (plen >= 64) {
            err.code = CXPR_ERR_SYNTAX;
            err.message = "Parameter name too long";
            return err;
        }
        memcpy(param_buf[param_count], pstart, plen);
        param_buf[param_count][plen] = '\0';
        param_count++;
    }

    while (*p == ' ' || *p == '\t') p++;
    if (p[0] != '=' || p[1] != '>') {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Expected '=>'";
        return err;
    }
    p += 2;
    while (*p == ' ' || *p == '\t') p++;

    if (!*p) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Empty function body";
        return err;
    }

    cxpr_parser* parser = cxpr_parser_new();
    if (!parser) {
        err.code = CXPR_ERR_OUT_OF_MEMORY;
        err.message = "Out of memory";
        return err;
    }
    cxpr_ast* body_ast = cxpr_parse(parser, p, &err);
    cxpr_parser_free(parser);
    if (!body_ast) return err;

    const char* pnames[CXPR_DEF_MAX_PARAMS];
    for (size_t i = 0; i < param_count; i++) pnames[i] = param_buf[i];

    cxpr_def_field_set sets[CXPR_DEF_MAX_PARAMS];
    memset(sets, 0, sizeof(sets));
    if (!collect_fields_in_ast(body_ast, pnames, param_count, sets) ||
        !collect_transitive_fields_in_ast(body_ast, reg, pnames, param_count, sets)) {
        goto oom;
    }

    owned_names = (char**)calloc(param_count ? param_count : 1, sizeof(char*));
    owned_fields = (char***)calloc(param_count ? param_count : 1, sizeof(char**));
    owned_counts = (size_t*)calloc(param_count ? param_count : 1, sizeof(size_t));
    if (!owned_names || !owned_fields || !owned_counts) goto oom;

    for (size_t i = 0; i < param_count; i++) {
        owned_names[i] = cxpr_strdup(param_buf[i]);
        if (!owned_names[i]) goto oom;

        owned_counts[i] = sets[i].count;
        if (sets[i].count > 0) {
            owned_fields[i] = (char**)calloc(sets[i].count, sizeof(char*));
            if (!owned_fields[i]) goto oom;
            for (size_t f = 0; f < sets[i].count; f++) {
                owned_fields[i][f] = cxpr_strdup(sets[i].fields[f]);
                if (!owned_fields[i][f]) goto oom;
            }
        }
    }

    {
        cxpr_func_entry* existing = cxpr_registry_find(reg, fname);
        if (existing) {
            cxpr_registry_clear_owned_entry(existing);
            existing->sync_func = NULL;
            existing->value_func = NULL;
            existing->typed_func = NULL;
            existing->native_kind = CXPR_NATIVE_KIND_NONE;
            memset(&existing->native_scalar, 0, sizeof(existing->native_scalar));
            existing->min_args = param_count;
            existing->max_args = param_count;
            existing->arg_types = NULL;
            existing->arg_type_count = 0;
            existing->return_type = CXPR_VALUE_NUMBER;
            existing->has_return_type = false;
            existing->userdata = NULL;
            existing->userdata_free = NULL;
            existing->defined_body = body_ast;
            existing->defined_program = NULL;
            existing->defined_program_failed = false;
            existing->defined_param_names = owned_names;
            existing->defined_param_count = param_count;
            existing->defined_param_fields = owned_fields;
            existing->defined_param_field_counts = owned_counts;
            reg->version++;
            for (size_t i = 0; i < CXPR_DEF_MAX_PARAMS; i++) {
                cxpr_def_field_set_destroy(&sets[i]);
            }
            return err;
        }

        if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) {
            err.code = CXPR_ERR_OUT_OF_MEMORY;
            err.message = "Registry growth failed";
            return err;
        }
        cxpr_func_entry* entry = &reg->entries[reg->count++];
        cxpr_registry_prepare_entry(entry, fname);
        entry->min_args = param_count;
        entry->max_args = param_count;
        entry->return_type = CXPR_VALUE_NUMBER;
        entry->has_return_type = false;
        entry->defined_body = body_ast;
        entry->defined_program = NULL;
        entry->defined_program_failed = false;
        entry->defined_param_names = owned_names;
        entry->defined_param_count = param_count;
        entry->defined_param_fields = owned_fields;
        entry->defined_param_field_counts = owned_counts;
        reg->version++;
        for (size_t i = 0; i < CXPR_DEF_MAX_PARAMS; i++) {
            cxpr_def_field_set_destroy(&sets[i]);
        }
        return err;
    }

oom:
    cxpr_ast_free(body_ast);
    if (owned_names) {
        for (size_t i = 0; i < param_count; i++) free(owned_names[i]);
        free(owned_names);
    }
    if (owned_fields) {
        for (size_t i = 0; i < param_count; i++) {
            if (owned_fields[i]) {
                for (size_t f = 0; f < owned_counts[i]; f++) free(owned_fields[i][f]);
                free(owned_fields[i]);
            }
        }
        free(owned_fields);
    }
    free(owned_counts);
    for (size_t i = 0; i < CXPR_DEF_MAX_PARAMS; i++) {
        cxpr_def_field_set_destroy(&sets[i]);
    }
    err.code = CXPR_ERR_OUT_OF_MEMORY;
    err.message = "Out of memory";
    return err;
}
