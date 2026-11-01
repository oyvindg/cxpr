/**
 * @file define.c
 * @brief Registry support for expression-defined functions.
 */

#include "internal.h"
#include "ir/internal.h"
#include "core.h"
#include "ast/internal.h"
#include <cxpr/builtin.h>
#include <cxpr/codegen.h>
#include <cxpr/token.h>
#include <stdlib.h>
#include <string.h>

#define CXPR_DEF_MAX_PARAMS 16

typedef struct {
    const char** fields;
    size_t count;
    size_t capacity;
} cxpr_def_field_set;

typedef struct {
    cxpr_value_type type;
    bool known;
    bool conflict;
} cxpr_def_inferred_type;

static bool cxpr_def_numeric_param_name(const char* name) {
    static const char* names[] = {
        "value", "values", "expression", "source", "left", "right",
        "period", "periodx", "samples", "bars", "length", "offset",
        "index", "window", "threshold", "limit", "start", "end",
        "center", "factor", "multiplier", "pct", "percent"
    };
    if (!name) return false;
    for (size_t i = 0; i < CXPR_ARRAY_COUNT(names); ++i) {
        if (strcmp(name, names[i]) == 0) return true;
    }
    return strstr(name, "_period") || strstr(name, "_bars") ||
           strstr(name, "_pct") || strstr(name, "_length");
}

static bool cxpr_def_bool_param_name(const char* name) {
    return name && (strcmp(name, "condition") == 0 ||
                    strcmp(name, "inclusive_start") == 0 ||
                    strcmp(name, "inclusive_end") == 0);
}

static void cxpr_def_constrain(cxpr_def_inferred_type* type,
                               cxpr_value_type expected) {
    if (!type || type->conflict) return;
    if (!type->known) {
        type->type = expected;
        type->known = true;
    } else if (type->type != expected) {
        type->conflict = true;
    }
}

static size_t cxpr_def_param_index(const cxpr_expr_ast* node,
                                   const char* const* param_names,
                                   size_t param_count) {
    if (!node || node->type != CXPR_NODE_IDENTIFIER) return param_count;
    for (size_t i = 0; i < param_count; ++i) {
        if (strcmp(node->data.identifier.name, param_names[i]) == 0) return i;
    }
    return param_count;
}

static size_t cxpr_def_call_param_index(const char* arg_name, size_t arg_index,
                                        const char* const* formal_names,
                                        size_t formal_count) {
    if (!arg_name) return arg_index;
    for (size_t i = 0; i < formal_count; ++i) {
        if (strcmp(arg_name, formal_names[i]) == 0) return i;
    }
    return arg_index;
}

