/**
 * @file eval/snapshot/build.c
 * @brief Single-evaluation AST diagnostics.
 */

#include <cxpr/expr/ast.h>
#include <cxpr/context.h>
#include <cxpr/eval.h>
#include <cxpr/snapshot.h>
#include <cxpr/registry.h>
#include <cxpr/token.h>
#include <cxpr/typecheck.h>

#include "ast/internal.h"
#include "context/state.h"
#include "eval/snapshot/internal.h"
#include "expression/internal.h"
#include "registry/internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const cxpr_context* ctx;
    const cxpr_registry* reg;
    cxpr_eval_snapshot* snapshot;
    cxpr_error* err;
    int failed;
} cxpr_snapshot_builder;

static const char* cxpr_snapshot_binary_op_text(int op) {
    switch (op) {
        case CXPR_TOK_PLUS: return "+";
        case CXPR_TOK_MINUS: return "-";
        case CXPR_TOK_STAR: return "*";
        case CXPR_TOK_SLASH: return "/";
        case CXPR_TOK_PERCENT: return "%";
        case CXPR_TOK_POWER: return "^";
        case CXPR_TOK_EQ: return "==";
        case CXPR_TOK_NEQ: return "!=";
        case CXPR_TOK_LT: return "<";
        case CXPR_TOK_GT: return ">";
        case CXPR_TOK_LTE: return "<=";
        case CXPR_TOK_GTE: return ">=";
        case CXPR_TOK_AND: return "and";
        case CXPR_TOK_OR: return "or";
        case CXPR_TOK_IN: return "in";
        default: return "op";
    }
}

static const char* cxpr_snapshot_unary_op_text(int op) {
    switch (op) {
        case CXPR_TOK_NOT: return "not";
        case CXPR_TOK_MINUS: return "-";
        case CXPR_TOK_PLUS: return "+";
        default: return "unary";
    }
}

static const char* cxpr_snapshot_kind(const cxpr_expr_ast* ast) {
    if (!ast) return "unknown";
    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_NUMBER: return "number";
        case CXPR_NODE_BOOL: return "bool";
        case CXPR_NODE_ARRAY: return "array";
        case CXPR_NODE_RECORD: return "record";
        case CXPR_NODE_STRING: return "string";
        case CXPR_NODE_IDENTIFIER: return "identifier";
        case CXPR_NODE_VARIABLE: return "variable";
        case CXPR_NODE_FIELD_ACCESS: return "field_access";
        case CXPR_NODE_CHAIN_ACCESS: return "chain_access";
        case CXPR_NODE_PRODUCER_ACCESS: return "producer_access";
        case CXPR_NODE_BINARY_OP: return "binary_op";
        case CXPR_NODE_UNARY_OP: return "unary_op";
        case CXPR_NODE_FUNCTION_CALL: return "function_call";
        case CXPR_NODE_LOOKBACK: return "lookback";
        case CXPR_NODE_TERNARY: return "ternary";
        default: return "unknown";
    }
}

static char* cxpr_snapshot_label(const cxpr_expr_ast* ast) {
    char buf[128];
    size_t depth;

    if (!ast) return cxpr_snapshot_strdup("?");
    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_NUMBER:
            return ast->data.number.lexeme
                ? cxpr_snapshot_strdup(ast->data.number.lexeme)
                : cxpr_snapshot_printf_number(cxpr_expr_ast_number_value(ast));
        case CXPR_NODE_BOOL:
            return cxpr_snapshot_strdup(cxpr_expr_ast_bool_value(ast) ? "true" : "false");
        case CXPR_NODE_STRING:
            return cxpr_snapshot_strdup(cxpr_expr_ast_string_value(ast));
        case CXPR_NODE_IDENTIFIER:
            return cxpr_snapshot_strdup(cxpr_expr_ast_identifier_name(ast));
        case CXPR_NODE_VARIABLE:
            snprintf(buf, sizeof(buf), "$%s", cxpr_expr_ast_param_name(ast));
            return cxpr_snapshot_strdup(buf);
        case CXPR_NODE_FIELD_ACCESS:
            if (cxpr_expr_ast_field_base(ast)) {
                snprintf(buf, sizeof(buf), "(expr).%s", cxpr_expr_ast_field_name(ast));
                return cxpr_snapshot_strdup(buf);
            }
            snprintf(buf, sizeof(buf), "%s.%s",
                     cxpr_expr_ast_field_object(ast), cxpr_expr_ast_field_name(ast));
            return cxpr_snapshot_strdup(buf);
        case CXPR_NODE_CHAIN_ACCESS:
            depth = cxpr_expr_ast_chain_count(ast);
            return cxpr_snapshot_strdup(depth ? cxpr_expr_ast_chain_segment(ast, depth - 1u) : "chain");
        case CXPR_NODE_BINARY_OP:
            return cxpr_snapshot_strdup(cxpr_snapshot_binary_op_text(cxpr_expr_ast_operator(ast)));
        case CXPR_NODE_UNARY_OP:
            return cxpr_snapshot_strdup(cxpr_snapshot_unary_op_text(cxpr_expr_ast_operator(ast)));
        case CXPR_NODE_FUNCTION_CALL:
            return cxpr_snapshot_strdup(cxpr_expr_ast_call_name(ast));
        case CXPR_NODE_PRODUCER_ACCESS:
            return cxpr_snapshot_strdup(cxpr_expr_ast_producer_name(ast));
        case CXPR_NODE_LOOKBACK:
            return cxpr_snapshot_strdup("lookback");
        case CXPR_NODE_TERNARY:
            return cxpr_snapshot_strdup("?:");
        case CXPR_NODE_ARRAY:
            return cxpr_snapshot_strdup("array");
        case CXPR_NODE_RECORD:
            return cxpr_snapshot_strdup("record");
        default:
            return cxpr_snapshot_strdup("?");
    }
}

static bool cxpr_snapshot_reserve(cxpr_eval_snapshot* snapshot) {
    cxpr_snapshot_node* grown;
    size_t cap;

    if (snapshot->node_count < snapshot->node_capacity) return true;
    cap = snapshot->node_capacity ? snapshot->node_capacity * 2u : 32u;
    grown = (cxpr_snapshot_node*)realloc(snapshot->nodes, cap * sizeof(*grown));
    if (!grown) return false;
    snapshot->nodes = grown;
    snapshot->node_capacity = cap;
    return true;
}

