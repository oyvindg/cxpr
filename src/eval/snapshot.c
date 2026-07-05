/**
 * @file snapshot.c
 * @brief Single-evaluation AST diagnostics.
 */

#include <cxpr/ast.h>
#include <cxpr/context.h>
#include <cxpr/eval.h>
#include <cxpr/eval_snapshot.h>
#include <cxpr/registry.h>
#include <cxpr/token.h>

#include "context/state.h"
#include "expression/internal.h"

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

static char* cxpr_snapshot_strdup(const char* text) {
    size_t len;
    char* copy;

    if (!text) text = "";
    len = strlen(text);
    copy = (char*)malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static char* cxpr_snapshot_printf_number(double value) {
    char buf[64];

    if (isnan(value)) return cxpr_snapshot_strdup("nan");
    if (isinf(value)) return cxpr_snapshot_strdup(value < 0.0 ? "-inf" : "inf");
    snprintf(buf, sizeof(buf), "%.12g", value);
    return cxpr_snapshot_strdup(buf);
}

static char* cxpr_snapshot_value_text(const cxpr_value* value);

static int cxpr_snapshot_append_text(char** buf, size_t* len, size_t* cap, const char* text) {
    size_t text_len;
    size_t needed;
    char* grown;

    if (!buf || !len || !cap) return 0;
    if (!text) text = "";
    text_len = strlen(text);
    needed = *len + text_len + 1u;
    if (needed > *cap) {
        size_t next = *cap ? *cap : 64u;
        while (next < needed) {
            if (next > (size_t)-1 / 2u) {
                next = needed;
                break;
            }
            next *= 2u;
        }
        grown = (char*)realloc(*buf, next);
        if (!grown) return 0;
        *buf = grown;
        *cap = next;
    }
    memcpy(*buf + *len, text, text_len);
    *len += text_len;
    (*buf)[*len] = '\0';
    return 1;
}

static char* cxpr_snapshot_array_text(const cxpr_array_value* array) {
    char* out = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    size_t shown;

    if (!array) return cxpr_snapshot_strdup("[]");
    shown = array->count < 6u ? array->count : 6u;
    if (!cxpr_snapshot_append_text(&out, &len, &cap, "[")) return NULL;
    for (size_t i = 0; i < shown; ++i) {
        char* item = cxpr_snapshot_value_text(&array->values[i]);
        if (!item) {
            free(out);
            return NULL;
        }
        if (i > 0u && !cxpr_snapshot_append_text(&out, &len, &cap, ", ")) {
            free(item);
            free(out);
            return NULL;
        }
        if (!cxpr_snapshot_append_text(&out, &len, &cap, item)) {
            free(item);
            free(out);
            return NULL;
        }
        free(item);
    }
    if (array->count > shown) {
        if (shown > 0u && !cxpr_snapshot_append_text(&out, &len, &cap, ", ")) {
            free(out);
            return NULL;
        }
        if (!cxpr_snapshot_append_text(&out, &len, &cap, "...")) {
            free(out);
            return NULL;
        }
    }
    if (!cxpr_snapshot_append_text(&out, &len, &cap, "]")) {
        free(out);
        return NULL;
    }
    return out;
}

static char* cxpr_snapshot_value_text(const cxpr_value* value) {
    char buf[64];

    if (!value) return NULL;
    switch (value->type) {
        case CXPR_VALUE_NUMBER:
            return cxpr_snapshot_printf_number(value->d);
        case CXPR_VALUE_BOOL:
            return cxpr_snapshot_strdup(value->b ? "true" : "false");
        case CXPR_VALUE_STRING:
            return cxpr_snapshot_strdup(value->str ? value->str : "");
        case CXPR_VALUE_NULL:
            return cxpr_snapshot_strdup("null");
        case CXPR_VALUE_TIMESTAMP:
        case CXPR_VALUE_DURATION:
            snprintf(buf, sizeof(buf), "%lld", (long long)value->i64);
            return cxpr_snapshot_strdup(buf);
        case CXPR_VALUE_STRUCT:
            return cxpr_snapshot_strdup("{struct}");
        case CXPR_VALUE_ARRAY:
            return cxpr_snapshot_array_text(value->a);
        default:
            return cxpr_snapshot_strdup("{value}");
    }
}

static cxpr_snapshot_state cxpr_snapshot_state_for_value(const cxpr_value* value) {
    if (!value) return CXPR_SNAPSHOT_STATE_UNKNOWN;
    if (value->type == CXPR_VALUE_BOOL) {
        return value->b ? CXPR_SNAPSHOT_STATE_TRUE : CXPR_SNAPSHOT_STATE_FALSE;
    }
    if (value->type == CXPR_VALUE_NUMBER) return CXPR_SNAPSHOT_STATE_NUMBER;
    return CXPR_SNAPSHOT_STATE_VALUE;
}

const char* cxpr_snapshot_state_name(cxpr_snapshot_state state) {
    switch (state) {
        case CXPR_SNAPSHOT_STATE_TRUE: return "true";
        case CXPR_SNAPSHOT_STATE_FALSE: return "false";
        case CXPR_SNAPSHOT_STATE_NUMBER: return "number";
        case CXPR_SNAPSHOT_STATE_VALUE: return "value";
        case CXPR_SNAPSHOT_STATE_SKIPPED: return "skipped";
        case CXPR_SNAPSHOT_STATE_ERROR: return "error";
        default: return "unknown";
    }
}

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

static const char* cxpr_snapshot_kind(const cxpr_ast* ast) {
    if (!ast) return "unknown";
    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_NUMBER: return "number";
        case CXPR_NODE_BOOL: return "bool";
        case CXPR_NODE_ARRAY: return "array";
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

static char* cxpr_snapshot_label(const cxpr_ast* ast) {
    char buf[128];
    size_t depth;

    if (!ast) return cxpr_snapshot_strdup("?");
    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_NUMBER:
            return cxpr_snapshot_printf_number(cxpr_ast_number_value(ast));
        case CXPR_NODE_BOOL:
            return cxpr_snapshot_strdup(cxpr_ast_bool_value(ast) ? "true" : "false");
        case CXPR_NODE_STRING:
            return cxpr_snapshot_strdup(cxpr_ast_string_value(ast));
        case CXPR_NODE_IDENTIFIER:
            return cxpr_snapshot_strdup(cxpr_ast_identifier_name(ast));
        case CXPR_NODE_VARIABLE:
            snprintf(buf, sizeof(buf), "$%s", cxpr_ast_variable_name(ast));
            return cxpr_snapshot_strdup(buf);
        case CXPR_NODE_FIELD_ACCESS:
            snprintf(buf, sizeof(buf), "%s.%s",
                     cxpr_ast_field_object(ast), cxpr_ast_field_name(ast));
            return cxpr_snapshot_strdup(buf);
        case CXPR_NODE_CHAIN_ACCESS:
            depth = cxpr_ast_chain_depth(ast);
            return cxpr_snapshot_strdup(depth ? cxpr_ast_chain_segment(ast, depth - 1u) : "chain");
        case CXPR_NODE_BINARY_OP:
            return cxpr_snapshot_strdup(cxpr_snapshot_binary_op_text(cxpr_ast_operator(ast)));
        case CXPR_NODE_UNARY_OP:
            return cxpr_snapshot_strdup(cxpr_snapshot_unary_op_text(cxpr_ast_operator(ast)));
        case CXPR_NODE_FUNCTION_CALL:
            return cxpr_snapshot_strdup(cxpr_ast_function_name(ast));
        case CXPR_NODE_PRODUCER_ACCESS:
            snprintf(buf, sizeof(buf), "%s.%s",
                     cxpr_ast_producer_name(ast), cxpr_ast_producer_field(ast));
            return cxpr_snapshot_strdup(buf);
        case CXPR_NODE_LOOKBACK:
            return cxpr_snapshot_strdup("lookback");
        case CXPR_NODE_TERNARY:
            return cxpr_snapshot_strdup("?:");
        case CXPR_NODE_ARRAY:
            return cxpr_snapshot_strdup("array");
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

static void cxpr_snapshot_set_string(char** dst, char* value);

static cxpr_snapshot_node* cxpr_snapshot_add_node(cxpr_snapshot_builder* b,
                                                  const cxpr_ast* ast,
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
    node->source = cxpr_ast_to_string(ast);
    node->resolved = cxpr_snapshot_strdup(active ? "" :
        (inactive_reason ? inactive_reason : "not evaluated"));
    node->active = active;
    node->state = active ? CXPR_SNAPSHOT_STATE_UNKNOWN : CXPR_SNAPSHOT_STATE_SKIPPED;

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

static void cxpr_snapshot_set_string(char** dst, char* value) {
    free(*dst);
    *dst = value;
}

static bool cxpr_snapshot_role_is_display_prefix(const char* role);

static char* cxpr_snapshot_display_label_for_node(const cxpr_snapshot_node* node) {
    const char* title;
    const char* value;
    size_t len;
    char* out;

    if (!node) return cxpr_snapshot_strdup("");
    title = node->label ? node->label : "";
    value = node->value_text && node->value_text[0] ? node->value_text : node->resolved;
    if (!value) value = "";
    if (value[0] == '\0') return cxpr_snapshot_strdup(title);

    if (strcmp(node->kind ? node->kind : "", "binary_op") == 0 ||
        strcmp(node->kind ? node->kind : "", "lookback") == 0 ||
        strcmp(node->kind ? node->kind : "", "function_call") == 0) {
        title = node->source && node->source[0] ? node->source : title;
    }
    if (strcmp(title, value) == 0) {
        if (cxpr_snapshot_role_is_display_prefix(node->role)) {
            len = strlen(node->role) + strlen(title) + 3u;
            out = (char*)malloc(len);
            if (!out) return NULL;
            snprintf(out, len, "%s: %s", node->role, title);
            return out;
        }
        return cxpr_snapshot_strdup(title);
    }
    if (cxpr_snapshot_role_is_display_prefix(node->role)) {
        len = strlen(node->role) + strlen(title) + strlen(value) + 8u;
        out = (char*)malloc(len);
        if (!out) return NULL;
        snprintf(out, len, "%s: %s\n= %s", node->role, title, value);
        return out;
    }

    len = strlen(title) + strlen(value) + 4u;
    out = (char*)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s\n= %s", title, value);
    return out;
}

static bool cxpr_snapshot_role_is_display_prefix(const char* role) {
    if (!role || !role[0]) return false;
    if (strcmp(role, "root") == 0 ||
        strcmp(role, "left") == 0 ||
        strcmp(role, "right") == 0 ||
        strcmp(role, "source") == 0 ||
        strcmp(role, "value") == 0 ||
        strcmp(role, "values") == 0 ||
        strcmp(role, "samples") == 0 ||
        strcmp(role, "index") == 0) {
        return false;
    }
    if (strncmp(role, "arg", 3u) == 0 && role[3] >= '0' && role[3] <= '9') {
        for (size_t i = 4u; role[i]; ++i) {
            if (role[i] < '0' || role[i] > '9') return true;
        }
        return false;
    }
    return true;
}

static const char* cxpr_snapshot_function_arg_role(const char* function_name,
                                                   size_t index,
                                                   const char* explicit_name) {
    if (explicit_name) return explicit_name;
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

static bool cxpr_snapshot_trend_function(const char* name) {
    return name &&
           (strcmp(name, "falling") == 0 ||
            strcmp(name, "rising") == 0 ||
            strcmp(name, "net_up") == 0 ||
            strcmp(name, "net_down") == 0);
}

static void cxpr_snapshot_refresh_display_label(cxpr_snapshot_node* node) {
    char* display_label = cxpr_snapshot_display_label_for_node(node);
    if (!display_label) return;
    cxpr_snapshot_set_string(&node->display_label, display_label);
}

static bool cxpr_snapshot_set_value(cxpr_snapshot_node* node, const cxpr_value* value) {
    cxpr_value clone;
    char* text;

    clone = cxpr_value_clone(value);
    text = cxpr_snapshot_value_text(value);
    if (!text) {
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
                                    const cxpr_ast* ast,
                                    cxpr_snapshot_node* node) {
    cxpr_value value;
    cxpr_error local_err = {0};

    if (!cxpr_eval_ast(ast, b->ctx, b->reg, &value, &local_err)) {
        node->state = CXPR_SNAPSHOT_STATE_ERROR;
        cxpr_snapshot_set_string(&node->resolved,
                                 cxpr_snapshot_strdup(local_err.message ? local_err.message : "error"));
        return true;
    }
    if (!cxpr_snapshot_set_value(node, &value)) {
        cxpr_value_free(&value);
        b->failed = 1;
        if (b->err) {
            *b->err = (cxpr_error){0};
            b->err->code = CXPR_ERR_OUT_OF_MEMORY;
            b->err->message = "Failed to allocate snapshot value";
        }
        return false;
    }
    cxpr_value_free(&value);
    return true;
}

static void cxpr_snapshot_add_timeseries_samples(cxpr_snapshot_builder* b,
                                                 const cxpr_ast* ast,
                                                 cxpr_snapshot_node* node,
                                                 int active) {
    const cxpr_ast* value_ast;
    const cxpr_ast* samples_ast;
    cxpr_value samples_value;
    cxpr_error local_err = {0};
    long long sample_count;
    char* value_source;

    if (!active || !ast || cxpr_ast_type(ast) != CXPR_NODE_FUNCTION_CALL) return;
    if (!cxpr_snapshot_trend_function(cxpr_ast_function_name(ast))) return;
    if (cxpr_ast_function_argc(ast) < 2u) return;

    value_ast = cxpr_ast_function_arg(ast, 0u);
    samples_ast = cxpr_ast_function_arg(ast, 1u);
    if (!cxpr_eval_ast(samples_ast, b->ctx, b->reg, &samples_value, &local_err)) return;
    if (samples_value.type != CXPR_VALUE_NUMBER || !isfinite(samples_value.d)) {
        cxpr_value_free(&samples_value);
        return;
    }
    sample_count = (long long)floor(samples_value.d);
    cxpr_value_free(&samples_value);
    if (sample_count < 1) return;
    if (sample_count > 8) sample_count = 8;

    value_source = cxpr_ast_to_string(value_ast);
    if (!value_source) {
        b->failed = 1;
        return;
    }

    for (long long i = 0; i < sample_count; ++i) {
        char role[32];
        char source[512];
        cxpr_snapshot_node* sample_node;
        cxpr_value value;
        cxpr_error value_err = {0};

        snprintf(role, sizeof(role), "sample[%lld]", i);
        if (i == 0) {
            snprintf(source, sizeof(source), "%s", value_source);
        } else {
            snprintf(source, sizeof(source), "%s[%lld]", value_source, i);
        }

        sample_node = cxpr_snapshot_add_node(b, value_ast, node->id, 1, role, 1, NULL);
        if (!sample_node) break;
        cxpr_snapshot_set_string(&sample_node->label, cxpr_snapshot_strdup(source));
        cxpr_snapshot_set_string(&sample_node->source, cxpr_snapshot_strdup(source));
        if (cxpr_eval_ast_at_offset(value_ast, (double)i, b->ctx, b->reg, &value, &value_err)) {
            (void)cxpr_snapshot_set_value(sample_node, &value);
            cxpr_value_free(&value);
        } else {
            sample_node->state = CXPR_SNAPSHOT_STATE_ERROR;
            cxpr_snapshot_set_string(&sample_node->resolved,
                                     cxpr_snapshot_strdup(value_err.message ? value_err.message : "error"));
            cxpr_snapshot_refresh_display_label(sample_node);
        }
    }

    free(value_source);
}

static void cxpr_snapshot_add_function_name_node(cxpr_snapshot_builder* b,
                                                 const cxpr_ast* ast,
                                                 cxpr_snapshot_node* node,
                                                 int active) {
    const char* name;
    cxpr_snapshot_node* function_node;

    if (!active || !ast || cxpr_ast_type(ast) != CXPR_NODE_FUNCTION_CALL) return;
    name = cxpr_ast_function_name(ast);
    if (!name || !name[0]) return;

    function_node = cxpr_snapshot_add_node(b, ast, node->id, 1, "function", 1, NULL);
    if (!function_node) return;
    cxpr_snapshot_set_string(&function_node->label, cxpr_snapshot_strdup(name));
    cxpr_snapshot_set_string(&function_node->source, cxpr_snapshot_strdup(name));
    cxpr_snapshot_set_string(&function_node->resolved, cxpr_snapshot_strdup(""));
    function_node->state = CXPR_SNAPSHOT_STATE_VALUE;
    cxpr_snapshot_refresh_display_label(function_node);
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
    len = strlen(op) + strlen(value) + 2u;
    out = (char*)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s%s", op, value);
    return out;
}

static size_t cxpr_snapshot_visit(cxpr_snapshot_builder* b,
                                  const cxpr_ast* ast,
                                  size_t parent_id,
                                  int has_parent,
                                  const char* role,
                                  int active,
                                  const char* inactive_reason);

static void cxpr_snapshot_mark_children_skipped(cxpr_snapshot_builder* b,
                                                const cxpr_ast* ast,
                                                size_t parent_id,
                                                const char* role,
                                                const char* reason) {
    (void)cxpr_snapshot_visit(b, ast, parent_id, 1, role, 0, reason);
}

static size_t cxpr_snapshot_visit_children(cxpr_snapshot_builder* b,
                                           const cxpr_ast* ast,
                                           cxpr_snapshot_node* node,
                                           int active) {
    size_t first_child = (size_t)-1;
    size_t child;
    size_t count;
    char role[32];

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_BINARY_OP:
            child = cxpr_snapshot_visit(b, cxpr_ast_left(ast), node->id, 1, "left",
                                        active, node->resolved);
            if (first_child == (size_t)-1) first_child = child;
            (void)cxpr_snapshot_visit(b, cxpr_ast_right(ast), node->id, 1, "right",
                                      active, node->resolved);
            break;
        case CXPR_NODE_UNARY_OP:
            child = cxpr_snapshot_visit(b, cxpr_ast_operand(ast), node->id, 1, "operand",
                                        active, node->resolved);
            if (first_child == (size_t)-1) first_child = child;
            break;
        case CXPR_NODE_FUNCTION_CALL:
            cxpr_snapshot_add_function_name_node(b, ast, node, active);
            count = cxpr_ast_function_argc(ast);
            for (size_t i = 0; i < count; ++i) {
                const char* name = cxpr_snapshot_function_arg_role(
                    cxpr_ast_function_name(ast),
                    i,
                    cxpr_ast_function_arg_name(ast, i));
                if (name) {
                    snprintf(role, sizeof(role), "%s", name);
                } else {
                    snprintf(role, sizeof(role), "arg%zu", i);
                }
                child = cxpr_snapshot_visit(b, cxpr_ast_function_arg(ast, i), node->id, 1,
                                            role, active, node->resolved);
                if (first_child == (size_t)-1) first_child = child;
            }
            cxpr_snapshot_add_timeseries_samples(b, ast, node, active);
            break;
        case CXPR_NODE_PRODUCER_ACCESS:
            count = cxpr_ast_producer_argc(ast);
            for (size_t i = 0; i < count; ++i) {
                const char* name = cxpr_ast_producer_arg_name(ast, i);
                if (name) {
                    snprintf(role, sizeof(role), "%s", name);
                } else {
                    snprintf(role, sizeof(role), "arg%zu", i);
                }
                child = cxpr_snapshot_visit(b, cxpr_ast_producer_arg(ast, i), node->id, 1,
                                            role, active, node->resolved);
                if (first_child == (size_t)-1) first_child = child;
            }
            break;
        case CXPR_NODE_LOOKBACK:
            child = cxpr_snapshot_visit(b, cxpr_ast_lookback_target(ast), node->id, 1,
                                        "source", active, node->resolved);
            if (first_child == (size_t)-1) first_child = child;
            (void)cxpr_snapshot_visit(b, cxpr_ast_lookback_index(ast), node->id, 1,
                                      "index", active, node->resolved);
            break;
        case CXPR_NODE_TERNARY:
            child = cxpr_snapshot_visit(b, cxpr_ast_ternary_condition(ast), node->id, 1,
                                        "condition", active, node->resolved);
            if (first_child == (size_t)-1) first_child = child;
            (void)cxpr_snapshot_visit(b, cxpr_ast_ternary_true_branch(ast), node->id, 1,
                                      "true", active, node->resolved);
            (void)cxpr_snapshot_visit(b, cxpr_ast_ternary_false_branch(ast), node->id, 1,
                                      "false", active, node->resolved);
            break;
        default:
            break;
    }
    return first_child;
}

static size_t cxpr_snapshot_visit_binary(cxpr_snapshot_builder* b,
                                         const cxpr_ast* ast,
                                         cxpr_snapshot_node* node,
                                         int active) {
    size_t left_id;
    size_t right_id;
    cxpr_snapshot_node* left_node;
    cxpr_snapshot_node* right_node;
    int op;

    if (!active) {
        return cxpr_snapshot_visit_children(b, ast, node, 0);
    }

    op = cxpr_ast_operator(ast);
    left_id = cxpr_snapshot_visit(b, cxpr_ast_left(ast), node->id, 1, "left", 1, NULL);
    if (b->failed) return left_id;
    left_node = &b->snapshot->nodes[left_id];

    if (op == CXPR_TOK_AND && left_node->has_value &&
        left_node->value.type == CXPR_VALUE_BOOL && !left_node->value.b) {
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_ast_right(ast), node->id, "right",
            "and short-circuit: left side is false");
        (void)cxpr_snapshot_set_value(node, &left_node->value);
        cxpr_snapshot_set_string(&node->resolved,
                                 cxpr_snapshot_join3(left_node->resolved, "and",
                                                     "right not evaluated"));
        return left_id;
    }
    if (op == CXPR_TOK_OR && left_node->has_value &&
        left_node->value.type == CXPR_VALUE_BOOL && left_node->value.b) {
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_ast_right(ast), node->id, "right",
            "or short-circuit: left side is true");
        (void)cxpr_snapshot_set_value(node, &left_node->value);
        cxpr_snapshot_set_string(&node->resolved,
                                 cxpr_snapshot_join3(left_node->resolved, "or",
                                                     "right not evaluated"));
        return left_id;
    }

    right_id = cxpr_snapshot_visit(b, cxpr_ast_right(ast), node->id, 1, "right", 1, NULL);
    if (b->failed) return left_id;
    right_node = &b->snapshot->nodes[right_id];
    (void)cxpr_snapshot_eval_node(b, ast, node);
    cxpr_snapshot_set_string(&node->resolved,
                             cxpr_snapshot_join3(left_node->value_text ? left_node->value_text : left_node->resolved,
                                                 cxpr_snapshot_binary_op_text(op),
                                                 right_node->value_text ? right_node->value_text : right_node->resolved));
    return left_id;
}

static size_t cxpr_snapshot_visit_ternary(cxpr_snapshot_builder* b,
                                          const cxpr_ast* ast,
                                          cxpr_snapshot_node* node,
                                          int active) {
    size_t cond_id;
    size_t branch_id;
    cxpr_snapshot_node* cond;
    cxpr_snapshot_node* branch;

    if (!active) {
        return cxpr_snapshot_visit_children(b, ast, node, 0);
    }

    cond_id = cxpr_snapshot_visit(b, cxpr_ast_ternary_condition(ast), node->id, 1,
                                  "condition", 1, NULL);
    if (b->failed) return cond_id;
    cond = &b->snapshot->nodes[cond_id];
    if (!cond->has_value || cond->value.type != CXPR_VALUE_BOOL) {
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_ast_ternary_true_branch(ast), node->id, "true",
            "ternary branch not evaluated: condition is not boolean");
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_ast_ternary_false_branch(ast), node->id, "false",
            "ternary branch not evaluated: condition is not boolean");
        (void)cxpr_snapshot_eval_node(b, ast, node);
        return cond_id;
    }

    if (cond->value.b) {
        branch_id = cxpr_snapshot_visit(b, cxpr_ast_ternary_true_branch(ast), node->id, 1,
                                        "true", 1, NULL);
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_ast_ternary_false_branch(ast), node->id, "false",
            "ternary condition true: false branch not evaluated");
    } else {
        cxpr_snapshot_mark_children_skipped(
            b, cxpr_ast_ternary_true_branch(ast), node->id, "true",
            "ternary condition false: true branch not evaluated");
        branch_id = cxpr_snapshot_visit(b, cxpr_ast_ternary_false_branch(ast), node->id, 1,
                                        "false", 1, NULL);
    }
    if (b->failed) return cond_id;
    branch = &b->snapshot->nodes[branch_id];
    if (branch->has_value) (void)cxpr_snapshot_set_value(node, &branch->value);
    cxpr_snapshot_set_string(&node->resolved,
                             cxpr_snapshot_strdup(branch->value_text ? branch->value_text : branch->resolved));
    return cond_id;
}