static void cxpr_def_infer_ast(const cxpr_expr_ast* node, const cxpr_registry* reg,
                               const char* const* param_names, size_t param_count,
                               cxpr_def_inferred_type* inferred,
                               const cxpr_value_type* expected) {
    size_t param_index;
    if (!node) return;

    param_index = cxpr_def_param_index(node, param_names, param_count);
    if (param_index < param_count) {
        if (expected) cxpr_def_constrain(&inferred[param_index], *expected);
        return;
    }

    switch (node->type) {
    case CXPR_NODE_FIELD_ACCESS:
        param_index = cxpr_def_param_index(node->data.field_access.base,
                                           param_names, param_count);
        if (param_index < param_count) {
            cxpr_def_constrain(&inferred[param_index], CXPR_VALUE_STRUCT);
        } else if (!node->data.field_access.base && node->data.field_access.object) {
            for (size_t i = 0; i < param_count; ++i) {
                if (strcmp(node->data.field_access.object, param_names[i]) == 0) {
                    cxpr_def_constrain(&inferred[i], CXPR_VALUE_STRUCT);
                    break;
                }
            }
        }
        cxpr_def_infer_ast(node->data.field_access.base, reg, param_names,
                          param_count, inferred, NULL);
        break;
    case CXPR_NODE_CHAIN_ACCESS:
        if (node->data.chain_access.depth > 1u) {
            for (size_t i = 0; i < param_count; ++i) {
                if (strcmp(node->data.chain_access.path[0], param_names[i]) == 0) {
                    cxpr_def_constrain(&inferred[i], CXPR_VALUE_STRUCT);
                    break;
                }
            }
        }
        break;
    case CXPR_NODE_BINARY_OP: {
        int op = node->data.binary_op.op;
        cxpr_value_type operand_type = CXPR_VALUE_NUMBER;
        if (op == CXPR_TOK_AND || op == CXPR_TOK_OR) operand_type = CXPR_VALUE_BOOL;
        if (op != CXPR_TOK_EQ && op != CXPR_TOK_NEQ && op != CXPR_TOK_IN) {
            cxpr_def_infer_ast(node->data.binary_op.left, reg, param_names,
                               param_count, inferred, &operand_type);
            cxpr_def_infer_ast(node->data.binary_op.right, reg, param_names,
                               param_count, inferred, &operand_type);
        } else {
            cxpr_def_infer_ast(node->data.binary_op.left, reg, param_names,
                               param_count, inferred, NULL);
            cxpr_def_infer_ast(node->data.binary_op.right, reg, param_names,
                               param_count, inferred, NULL);
        }
        break;
    }
    case CXPR_NODE_UNARY_OP: {
        cxpr_value_type operand_type = node->data.unary_op.op == CXPR_TOK_NOT
            ? CXPR_VALUE_BOOL : CXPR_VALUE_NUMBER;
        cxpr_def_infer_ast(node->data.unary_op.operand, reg, param_names,
                           param_count, inferred, &operand_type);
        break;
    }
    case CXPR_NODE_INDEX: {
        cxpr_value_type number = CXPR_VALUE_NUMBER;
        cxpr_def_infer_ast(node->data.index.target, reg, param_names,
                           param_count, inferred, &number);
        cxpr_def_infer_ast(node->data.index.index, reg, param_names,
                           param_count, inferred, &number);
        break;
    }
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, node->data.function_call.name);
        const cxpr_function_info* builtin = cxpr_builtin_find(node->data.function_call.name);
        const char* const* formal_names = NULL;
        size_t formal_count = 0u;
        if (entry) formal_names = cxpr_registry_entry_param_names(entry, &formal_count);
        if ((!formal_names || formal_count == 0u) && builtin) {
            formal_names = builtin->param_names;
            formal_count = builtin->param_count;
        }
        for (size_t i = 0; i < node->data.function_call.argc; ++i) {
            const char* arg_name = node->data.function_call.arg_names
                ? node->data.function_call.arg_names[i] : NULL;
            size_t formal = cxpr_def_call_param_index(arg_name, i,
                                                      formal_names, formal_count);
            const cxpr_value_type* arg_expected = NULL;
            cxpr_value_type semantic_type;
            if (entry && entry->arg_types && formal < entry->arg_type_count) {
                arg_expected = &entry->arg_types[formal];
            } else if (formal_names && formal < formal_count &&
                       cxpr_def_bool_param_name(formal_names[formal])) {
                semantic_type = CXPR_VALUE_BOOL;
                arg_expected = &semantic_type;
            } else if (formal_names && formal < formal_count &&
                       cxpr_def_numeric_param_name(formal_names[formal])) {
                semantic_type = CXPR_VALUE_NUMBER;
                arg_expected = &semantic_type;
            }
            cxpr_def_infer_ast(node->data.function_call.args[i], reg, param_names,
                               param_count, inferred, arg_expected);
        }
        break;
    }
    case CXPR_NODE_TERNARY: {
        cxpr_value_type boolean = CXPR_VALUE_BOOL;
        cxpr_def_infer_ast(node->data.ternary.condition, reg, param_names,
                           param_count, inferred, &boolean);
        cxpr_def_infer_ast(node->data.ternary.true_branch, reg, param_names,
                           param_count, inferred, expected);
        cxpr_def_infer_ast(node->data.ternary.false_branch, reg, param_names,
                           param_count, inferred, expected);
        break;
    }
    default:
        break;
    }
}