void cxpr_snapshot_set_string(char** dst, char* value);

static cxpr_snapshot_node* cxpr_snapshot_add_node(cxpr_snapshot_builder* b,
                                                  const cxpr_expr_ast* ast,
                                                  size_t parent_id,
                                                  int has_parent,
                                                  const char* role,
                                                  int active,
                                                  const char* inactive_reason) {
    cxpr_snapshot_node* node;

    if (!cxpr_snapshot_reserve(b->snapshot)) {
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot node";
        }
        return NULL;
    }

    node = &b->snapshot->nodes[b->snapshot->node_count];
    memset(node, 0, sizeof(*node));
    node->id = b->snapshot->node_count++;
    node->parent_id = parent_id;
    node->has_parent = has_parent;
    node->role = cxpr_snapshot_strdup(role ? role : "");
    node->kind = cxpr_snapshot_strdup(cxpr_snapshot_kind(ast));
    node->label = cxpr_snapshot_label(ast);
    node->display_label = cxpr_snapshot_strdup(node->label ? node->label : "");
    node->source = cxpr_expr_ast_to_string(ast);
    node->resolved = cxpr_snapshot_strdup(active ? "" :
        (inactive_reason ? inactive_reason : "not evaluated"));
    node->active = active;
    node->state = active ? CXPR_SNAPSHOT_STATE_UNKNOWN : CXPR_SNAPSHOT_STATE_SKIPPED;
    if (b->reg) {
        cxpr_error type_err = {0};
        node->has_inferred_type = cxpr_typecheck(
            ast, b->reg, &node->inferred_type, &type_err) ? 1 : 0;
    }

    if (!node->role || !node->kind || !node->label || !node->display_label ||
        !node->source || !node->resolved) {
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot strings";
        }
        return NULL;
    }
    if (strncmp(node->role, "arg", 3u) == 0 &&
         node->role[3] >= '0' && node->role[3] <= '9' &&
         strcmp(node->kind, "binary_op") != 0 &&
         strcmp(node->kind, "unary_op") != 0 &&
         strcmp(node->kind, "ternary") != 0 &&
         strcmp(node->kind, "function_call") != 0 &&
         strcmp(node->kind, "producer_access") != 0 &&
         strcmp(node->kind, "field_access") != 0 &&
         strcmp(node->kind, "chain_access") != 0 &&
         strcmp(node->kind, "lookback") != 0) {
        char* role_label = cxpr_snapshot_strdup(node->role);
        char* role_display_label = cxpr_snapshot_strdup(node->role);
        if (!role_label || !role_display_label) {
            free(role_label);
            free(role_display_label);
            b->failed = 1;
            if (b->err) {
                *b->err = (cxpr_error){0};
                b->err->code = CXPR_ERR_OUT_OF_MEMORY;
                b->err->message = "Failed to allocate snapshot role label";
            }
            return NULL;
        }
        cxpr_snapshot_set_string(&node->label, role_label);
        cxpr_snapshot_set_string(&node->display_label, role_display_label);
    }
    if (!active && node->resolved && node->resolved[0]) {
        char* inactive_label;
        size_t inactive_len = strlen(node->label ? node->label : "") + strlen(node->resolved) + 4u;
        inactive_label = (char*)malloc(inactive_len);
        if (!inactive_label) {
            b->failed = 1;
            return NULL;
        }
        snprintf(inactive_label, inactive_len, "%s\n= %s", node->label ? node->label : "", node->resolved);
        cxpr_snapshot_set_string(&node->display_label, inactive_label);
    }
    return node;
}

void cxpr_snapshot_set_string(char** dst, char* value) {
    free(*dst);
    *dst = value;
}

static bool cxpr_snapshot_role_is_display_prefix(const char* role);
static bool cxpr_snapshot_role_is_positional_arg(const char* role);
static bool cxpr_snapshot_role_uses_colon(const char* role);
static void cxpr_snapshot_refresh_display_label(cxpr_snapshot_node* node);

static char* cxpr_snapshot_display_label_for_node(const cxpr_snapshot_node* node) {
    const char* title;
    const char* value;
    const char* final_value;
    int show_final_value;
    size_t len;
    char* out;

    if (!node) return cxpr_snapshot_strdup("");
    title = node->label ? node->label : "";
    value = node->resolved && node->resolved[0] ? node->resolved : node->value_text;
    final_value = node->value_text ? node->value_text : "";
    if (!value) value = "";
    if (value[0] == '\0') return cxpr_snapshot_strdup(title);
    if (strcmp(node->kind ? node->kind : "", "record") == 0) {
        return cxpr_snapshot_strdup(final_value[0] ? final_value : value);
    }
    if (strcmp(node->kind ? node->kind : "", "function_call") == 0) {
        if (final_value[0] == '\0' || strcmp(title, final_value) == 0) {
            return cxpr_snapshot_strdup(title);
        }
        len = strlen(title) + strlen(final_value) + 4u;
        out = (char*)malloc(len);
        if (!out) return NULL;
        snprintf(out, len, "%s\n= %s", title, final_value);
        return out;
    }
    if (strcmp(node->kind ? node->kind : "", "binary_op") == 0 ||
        strcmp(node->kind ? node->kind : "", "unary_op") == 0 ||
        strcmp(node->kind ? node->kind : "", "ternary") == 0) {
        if (final_value[0] == '\0' || strcmp(title, final_value) == 0) {
            return cxpr_snapshot_strdup(title);
        }
        len = strlen(title) + strlen(final_value) + 4u;
        out = (char*)malloc(len);
        if (!out) return NULL;
        snprintf(out, len, "%s\n= %s", title, final_value);
        return out;
    }
    if (strcmp(node->kind ? node->kind : "", "producer_access") == 0 &&
        node->source && strcmp(value, node->source) == 0 && final_value[0] == '\0') {
        return cxpr_snapshot_strdup(title);
    }
    show_final_value = final_value[0] != '\0' && strcmp(value, final_value) != 0;

    if (strcmp(node->kind ? node->kind : "", "lookback") == 0 ||
        strcmp(node->kind ? node->kind : "", "field_access") == 0 ||
        strcmp(node->kind ? node->kind : "", "chain_access") == 0) {
        title = node->source && node->source[0] ? node->source : title;
    }
    if (cxpr_snapshot_role_is_positional_arg(node->role) &&
        node->source && node->source[0]) {
        title = node->source;
    }
    if (strcmp(title, value) == 0) {
        if (cxpr_snapshot_role_is_display_prefix(node->role) &&
            strcmp(node->role ? node->role : "", title) != 0) {
            len = strlen(node->role) + strlen(title) + 4u;
            out = (char*)malloc(len);
            if (!out) return NULL;
            if (cxpr_snapshot_role_uses_colon(node->role)) {
                snprintf(out, len, "%s: %s", node->role, title);
            } else {
                snprintf(out, len, "%s =\n%s", node->role, title);
            }
            return out;
        }
        return cxpr_snapshot_strdup(title);
    }
    if (cxpr_snapshot_role_is_display_prefix(node->role) &&
        strcmp(node->role ? node->role : "", title) != 0) {
        len = strlen(node->role) + strlen(title) + strlen(value) + 8u;
        if (show_final_value) len += strlen(final_value) + 4u;
        out = (char*)malloc(len);
        if (!out) return NULL;
        if (cxpr_snapshot_role_is_positional_arg(node->role)) {
            if (show_final_value) {
                snprintf(out, len, "%s =\n%s\n= %s\n= %s", node->role, title, value, final_value);
            } else {
                snprintf(out, len, "%s =\n%s\n= %s", node->role, title, value);
            }
        } else if (show_final_value && cxpr_snapshot_role_uses_colon(node->role)) {
            snprintf(out, len, "%s: %s\n= %s\n= %s", node->role, title, value, final_value);
        } else if (show_final_value) {
            snprintf(out, len, "%s =\n%s\n= %s\n= %s", node->role, title, value, final_value);
        } else if (cxpr_snapshot_role_uses_colon(node->role)) {
            snprintf(out, len, "%s: %s\n= %s", node->role, title, value);
        } else {
            snprintf(out, len, "%s =\n%s\n= %s", node->role, title, value);
        }
        return out;
    }

    len = strlen(title) + strlen(value) + 4u;
    if (show_final_value) len += strlen(final_value) + 4u;
    out = (char*)malloc(len);
    if (!out) return NULL;
    if (show_final_value) {
        snprintf(out, len, "%s\n= %s\n= %s", title, value, final_value);
    } else {
        snprintf(out, len, "%s\n= %s", title, value);
    }
    return out;
}

