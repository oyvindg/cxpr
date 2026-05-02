/**
 * @file canonical.c
 * @brief Canonical source-plan rendering and source-plan lifecycle helpers.
 */

#include "internal.h"

#include <cxpr/ast.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t cxpr_source_plan_stable_hash(const char* text) {
    const unsigned char* cursor = (const unsigned char*)text;
    uint64_t hash = 1469598103934665603ULL;

    if (!cursor) return 0ULL;
    while (*cursor != '\0') {
        hash ^= (uint64_t)(*cursor++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

char* cxpr_source_plan_strdup(const char* text) {
    char* out;
    size_t len;

    if (!text) return NULL;
    len = strlen(text);
    out = malloc(len + 1u);
    if (!out) return NULL;
    memcpy(out, text, len + 1u);
    return out;
}

void cxpr_source_plan_node_clear(cxpr_source_plan_node* node) {
    if (!node) return;
    free(node->name);
    free(node->field_name);
    free(node->scope_value);
    free(node->arg_slots);
    if (node->source) {
        cxpr_source_plan_node_clear(node->source);
        free(node->source);
    }
    memset(node, 0, sizeof(*node));
    node->lookback_slot = SIZE_MAX;
}

static int cxpr_source_canonical_text_append(char** target, const char* suffix) {
    char* grown;
    size_t base_len;
    size_t suffix_len;

    if (!target || !suffix) return 0;
    if (!*target) {
        *target = cxpr_source_plan_strdup(suffix);
        return *target != NULL;
    }

    base_len = strlen(*target);
    suffix_len = strlen(suffix);
    grown = realloc(*target, base_len + suffix_len + 1u);
    if (!grown) return 0;
    memcpy(grown + base_len, suffix, suffix_len + 1u);
    *target = grown;
    return 1;
}

static int cxpr_source_canonical_slot_append(char** target, size_t slot) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "$%zu", slot);
    return cxpr_source_canonical_text_append(target, buffer);
}

static const char* cxpr_source_canonical_operator(int op) {
    switch (op) {
        case CXPR_TOK_PLUS: return "__plus__";
        case CXPR_TOK_MINUS: return "__minus__";
        case CXPR_TOK_STAR: return "__star__";
        case CXPR_TOK_SLASH: return "__div__";
        case CXPR_TOK_PERCENT: return "__mod__";
        case CXPR_TOK_POWER: return "__pow__";
        case CXPR_TOK_EQ: return "__eq__";
        case CXPR_TOK_NEQ: return "__neq__";
        case CXPR_TOK_LT: return "__lt__";
        case CXPR_TOK_GT: return "__gt__";
        case CXPR_TOK_LTE: return "__lte__";
        case CXPR_TOK_GTE: return "__gte__";
        case CXPR_TOK_AND: return "__and__";
        case CXPR_TOK_OR: return "__or__";
        case CXPR_TOK_NOT: return "__not__";
        default: return "__op__";
    }
}

static int cxpr_source_canonical_rendered_append(char** out_text, const cxpr_ast* sub) {
    char* piece = NULL;
    if (!cxpr_source_plan_render_ast_canonical(sub, &piece)) return 0;
    if (!cxpr_source_canonical_text_append(out_text, piece)) {
        free(piece);
        return 0;
    }
    free(piece);
    return 1;
}