static size_t cxpr_snapshot_visit_lookback(cxpr_snapshot_builder* b,
                                           const cxpr_ast* ast,
                                           cxpr_snapshot_node* node,
                                           int active) {
    cxpr_snapshot_node* target;
    size_t target_id;
    char* target_source;
    char* target_label;
    size_t target_label_len;

    if (!active) {
        return cxpr_snapshot_visit_children(b, ast, node, 0);
    }

    target = cxpr_snapshot_add_node(b, cxpr_ast_lookback_target(ast), node->id, 1,
                                    "source", 1, NULL);
    if (!target) return (size_t)-1;
    target_id = target->id;
    target_source = cxpr_ast_to_string(cxpr_ast_lookback_target(ast));
    target_label_len = strlen(target_source ? target_source : "") + 1u;
    target_label = (char*)malloc(target_label_len);
    if (!target_label) {
        free(target_source);
        b->failed = 1;
        return target_id;
    }
    snprintf(target_label, target_label_len, "%s", target_source ? target_source : "");
    free(target_source);
    cxpr_snapshot_set_string(&target->label, target_label);
    cxpr_snapshot_set_string(&target->resolved, cxpr_snapshot_strdup(""));
    target->state = CXPR_SNAPSHOT_STATE_VALUE;
    cxpr_snapshot_refresh_display_label(target);

    (void)cxpr_snapshot_visit(b, cxpr_ast_lookback_index(ast), node->id, 1,
                              "index", 1, NULL);
    if (!b->failed) (void)cxpr_snapshot_eval_node(b, ast, node);
    return target_id;
}