static bool cxpr_snapshot_role_is_display_prefix(const char* role) {
    if (!role || !role[0]) return false;
    if (strncmp(role, "sample[", 7u) == 0) return false;
    if (strcmp(role, "root") == 0 ||
        strcmp(role, "left") == 0 ||
        strcmp(role, "right") == 0 ||
        strcmp(role, "operand") == 0 ||
        strcmp(role, "condition") == 0 ||
        strcmp(role, "true") == 0 ||
        strcmp(role, "false") == 0 ||
        strcmp(role, "source") == 0 ||
        strcmp(role, "value") == 0 ||
        strcmp(role, "values") == 0 ||
        strcmp(role, "samples") == 0 ||
        strcmp(role, "index") == 0) {
        return false;
    }
    if (strncmp(role, "arg", 3u) == 0 && role[3] >= '0' && role[3] <= '9') {
        return true;
    }
    return true;
}

static bool cxpr_snapshot_role_is_positional_arg(const char* role) {
    if (!role || strncmp(role, "arg", 3u) != 0 ||
        role[3] < '0' || role[3] > '9') {
        return false;
    }
    for (size_t i = 4u; role[i]; ++i) {
        if (role[i] < '0' || role[i] > '9') return false;
    }
    return true;
}

static bool cxpr_snapshot_role_uses_colon(const char* role) {
    return role && (strncmp(role, "item[", 5u) == 0 || strncmp(role, "sample[", 7u) == 0);
}

static const char* cxpr_snapshot_registered_function_arg_role(const cxpr_registry* reg,
                                                              const char* function_name,
                                                              size_t index) {
    cxpr_func_entry* entry;
    const char* const* param_names;
    size_t param_count = 0u;

    if (!reg || !function_name) return NULL;
    entry = cxpr_registry_find(reg, function_name);
    if (!entry) return NULL;
    param_names = cxpr_registry_entry_param_names(entry, &param_count);
    if (!param_names || index >= param_count) return NULL;
    return param_names[index];
}

static const char* cxpr_snapshot_function_arg_role(const cxpr_registry* reg,
                                                   const char* function_name,
                                                   size_t index,
                                                   const char* explicit_name) {
    if (explicit_name) return explicit_name;
    {
        const char* registered_name =
            cxpr_snapshot_registered_function_arg_role(reg, function_name, index);
        if (registered_name) return registered_name;
    }
    if (!function_name) return NULL;
    if (strcmp(function_name, "falling") == 0 ||
        strcmp(function_name, "rising") == 0 ||
        strcmp(function_name, "net_up") == 0 ||
        strcmp(function_name, "net_down") == 0) {
        return index == 0u ? "value" : (index == 1u ? "samples" : NULL);
    }
    if (strcmp(function_name, "contains") == 0) {
        return index == 0u ? "source" : (index == 1u ? "values" : NULL);
    }
    if (strcmp(function_name, "within") == 0) {
        switch (index) {
            case 0u: return "source";
            case 1u: return "min";
            case 2u: return "max";
            case 3u: return "include_min";
            case 4u: return "include_max";
            default: return NULL;
        }
    }
    return NULL;
}

static int cxpr_snapshot_role_is_function_arg_label(const char* role) {
    if (!role || !role[0]) return 0;
    return strcmp(role, "function") != 0;
}