static bool cxpr_def_ast_type(const cxpr_expr_ast* node, const cxpr_registry* reg,
                              const char* const* param_names, size_t param_count,
                              const cxpr_def_inferred_type* inferred,
                              cxpr_value_type* out) {
    size_t index;
    if (!node || !out) return false;
    switch (node->type) {
    case CXPR_NODE_NUMBER: *out = CXPR_VALUE_NUMBER; return true;
    case CXPR_NODE_INT64: *out = CXPR_VALUE_INT64; return true;
    case CXPR_NODE_BOOL: *out = CXPR_VALUE_BOOL; return true;
    case CXPR_NODE_STRING: *out = CXPR_VALUE_STRING; return true;
    case CXPR_NODE_ARRAY: *out = CXPR_VALUE_ARRAY; return true;
    case CXPR_NODE_RECORD: *out = CXPR_VALUE_STRUCT; return true;
    case CXPR_NODE_IDENTIFIER:
        index = cxpr_def_param_index(node, param_names, param_count);
        if (index < param_count && inferred[index].known && !inferred[index].conflict) {
            *out = inferred[index].type;
            return true;
        }
        return false;
    case CXPR_NODE_BINARY_OP:
        if (node->data.binary_op.op == CXPR_TOK_AND ||
            node->data.binary_op.op == CXPR_TOK_OR ||
            (node->data.binary_op.op >= CXPR_TOK_EQ &&
             node->data.binary_op.op <= CXPR_TOK_GTE) ||
            node->data.binary_op.op == CXPR_TOK_IN) {
            *out = CXPR_VALUE_BOOL;
        } else {
            *out = CXPR_VALUE_NUMBER;
        }
        return true;
    case CXPR_NODE_UNARY_OP:
        *out = node->data.unary_op.op == CXPR_TOK_NOT
            ? CXPR_VALUE_BOOL : CXPR_VALUE_NUMBER;
        return true;
    case CXPR_NODE_INDEX:
    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
        *out = CXPR_VALUE_NUMBER;
        return true;
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, node->data.function_call.name);
        const cxpr_function_info* builtin = cxpr_builtin_find(node->data.function_call.name);
        if (entry && entry->has_return_type) {
            *out = entry->return_type;
            return true;
        }
        if (builtin) {
            *out = builtin->return_type;
            return builtin->return_type != CXPR_VALUE_NULL;
        }
        return false;
    }
    case CXPR_NODE_TERNARY: {
        cxpr_value_type left;
        cxpr_value_type right;
        if (!cxpr_def_ast_type(node->data.ternary.true_branch, reg, param_names,
                               param_count, inferred, &left) ||
            !cxpr_def_ast_type(node->data.ternary.false_branch, reg, param_names,
                               param_count, inferred, &right) || left != right) return false;
        *out = left;
        return true;
    }
    default:
        return false;
    }
}

static cxpr_value_type* cxpr_def_infer_param_types(
    const cxpr_registry* reg, const char* const* param_names, size_t param_count,
    const cxpr_expr_ast* const* bodies, size_t body_count,
    cxpr_def_inferred_type* inferred) {
    cxpr_value_type* types;
    memset(inferred, 0, sizeof(*inferred) * param_count);
    for (size_t i = 0; i < body_count; ++i) {
        cxpr_def_infer_ast(bodies[i], reg, param_names, param_count, inferred, NULL);
    }
    for (size_t i = 0; i < param_count; ++i) {
        if (!inferred[i].known || inferred[i].conflict) return NULL;
    }
    types = (cxpr_value_type*)malloc(sizeof(*types) * param_count);
    if (!types && param_count > 0u) return NULL;
    for (size_t i = 0; i < param_count; ++i) types[i] = inferred[i].type;
    return types;
}

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

