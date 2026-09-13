/**
 * @file model/compile/infer.c
 * @brief Infer model inputs, result kinds, and anonymous outputs.
 */

#include "core.h"
#include "ast/internal.h"
#include "ir/compile/internal.h"
#include "lookback.h"
#include "model/internal.h"
#include "model/compile/internal.h"
#include <cxpr/resample.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/source.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static bool cxpr_model_input_name_exists(char* const* inputs, size_t count, const char* name) {
    if (!name) return false;
    for (size_t i = 0u; i < count; ++i) {
        if (cxpr_model_names_match(inputs[i], name)) return true;
    }
    return false;
}

static cxpr_model_result_kind cxpr_model_infer_result_kind_depth(
    const cxpr_expr_ast* ast,
    const cxpr_registry* reg,
    size_t depth) {
    cxpr_model_result_kind left;
    cxpr_model_result_kind right;

    if (!ast || depth > CXPR_IR_INFER_DEPTH_LIMIT) {
        return CXPR_MODEL_RESULT_UNKNOWN;
    }
    if (ast->type == CXPR_NODE_INT64) return CXPR_MODEL_RESULT_INT64;
    if (ast->type == CXPR_NODE_PRODUCER_ACCESS && reg) {
        cxpr_func_entry* entry =
            cxpr_registry_find(reg, ast->data.producer_access.name);
        const cxpr_model_child_program* child_ref =
            entry && entry->model_producer_userdata
                ? (const cxpr_model_child_program*)entry->model_producer_userdata
                : NULL;
        if (child_ref && child_ref->program) {
            for (size_t i = 0u;
                 i < cxpr_model_compiled_output_count(child_ref->program);
                 ++i) {
                const char* name =
                    cxpr_model_compiled_output_name(child_ref->program, i);
                if (name &&
                    cxpr_model_names_match(
                        name, ast->data.producer_access.field)) {
                    return cxpr_model_compiled_output_result_kind(
                        child_ref->program, i);
                }
            }
        }
    }
    if (ast->type == CXPR_NODE_INDEX) {
        cxpr_value_type result_type = CXPR_VALUE_NULL;
        if (reg && cxpr_registry_index_target_info(
                       reg, ast->data.index.target, NULL,
                       &result_type, NULL)) {
            if (result_type == CXPR_VALUE_NUMBER) return CXPR_MODEL_RESULT_NUMBER;
            if (result_type == CXPR_VALUE_BOOL) return CXPR_MODEL_RESULT_BOOL;
            return CXPR_MODEL_RESULT_UNKNOWN;
        }
        return cxpr_model_infer_result_kind_depth(
            ast->data.index.target, reg, depth + 1u);
    }
    if (ast->type == CXPR_NODE_BINARY_OP) {
        left = cxpr_model_infer_result_kind_depth(
            ast->data.binary_op.left, reg, depth + 1u);
        right = cxpr_model_infer_result_kind_depth(
            ast->data.binary_op.right, reg, depth + 1u);
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_LT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GT:
        case CXPR_TOK_GTE:
            return CXPR_MODEL_RESULT_BOOL;
        case CXPR_TOK_AND:
        case CXPR_TOK_OR:
            return CXPR_MODEL_RESULT_BOOL;
        default:
            if (left == CXPR_MODEL_RESULT_INT64 && right == CXPR_MODEL_RESULT_INT64)
                return CXPR_MODEL_RESULT_INT64;
            break;
        }
    }
    switch (cxpr_ir_infer_fast_result_kind(ast, reg, depth)) {
    case CXPR_IR_RESULT_DOUBLE: return CXPR_MODEL_RESULT_NUMBER;
    case CXPR_IR_RESULT_BOOL: return CXPR_MODEL_RESULT_BOOL;
    default: return CXPR_MODEL_RESULT_UNKNOWN;
    }
}

cxpr_model_result_kind cxpr_model_infer_result_kind(
    const cxpr_expr_ast* ast,
    const cxpr_registry* reg) {
    return cxpr_model_infer_result_kind_depth(ast, reg, 0u);
}

static bool cxpr_model_append_inferred_input(char*** inputs,
                                             size_t* input_count,
                                             const char* name,
                                             cxpr_error* err) {
    char** grown;
    if (!inputs || !input_count || !name || !name[0]) return true;
    if (cxpr_model_input_name_exists(*inputs, *input_count, name)) return true;
    grown = (char**)realloc(*inputs, (*input_count + 1u) * sizeof(char*));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    *inputs = grown;
    (*inputs)[*input_count] = cxpr_strdup(name);
    if (!(*inputs)[*input_count]) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    (*input_count)++;
    return true;
}