static bool cxpr_snapshot_relabel_arg_node(cxpr_snapshot_builder* b,
                                           cxpr_snapshot_node* node,
                                           const char* role) {
    char* role_label;
    const char* source;
    const char* resolved;
    const char* value;
    size_t display_len;
    char* display_label;

    if (!node || !cxpr_snapshot_role_is_function_arg_label(role)) return true;
    if (strcmp(node->kind ? node->kind : "", "binary_op") == 0 ||
        strcmp(node->kind ? node->kind : "", "unary_op") == 0 ||
        strcmp(node->kind ? node->kind : "", "ternary") == 0 ||
        strcmp(node->kind ? node->kind : "", "function_call") == 0 ||
        strcmp(node->kind ? node->kind : "", "producer_access") == 0 ||
        strcmp(node->kind ? node->kind : "", "field_access") == 0 ||
        strcmp(node->kind ? node->kind : "", "chain_access") == 0 ||
        strcmp(node->kind ? node->kind : "", "lookback") == 0) {
        return true;
    }
    role_label = cxpr_snapshot_strdup(role);
    if (!role_label) {
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot argument label";
        }
        return false;
    }
    cxpr_snapshot_set_string(&node->label, role_label);
    cxpr_snapshot_refresh_display_label(node);

    source = node->source ? node->source : "";
    resolved = node->resolved ? node->resolved : "";
    value = node->value_text ? node->value_text : "";
    if (!source[0]) return true;

    display_len = strlen(role) + strlen(source) + 5u;
    if (resolved[0] && strcmp(resolved, source) != 0) {
        display_len += strlen(resolved) + 4u;
    }
    if (value[0] && strcmp(value, resolved[0] ? resolved : source) != 0) {
        display_len += strlen(value) + 4u;
    }
    display_label = (char*)malloc(display_len);
    if (!display_label) {
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot argument display label";
        }
        return false;
    }
    snprintf(display_label, display_len, "%s =\n%s", role, source);
    if (resolved[0] && strcmp(resolved, source) != 0) {
        strcat(display_label, "\n= ");
        strcat(display_label, resolved);
    }
    if (value[0] && strcmp(value, resolved[0] ? resolved : source) != 0) {
        strcat(display_label, "\n= ");
        strcat(display_label, value);
    }
    cxpr_snapshot_set_string(&node->display_label, display_label);
    return true;
}

static void cxpr_snapshot_refresh_display_label(cxpr_snapshot_node* node) {
    char* display_label = cxpr_snapshot_display_label_for_node(node);
    if (!display_label) return;
    cxpr_snapshot_set_string(&node->display_label, display_label);
}

static cxpr_snapshot_node* cxpr_snapshot_add_text_child(cxpr_snapshot_builder* b,
                                                        const cxpr_expr_ast* ast,
                                                        size_t parent_id,
                                                        const char* role,
                                                        const char* kind,
                                                        const char* label,
                                                        const char* source,
                                                        int active) {
    cxpr_snapshot_node* node;
    const char* text;
    size_t display_len;
    char* display_label;

    node = cxpr_snapshot_add_node(b, ast, parent_id, 1, role, active, NULL);
    if (!node) return NULL;
    cxpr_snapshot_set_string(&node->kind, cxpr_snapshot_strdup(kind ? kind : "value"));
    cxpr_snapshot_set_string(&node->label, cxpr_snapshot_strdup(label ? label : ""));
    cxpr_snapshot_set_string(&node->source, cxpr_snapshot_strdup(source ? source : (label ? label : "")));
    cxpr_snapshot_set_string(&node->resolved, cxpr_snapshot_strdup(source ? source : (label ? label : "")));
    if (!node->kind || !node->label || !node->source || !node->resolved) {
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot metadata node";
        }
        return NULL;
    }
    node->state = active ? CXPR_SNAPSHOT_STATE_VALUE : CXPR_SNAPSHOT_STATE_SKIPPED;
    text = source ? source : (label ? label : "");
    display_len = strlen(role ? role : "") + strlen(text) + 5u;
    display_label = (char*)malloc(display_len);
    if (!display_label) {
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot metadata label";
        }
        return NULL;
    }
    if (role && (strcmp(role, "object") == 0 || strcmp(role, "field") == 0)) {
        snprintf(display_label, display_len, "%s", text);
    } else {
        snprintf(display_label, display_len, "%s =\n%s", role ? role : "", text);
    }
    cxpr_snapshot_set_string(&node->display_label, display_label);
    return node;
}

static bool cxpr_snapshot_set_value(cxpr_snapshot_node* node, const cxpr_value* value) {
    cxpr_value clone;
    char* text;

    clone = cxpr_value_clone(value);
    text = cxpr_snapshot_value_text(value);
    if (!text || cxpr_snapshot_value_clone_failed(value, &clone)) {
        free(text);
        cxpr_value_free(&clone);
        return false;
    }

    cxpr_value_free(&node->value);
    node->value = clone;
    node->has_value = 1;
    cxpr_snapshot_set_string(&node->value_text, text);
    if (!node->resolved || node->resolved[0] == '\0') {
        cxpr_snapshot_set_string(&node->resolved, cxpr_snapshot_strdup(text));
    }
    node->state = cxpr_snapshot_state_for_value(value);
    cxpr_snapshot_refresh_display_label(node);
    return true;
}

static bool cxpr_snapshot_eval_node(cxpr_snapshot_builder* b,
                                    const cxpr_expr_ast* ast,
                                    cxpr_snapshot_node* node) {
    cxpr_value value;
    cxpr_error local_err = {0};
    const char* references[1];
    const char* parameters[1];

    if (!b->ctx &&
        (cxpr_expr_ast_references(ast, references, 1u) > 0u ||
         cxpr_expr_ast_variables_used(ast, parameters, 1u) > 0u)) {
        node->state = CXPR_SNAPSHOT_STATE_UNKNOWN;
        cxpr_snapshot_set_string(&node->resolved, cxpr_expr_ast_to_string(ast));
        cxpr_snapshot_refresh_display_label(node);
        return true;
    }

    if (!cxpr_eval_ast(ast, b->ctx, b->reg, &value, &local_err)) {
        /* A context-free snapshot describes an expression symbolically. Missing
         * document inputs, parameters, and imported functions are therefore
         * unresolved; document analysis remains responsible for validation. */
        if (!b->ctx && (local_err.code == CXPR_ERR_UNKNOWN_IDENTIFIER ||
                        local_err.code == CXPR_ERR_UNKNOWN_FUNCTION ||
                        (local_err.message &&
                         strcmp(local_err.message,
                                "Native lookback requires a registry lookback resolver") == 0))) {
            char* source = cxpr_expr_ast_to_string(ast);
            node->state = CXPR_SNAPSHOT_STATE_UNKNOWN;
            cxpr_snapshot_set_string(&node->resolved, source);
            cxpr_snapshot_refresh_display_label(node);
            return true;
        }
        node->state = CXPR_SNAPSHOT_STATE_ERROR;
        cxpr_snapshot_set_string(&node->resolved,
                                 cxpr_snapshot_strdup(local_err.message ? local_err.message : "error"));
        return true;
    }
    if (!cxpr_snapshot_set_value(node, &value)) {
        if (cxpr_expr_ast_kind_of(ast) != CXPR_NODE_STRING) cxpr_value_free(&value);
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot value";
        }
        return false;
    }
    /*
     * String literal evaluation borrows storage owned by the AST. Releasing it
     * here invalidates later parent-node evaluation in the same snapshot walk.
     */
    if (cxpr_expr_ast_kind_of(ast) != CXPR_NODE_STRING) cxpr_value_free(&value);
    return true;
}