int cxpr_source_plan_render_ast_canonical(const cxpr_ast* ast, char** out_text) {
    char buffer[64];
    size_t i;

    if (!ast || !out_text) return 0;
    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_NUMBER:
            snprintf(buffer, sizeof(buffer), "%.17g", cxpr_ast_number_value(ast));
            return cxpr_source_canonical_text_append(out_text, buffer);
        case CXPR_NODE_BOOL:
            return cxpr_source_canonical_text_append(
                out_text, cxpr_ast_bool_value(ast) ? "true" : "false");
        case CXPR_NODE_STRING: {
            const char* sv = cxpr_ast_string_value(ast);
            if (!cxpr_source_canonical_text_append(out_text, "\"")) return 0;
            if (sv && !cxpr_source_canonical_text_append(out_text, sv)) return 0;
            return cxpr_source_canonical_text_append(out_text, "\"");
        }
        case CXPR_NODE_IDENTIFIER:
            return cxpr_source_canonical_text_append(out_text, cxpr_ast_identifier_name(ast));
        case CXPR_NODE_VARIABLE:
            if (!cxpr_source_canonical_text_append(out_text, "$")) return 0;
            return cxpr_source_canonical_text_append(out_text, cxpr_ast_variable_name(ast));
        case CXPR_NODE_BINARY_OP:
            if (!cxpr_source_canonical_text_append(out_text, "(")) return 0;
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_left(ast))) return 0;
            if (!cxpr_source_canonical_text_append(
                    out_text, cxpr_source_canonical_operator(cxpr_ast_operator(ast)))) {
                return 0;
            }
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_right(ast))) return 0;
            return cxpr_source_canonical_text_append(out_text, ")");
        case CXPR_NODE_UNARY_OP:
            if (!cxpr_source_canonical_text_append(
                    out_text, cxpr_source_canonical_operator(cxpr_ast_operator(ast)))) {
                return 0;
            }
            if (!cxpr_source_canonical_text_append(out_text, "(")) return 0;
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_operand(ast))) return 0;
            return cxpr_source_canonical_text_append(out_text, ")");
        case CXPR_NODE_FUNCTION_CALL: {
            const char* fn = cxpr_ast_function_name(ast);
            size_t argc = cxpr_ast_function_argc(ast);
            if (!fn || !cxpr_source_canonical_text_append(out_text, fn) ||
                !cxpr_source_canonical_text_append(out_text, "(")) {
                return 0;
            }
            for (i = 0u; i < argc; ++i) {
                if (i > 0u && !cxpr_source_canonical_text_append(out_text, ",")) return 0;
                if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_function_arg(ast, i))) return 0;
            }
            return cxpr_source_canonical_text_append(out_text, ")");
        }
        case CXPR_NODE_PRODUCER_ACCESS: {
            const char* name = cxpr_ast_producer_name(ast);
            const char* field = cxpr_ast_producer_field(ast);
            size_t argc = cxpr_ast_producer_argc(ast);
            if (!name || !cxpr_source_canonical_text_append(out_text, name) ||
                !cxpr_source_canonical_text_append(out_text, "(")) {
                return 0;
            }
            for (i = 0u; i < argc; ++i) {
                if (i > 0u && !cxpr_source_canonical_text_append(out_text, ",")) return 0;
                if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_producer_arg(ast, i))) return 0;
            }
            if (!cxpr_source_canonical_text_append(out_text, ").")) return 0;
            return field ? cxpr_source_canonical_text_append(out_text, field) : 0;
        }
        case CXPR_NODE_LOOKBACK:
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_lookback_target(ast))) return 0;
            if (!cxpr_source_canonical_text_append(out_text, "[")) return 0;
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_lookback_index(ast))) return 0;
            return cxpr_source_canonical_text_append(out_text, "]");
        case CXPR_NODE_TERNARY:
            if (!cxpr_source_canonical_text_append(out_text, "(")) return 0;
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_ternary_condition(ast))) return 0;
            if (!cxpr_source_canonical_text_append(out_text, "?")) return 0;
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_ternary_true_branch(ast))) return 0;
            if (!cxpr_source_canonical_text_append(out_text, ":")) return 0;
            if (!cxpr_source_canonical_rendered_append(out_text, cxpr_ast_ternary_false_branch(ast))) return 0;
            return cxpr_source_canonical_text_append(out_text, ")");
        case CXPR_NODE_FIELD_ACCESS: {
            const char* obj = cxpr_ast_field_object(ast);
            const char* fld = cxpr_ast_field_name(ast);
            if (!obj || !fld) return 0;
            if (!cxpr_source_canonical_text_append(out_text, obj) ||
                !cxpr_source_canonical_text_append(out_text, ".") ||
                !cxpr_source_canonical_text_append(out_text, fld)) {
                return 0;
            }
            return 1;
        }
        default:
            return 0;
    }
}

