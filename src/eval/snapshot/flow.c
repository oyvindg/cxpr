#include "eval/snapshot/internal.h"

#include "context/state.h"
#include "expression/internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                                                const cxpr_value* value,
                                                int has_value,
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
    if (has_value && value) {
        node->value = cxpr_value_clone(value);
        if (cxpr_snapshot_value_clone_failed(value, &node->value)) {
            cxpr_value_free(&node->value);
        } else {
            node->has_value = 1;
        }
    }
    node->state = state;
    label_len = strlen(name ? name : "") + strlen(value_text ? value_text : "") + 4u;
    node->display_label = (char*)malloc(label_len);
    if (!node->name || !node->kind || !node->value_text || !node->display_label ||
        (has_value && value && !node->has_value)) {
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
            found ? &value : NULL,
            found ? 1 : 0,
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
        if (entry->evaluated) {
            node->value = cxpr_value_clone(&entry->result);
            if (cxpr_snapshot_value_clone_failed(&entry->result, &node->value)) {
                cxpr_value_free(&node->value);
            } else {
                node->has_value = 1;
            }
        }
        node->display_label = NULL;
        if (!node->name || !node->kind || !node->value_text ||
            (entry->evaluated && !node->has_value)) {
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
            const char* value_text = node->value_text ? node->value_text : "";
            int show_final_value = value_text[0] != '\0' && strcmp(resolved, value_text) != 0;
            size_t len = strlen(node->name) + strlen(expr) + strlen(resolved) + 10u;
            if (show_final_value) len += strlen(value_text) + 4u;
            node->display_label = (char*)malloc(len);
            if (!node->display_label) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Failed to allocate flow display label";
                }
                goto fail;
            }
            if (show_final_value) {
                snprintf(node->display_label, len, "%s =\n%s\n= %s\n= %s",
                         node->name, expr, resolved, value_text);
            } else {
                snprintf(node->display_label, len, "%s =\n%s\n= %s", node->name, expr, resolved);
            }
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
                found ? &value : NULL,
                found ? 1 : 0,
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
                found ? &value : NULL,
                found ? 1 : 0,
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
        cxpr_value_free(&flow->nodes[i].value);
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

bool cxpr_eval_snapshot_flow_write_json_ex(const cxpr_eval_snapshot_flow* flow,
                                           const cxpr_snapshot_json_hooks* hooks,
                                           FILE* out) {
    if (!flow || !out) return false;

    if (!cxpr_snapshot_write_document_prefix(out, "cxpr.eval_snapshot_flow.v2", hooks)) {
        return false;
    }
    if (fputs(",\n  \"flow\": {\n    \"nodes\": [\n", out) == EOF) return false;
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
        if (!cxpr_snapshot_write_optional_value_fields(out, &node->value, node->has_value)) {
            return false;
        }
        if (fprintf(out, ", \"state\": \"%s\", \"snapshot_index\": %zu",
                    cxpr_snapshot_state_name(node->state), i) < 0) return false;
        if (hooks && hooks->write_flow_node_host_json) {
            if (fputs(", \"host\": ", out) == EOF) return false;
            if (!hooks->write_flow_node_host_json(out, flow, i, hooks->userdata)) return false;
        }
        if (fputs(" } }", out) == EOF) return false;
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
        if (!cxpr_eval_snapshot_write_json_ex(&node->ast, hooks, out)) return false;
        if (fputs("    }", out) == EOF) return false;
    }

    return fputs("\n  ]\n}\n", out) != EOF;
}

bool cxpr_eval_snapshot_flow_write_json(const cxpr_eval_snapshot_flow* flow, FILE* out) {
    return cxpr_eval_snapshot_flow_write_json_ex(flow, NULL, out);
}