static void cxpr_snapshot_add_producer_field_node(cxpr_snapshot_builder* b,
                                                  const cxpr_expr_ast* ast,
                                                  size_t producer_parent_id,
                                                  int active) {
    const char* field;
    cxpr_snapshot_node* field_node;
    size_t display_len;
    char* display_label;

    if (!active || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_PRODUCER_ACCESS) return;
    field = cxpr_expr_ast_producer_field(ast);
    if (!field || !field[0]) return;

    field_node = cxpr_snapshot_add_node(b, ast, producer_parent_id, 1, "field", 1, NULL);
    if (!field_node) return;
    cxpr_snapshot_set_string(&field_node->kind, cxpr_snapshot_strdup("field"));
    cxpr_snapshot_set_string(&field_node->label, cxpr_snapshot_strdup("field"));
    cxpr_snapshot_set_string(&field_node->source, cxpr_snapshot_strdup(field));
    cxpr_snapshot_set_string(&field_node->resolved, cxpr_snapshot_strdup(field));
    field_node->state = CXPR_SNAPSHOT_STATE_VALUE;
    display_len = strlen(field) + 10u;
    display_label = (char*)malloc(display_len);
    if (!display_label) {
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot producer field label";
        }
        return;
    }
    snprintf(display_label, display_len, "%s", field);
    cxpr_snapshot_set_string(&field_node->display_label, display_label);
}

static char* cxpr_snapshot_join3(const char* left, const char* mid, const char* right) {
    size_t len;
    char* out;

    if (!left) left = "";
    if (!mid) mid = "";
    if (!right) right = "";
    len = strlen(left) + strlen(mid) + strlen(right) + 3u;
    out = (char*)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s %s %s", left, mid, right);
    return out;
}

static char* cxpr_snapshot_join_unary(const char* op, const char* value) {
    size_t len;
    char* out;

    if (!op) op = "";
    if (!value) value = "";
    len = strlen(op) + strlen(value) + 3u;
    out = (char*)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s %s", op, value);
    return out;
}

static char* cxpr_snapshot_join_ternary(const char* condition,
                                        const char* true_value,
                                        const char* false_value) {
    size_t len;
    char* out;

    if (!condition) condition = "";
    if (!true_value) true_value = "";
    if (!false_value) false_value = "";
    len = strlen(condition) + strlen(true_value) + strlen(false_value) + 8u;
    out = (char*)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s ? %s : %s", condition, true_value, false_value);
    return out;
}

static size_t cxpr_snapshot_visit(cxpr_snapshot_builder* b,
                                  const cxpr_expr_ast* ast,
                                  size_t parent_id,
                                  int has_parent,
                                  const char* role,
                                  int active,
                                  const char* inactive_reason);

static const char* cxpr_snapshot_window_reducer_name(const char* name) {
    if (!name) return NULL;
    if (strcmp(name, "__cxpr_window_highest") == 0) return "max";
    if (strcmp(name, "__cxpr_window_lowest") == 0) return "min";
    if (strcmp(name, "__cxpr_window_sum") == 0) return "sum";
    if (strcmp(name, "__cxpr_window_mean") == 0) return "mean";
    if (strcmp(name, "__cxpr_window_wma") == 0) return "wma";
    if (strcmp(name, "__cxpr_window_stddev") == 0) return "stddev";
    if (strcmp(name, "__cxpr_window_roc") == 0) return "roc";
    if (strcmp(name, "__cxpr_window_mean_absdev") == 0) return "mean_absdev";
    return NULL;
}

static void cxpr_snapshot_restore_window_call(cxpr_snapshot_builder* b,
                                              const cxpr_expr_ast* ast,
                                              size_t reducer_id) {
    const char* reducer = cxpr_snapshot_window_reducer_name(cxpr_expr_ast_call_name(ast));
    cxpr_snapshot_node* window;
    size_t window_id;

    if (!reducer) return;
    window = cxpr_snapshot_add_node(b, ast, reducer_id, 1, "value", 1, NULL);
    if (!window) return;
    window_id = window->id;
    cxpr_snapshot_set_string(&window->kind, cxpr_snapshot_strdup("function_call"));
    cxpr_snapshot_set_string(&window->label, cxpr_snapshot_strdup("window"));
    cxpr_snapshot_set_string(&window->display_label, cxpr_snapshot_strdup("window"));
    cxpr_snapshot_set_string(&window->source, cxpr_snapshot_strdup("window"));
    cxpr_snapshot_set_string(&window->resolved, cxpr_snapshot_strdup("window"));
    window->state = CXPR_SNAPSHOT_STATE_UNKNOWN;

    for (size_t i = 0u; i < window_id; ++i) {
        cxpr_snapshot_node* child = &b->snapshot->nodes[i];
        if (!child->has_parent || child->parent_id != reducer_id) continue;
        if (strcmp(child->role ? child->role : "", "value") == 0) {
            child->parent_id = window_id;
            cxpr_snapshot_set_string(&child->role, cxpr_snapshot_strdup("expression"));
        } else if (strcmp(child->role ? child->role : "", "samples") == 0) {
            child->parent_id = window_id;
            cxpr_snapshot_set_string(&child->role, cxpr_snapshot_strdup("period"));
        }
    }

    {
        cxpr_snapshot_node* node = &b->snapshot->nodes[reducer_id];
        cxpr_snapshot_set_string(&node->label, cxpr_snapshot_strdup(reducer));
        cxpr_snapshot_set_string(&node->source, cxpr_snapshot_strdup(reducer));
        cxpr_snapshot_set_string(&node->resolved, cxpr_snapshot_strdup(reducer));
        cxpr_snapshot_refresh_display_label(node);
    }
}