static size_t cxpr_snapshot_visit(cxpr_snapshot_builder* b,
                                  const cxpr_ast* ast,
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

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_BINARY_OP:
            (void)cxpr_snapshot_visit_binary(b, ast, node, 1);
            break;
        case CXPR_NODE_TERNARY:
            (void)cxpr_snapshot_visit_ternary(b, ast, node, 1);
            break;
        case CXPR_NODE_UNARY_OP:
            first_child = cxpr_snapshot_visit(b, cxpr_ast_operand(ast), node_id, 1,
                                              "operand", 1, NULL);
            if (!b->failed) {
                (void)cxpr_snapshot_eval_node(b, ast, node);
                cxpr_snapshot_set_string(&node->resolved,
                                         cxpr_snapshot_join_unary(node->label,
                                             b->snapshot->nodes[first_child].value_text ?
                                             b->snapshot->nodes[first_child].value_text :
                                             b->snapshot->nodes[first_child].resolved));
            }
            break;
        case CXPR_NODE_LOOKBACK:
            (void)cxpr_snapshot_visit_lookback(b, ast, node, 1);
            break;
        default:
            (void)cxpr_snapshot_visit_children(b, ast, node, 1);
            if (!b->failed) (void)cxpr_snapshot_eval_node(b, ast, node);
            break;
    }

    return node_id;
}