static const char* cxpr_model_import_leaf_name(const char* import_name);

static const cxpr_model_compiled* cxpr_model_import_program_for_name(
    const cxpr_model* model,
    const cxpr_model_import* imports,
    size_t import_count,
    const char* name) {
    if (!name) return NULL;
    for (size_t i = 0u; i < import_count; ++i) {
        const char* ns = cxpr_model_import_namespace_name(model, imports[i].name);
        if (cxpr_model_names_match(imports[i].name, name) ||
            cxpr_model_names_match(ns, name)) {
            return imports[i].program;
        }
    }
    return NULL;
}

static bool cxpr_model_append_synthetic_binding(cxpr_model* model,
                                                const char* name,
                                                const char* source,
                                                cxpr_expr_ast* expr,
                                                cxpr_error* err) {
    cxpr_model_binding* grown;
    if (!model || !name || !source || !expr) return false;
    grown = (cxpr_model_binding*)realloc(
        model->bindings, (model->binding_count + 1u) * sizeof(*model->bindings));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    model->bindings = grown;
    model->bindings[model->binding_count].kind = CXPR_MODEL_BINDING_EXPR;
    model->bindings[model->binding_count].name = cxpr_strdup(name);
    model->bindings[model->binding_count].source = cxpr_strdup(source);
    model->bindings[model->binding_count].expr = expr;
    model->bindings[model->binding_count].span = (cxpr_source_span){0};
    model->bindings[model->binding_count].has_span = false;
    if (!model->bindings[model->binding_count].name ||
        !model->bindings[model->binding_count].source) {
        free(model->bindings[model->binding_count].name);
        free(model->bindings[model->binding_count].source);
        model->bindings[model->binding_count].expr = NULL;
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    model->binding_count++;
    return true;
}

static bool cxpr_model_append_output_name(cxpr_model* model,
                                          const char* name,
                                          cxpr_error* err) {
    char** grown;
    if (!model || !name) return false;
    for (size_t i = 0u; i < model->output_count; ++i) {
        if (cxpr_model_names_match(model->outputs[i], name)) return true;
    }
    grown = (char**)realloc(model->outputs,
                            (model->output_count + 1u) * sizeof(*model->outputs));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    model->outputs = grown;
    model->outputs[model->output_count] = cxpr_strdup(name);
    if (!model->outputs[model->output_count]) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    model->output_count++;
    return true;
}

bool cxpr_model_copy_bindings_and_outputs(cxpr_model* dst,
                                                 const cxpr_model* src,
                                                 cxpr_error* err) {
    if (!dst || !src) return false;
    *dst = *src;
    dst->bindings = NULL;
    dst->binding_count = 0u;
    dst->outputs = NULL;
    dst->output_spans = NULL;
    dst->output_has_spans = NULL;
    dst->output_count = 0u;
    if (src->binding_count > 0u) {
        dst->bindings = (cxpr_model_binding*)calloc(src->binding_count,
                                                    sizeof(*dst->bindings));
        if (!dst->bindings) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        for (size_t i = 0u; i < src->binding_count; ++i) {
            dst->bindings[i].kind = src->bindings[i].kind;
            dst->bindings[i].declared_type = src->bindings[i].declared_type;
            dst->bindings[i].element_type = src->bindings[i].element_type;
            dst->bindings[i].buffer_samples = src->bindings[i].buffer_samples;
            dst->bindings[i].name = cxpr_strdup(src->bindings[i].name);
            dst->bindings[i].source = src->bindings[i].source
                ? cxpr_strdup(src->bindings[i].source) : NULL;
            dst->bindings[i].expr = src->bindings[i].expr
                ? cxpr_expr_ast_clone(src->bindings[i].expr) : NULL;
            dst->bindings[i].span = src->bindings[i].span;
            dst->bindings[i].has_span = src->bindings[i].has_span;
            if (!dst->bindings[i].name ||
                (src->bindings[i].source && !dst->bindings[i].source) ||
                (src->bindings[i].expr && !dst->bindings[i].expr)) {
                dst->binding_count = i + 1u;
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
        }
        dst->binding_count = src->binding_count;
    }
    if (src->output_count > 0u) {
        dst->outputs = (char**)calloc(src->output_count, sizeof(*dst->outputs));
        if (!dst->outputs) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        if (src->output_spans && src->output_has_spans) {
            dst->output_spans =
                (cxpr_source_span*)calloc(src->output_count, sizeof(*dst->output_spans));
            dst->output_has_spans = (bool*)calloc(src->output_count, sizeof(*dst->output_has_spans));
            if (!dst->output_spans || !dst->output_has_spans) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
        }
        for (size_t i = 0u; i < src->output_count; ++i) {
            dst->outputs[i] = cxpr_strdup(src->outputs[i]);
            if (!dst->outputs[i]) {
                dst->output_count = i + 1u;
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            if (dst->output_spans && dst->output_has_spans) {
                dst->output_spans[i] = src->output_spans[i];
                dst->output_has_spans[i] = src->output_has_spans[i];
            }
        }
        dst->output_count = src->output_count;
    }
    return true;
}

void cxpr_model_expanded_copy_free(cxpr_model* model) {
    if (!model) return;
    for (size_t i = 0u; i < model->binding_count; ++i) {
        free(model->bindings[i].name);
        free(model->bindings[i].source);
        cxpr_expr_ast_free(model->bindings[i].expr);
    }
    free(model->bindings);
    for (size_t i = 0u; i < model->output_count; ++i) {
        free(model->outputs[i]);
    }
    free(model->outputs);
    free(model->output_spans);
    free(model->output_has_spans);
    model->bindings = NULL;
    model->binding_count = 0u;
    model->outputs = NULL;
    model->output_spans = NULL;
    model->output_has_spans = NULL;
    model->output_count = 0u;
}

static char** cxpr_model_clone_arg_names_for_producer(char* const* names,
                                                      size_t argc,
                                                      cxpr_error* err) {
    char** out;
    bool any = false;
    if (!names || argc == 0u) return NULL;
    out = (char**)calloc(argc, sizeof(char*));
    if (!out) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    for (size_t i = 0u; i < argc; ++i) {
        if (!names[i]) continue;
        any = true;
        out[i] = cxpr_strdup(names[i]);
        if (!out[i]) {
            for (size_t j = 0u; j < i; ++j) free(out[j]);
            free(out);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
    }
    if (!any) {
        free(out);
        return NULL;
    }
    return out;
}

static cxpr_expr_ast* cxpr_model_clone_call_as_field_access(const cxpr_expr_ast* call,
                                                       const char* field,
                                                       cxpr_error* err) {
    cxpr_expr_ast** args = NULL;
    char** arg_names = NULL;
    size_t argc;
    cxpr_expr_ast* out;
    if (!call || call->type != CXPR_NODE_FUNCTION_CALL || !field) return NULL;
    argc = call->data.function_call.argc;
    if (argc > 0u) {
        args = (cxpr_expr_ast**)calloc(argc, sizeof(*args));
        if (!args) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        for (size_t i = 0u; i < argc; ++i) {
            args[i] = cxpr_expr_ast_clone(call->data.function_call.args[i]);
            if (!args[i]) {
                for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(args[j]);
                free(args);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
        arg_names = cxpr_model_clone_arg_names_for_producer(
            call->data.function_call.arg_names, argc, err);
        if (call->data.function_call.arg_names && !arg_names && err && err->code != CXPR_OK) {
            for (size_t i = 0u; i < argc; ++i) cxpr_expr_ast_free(args[i]);
            free(args);
            return NULL;
        }
    }
    out = cxpr_expr_ast_producer_field_named_new(call->data.function_call.name,
                                             args,
                                             arg_names,
                                             argc,
                                             field);
    if (!out) {
        for (size_t i = 0u; i < argc; ++i) cxpr_expr_ast_free(args ? args[i] : NULL);
        free(args);
        if (arg_names) {
            for (size_t i = 0u; i < argc; ++i) free(arg_names[i]);
            free(arg_names);
        }
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
    }
    return out;
}

static const char* cxpr_model_import_leaf_name(const char* import_name) {
    const char* slash;
    if (!import_name) return NULL;
    slash = strrchr(import_name, '/');
    return slash && slash[1] != '\0' ? slash + 1 : import_name;
}

const char* cxpr_model_import_namespace_name(const cxpr_model* model,
                                                    const char* import_name) {
    if (!model || !import_name) return import_name;
    for (size_t i = 0u; i < model->use_count; ++i) {
        if (cxpr_model_names_match(model->uses[i], import_name)) {
            return model->use_aliases && model->use_aliases[i]
                       ? model->use_aliases[i]
                       : cxpr_model_import_leaf_name(import_name);
        }
    }
    return import_name;
}

static bool cxpr_model_infer_child_inputs_from_ast(const cxpr_expr_ast* ast,
                                                   const cxpr_model* model,
                                                   const cxpr_model_import* imports,
                                                   size_t import_count,
                                                   char*** inputs,
                                                   size_t* input_count,
                                                   cxpr_error* err) {
    if (!ast) return true;
    switch (ast->type) {
        case CXPR_NODE_ARRAY:
            for (size_t i = 0u; i < ast->data.array.count; ++i) {
                if (!cxpr_model_infer_child_inputs_from_ast(
                        ast->data.array.elements[i], model, imports, import_count, inputs, input_count, err)) {
                    return false;
                }
            }
            return true;
        case CXPR_NODE_RECORD:
            for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
                if (!cxpr_model_infer_child_inputs_from_ast(
                        ast->data.record.field_values[i], model, imports, import_count, inputs, input_count, err)) {
                    return false;
                }
            }
            return true;
        case CXPR_NODE_BINARY_OP:
            return cxpr_model_infer_child_inputs_from_ast(
                       ast->data.binary_op.left, model, imports, import_count, inputs, input_count, err) &&
                   cxpr_model_infer_child_inputs_from_ast(
                       ast->data.binary_op.right, model, imports, import_count, inputs, input_count, err);
        case CXPR_NODE_UNARY_OP:
            return cxpr_model_infer_child_inputs_from_ast(
                ast->data.unary_op.operand, model, imports, import_count, inputs, input_count, err);
        case CXPR_NODE_FUNCTION_CALL:
            for (size_t i = 0u; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_model_infer_child_inputs_from_ast(
                        ast->data.function_call.args[i], model, imports, import_count, inputs, input_count, err)) {
                    return false;
                }
            }
            return true;
        case CXPR_NODE_PRODUCER_ACCESS: {
            const cxpr_model_compiled* child =
                cxpr_model_import_program_for_name(model, imports, import_count,
                                                   ast->data.producer_access.name);
            if (child) {
                bool call_supplies_source = false;
                if (child->source_arg) {
                    if (ast->data.producer_access.argc == child->constant_count + 1u &&
                        !cxpr_expr_ast_producer_has_named_args(ast)) {
                        call_supplies_source = true;
                    }
                    for (size_t arg_i = 0u; arg_i < ast->data.producer_access.argc; ++arg_i) {
                        const char* arg_name = cxpr_expr_ast_producer_arg_name(ast, arg_i);
                        if (arg_name && cxpr_model_names_match(arg_name, child->source_arg)) {
                            call_supplies_source = true;
                            break;
                        }
                    }
                }
                for (size_t i = 0u; i < child->input_count; ++i) {
                    bool call_supplies_input = false;
                    for (size_t arg_i = 0u; arg_i < ast->data.producer_access.argc; ++arg_i) {
                        const char* arg_name = cxpr_expr_ast_producer_arg_name(ast, arg_i);
                        if (arg_name && cxpr_model_names_match(arg_name, child->inputs[i])) {
                            call_supplies_input = true;
                            break;
                        }
                    }
                    if (call_supplies_input) continue;
                    if (call_supplies_source &&
                        child->source_arg &&
                        cxpr_model_names_match(child->inputs[i], child->source_arg)) {
                        continue;
                    }
                    if (!cxpr_model_append_inferred_input(
                            inputs, input_count, child->inputs[i], err)) {
                        return false;
                    }
                }
            }
            for (size_t i = 0u; i < ast->data.producer_access.argc; ++i) {
                if (!cxpr_model_infer_child_inputs_from_ast(
                        ast->data.producer_access.args[i], model, imports, import_count, inputs, input_count, err)) {
                    return false;
                }
            }
            return true;
        }
        case CXPR_NODE_INDEX:
            return cxpr_model_infer_child_inputs_from_ast(
                       ast->data.index.target, model, imports, import_count, inputs, input_count, err) &&
                   cxpr_model_infer_child_inputs_from_ast(
                       ast->data.index.index, model, imports, import_count, inputs, input_count, err);
        case CXPR_NODE_TERNARY:
            return cxpr_model_infer_child_inputs_from_ast(
                       ast->data.ternary.condition, model, imports, import_count, inputs, input_count, err) &&
                   cxpr_model_infer_child_inputs_from_ast(
                       ast->data.ternary.true_branch, model, imports, import_count, inputs, input_count, err) &&
                   cxpr_model_infer_child_inputs_from_ast(
                       ast->data.ternary.false_branch, model, imports, import_count, inputs, input_count, err);
        default:
            return true;
    }
}

static bool cxpr_model_binding_name_exists(const cxpr_model* model, const char* name) {
    if (!model || !name) return false;
    for (size_t i = 0u; i < model->binding_count; ++i) {
        if (cxpr_model_names_match(model->bindings[i].name, name)) return true;
    }
    return false;
}

static bool cxpr_model_constant_name_exists(const cxpr_model* model, const char* name) {
    if (!model || !name) return false;
    for (size_t i = 0u; i < model->constant_count; ++i) {
        if (cxpr_model_names_match(model->constants[i].name, name)) return true;
    }
    return false;
}

bool cxpr_model_infer_inputs_for_compile(const cxpr_model* model,
                                                const cxpr_model_import* imports,
                                                size_t import_count,
                                                char*** out_inputs,
                                                size_t* out_input_count,
                                                cxpr_error* err) {
    const char* refs[256];
    char** inputs = NULL;
    size_t input_count = 0u;
    bool infer_direct_refs;
    if (out_inputs) *out_inputs = NULL;
    if (out_input_count) *out_input_count = 0u;
    if (!model) return true;

    infer_direct_refs = model->input_count == 0u;
    for (size_t i = 0u; i < model->input_count; ++i) {
        if (!cxpr_model_append_inferred_input(&inputs, &input_count, model->inputs[i], err)) {
            goto fail;
        }
    }

    for (size_t i = 0u; i < model->binding_count; ++i) {
        size_t nrefs;
        if (!model->bindings[i].expr) continue;
        if (!cxpr_model_infer_child_inputs_from_ast(
                model->bindings[i].expr,
                model,
                imports,
                import_count,
                &inputs,
                &input_count,
                err)) {
            goto fail;
        }
        if (!infer_direct_refs) continue;
        nrefs = cxpr_expr_ast_references(model->bindings[i].expr, refs, CXPR_ARRAY_COUNT(refs));
        for (size_t j = 0u; j < nrefs && j < CXPR_ARRAY_COUNT(refs); ++j) {
            if (cxpr_model_binding_name_exists(model, refs[j]) ||
                cxpr_model_constant_name_exists(model, refs[j])) {
                continue;
            }
            if (!cxpr_model_append_inferred_input(&inputs, &input_count, refs[j], err)) goto fail;
        }
    }
    if (input_count == model->input_count) {
        for (size_t i = 0u; i < input_count; ++i) free(inputs[i]);
        free(inputs);
        inputs = NULL;
        input_count = 0u;
    }
    if (out_inputs) *out_inputs = inputs;
    if (out_input_count) *out_input_count = input_count;
    return true;

fail:
    for (size_t i = 0u; i < input_count; ++i) free(inputs[i]);
    free(inputs);
    return false;
}

bool cxpr_model_expand_anonymous_outputs(cxpr_model* model,
                                                const cxpr_model_import* imports,
                                                size_t import_count,
                                                cxpr_error* err) {
    if (!model || model->anonymous_output_count == 0u) return true;
    for (size_t i = 0u; i < model->anonymous_output_count; ++i) {
        const cxpr_expr_ast* expr = model->anonymous_outputs[i].expr;
        const cxpr_model_compiled* child;
        if (!expr || expr->type != CXPR_NODE_FUNCTION_CALL) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Anonymous out must be a record function call", 0, 0);
            return false;
        }
        child = cxpr_model_import_program_for_name(
            model, imports, import_count, expr->data.function_call.name);
        if (!child || child->output_count == 0u) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                 "Anonymous out references unknown record function", 0, 0);
            return false;
        }
        for (size_t field_i = 0u; field_i < child->output_count; ++field_i) {
            const char* field = child->outputs[field_i];
            cxpr_expr_ast* field_ast = cxpr_model_clone_call_as_field_access(expr, field, err);
            if (!field_ast) return false;
            if (!cxpr_model_append_synthetic_binding(
                    model, field, model->anonymous_outputs[i].source, field_ast, err)) {
                cxpr_expr_ast_free(field_ast);
                return false;
            }
            if (!cxpr_model_append_output_name(model, field, err)) {
                return false;
            }
        }
    }
    return true;
}