static void cxpr_snapshot_mark_children_skipped(cxpr_snapshot_builder* b,
                                                const cxpr_expr_ast* ast,
                                                size_t parent_id,
                                                const char* role,
                                                const char* reason) {
    (void)cxpr_snapshot_visit(b, ast, parent_id, 1, role, 0, reason);
}

static size_t cxpr_snapshot_visit_children(cxpr_snapshot_builder* b,
                                           const cxpr_expr_ast* ast,
                                           cxpr_snapshot_node* node,
                                           int active) {
    size_t first_child = (size_t)-1;
    size_t child;
    size_t count;
    size_t parent_id;
    char* inactive_reason = NULL;
    char role[32];

    parent_id = node->id;
    if (!active && node->resolved) {
        inactive_reason = cxpr_snapshot_strdup(node->resolved);
        if (!inactive_reason) {
            b->failed = 1;
            return (size_t)-1;
        }
    }
    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_RECORD:
            count = cxpr_expr_ast_record_field_count(ast);
            for (size_t i = 0; i < count; ++i) {
                const char* field_name = cxpr_expr_ast_record_field_name(ast, i);
                child = cxpr_snapshot_visit(
                    b, cxpr_expr_ast_record_field_value(ast, i), parent_id, 1,
                    field_name ? field_name : "field", active, inactive_reason);
                if (first_child == (size_t)-1) first_child = child;
            }
            break;
        case CXPR_NODE_ARRAY:
            count = ast->data.array.count;
            for (size_t i = 0; i < count; ++i) {
                snprintf(role, sizeof(role), "item[%zu]", i);
                child = cxpr_snapshot_visit(b, ast->data.array.elements[i], parent_id, 1,
                                            role, active, inactive_reason);
                if (first_child == (size_t)-1) first_child = child;
            }
            break;
        case CXPR_NODE_FIELD_ACCESS:
            if (cxpr_expr_ast_field_base(ast)) {
                child = cxpr_snapshot_visit(b, cxpr_expr_ast_field_base(ast), parent_id, 1,
                                            "base", active, inactive_reason);
            } else {
                child = cxpr_snapshot_add_text_child(b, ast, parent_id, "object",
                                                     "identifier",
                                                     "object",
                                                     cxpr_expr_ast_field_object(ast),
                                                     active)
                    ? b->snapshot->node_count - 1u
                    : (size_t)-1;
            }
            if (first_child == (size_t)-1) first_child = child;
            (void)cxpr_snapshot_add_text_child(b, ast, parent_id, "field",
                                               "field",
                                               "field",
                                               cxpr_expr_ast_field_name(ast),
                                               active);
            break;
        case CXPR_NODE_CHAIN_ACCESS:
            count = cxpr_expr_ast_chain_count(ast);
            for (size_t i = 0; i < count; ++i) {
                snprintf(role, sizeof(role), "segment[%zu]", i);
                child = cxpr_snapshot_add_text_child(
                    b,
                    ast,
                    parent_id,
                    role,
                    i == 0u ? "identifier" : "field",
                    role,
                    cxpr_expr_ast_chain_segment(ast, i),
                    active)
                    ? b->snapshot->node_count - 1u
                    : (size_t)-1;
                if (first_child == (size_t)-1) first_child = child;
            }
            break;
        case CXPR_NODE_BINARY_OP:
            child = cxpr_snapshot_visit(b, cxpr_expr_ast_binary_left(ast), parent_id, 1, "left",
                                        active, inactive_reason);
            if (first_child == (size_t)-1) first_child = child;
            (void)cxpr_snapshot_visit(b, cxpr_expr_ast_binary_right(ast), parent_id, 1, "right",
                                      active, inactive_reason);
            break;
        case CXPR_NODE_UNARY_OP:
            child = cxpr_snapshot_visit(b, cxpr_expr_ast_unary_operand(ast), parent_id, 1, "operand",
                                        active, inactive_reason);
            if (first_child == (size_t)-1) first_child = child;
            break;
        case CXPR_NODE_FUNCTION_CALL:
            count = cxpr_expr_ast_call_arg_count(ast);
            for (size_t i = 0; i < count; ++i) {
                const char* name = cxpr_snapshot_function_arg_role(
                    b->reg,
                    cxpr_expr_ast_call_name(ast),
                    i,
                    cxpr_expr_ast_call_arg_name(ast, i));
                if (name) {
                    snprintf(role, sizeof(role), "%s", name);
                } else {
                    snprintf(role, sizeof(role), "arg%zu", i);
                }
                child = cxpr_snapshot_visit(b, cxpr_expr_ast_call_arg(ast, i), parent_id, 1,
                                            role, active, inactive_reason);
                if (!b->failed) {
                    (void)cxpr_snapshot_relabel_arg_node(b, &b->snapshot->nodes[child], role);
                }
                if (first_child == (size_t)-1) first_child = child;
            }
            break;
        case CXPR_NODE_PRODUCER_ACCESS:
            cxpr_snapshot_add_producer_field_node(b, ast, parent_id, active);
            count = cxpr_expr_ast_producer_arg_count(ast);
            for (size_t i = 0; i < count; ++i) {
                const char* name = cxpr_expr_ast_producer_arg_name(ast, i);
                if (name) {
                    snprintf(role, sizeof(role), "%s", name);
                } else {
                    snprintf(role, sizeof(role), "arg%zu", i);
                }
                child = cxpr_snapshot_visit(b, cxpr_expr_ast_producer_arg(ast, i), parent_id, 1,
                                            role, active, inactive_reason);
                if (!b->failed) {
                    (void)cxpr_snapshot_relabel_arg_node(b, &b->snapshot->nodes[child], role);
                }
                if (first_child == (size_t)-1) first_child = child;
            }
            break;
        case CXPR_NODE_LOOKBACK:
            child = cxpr_snapshot_visit(b, cxpr_expr_ast_lookback_target(ast), parent_id, 1,
                                        "source", active, inactive_reason);
            if (first_child == (size_t)-1) first_child = child;
            (void)cxpr_snapshot_visit(b, cxpr_expr_ast_lookback_index(ast), parent_id, 1,
                                      "index", active, inactive_reason);
            break;
        case CXPR_NODE_TERNARY:
            child = cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_condition(ast), parent_id, 1,
                                        "condition", active, inactive_reason);
            if (first_child == (size_t)-1) first_child = child;
            (void)cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_true(ast), parent_id, 1,
                                      "true", active, inactive_reason);
            (void)cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_false(ast), parent_id, 1,
                                      "false", active, inactive_reason);
            break;
        default:
            break;
    }
    free(inactive_reason);
    return first_child;
}