static int cxpr_source_plan_node_canonical_build(
    const cxpr_source_plan_node* node,
    char** out_text) {
    size_t index;
    char* text = NULL;

    if (!node || !out_text) return 0;
    switch (node->kind) {
        case CXPR_SOURCE_PLAN_FIELD:
            if (!cxpr_source_canonical_text_append(&text, "field:") ||
                !cxpr_source_canonical_text_append(&text, node->name)) {
                return 0;
            }
            if (node->scope_value &&
                (!cxpr_source_canonical_text_append(&text, "@tf:") ||
                 !cxpr_source_canonical_text_append(&text, node->scope_value))) {
                return 0;
            }
            break;
        case CXPR_SOURCE_PLAN_INDICATOR:
            if (!cxpr_source_canonical_text_append(&text, "indicator:") ||
                !cxpr_source_canonical_text_append(&text, node->name) ||
                !cxpr_source_canonical_text_append(&text, "(")) {
                return 0;
            }
            for (index = 0u; index < node->arg_count; ++index) {
                if (index > 0u && !cxpr_source_canonical_text_append(&text, ", ")) return 0;
                if (!cxpr_source_canonical_slot_append(&text, node->arg_slots[index])) return 0;
            }
            if (!cxpr_source_canonical_text_append(&text, ")")) return 0;
            if (node->scope_value &&
                (!cxpr_source_canonical_text_append(&text, "@tf:") ||
                 !cxpr_source_canonical_text_append(&text, node->scope_value))) {
                return 0;
            }
            if (node->field_name &&
                (!cxpr_source_canonical_text_append(&text, ".") ||
                 !cxpr_source_canonical_text_append(&text, node->field_name))) {
                return 0;
            }
            break;
        case CXPR_SOURCE_PLAN_SMOOTHING:
            if (!node->source) return 0;
            if (!cxpr_source_canonical_text_append(&text, "smooth:") ||
                !cxpr_source_canonical_text_append(&text, node->name) ||
                !cxpr_source_canonical_text_append(&text, "(")) {
                return 0;
            }
            {
                char* nested = NULL;
                if (!cxpr_source_plan_node_canonical_build(node->source, &nested)) return 0;
                if (!cxpr_source_canonical_text_append(&text, nested)) {
                    free(nested);
                    return 0;
                }
                free(nested);
            }
            for (index = 0u; index < node->arg_count; ++index) {
                if (!cxpr_source_canonical_text_append(&text, ", ")) return 0;
                if (!cxpr_source_canonical_slot_append(&text, node->arg_slots[index])) return 0;
            }
            if (!cxpr_source_canonical_text_append(&text, ")")) return 0;
            break;
        case CXPR_SOURCE_PLAN_EXPRESSION:
            if (!node->name) return 0;
            if (!cxpr_source_canonical_text_append(&text, "expr:") ||
                !cxpr_source_canonical_text_append(&text, node->name)) {
                return 0;
            }
            break;
        default:
            return 0;
    }

    if (node->lookback_slot != SIZE_MAX) {
        if (!cxpr_source_canonical_text_append(&text, "[") ||
            !cxpr_source_canonical_slot_append(&text, node->lookback_slot) ||
            !cxpr_source_canonical_text_append(&text, "]")) {
            free(text);
            return 0;
        }
    }

    *out_text = text;
    return 1;
}

int cxpr_source_plan_finalize_node_canonical(
    cxpr_source_plan_ast* plan,
    cxpr_source_plan_node* node) {
    if (!plan || !node) return 0;
    free(plan->canonical);
    plan->canonical = NULL;
    if (!cxpr_source_plan_node_canonical_build(node, &plan->canonical)) return 0;
    node->node_id = cxpr_source_plan_stable_hash(plan->canonical);
    return node->node_id != 0ULL;
}