static bool collect_fields_in_ast(const cxpr_expr_ast* node,
                                  const char* const* param_names, size_t param_count,
                                  cxpr_def_field_set* sets) {
    const char* object = NULL;

    if (!node) return true;

    if (node->type == CXPR_NODE_FIELD_ACCESS) {
        if (!node->data.field_access.base) {
            object = node->data.field_access.object;
        } else if (node->data.field_access.base->type == CXPR_NODE_IDENTIFIER) {
            object = node->data.field_access.base->data.identifier.name;
        }
    }
    if (object) {
        const char* fld = node->data.field_access.field;
        for (size_t i = 0; i < param_count; i++) {
            if (strcmp(object, param_names[i]) != 0) continue;
            if (!cxpr_def_field_set_add(&sets[i], fld)) return false;
            break;
        }
        return true;
    }
    if (node->type == CXPR_NODE_CHAIN_ACCESS && node->data.chain_access.depth == 2u) {
        for (size_t i = 0; i < param_count; ++i) {
            if (strcmp(node->data.chain_access.path[0], param_names[i]) != 0) continue;
            return cxpr_def_field_set_add(&sets[i], node->data.chain_access.path[1]);
        }
        return true;
    }

    switch (node->type) {
    case CXPR_NODE_FIELD_ACCESS:
        return collect_fields_in_ast(
            node->data.field_access.base, param_names, param_count, sets);
    case CXPR_NODE_CHAIN_ACCESS:
        return true;
    case CXPR_NODE_BINARY_OP:
        return collect_fields_in_ast(node->data.binary_op.left, param_names, param_count, sets) &&
               collect_fields_in_ast(node->data.binary_op.right, param_names, param_count, sets);
    case CXPR_NODE_UNARY_OP:
        return collect_fields_in_ast(node->data.unary_op.operand, param_names, param_count, sets);
    case CXPR_NODE_INDEX:
        return collect_fields_in_ast(node->data.index.target, param_names, param_count, sets) &&
               collect_fields_in_ast(node->data.index.index, param_names, param_count, sets);
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

static bool collect_transitive_fields_in_ast(const cxpr_expr_ast* node, const cxpr_registry* reg,
                                             const char* const* param_names, size_t param_count,
                                             cxpr_def_field_set* sets) {
    if (!node || !reg) return true;

    switch (node->type) {
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, node->data.function_call.name);
        if (entry && entry->defined_param_fields &&
            entry->defined_param_count == node->data.function_call.argc) {
            for (size_t arg_index = 0; arg_index < node->data.function_call.argc; arg_index++) {
                const cxpr_expr_ast* arg = node->data.function_call.args[arg_index];

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
    case CXPR_NODE_INDEX:
        return collect_transitive_fields_in_ast(node->data.index.target, reg,
                                                param_names, param_count, sets) &&
               collect_transitive_fields_in_ast(node->data.index.index, reg,
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
    cxpr_value_type* inferred_types = NULL;
    cxpr_def_inferred_type inferred[CXPR_DEF_MAX_PARAMS];
    cxpr_value_type inferred_return = CXPR_VALUE_NUMBER;
    bool has_inferred_return = false;

    if (!reg || !def) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "NULL registry or definition";
        return err;
    }

    const char* p = def;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] == 'f' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
        p += 2;
        while (*p == ' ' || *p == '\t') p++;
    }

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
    if (p[0] == '=' && p[1] == '>') {
        p += 2;
    } else if (p[0] == '=') {
        p += 1;
    } else {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Expected '=>' or '='";
        return err;
    }
    while (*p == ' ' || *p == '\t') p++;

    if (!*p) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Empty function body";
        return err;
    }

    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    if (!parser) {
        err.code = CXPR_ERR_OUT_OF_MEMORY;
        err.message = "Out of memory";
        return err;
    }
    cxpr_expr_ast* body_ast = cxpr_expr_ast_parse(parser, p, &err);
    cxpr_expr_parser_free(parser);
    if (!body_ast) return err;

    const char* pnames[CXPR_DEF_MAX_PARAMS];
    for (size_t i = 0; i < param_count; i++) pnames[i] = param_buf[i];

    cxpr_def_field_set sets[CXPR_DEF_MAX_PARAMS];
    memset(sets, 0, sizeof(sets));
    if (!collect_fields_in_ast(body_ast, pnames, param_count, sets) ||
        !collect_transitive_fields_in_ast(body_ast, reg, pnames, param_count, sets)) {
        goto oom;
    }
    {
        const cxpr_expr_ast* bodies[] = {body_ast};
        inferred_types = cxpr_def_infer_param_types(reg, pnames, param_count,
                                                    bodies, 1u, inferred);
        has_inferred_return = cxpr_def_ast_type(body_ast, reg, pnames, param_count,
                                                inferred, &inferred_return);
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
            existing->arg_types = inferred_types;
            existing->arg_type_count = inferred_types ? param_count : 0u;
            existing->return_type = inferred_return;
            existing->has_return_type = has_inferred_return;
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
        entry->arg_types = inferred_types;
        entry->arg_type_count = inferred_types ? param_count : 0u;
        entry->return_type = inferred_return;
        entry->has_return_type = has_inferred_return;
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
    cxpr_expr_ast_free(body_ast);
    free(inferred_types);
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

char* cxpr_registry_defined_fn_to_c_function(const cxpr_registry* reg,
                                             const char* name,
                                             const char* qualifiers,
                                             const char* return_type,
                                             const char* function_name,
                                             cxpr_error* err) {
    cxpr_func_entry* entry;
    cxpr_expr_compiled program = {0};
    cxpr_c_program_arg args[CXPR_DEF_MAX_PARAMS];
    char* code;

    if (err) *err = (cxpr_error){0};
    if (!reg || !name || !function_name) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Invalid defined function C backend arguments";
        }
        return NULL;
    }
    entry = cxpr_registry_find(reg, name);
    if (!entry || !entry->defined_body || entry->defined_return_field_count > 0u) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            err->message = "Unknown scalar expression-defined function";
        }
        return NULL;
    }
    if (entry->defined_param_count > CXPR_DEF_MAX_PARAMS) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Too many defined function parameters";
        }
        return NULL;
    }

    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        args[i] = (cxpr_c_program_arg){
            .kind = CXPR_C_PROGRAM_ARG_LOCAL,
            .name = entry->defined_param_names[i],
            .c_name = entry->defined_param_names[i],
            .local_index = i,
        };
    }

    if (!cxpr_ir_compile_with_locals(entry->defined_body, reg,
                                     (const char* const*)entry->defined_param_names,
                                     entry->defined_param_count,
                                     &program.ir,
                                     err)) {
        cxpr_ir_program_reset(&program.ir);
        return NULL;
    }
    program.ast = entry->defined_body;
    code = cxpr_expr_compiled_to_c_function(&program,
                                      qualifiers,
                                      return_type,
                                      function_name,
                                      args,
                                      entry->defined_param_count,
                                      err);
    cxpr_ir_program_reset(&program.ir);
    return code;
}