static size_t cxpr_snapshot_visit_binary(cxpr_snapshot_builder* b,
                                         const cxpr_expr_ast* ast,
                                         cxpr_snapshot_node* node,
                                         int active) {
    size_t parent_id;
    size_t left_id;
    size_t right_id;
    cxpr_snapshot_node* left_node;
    cxpr_snapshot_node* right_node;
    int op;

    if (!active) {
        return cxpr_snapshot_visit_children(b, ast, node, 0);
    }

    parent_id = node->id;
    op = cxpr_expr_ast_operator(ast);
    left_id = cxpr_snapshot_visit(b, cxpr_expr_ast_binary_left(ast), parent_id, 1, "left", 1, NULL);
    if (b->failed) return left_id;
    node = &b->snapshot->nodes[parent_id];
    left_node = &b->snapshot->nodes[left_id];

    if (op == CXPR_TOK_AND && left_node->has_value &&
        left_node->value.type == CXPR_VALUE_BOOL && !left_node->value.b) {
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_expr_ast_binary_right(ast), parent_id, "right",
            "and short-circuit: left side is false");
        node = &b->snapshot->nodes[parent_id];
        left_node = &b->snapshot->nodes[left_id];
        (void)cxpr_snapshot_set_value(node, &left_node->value);
        cxpr_snapshot_set_string(&node->resolved,
                                 cxpr_snapshot_join3(left_node->resolved, "and",
                                                     "right not evaluated"));
        cxpr_snapshot_refresh_display_label(node);
        return left_id;
    }
    if (op == CXPR_TOK_OR && left_node->has_value &&
        left_node->value.type == CXPR_VALUE_BOOL && left_node->value.b) {
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_expr_ast_binary_right(ast), parent_id, "right",
            "or short-circuit: left side is true");
        node = &b->snapshot->nodes[parent_id];
        left_node = &b->snapshot->nodes[left_id];
        (void)cxpr_snapshot_set_value(node, &left_node->value);
        cxpr_snapshot_set_string(&node->resolved,
                                 cxpr_snapshot_join3(left_node->resolved, "or",
                                                     "right not evaluated"));
        cxpr_snapshot_refresh_display_label(node);
        return left_id;
    }

    right_id = cxpr_snapshot_visit(b, cxpr_expr_ast_binary_right(ast), parent_id, 1, "right", 1, NULL);
    if (b->failed) return left_id;
    node = &b->snapshot->nodes[parent_id];
    left_node = &b->snapshot->nodes[left_id];
    right_node = &b->snapshot->nodes[right_id];
    (void)cxpr_snapshot_eval_node(b, ast, node);
    cxpr_snapshot_set_string(&node->resolved,
                             cxpr_snapshot_join3(left_node->value_text ? left_node->value_text : left_node->resolved,
                                                 cxpr_snapshot_binary_op_text(op),
                                                 right_node->value_text ? right_node->value_text : right_node->resolved));
    cxpr_snapshot_refresh_display_label(node);
    return left_id;
}

static size_t cxpr_snapshot_visit_ternary(cxpr_snapshot_builder* b,
                                          const cxpr_expr_ast* ast,
                                          cxpr_snapshot_node* node,
                                          int active) {
    size_t parent_id;
    size_t cond_id;
    size_t branch_id;
    cxpr_snapshot_node* cond;
    cxpr_snapshot_node* true_node;
    cxpr_snapshot_node* false_node;
    cxpr_snapshot_node* branch;
    const char* cond_text;
    const char* true_text;
    const char* false_text;

    if (!active) {
        return cxpr_snapshot_visit_children(b, ast, node, 0);
    }

    parent_id = node->id;
    cond_id = cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_condition(ast), parent_id, 1,
                                  "condition", 1, NULL);
    if (b->failed) return cond_id;
    cond = &b->snapshot->nodes[cond_id];
    if (!cond->has_value || cond->value.type != CXPR_VALUE_BOOL) {
        if (!b->ctx) {
            (void)cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_true(ast), parent_id, 1,
                                      "true", 1, NULL);
            (void)cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_false(ast), parent_id, 1,
                                      "false", 1, NULL);
            node = &b->snapshot->nodes[parent_id];
            node->state = CXPR_SNAPSHOT_STATE_UNKNOWN;
            cxpr_snapshot_set_string(&node->resolved, cxpr_expr_ast_to_string(ast));
            cxpr_snapshot_refresh_display_label(node);
            return cond_id;
        }
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_expr_ast_ternary_true(ast), parent_id, "true",
            "ternary branch not evaluated: condition is not boolean");
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_expr_ast_ternary_false(ast), parent_id, "false",
            "ternary branch not evaluated: condition is not boolean");
        node = &b->snapshot->nodes[parent_id];
        (void)cxpr_snapshot_eval_node(b, ast, node);
        return cond_id;
    }

    if (cond->value.b) {
        branch_id = cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_true(ast), parent_id, 1,
                                        "true", 1, NULL);
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_expr_ast_ternary_false(ast), parent_id, "false",
            "ternary condition true: false branch not evaluated");
    } else {
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_expr_ast_ternary_true(ast), parent_id, "true",
            "ternary condition false: true branch not evaluated");
        branch_id = cxpr_snapshot_visit(b, cxpr_expr_ast_ternary_false(ast), parent_id, 1,
                                        "false", 1, NULL);
    }
    if (b->failed) return cond_id;
    node = &b->snapshot->nodes[parent_id];
    cond = &b->snapshot->nodes[cond_id];
    branch = &b->snapshot->nodes[branch_id];
    true_node = NULL;
    false_node = NULL;
    for (size_t i = 0; i < b->snapshot->node_count; ++i) {
        cxpr_snapshot_node* candidate = &b->snapshot->nodes[i];
        if (!candidate->has_parent || candidate->parent_id != parent_id || !candidate->role) {
            continue;
        }
        if (strcmp(candidate->role, "true") == 0) true_node = candidate;
        if (strcmp(candidate->role, "false") == 0) false_node = candidate;
    }
    if (branch->has_value) (void)cxpr_snapshot_set_value(node, &branch->value);
    cond_text = cond->value_text ? cond->value_text : cond->resolved;
    true_text = true_node && true_node->active
        ? (true_node->value_text ? true_node->value_text : true_node->resolved)
        : "not evaluated";
    false_text = false_node && false_node->active
        ? (false_node->value_text ? false_node->value_text : false_node->resolved)
        : "not evaluated";
    cxpr_snapshot_set_string(&node->resolved,
                             cxpr_snapshot_join_ternary(cond_text, true_text, false_text));
    cxpr_snapshot_refresh_display_label(node);
    return cond_id;
}