bool cxpr_eval_snapshot_build(const cxpr_ast* ast,
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

    out_snapshot->expression = cxpr_ast_to_string(ast);
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

static bool cxpr_snapshot_json_string(FILE* out, const char* text) {
    const unsigned char* p = (const unsigned char*)(text ? text : "");

    if (fputc('"', out) == EOF) return false;
    while (*p) {
        switch (*p) {
            case '"':
                if (fputs("\\\"", out) == EOF) return false;
                break;
            case '\\':
                if (fputs("\\\\", out) == EOF) return false;
                break;
            case '\n':
                if (fputs("\\n", out) == EOF) return false;
                break;
            case '\r':
                if (fputs("\\r", out) == EOF) return false;
                break;
            case '\t':
                if (fputs("\\t", out) == EOF) return false;
                break;
            default:
                if (*p < 0x20u) {
                    if (fprintf(out, "\\u%04x", (unsigned int)*p) < 0) return false;
                } else if (fputc((int)*p, out) == EOF) {
                    return false;
                }
                break;
        }
        ++p;
    }
    return fputc('"', out) != EOF;
}

bool cxpr_eval_snapshot_write_json(const cxpr_eval_snapshot* snapshot, FILE* out) {
    if (!snapshot || !out) return false;

    if (fputs("{\n  \"expression\": ", out) == EOF) return false;
    if (!cxpr_snapshot_json_string(out, snapshot->expression)) return false;
    if (fputs(",\n  \"resolved\": ", out) == EOF) return false;
    if (!cxpr_snapshot_json_string(out, snapshot->resolved)) return false;
    if (fprintf(out, ",\n  \"state\": \"%s\",\n  \"elements\": {\n    \"nodes\": [\n",
                cxpr_snapshot_state_name(snapshot->state)) < 0) return false;

    for (size_t i = 0; i < snapshot->node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot->nodes[i];
        if (i > 0u && fputs(",\n", out) == EOF) return false;
        if (fprintf(out, "      { \"data\": { \"id\": \"n%zu\", \"numeric_id\": %zu, ",
                    node->id, node->id) < 0) return false;
        if (fputs("\"role\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->role)) return false;
        if (fputs(", \"kind\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->kind)) return false;
        if (fputs(", \"label\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->label)) return false;
        if (fputs(", \"display_label\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->display_label)) return false;
        if (fputs(", \"source\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->source)) return false;
        if (fputs(", \"resolved\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->resolved)) return false;
        if (fputs(", \"value\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->value_text)) return false;
        if (fprintf(out, ", \"active\": %s, \"state\": \"%s\" } }",
                    node->active ? "true" : "false",
                    cxpr_snapshot_state_name(node->state)) < 0) return false;
    }

    if (fputs("\n    ],\n    \"edges\": [\n", out) == EOF) return false;
    {
        int first = 1;
        for (size_t i = 0; i < snapshot->node_count; ++i) {
            const cxpr_snapshot_node* node = &snapshot->nodes[i];
            if (!node->has_parent) continue;
            if (!first && fputs(",\n", out) == EOF) return false;
            first = 0;
            if (fprintf(out, "      { \"data\": { \"id\": \"e%zu\", \"source\": \"n%zu\", \"target\": \"n%zu\", \"role\": ",
                        node->id, node->parent_id, node->id) < 0) return false;
            if (!cxpr_snapshot_json_string(out, node->role)) return false;
            if (fputs(" } }", out) == EOF) return false;
        }
    }
    return fputs("\n    ]\n  }\n}\n", out) != EOF;
}

static bool cxpr_snapshot_flow_reserve_node(cxpr_eval_snapshot_flow* flow) {
    cxpr_eval_snapshot_flow_node* grown;
    size_t cap;

    if (flow->node_count < flow->node_capacity) return true;
    cap = flow->node_capacity ? flow->node_capacity * 2u : 16u;
    grown = (cxpr_eval_snapshot_flow_node*)realloc(flow->nodes, cap * sizeof(*grown));
    if (!grown) return false;
    flow->nodes = grown;
    flow->node_capacity = cap;
    return true;
}

static bool cxpr_snapshot_flow_reserve_edge(cxpr_eval_snapshot_flow* flow) {
    cxpr_eval_snapshot_flow_edge* grown;
    size_t cap;

    if (flow->edge_count < flow->edge_capacity) return true;
    cap = flow->edge_capacity ? flow->edge_capacity * 2u : 32u;
    grown = (cxpr_eval_snapshot_flow_edge*)realloc(flow->edges, cap * sizeof(*grown));
    if (!grown) return false;
    flow->edges = grown;
    flow->edge_capacity = cap;
    return true;
}

static bool cxpr_snapshot_flow_add_edge(cxpr_eval_snapshot_flow* flow,
                                        size_t source_index,
                                        size_t target_index) {
    cxpr_eval_snapshot_flow_edge* edge;

    for (size_t i = 0; i < flow->edge_count; ++i) {
        if (flow->edges[i].source_index == source_index &&
            flow->edges[i].target_index == target_index) {
            return true;
        }
    }

    if (!cxpr_snapshot_flow_reserve_edge(flow)) return false;
    edge = &flow->edges[flow->edge_count++];
    memset(edge, 0, sizeof(*edge));
    edge->source_index = source_index;
    edge->target_index = target_index;
    edge->source_name = cxpr_snapshot_strdup(flow->nodes[source_index].name);
    edge->target_name = cxpr_snapshot_strdup(flow->nodes[target_index].name);
    return edge->source_name && edge->target_name;
}

static size_t cxpr_snapshot_flow_find_node(const cxpr_eval_snapshot_flow* flow,
                                           const char* name,
                                           const char* kind) {
    for (size_t i = 0; i < flow->node_count; ++i) {
        if (strcmp(flow->nodes[i].name ? flow->nodes[i].name : "", name ? name : "") == 0 &&
            strcmp(flow->nodes[i].kind ? flow->nodes[i].kind : "", kind ? kind : "") == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static size_t cxpr_snapshot_flow_add_value_node(cxpr_eval_snapshot_flow* flow,
                                                const char* name,
                                                const char* kind,
                                                const char* value_text,
                                                cxpr_snapshot_state state) {
    cxpr_eval_snapshot_flow_node* node;
    size_t existing = cxpr_snapshot_flow_find_node(flow, name, kind);
    size_t label_len;

    if (existing != (size_t)-1) return existing;
    if (!cxpr_snapshot_flow_reserve_node(flow)) return (size_t)-1;

    node = &flow->nodes[flow->node_count];
    memset(node, 0, sizeof(*node));
    node->name = cxpr_snapshot_strdup(name);
    node->kind = cxpr_snapshot_strdup(kind);
    node->value_text = cxpr_snapshot_strdup(value_text ? value_text : "");
    node->state = state;
    label_len = strlen(name ? name : "") + strlen(value_text ? value_text : "") + 4u;
    node->display_label = (char*)malloc(label_len);
    if (!node->name || !node->kind || !node->value_text || !node->display_label) {
        return (size_t)-1;
    }
    if (value_text && value_text[0]) {
        snprintf(node->display_label, label_len, "%s\n= %s", name, value_text);
    } else {
        snprintf(node->display_label, label_len, "%s", name ? name : "");
    }
    return flow->node_count++;
}

static bool cxpr_snapshot_flow_add_lookback_sources(cxpr_eval_snapshot_flow* flow,
                                                    const cxpr_ast* ast,
                                                    const cxpr_context* ctx,
                                                    const cxpr_registry* reg,
                                                    size_t target_flow,
                                                    cxpr_error* err) {
    if (!ast) return true;

    if (cxpr_ast_type(ast) == CXPR_NODE_LOOKBACK) {
        char* name = cxpr_ast_to_string(ast);
        char* value_text = NULL;
        cxpr_value value = cxpr_null();
        cxpr_error eval_err = {0};
        bool found = false;
        size_t source_flow;

        if (!name) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Failed to allocate lookback source name";
            }
            return false;
        }

        found = cxpr_eval_ast(ast, ctx, reg, &value, &eval_err);
        if (found) value_text = cxpr_snapshot_value_text(&value);
        source_flow = cxpr_snapshot_flow_add_value_node(
            flow,
            name,
            "source",
            value_text,
            found ? cxpr_snapshot_state_for_value(&value) : CXPR_SNAPSHOT_STATE_VALUE);
        free(value_text);
        cxpr_value_free(&value);
        if (source_flow == (size_t)-1 ||
            !cxpr_snapshot_flow_add_edge(flow, source_flow, target_flow)) {
            free(name);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Failed to allocate lookback source flow node";
            }
            return false;
        }
        free(name);
    }

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_BINARY_OP:
            return cxpr_snapshot_flow_add_lookback_sources(
                       flow, cxpr_ast_left(ast), ctx, reg, target_flow, err) &&
                   cxpr_snapshot_flow_add_lookback_sources(
                       flow, cxpr_ast_right(ast), ctx, reg, target_flow, err);
        case CXPR_NODE_UNARY_OP:
            return cxpr_snapshot_flow_add_lookback_sources(
                flow, cxpr_ast_operand(ast), ctx, reg, target_flow, err);
        case CXPR_NODE_FUNCTION_CALL:
            for (size_t i = 0; i < cxpr_ast_function_argc(ast); ++i) {
                if (!cxpr_snapshot_flow_add_lookback_sources(
                        flow, cxpr_ast_function_arg(ast, i), ctx, reg, target_flow, err)) {
                    return false;
                }
            }
            return true;
        case CXPR_NODE_PRODUCER_ACCESS:
            for (size_t i = 0; i < cxpr_ast_producer_argc(ast); ++i) {
                if (!cxpr_snapshot_flow_add_lookback_sources(
                        flow, cxpr_ast_producer_arg(ast, i), ctx, reg, target_flow, err)) {
                    return false;
                }
            }
            return true;
        case CXPR_NODE_LOOKBACK:
            return cxpr_snapshot_flow_add_lookback_sources(
                       flow, cxpr_ast_lookback_target(ast), ctx, reg, target_flow, err) &&
                   cxpr_snapshot_flow_add_lookback_sources(
                       flow, cxpr_ast_lookback_index(ast), ctx, reg, target_flow, err);
        case CXPR_NODE_TERNARY:
            return cxpr_snapshot_flow_add_lookback_sources(
                       flow, cxpr_ast_ternary_condition(ast), ctx, reg, target_flow, err) &&
                   cxpr_snapshot_flow_add_lookback_sources(
                       flow, cxpr_ast_ternary_true_branch(ast), ctx, reg, target_flow, err) &&
                   cxpr_snapshot_flow_add_lookback_sources(
                       flow, cxpr_ast_ternary_false_branch(ast), ctx, reg, target_flow, err);
        default:
            return true;
    }
}

bool cxpr_eval_snapshot_build_flow(const cxpr_evaluator* evaluator,
                                   cxpr_context* ctx,
                                   const cxpr_registry* reg,
                                   cxpr_eval_snapshot_flow* out_flow,
                                   cxpr_error* err) {
    size_t* expr_to_flow = NULL;
    const cxpr_evaluator* previous_scope = NULL;

    if (!out_flow) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Flow snapshot output is NULL";
        }
        return false;
    }
    memset(out_flow, 0, sizeof(*out_flow));
    if (err) *err = (cxpr_error){0};

    if (!evaluator || !evaluator->compiled) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Flow snapshot requires a compiled evaluator";
        }
        return false;
    }

    expr_to_flow = (size_t*)malloc(evaluator->count * sizeof(*expr_to_flow));
    if (!expr_to_flow && evaluator->count > 0u) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Failed to allocate flow index";
        }
        return false;
    }
    for (size_t i = 0; i < evaluator->count; ++i) expr_to_flow[i] = (size_t)-1;

    if (ctx) {
        previous_scope = ctx->expression_scope;
        cxpr_context_set_expression_scope(ctx, evaluator);
    }

    for (size_t order_i = 0; order_i < evaluator->eval_order_count; ++order_i) {
        size_t expr_i = evaluator->eval_order[order_i];
        const cxpr_expression_entry* entry = &evaluator->expressions[expr_i];
        cxpr_eval_snapshot_flow_node* node;

        if (!cxpr_snapshot_flow_reserve_node(out_flow)) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Failed to allocate flow node";
            }
            goto fail;
        }

        node = &out_flow->nodes[out_flow->node_count];
        memset(node, 0, sizeof(*node));
        node->name = cxpr_snapshot_strdup(entry->name);
        node->kind = cxpr_snapshot_strdup("expression");
        node->state = entry->evaluated ?
            cxpr_snapshot_state_for_value(&entry->result) :
            CXPR_SNAPSHOT_STATE_UNKNOWN;
        node->value_text = entry->evaluated ?
            cxpr_snapshot_value_text(&entry->result) :
            cxpr_snapshot_strdup("");
        node->display_label = NULL;
        if (!node->name || !node->kind || !node->value_text) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Failed to allocate flow node strings";
            }
            goto fail;
        }
        if (!cxpr_eval_snapshot_build(entry->ast, ctx, reg ? reg : evaluator->registry,
                                      &node->ast, err)) {
            goto fail;
        }
        if (entry->expression) {
            cxpr_snapshot_set_string(&node->ast.expression,
                                     cxpr_snapshot_strdup(entry->expression));
            if (!node->ast.expression) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Failed to allocate original expression string";
                }
                goto fail;
            }
        }
        {
            const char* expr = node->ast.expression ? node->ast.expression : "";
            const char* resolved = node->ast.resolved ? node->ast.resolved : node->value_text;
            size_t len = strlen(node->name) + strlen(expr) + strlen(resolved) + 10u;
            node->display_label = (char*)malloc(len);
            if (!node->display_label) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Failed to allocate flow display label";
                }
                goto fail;
            }
            snprintf(node->display_label, len, "%s = %s\n= %s", node->name, expr, resolved);
        }
        expr_to_flow[expr_i] = out_flow->node_count++;
    }

    for (size_t expr_i = 0; expr_i < evaluator->count; ++expr_i) {
        const cxpr_expression_entry* entry = &evaluator->expressions[expr_i];
        size_t target_flow = expr_to_flow[expr_i];
        const char* refs[256];
        const char* params[256];
        size_t nrefs;
        size_t nparams;

        if (target_flow == (size_t)-1 || !entry->ast) continue;
        nrefs = cxpr_ast_references(entry->ast, refs, 256);
        for (size_t dep_i = 0; dep_i < evaluator->count; ++dep_i) {
            size_t source_flow = expr_to_flow[dep_i];
            if (source_flow == (size_t)-1 || source_flow == target_flow) continue;
            for (size_t r = 0; r < nrefs && r < 256u; ++r) {
                if (cxpr_expression_reference_matches_name(
                        refs[r], evaluator->expressions[dep_i].name)) {
                    if (!cxpr_snapshot_flow_add_edge(out_flow, source_flow, target_flow)) {
                        if (err) {
                            err->code = CXPR_ERR_OUT_OF_MEMORY;
                            err->message = "Failed to allocate flow edge";
                        }
                        goto fail;
                    }
                    break;
                }
            }
        }
        for (size_t r = 0; r < nrefs && r < 256u; ++r) {
            bool is_expression_ref = false;
            bool found = false;
            cxpr_value value;
            char* value_text = NULL;
            size_t source_flow;

            for (size_t dep_i = 0; dep_i < evaluator->count; ++dep_i) {
                if (cxpr_expression_reference_matches_name(
                        refs[r], evaluator->expressions[dep_i].name)) {
                    is_expression_ref = true;
                    break;
                }
            }
            if (is_expression_ref) continue;
            value = cxpr_context_get_typed(ctx, refs[r], &found);
            if (found) value_text = cxpr_snapshot_value_text(&value);
            source_flow = cxpr_snapshot_flow_add_value_node(
                out_flow,
                refs[r],
                "source",
                value_text,
                found ? cxpr_snapshot_state_for_value(&value) : CXPR_SNAPSHOT_STATE_VALUE);
            free(value_text);
            if (source_flow == (size_t)-1 ||
                !cxpr_snapshot_flow_add_edge(out_flow, source_flow, target_flow)) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Failed to allocate source flow node";
                }
                goto fail;
            }
        }

        if (!cxpr_snapshot_flow_add_lookback_sources(
                out_flow, entry->ast, ctx, reg ? reg : evaluator->registry, target_flow, err)) {
            goto fail;
        }

        nparams = cxpr_ast_variables_used(entry->ast, params, 256);
        for (size_t p = 0; p < nparams && p < 256u; ++p) {
            bool found = false;
            cxpr_value value;
            char* value_text = NULL;
            char param_name[512];
            size_t param_flow;

            snprintf(param_name, sizeof(param_name), "$%s", params[p] ? params[p] : "");
            value = cxpr_context_get_param_typed(ctx, params[p], &found);
            if (found) value_text = cxpr_snapshot_value_text(&value);
            param_flow = cxpr_snapshot_flow_add_value_node(
                out_flow,
                param_name,
                "param",
                value_text,
                found ? cxpr_snapshot_state_for_value(&value) : CXPR_SNAPSHOT_STATE_VALUE);
            free(value_text);
            if (param_flow == (size_t)-1 ||
                !cxpr_snapshot_flow_add_edge(out_flow, param_flow, target_flow)) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Failed to allocate param flow node";
                }
                goto fail;
            }
        }
    }

    if (ctx) cxpr_context_set_expression_scope(ctx, previous_scope);
    free(expr_to_flow);
    return true;