cxpr_error cxpr_registry_define_record_fn(cxpr_registry* reg, const char* name,
                                          const char* const* param_names,
                                          size_t param_count,
                                          const char* const* field_names,
                                          const cxpr_expr_ast* const* field_bodies,
                                          size_t field_count) {
    cxpr_error err = {0};
    cxpr_def_field_set sets[CXPR_DEF_MAX_PARAMS];
    char** owned_params = NULL;
    char*** owned_fields = NULL;
    size_t* owned_counts = NULL;
    char** owned_return_names = NULL;
    cxpr_expr_ast** owned_return_bodies = NULL;
    cxpr_value_type* inferred_types = NULL;
    cxpr_def_inferred_type inferred[CXPR_DEF_MAX_PARAMS];

    memset(sets, 0, sizeof(sets));
    if (!reg || !name || (!param_names && param_count > 0u) ||
        !field_names || !field_bodies || field_count == 0u ||
        param_count > CXPR_DEF_MAX_PARAMS) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "Invalid record function";
        return err;
    }

    for (size_t i = 0; i < field_count; ++i) {
        if (!field_names[i] || !field_bodies[i]) {
            err.code = CXPR_ERR_SYNTAX;
            err.message = "Invalid record function field";
            return err;
        }
        if (!collect_fields_in_ast(field_bodies[i], param_names, param_count, sets) ||
            !collect_transitive_fields_in_ast(field_bodies[i], reg,
                                              param_names, param_count, sets)) {
            goto oom;
        }
    }
    inferred_types = cxpr_def_infer_param_types(reg, param_names, param_count,
                                                field_bodies, field_count, inferred);

    owned_params = (char**)calloc(param_count ? param_count : 1u, sizeof(char*));
    owned_fields = (char***)calloc(param_count ? param_count : 1u, sizeof(char**));
    owned_counts = (size_t*)calloc(param_count ? param_count : 1u, sizeof(size_t));
    owned_return_names = (char**)calloc(field_count, sizeof(char*));
    owned_return_bodies = (cxpr_expr_ast**)calloc(field_count, sizeof(cxpr_expr_ast*));
    if (!owned_params || !owned_fields || !owned_counts ||
        !owned_return_names || !owned_return_bodies) {
        goto oom;
    }

    for (size_t i = 0; i < param_count; ++i) {
        owned_params[i] = cxpr_strdup(param_names[i]);
        if (!owned_params[i]) goto oom;
        owned_counts[i] = sets[i].count;
        if (sets[i].count > 0u) {
            owned_fields[i] = (char**)calloc(sets[i].count, sizeof(char*));
            if (!owned_fields[i]) goto oom;
            for (size_t f = 0; f < sets[i].count; ++f) {
                owned_fields[i][f] = cxpr_strdup(sets[i].fields[f]);
                if (!owned_fields[i][f]) goto oom;
            }
        }
    }

    for (size_t i = 0; i < field_count; ++i) {
        owned_return_names[i] = cxpr_strdup(field_names[i]);
        owned_return_bodies[i] = cxpr_expr_ast_clone(field_bodies[i]);
        if (!owned_return_names[i] || !owned_return_bodies[i]) goto oom;
    }

    {
        cxpr_func_entry* entry = cxpr_registry_find(reg, name);
        if (entry) {
            cxpr_registry_clear_owned_entry(entry);
        } else {
            if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) goto oom;
            entry = &reg->entries[reg->count++];
            cxpr_registry_prepare_entry(entry, name);
            if (!entry->name) goto oom;
        }

        entry->sync_func = NULL;
        entry->value_func = NULL;
        entry->typed_func = NULL;
        entry->ast_func = NULL;
        entry->struct_producer = NULL;
        entry->typed_struct_producer = NULL;
        entry->ast_func_handler = NULL;
        entry->native_kind = CXPR_NATIVE_KIND_NONE;
        memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
        entry->min_args = param_count;
        entry->max_args = param_count;
        entry->arg_types = inferred_types;
        entry->arg_type_count = inferred_types ? param_count : 0u;
        entry->return_type = CXPR_VALUE_STRUCT;
        entry->has_return_type = true;
        entry->defined_body = NULL;
        entry->defined_program = NULL;
        entry->defined_program_failed = false;
        entry->defined_param_names = owned_params;
        entry->defined_param_count = param_count;
        entry->defined_param_fields = owned_fields;
        entry->defined_param_field_counts = owned_counts;
        entry->defined_return_field_names = owned_return_names;
        entry->defined_return_field_bodies = owned_return_bodies;
        entry->defined_return_field_count = field_count;
        reg->version++;

        for (size_t i = 0; i < CXPR_DEF_MAX_PARAMS; ++i) {
            cxpr_def_field_set_destroy(&sets[i]);
        }
        return err;
    }

oom:
    free(inferred_types);
    if (owned_params) {
        for (size_t i = 0; i < param_count; ++i) free(owned_params[i]);
        free(owned_params);
    }
    if (owned_fields) {
        for (size_t i = 0; i < param_count; ++i) {
            if (owned_fields[i]) {
                for (size_t f = 0; f < (owned_counts ? owned_counts[i] : 0u); ++f) {
                    free(owned_fields[i][f]);
                }
                free(owned_fields[i]);
            }
        }
        free(owned_fields);
    }
    free(owned_counts);
    if (owned_return_names) {
        for (size_t i = 0; i < field_count; ++i) free(owned_return_names[i]);
        free(owned_return_names);
    }
    if (owned_return_bodies) {
        for (size_t i = 0; i < field_count; ++i) cxpr_expr_ast_free(owned_return_bodies[i]);
        free(owned_return_bodies);
    }
    for (size_t i = 0; i < CXPR_DEF_MAX_PARAMS; ++i) {
        cxpr_def_field_set_destroy(&sets[i]);
    }
    err.code = CXPR_ERR_OUT_OF_MEMORY;
    err.message = "Out of memory";
    return err;
}