static size_t cxpr_snapshot_visit_lookback(cxpr_snapshot_builder* b,
                                           const cxpr_expr_ast* ast,
                                           cxpr_snapshot_node* node,
                                           int active) {
    size_t parent_id;
    size_t target_id;

    if (!active) {
        return cxpr_snapshot_visit_children(b, ast, node, 0);
    }

    parent_id = node->id;
    target_id = cxpr_snapshot_visit(b, cxpr_expr_ast_lookback_target(ast), parent_id, 1,
                                    "source", 1, NULL);
    if (b->failed) return target_id;
    (void)cxpr_snapshot_visit(b, cxpr_expr_ast_lookback_index(ast), parent_id, 1,
                              "index", 1, NULL);
    if (!b->failed) {
        node = &b->snapshot->nodes[parent_id];
        (void)cxpr_snapshot_eval_node(b, ast, node);
    }
    return target_id;
}

static size_t cxpr_snapshot_visit(cxpr_snapshot_builder* b,
                                  const cxpr_expr_ast* ast,
                                  size_t parent_id,
                                  int has_parent,
                                  const char* role,
                                  int active,
                                  const char* inactive_reason) {
    cxpr_snapshot_node* node;
    size_t node_id;
    size_t first_child;

    node = cxpr_snapshot_add_node(b, ast, parent_id, has_parent, role, active,
                                  inactive_reason);
    if (!node) return (size_t)-1;
    node_id = node->id;

    if (!active) {
        (void)cxpr_snapshot_visit_children(b, ast, node, 0);
        return node_id;
    }

    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_BINARY_OP:
            (void)cxpr_snapshot_visit_binary(b, ast, node, 1);
            break;
        case CXPR_NODE_TERNARY:
            (void)cxpr_snapshot_visit_ternary(b, ast, node, 1);
            break;
        case CXPR_NODE_UNARY_OP:
            if (cxpr_expr_ast_operator(ast) == CXPR_TOK_MINUS &&
                cxpr_expr_ast_kind_of(cxpr_expr_ast_unary_operand(ast)) == CXPR_NODE_NUMBER) {
                char* literal = cxpr_expr_ast_to_string(ast);
                node = &b->snapshot->nodes[node_id];
                cxpr_snapshot_set_string(&node->kind, cxpr_snapshot_strdup("number"));
                cxpr_snapshot_set_string(&node->label, literal ? cxpr_snapshot_strdup(literal) : NULL);
                if (!literal || !node->label) {
                    free(literal);
                    b->failed = 1;
                    break;
                }
                free(literal);
                (void)cxpr_snapshot_eval_node(b, ast, node);
                break;
            }
            first_child = cxpr_snapshot_visit(b, cxpr_expr_ast_unary_operand(ast), node_id, 1,
                                              "operand", 1, NULL);
            if (!b->failed) {
                node = &b->snapshot->nodes[node_id];
                (void)cxpr_snapshot_eval_node(b, ast, node);
                cxpr_snapshot_set_string(&node->resolved,
                                         cxpr_snapshot_join_unary(node->label,
                                             b->snapshot->nodes[first_child].value_text ?
                                             b->snapshot->nodes[first_child].value_text :
                                             b->snapshot->nodes[first_child].resolved));
                cxpr_snapshot_refresh_display_label(node);
            }
            break;
        case CXPR_NODE_LOOKBACK:
            (void)cxpr_snapshot_visit_lookback(b, ast, node, 1);
            break;
        default:
            (void)cxpr_snapshot_visit_children(b, ast, node, 1);
            if (!b->failed) {
                node = &b->snapshot->nodes[node_id];
                (void)cxpr_snapshot_eval_node(b, ast, node);
                if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
                    cxpr_snapshot_restore_window_call(b, ast, node_id);
                }
            }
            break;
    }

    return node_id;
}

bool cxpr_eval_snapshot_build(const cxpr_expr_ast* ast,
                              const cxpr_context* ctx,
                              const cxpr_registry* reg,
                              cxpr_eval_snapshot* out_snapshot,
                              cxpr_error* err) {
    cxpr_snapshot_builder b;

    if (!out_snapshot) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Snapshot output is NULL";
        }
        return false;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (err) *err = (cxpr_error){0};
    if (!ast) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Snapshot requires an AST";
        }
        return false;
    }

    out_snapshot->expression = cxpr_expr_ast_to_string(ast);
    if (!out_snapshot->expression) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Failed to allocate expression string";
        }
        return false;
    }

    b.ctx = ctx;
    b.reg = reg;
    b.snapshot = out_snapshot;
    b.err = err;
    b.failed = 0;
    (void)cxpr_snapshot_visit(&b, ast, 0u, 0, "root", 1, NULL);
    if (b.failed) {
        cxpr_eval_snapshot_free(out_snapshot);
        return false;
    }

    if (out_snapshot->node_count > 0u) {
        cxpr_snapshot_node* root = &out_snapshot->nodes[0];
        if (root->has_value) {
            out_snapshot->result = cxpr_value_clone(&root->value);
            out_snapshot->has_result = 1;
        }
        out_snapshot->resolved = cxpr_snapshot_strdup(root->resolved);
        out_snapshot->state = root->state;
    }
    return true;
}

void cxpr_eval_snapshot_free(cxpr_eval_snapshot* snapshot) {
    if (!snapshot) return;
    free(snapshot->expression);
    free(snapshot->resolved);
    cxpr_value_free(&snapshot->result);
    for (size_t i = 0; i < snapshot->node_count; ++i) {
        cxpr_snapshot_node* node = &snapshot->nodes[i];
        free(node->role);
        free(node->kind);
        free(node->label);
        free(node->display_label);
        free(node->source);
        free(node->resolved);
        free(node->value_text);
        cxpr_value_free(&node->value);
    }
    free(snapshot->nodes);
    memset(snapshot, 0, sizeof(*snapshot));
}