fail:
    if (ctx) cxpr_context_set_expression_scope(ctx, previous_scope);
    free(expr_to_flow);
    cxpr_eval_snapshot_flow_free(out_flow);
    return false;
}

void cxpr_eval_snapshot_flow_free(cxpr_eval_snapshot_flow* flow) {
    if (!flow) return;
    for (size_t i = 0; i < flow->node_count; ++i) {
        free(flow->nodes[i].name);
        free(flow->nodes[i].kind);
        free(flow->nodes[i].display_label);
        free(flow->nodes[i].value_text);
        cxpr_eval_snapshot_free(&flow->nodes[i].ast);
    }
    for (size_t i = 0; i < flow->edge_count; ++i) {
        free(flow->edges[i].source_name);
        free(flow->edges[i].target_name);
    }
    free(flow->nodes);
    free(flow->edges);
    memset(flow, 0, sizeof(*flow));
}

bool cxpr_eval_snapshot_flow_write_json(const cxpr_eval_snapshot_flow* flow, FILE* out) {
    if (!flow || !out) return false;

    if (fputs("{\n  \"flow\": {\n    \"nodes\": [\n", out) == EOF) return false;
    for (size_t i = 0; i < flow->node_count; ++i) {
        const cxpr_eval_snapshot_flow_node* node = &flow->nodes[i];
        if (i > 0u && fputs(",\n", out) == EOF) return false;
        if (fprintf(out, "      { \"data\": { \"id\": \"expr%zu\", \"index\": %zu, \"name\": ",
                    i, i) < 0) return false;
        if (!cxpr_snapshot_json_string(out, node->name)) return false;
        if (fputs(", \"kind\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->kind)) return false;
        if (fputs(", \"display_label\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->display_label)) return false;
        if (fputs(", \"expression\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->ast.expression)) return false;
        if (fputs(", \"resolved\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->ast.resolved)) return false;
        if (fputs(", \"value\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->value_text)) return false;
        if (fprintf(out, ", \"state\": \"%s\", \"snapshot_index\": %zu } }",
                    cxpr_snapshot_state_name(node->state), i) < 0) return false;
    }

    if (fputs("\n    ],\n    \"edges\": [\n", out) == EOF) return false;
    for (size_t i = 0; i < flow->edge_count; ++i) {
        const cxpr_eval_snapshot_flow_edge* edge = &flow->edges[i];
        if (i > 0u && fputs(",\n", out) == EOF) return false;
        if (fprintf(out,
                    "      { \"data\": { \"id\": \"flowe%zu\", \"source\": \"expr%zu\", \"target\": \"expr%zu\", \"source_name\": ",
                    i, edge->source_index, edge->target_index) < 0) return false;
        if (!cxpr_snapshot_json_string(out, edge->source_name)) return false;
        if (fputs(", \"target_name\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, edge->target_name)) return false;
        if (fputs(" } }", out) == EOF) return false;
    }

    if (fputs("\n    ]\n  },\n  \"snapshots\": [\n", out) == EOF) return false;
    for (size_t i = 0; i < flow->node_count; ++i) {
        const cxpr_eval_snapshot_flow_node* node = &flow->nodes[i];
        if (i > 0u && fputs(",\n", out) == EOF) return false;
        if (fputs("    { \"name\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->name)) return false;
        if (fputs(", \"snapshot\": ", out) == EOF) return false;
        if (!cxpr_eval_snapshot_write_json(&node->ast, out)) return false;
        if (fputs("    }", out) == EOF) return false;
    }

    return fputs("\n  ]\n}\n", out) != EOF;
}
