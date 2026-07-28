/**
 * @file model.c
 * @brief Minimal host-agnostic .cxpr model parser.
 */

#include "core.h"
#include "ast/internal.h"
#include "ir/compile/internal.h"
#include "lookback.h"
#include "model/internal.h"
#include "model/window/window.h"
#include "registry/internal.h"
#include <cxpr/source.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



void cxpr_model_set_error(cxpr_error* err, cxpr_error_code code,
                          const char* message, size_t line, size_t column) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0;
    err->line = line;
    err->column = column;
}


static bool cxpr_model_input_name_exists(char* const* inputs, size_t count, const char* name) {
    if (!name) return false;
    for (size_t i = 0u; i < count; ++i) {
        if (cxpr_model_names_match(inputs[i], name)) return true;
    }
    return false;
}

static cxpr_model_result_kind cxpr_model_infer_result_kind(const cxpr_ast* ast,
                                                             const cxpr_registry* reg) {
    switch (cxpr_ir_infer_fast_result_kind(ast, reg, 0u)) {
    case CXPR_IR_RESULT_DOUBLE: return CXPR_MODEL_RESULT_NUMBER;
    case CXPR_IR_RESULT_BOOL: return CXPR_MODEL_RESULT_BOOL;
    default: return CXPR_MODEL_RESULT_UNKNOWN;
    }
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
static const char* cxpr_model_import_namespace_name(const cxpr_model* model,
                                                    const char* import_name);
static char* cxpr_model_parse_source_arg_metadata(const cxpr_model* model);
static bool cxpr_model_parse_lifetime_metadata(const cxpr_model* model,
                                               cxpr_model_lifetime* out_lifetime,
                                               bool* out_saw_type,
                                               cxpr_error* err);

static const cxpr_model_program* cxpr_model_import_program_for_name(
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
                                                cxpr_ast* expr,
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

static bool cxpr_model_copy_bindings_and_outputs(cxpr_model* dst,
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
            dst->bindings[i].name = cxpr_strdup(src->bindings[i].name);
            dst->bindings[i].source = cxpr_strdup(src->bindings[i].source);
            dst->bindings[i].expr = cxpr_ast_clone(src->bindings[i].expr);
            dst->bindings[i].span = src->bindings[i].span;
            dst->bindings[i].has_span = src->bindings[i].has_span;
            if (!dst->bindings[i].name || !dst->bindings[i].source ||
                !dst->bindings[i].expr) {
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

static void cxpr_model_expanded_copy_free(cxpr_model* model) {
    if (!model) return;
    for (size_t i = 0u; i < model->binding_count; ++i) {
        free(model->bindings[i].name);
        free(model->bindings[i].source);
        cxpr_ast_free(model->bindings[i].expr);
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

static cxpr_ast* cxpr_model_clone_call_as_field_access(const cxpr_ast* call,
                                                       const char* field,
                                                       cxpr_error* err) {
    cxpr_ast** args = NULL;
    char** arg_names = NULL;
    size_t argc;
    cxpr_ast* out;
    if (!call || call->type != CXPR_NODE_FUNCTION_CALL || !field) return NULL;
    argc = call->data.function_call.argc;
    if (argc > 0u) {
        args = (cxpr_ast**)calloc(argc, sizeof(*args));
        if (!args) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        for (size_t i = 0u; i < argc; ++i) {
            args[i] = cxpr_ast_clone(call->data.function_call.args[i]);
            if (!args[i]) {
                for (size_t j = 0u; j < i; ++j) cxpr_ast_free(args[j]);
                free(args);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
        arg_names = cxpr_model_clone_arg_names_for_producer(
            call->data.function_call.arg_names, argc, err);
        if (call->data.function_call.arg_names && !arg_names && err && err->code != CXPR_OK) {
            for (size_t i = 0u; i < argc; ++i) cxpr_ast_free(args[i]);
            free(args);
            return NULL;
        }
    }
    out = cxpr_ast_new_producer_access_named(call->data.function_call.name,
                                             args,
                                             arg_names,
                                             argc,
                                             field);
    if (!out) {
        for (size_t i = 0u; i < argc; ++i) cxpr_ast_free(args ? args[i] : NULL);
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

static const char* cxpr_model_import_namespace_name(const cxpr_model* model,
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

static char* cxpr_model_dup_trimmed_metadata_value(const char* value) {
    const char* end;
    size_t len;
    char* out;
    if (!value) return NULL;
    end = value;
    while (*end && *end != '\n' && *end != ',' && *end != '}') end++;
    while (value < end && (*value == ' ' || *value == '\t' || *value == '\r')) value++;
    while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    if (end > value + 1u &&
        ((*value == '"' && end[-1] == '"') || (*value == '\'' && end[-1] == '\''))) {
        value++;
        end--;
    }
    len = (size_t)(end - value);
    if (len == 0u) return NULL;
    out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    memcpy(out, value, len);
    out[len] = '\0';
    return out;
}

static const char* cxpr_model_model_field_value(const cxpr_model* model,
                                                const char* key) {
    const cxpr_model_host_block* block;
    if (!model) return NULL;
    for (size_t i = 0u; i < model->metadata_count; ++i) {
        if (model->metadatas[i].target_kind != CXPR_MODEL_METADATA_TARGET_MODEL) continue;
        {
            const char* value = cxpr_model_metadata_field_value(model, i, key);
            if (value) return value;
        }
    }
    block = cxpr_model_host_block_by_kind(model, "model");
    return block ? cxpr_host_block_field_value_by_key(block, key) : NULL;
}

static char* cxpr_model_parse_source_arg_metadata(const cxpr_model* model) {
    const char* value = cxpr_model_model_field_value(model, "source_arg");
    if (value) {
        return cxpr_model_dup_trimmed_metadata_value(value);
    }
    return NULL;
}

static bool cxpr_model_parse_lifetime_metadata(const cxpr_model* model,
                                               cxpr_model_lifetime* out_lifetime,
                                               bool* out_saw_type,
                                               cxpr_error* err) {
    cxpr_model_lifetime lifetime = CXPR_MODEL_LIFETIME_SINGLETON;
    bool saw_type = false;
    if (!model) {
        if (out_lifetime) *out_lifetime = lifetime;
        if (out_saw_type) *out_saw_type = false;
        return true;
    }
    {
        const char* value;
        char* type;
        value = cxpr_model_model_field_value(model, "lifecycle");
        if (!value) value = cxpr_model_model_field_value(model, "type");
        if (!value) goto done;
        type = cxpr_model_dup_trimmed_metadata_value(value);
        if (!type) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        saw_type = true;
        if (cxpr_model_names_match(type, "singleton")) {
            lifetime = CXPR_MODEL_LIFETIME_SINGLETON;
        } else if (cxpr_model_names_match(type, "scoped")) {
            lifetime = CXPR_MODEL_LIFETIME_SCOPED;
        } else if (cxpr_model_names_match(type, "transient")) {
            lifetime = CXPR_MODEL_LIFETIME_TRANSIENT;
        } else {
            free(type);
            goto done;
        }
        free(type);
    }
done:
    if (out_lifetime) *out_lifetime = lifetime;
    if (out_saw_type) *out_saw_type = saw_type;
    return true;
}

static bool cxpr_model_infer_child_inputs_from_ast(const cxpr_ast* ast,
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
            const cxpr_model_program* child =
                cxpr_model_import_program_for_name(model, imports, import_count,
                                                   ast->data.producer_access.name);
            if (child) {
                bool call_supplies_source = false;
                if (child->source_arg) {
                    if (ast->data.producer_access.argc == child->constant_count + 1u &&
                        !cxpr_ast_producer_has_named_args(ast)) {
                        call_supplies_source = true;
                    }
                    for (size_t arg_i = 0u; arg_i < ast->data.producer_access.argc; ++arg_i) {
                        const char* arg_name = cxpr_ast_producer_arg_name(ast, arg_i);
                        if (arg_name && cxpr_model_names_match(arg_name, child->source_arg)) {
                            call_supplies_source = true;
                            break;
                        }
                    }
                }
                for (size_t i = 0u; i < child->input_count; ++i) {
                    bool call_supplies_input = false;
                    for (size_t arg_i = 0u; arg_i < ast->data.producer_access.argc; ++arg_i) {
                        const char* arg_name = cxpr_ast_producer_arg_name(ast, arg_i);
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
        case CXPR_NODE_LOOKBACK:
            return cxpr_model_infer_child_inputs_from_ast(
                       ast->data.lookback.target, model, imports, import_count, inputs, input_count, err) &&
                   cxpr_model_infer_child_inputs_from_ast(
                       ast->data.lookback.index, model, imports, import_count, inputs, input_count, err);
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

static bool cxpr_model_infer_inputs_for_compile(const cxpr_model* model,
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
        nrefs = cxpr_ast_references(model->bindings[i].expr, refs, CXPR_ARRAY_COUNT(refs));
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

static bool cxpr_model_expand_anonymous_outputs(cxpr_model* model,
                                                const cxpr_model_import* imports,
                                                size_t import_count,
                                                cxpr_error* err) {
    if (!model || model->anonymous_output_count == 0u) return true;
    for (size_t i = 0u; i < model->anonymous_output_count; ++i) {
        const cxpr_ast* expr = model->anonymous_outputs[i].expr;
        const cxpr_model_program* child;
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
            cxpr_ast* field_ast = cxpr_model_clone_call_as_field_access(expr, field, err);
            if (!field_ast) return false;
            if (!cxpr_model_append_synthetic_binding(
                    model, field, model->anonymous_outputs[i].source, field_ast, err)) {
                cxpr_ast_free(field_ast);
                return false;
            }
            if (!cxpr_model_append_output_name(model, field, err)) {
                return false;
            }
        }
    }
    return true;
}

static void cxpr_model_slot_ref_free(cxpr_model_slot_ref* ref) {
    if (!ref) return;
    free(ref->name);
    ref->name = NULL;
    ref->hash = 0u;
    ref->slot = 0u;
    ref->result_kind = CXPR_MODEL_RESULT_UNKNOWN;
}

void cxpr_model_fused_program_clear(cxpr_model_program* program) {
    if (!program) return;
    cxpr_ir_program_reset(&program->fused_ir);
    for (size_t i = 0; i < program->fused_slot_count; ++i) {
        free(program->fused_slot_names[i]);
    }
    free(program->fused_slot_names);
    free(program->fused_slot_hashes);
    for (size_t i = 0; i < program->fused_input_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_inputs[i]);
    }
    free(program->fused_inputs);
    for (size_t i = 0; i < program->fused_export_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_exports[i]);
    }
    free(program->fused_exports);
    for (size_t i = 0; i < program->fused_output_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_outputs[i]);
    }
    free(program->fused_outputs);
    free(program->fused_commits);
    program->has_fused_ir = false;
    program->has_fused_layout = false;
    program->fused_disabled_opcode = NULL;
    program->fused_slot_names = NULL;
    program->fused_slot_hashes = NULL;
    program->fused_slot_count = 0u;
    program->fused_inputs = NULL;
    program->fused_input_count = 0u;
    program->fused_exports = NULL;
    program->fused_export_count = 0u;
    program->fused_outputs = NULL;
    program->fused_output_count = 0u;
    program->fused_commits = NULL;
    program->fused_commit_count = 0u;
}

bool cxpr_model_names_match(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}


static bool cxpr_model_executable_eval_order(const cxpr_model* model,
                                             size_t* out_order,
                                             size_t executable_count,
                                             cxpr_error* err) {
    cxpr_expression_def* defs;
    cxpr_analysis* analyses;
    size_t* map;
    char** def_names;
    size_t def_count = 0u;
    bool ok;

    if (executable_count == 0u) return true;
    defs = (cxpr_expression_def*)calloc(executable_count, sizeof(cxpr_expression_def));
    analyses = (cxpr_analysis*)calloc(executable_count, sizeof(cxpr_analysis));
    map = (size_t*)calloc(executable_count, sizeof(size_t));
    def_names = (char**)calloc(executable_count, sizeof(char*));
    if (!defs || !analyses || !map || !def_names) {
        free(defs);
        free(analyses);
        free(map);
        free(def_names);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }

    for (size_t i = 0; i < model->binding_count; ++i) {
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE) continue;
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            size_t len = strlen(model->bindings[i].name) + strlen("__state_update_") + 1u;
            def_names[def_count] = (char*)malloc(len);
            if (!def_names[def_count]) {
                ok = false;
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                goto cleanup;
            }
            snprintf(def_names[def_count], len, "__state_update_%s", model->bindings[i].name);
            defs[def_count].name = def_names[def_count];
        } else {
            defs[def_count].name = model->bindings[i].name;
        }
        defs[def_count].expression = model->bindings[i].source;
        map[def_count] = i;
        def_count++;
    }

    ok = cxpr_analyze_expressions(defs, def_count, NULL, analyses, out_order, err);
    if (ok) {
        for (size_t i = 0; i < def_count; ++i) out_order[i] = map[out_order[i]];
    }
cleanup:
    for (size_t i = 0; i < executable_count; ++i) free(def_names[i]);
    free(defs);
    free(analyses);
    free(map);
    free(def_names);
    return ok;
}

static cxpr_model_result_kind cxpr_model_state_default_result_kind(
    const cxpr_model_program* program,
    const char* name) {
    if (!program || !name) return CXPR_MODEL_RESULT_UNKNOWN;
    for (size_t i = 0; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            return program->state_defaults[i].result_kind;
        }
    }
    return CXPR_MODEL_RESULT_UNKNOWN;
}

static const cxpr_model_compile_options cxpr_model_default_compile_options = {
    CXPR_MODEL_BACKEND_AUTO,
    true,
    false,
};

static bool cxpr_model_compile_options_resolve(
    const cxpr_model_compile_options* options,
    cxpr_model_compile_options* out,
    cxpr_error* err) {
    if (!out) return false;
    *out = options ? *options : cxpr_model_default_compile_options;
    switch (out->backend) {
    case CXPR_MODEL_BACKEND_AUTO:
    case CXPR_MODEL_BACKEND_IR:
    case CXPR_MODEL_BACKEND_C:
        break;
    default:
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Invalid model compile backend option", 0, 0);
        return false;
    }
    if (out->backend == CXPR_MODEL_BACKEND_IR && !out->fuse) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model IR backend requires fuse=true", 0, 0);
        return false;
    }
    if (out->enable_trace && out->backend != CXPR_MODEL_BACKEND_AUTO) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model backend tracing is not supported for explicit backends", 0, 0);
        return false;
    }
    return true;
}

static void cxpr_model_program_drop_runnable_fast_path(cxpr_model_program* program) {
    if (!program) return;
    cxpr_ir_program_reset(&program->fused_ir);
    program->has_fused_ir = false;
}

static bool cxpr_model_program_validate_c_backend(cxpr_model_program* program,
                                                  cxpr_error* err) {
    cxpr_error codegen_err = {0};
    char* source = cxpr_model_program_to_c_tick_function(
        program, "", "cxpr_model_backend_validate", &codegen_err);
    if (!source) {
        if (err) {
            *err = codegen_err;
            if (err->code == CXPR_OK) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Model C backend is not supported for this model", 0, 0);
            }
        }
        return false;
    }
    free(source);
    if (err) err->code = CXPR_OK;
    return true;
}

static bool cxpr_model_program_select_backend(cxpr_model_program* program,
                                              const cxpr_model* model,
                                              const cxpr_registry* compile_reg,
                                              const cxpr_model_compile_options* options,
                                              cxpr_error* err) {
    if (!program || !options) return false;
    program->requested_backend = options->backend;
    program->selected_backend = CXPR_MODEL_BACKEND_AUTO;
    program->compile_fuse = options->fuse;
    program->compile_trace = options->enable_trace;

    if (options->backend == CXPR_MODEL_BACKEND_AUTO) {
        if (options->enable_trace) {
            program->fused_disabled_opcode = "trace enabled";
            return true;
        }
        if (!options->fuse) {
            program->fused_disabled_opcode = "fast path disabled by compile options";
            return true;
        }
        if (!cxpr_model_try_compile_fused_ir(program, model, compile_reg, err)) {
            return false;
        }
        program->selected_backend = program->has_fused_ir
                                        ? CXPR_MODEL_BACKEND_IR
                                        : CXPR_MODEL_BACKEND_AUTO;
        return true;
    }

    if (!cxpr_model_try_compile_fused_ir(program, model, compile_reg, err)) {
        return false;
    }

    if (options->backend == CXPR_MODEL_BACKEND_IR) {
        if (!program->has_fused_ir) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Model IR backend requires scalar fast-path support", 0, 0);
            return false;
        }
        program->selected_backend = CXPR_MODEL_BACKEND_IR;
        return true;
    }

    if (!options->fuse) {
        cxpr_model_program_drop_runnable_fast_path(program);
    }
    if (!cxpr_model_program_validate_c_backend(program, err)) {
        return false;
    }
    program->selected_backend = CXPR_MODEL_BACKEND_C;
    return true;
}

cxpr_model_program* cxpr_compile_model(const cxpr_model* model,
                                       const cxpr_registry* reg,
                                       cxpr_error* err) {
    return cxpr_compile_model_with_options(model, reg, NULL, err);
}

cxpr_model_program* cxpr_compile_model_with_options(
    const cxpr_model* model,
    const cxpr_registry* reg,
    const cxpr_model_compile_options* options,
    cxpr_error* err) {
    return cxpr_compile_model_with_imports_and_options(model, reg, NULL, 0u, options, err);
}

static const char* cxpr_model_import_namespace_for(const cxpr_model* model,
                                                   const char* import_name) {
    return cxpr_model_import_namespace_name(model, import_name);
}

static size_t cxpr_model_program_exposed_param_count(const cxpr_model_program* program) {
    size_t explicit_count = 0u;
    if (!program) return 0u;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (program->constants[i].is_call_param) ++explicit_count;
    }
    return explicit_count > 0u ? explicit_count : program->constant_count;
}

static bool cxpr_model_program_constant_is_exposed(const cxpr_model_program* program,
                                                   size_t index) {
    size_t explicit_count = 0u;
    if (!program || index >= program->constant_count) return false;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (program->constants[i].is_call_param) ++explicit_count;
    }
    return explicit_count == 0u || program->constants[index].is_call_param;
}

static bool cxpr_model_program_inputs_are_implicit_market(
    const cxpr_model_program* program) {
    if (!program || program->input_count == 0u || program->source_arg) return false;
    for (size_t i = 0u; i < program->input_count; ++i) {
        const char* input = program->inputs[i];
        if (!input ||
            (!cxpr_model_names_match(input, "open") &&
             !cxpr_model_names_match(input, "high") &&
             !cxpr_model_names_match(input, "low") &&
             !cxpr_model_names_match(input, "close") &&
             !cxpr_model_names_match(input, "volume"))) {
            return false;
        }
    }
    return true;
}

static char* cxpr_model_join_namespace(const char* ns, const char* name) {
    size_t ns_len;
    size_t name_len;
    char* out;
    if (!ns || !name) return NULL;
    ns_len = strlen(ns);
    name_len = strlen(name);
    out = (char*)malloc(ns_len + 1u + name_len + 1u);
    if (!out) return NULL;
    memcpy(out, ns, ns_len);
    out[ns_len] = '.';
    memcpy(out + ns_len + 1u, name, name_len);
    out[ns_len + 1u + name_len] = '\0';
    return out;
}

static bool cxpr_model_import_entry_is_namespaced(const cxpr_func_entry* entry) {
    return entry && !entry->model_producer &&
           (entry->defined_body || entry->defined_return_field_count > 0u);
}

static bool cxpr_model_import_name_should_namespace(const cxpr_registry* source_registry,
                                                    const char* name) {
    cxpr_func_entry* entry;
    if (!source_registry || !name || strchr(name, '.')) return false;
    entry = cxpr_registry_find(source_registry, name);
    return cxpr_model_import_entry_is_namespaced(entry);
}

static bool cxpr_model_namespace_function_name(char** name,
                                               const char* namespace_name,
                                               const cxpr_registry* source_registry,
                                               cxpr_error* err) {
    char* qualified;
    if (!name || !*name ||
        !cxpr_model_import_name_should_namespace(source_registry, *name)) {
        return true;
    }
    qualified = cxpr_model_join_namespace(namespace_name, *name);
    if (!qualified) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    free(*name);
    *name = qualified;
    return true;
}

static bool cxpr_model_namespace_imported_ast(cxpr_ast* ast,
                                              const char* namespace_name,
                                              const cxpr_registry* source_registry,
                                              cxpr_error* err) {
    if (!ast) return true;
    switch (ast->type) {
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i) {
            if (!cxpr_model_namespace_imported_ast(
                    ast->data.array.elements[i], namespace_name, source_registry, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            if (!cxpr_model_namespace_imported_ast(
                    ast->data.record.field_values[i], namespace_name, source_registry, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_namespace_imported_ast(
                   ast->data.binary_op.left, namespace_name, source_registry, err) &&
               cxpr_model_namespace_imported_ast(
                   ast->data.binary_op.right, namespace_name, source_registry, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_namespace_imported_ast(
            ast->data.unary_op.operand, namespace_name, source_registry, err);
    case CXPR_NODE_FUNCTION_CALL:
        if (!cxpr_model_namespace_function_name(&ast->data.function_call.name,
                                                namespace_name, source_registry, err)) {
            return false;
        }
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i) {
            if (!cxpr_model_namespace_imported_ast(
                    ast->data.function_call.args[i], namespace_name, source_registry, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_PRODUCER_ACCESS: {
        bool rename = cxpr_model_import_name_should_namespace(
            source_registry, ast->data.producer_access.name);
        if (rename) {
            char* qualified = cxpr_model_join_namespace(namespace_name,
                                                        ast->data.producer_access.name);
            char* full_key = cxpr_model_join_namespace(qualified,
                                                       ast->data.producer_access.field);
            if (!qualified || !full_key) {
                free(qualified);
                free(full_key);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            free(ast->data.producer_access.name);
            free(ast->data.producer_access.full_key);
            ast->data.producer_access.name = qualified;
            ast->data.producer_access.full_key = full_key;
        }
        for (size_t i = 0u; i < ast->data.producer_access.argc; ++i) {
            if (!cxpr_model_namespace_imported_ast(
                    ast->data.producer_access.args[i], namespace_name, source_registry, err)) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_namespace_imported_ast(
                   ast->data.lookback.target, namespace_name, source_registry, err) &&
               cxpr_model_namespace_imported_ast(
                   ast->data.lookback.index, namespace_name, source_registry, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_namespace_imported_ast(
                   ast->data.ternary.condition, namespace_name, source_registry, err) &&
               cxpr_model_namespace_imported_ast(
                   ast->data.ternary.true_branch, namespace_name, source_registry, err) &&
               cxpr_model_namespace_imported_ast(
                   ast->data.ternary.false_branch, namespace_name, source_registry, err);
    default:
        return true;
    }
}

static bool cxpr_model_clone_defined_param_fields(cxpr_func_entry* dst,
                                                  const cxpr_func_entry* src,
                                                  cxpr_error* err) {
    if (!src->defined_param_fields && !src->defined_param_field_counts) return true;
    dst->defined_param_fields =
        (char***)calloc(src->defined_param_count ? src->defined_param_count : 1u,
                        sizeof(char**));
    dst->defined_param_field_counts =
        (size_t*)calloc(src->defined_param_count ? src->defined_param_count : 1u,
                        sizeof(size_t));
    if (!dst->defined_param_fields || !dst->defined_param_field_counts) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < src->defined_param_count; ++i) {
        size_t count = src->defined_param_field_counts ? src->defined_param_field_counts[i] : 0u;
        dst->defined_param_field_counts[i] = count;
        if (count == 0u) continue;
        dst->defined_param_fields[i] = cxpr_registry_clone_param_names(
            (const char* const*)src->defined_param_fields[i], count);
        if (!dst->defined_param_fields[i]) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    return true;
}

static bool cxpr_model_register_imported_defined_function(cxpr_model_program* program,
                                                          const char* namespace_name,
                                                          const cxpr_registry* source_registry,
                                                          const cxpr_func_entry* src,
                                                          cxpr_error* err) {
    char* qualified = NULL;
    cxpr_func_entry* entry;
    if (!program || !program->registry || !namespace_name || !src || !src->name) return true;
    if (!cxpr_model_import_entry_is_namespaced(src)) return true;
    qualified = strchr(src->name, '.') ? cxpr_strdup(src->name)
                                       : cxpr_model_join_namespace(namespace_name, src->name);
    if (!qualified) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    entry = cxpr_registry_find(program->registry, qualified);
    if (entry) {
        cxpr_registry_clear_owned_entry(entry);
    } else {
        if (program->registry->count >= program->registry->capacity &&
            !cxpr_registry_grow(program->registry)) {
            free(qualified);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        entry = &program->registry->entries[program->registry->count++];
        cxpr_registry_prepare_entry(entry, qualified);
        if (!entry->name) {
            free(qualified);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    free(qualified);
    entry->min_args = src->min_args;
    entry->max_args = src->max_args;
    entry->return_type = src->return_type;
    entry->has_return_type = src->has_return_type;
    entry->defined_body = cxpr_ast_clone(src->defined_body);
    if (src->defined_body && !entry->defined_body) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    if (!cxpr_model_namespace_imported_ast(
            entry->defined_body, namespace_name, source_registry, err)) {
        return false;
    }
    entry->defined_param_count = src->defined_param_count;
    entry->defined_param_names = cxpr_registry_clone_param_names(
        (const char* const*)src->defined_param_names, src->defined_param_count);
    if (src->defined_param_count > 0u && !entry->defined_param_names) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    if (!cxpr_model_clone_defined_param_fields(entry, src, err)) return false;
    entry->defined_return_field_count = src->defined_return_field_count;
    entry->defined_return_field_names = cxpr_registry_clone_param_names(
        (const char* const*)src->defined_return_field_names,
        src->defined_return_field_count);
    if (src->defined_return_field_count > 0u && !entry->defined_return_field_names) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    if (src->defined_return_field_count > 0u) {
        entry->defined_return_field_bodies =
            (cxpr_ast**)calloc(src->defined_return_field_count, sizeof(cxpr_ast*));
        if (!entry->defined_return_field_bodies) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        for (size_t i = 0u; i < src->defined_return_field_count; ++i) {
            entry->defined_return_field_bodies[i] =
                cxpr_ast_clone(src->defined_return_field_bodies[i]);
            if (!entry->defined_return_field_bodies[i]) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            if (!cxpr_model_namespace_imported_ast(
                    entry->defined_return_field_bodies[i], namespace_name,
                    source_registry, err)) {
                return false;
            }
        }
    }
    program->registry->version++;
    return true;
}

bool cxpr_model_program_register_imports(cxpr_model_program* program,
                                         const cxpr_model* model,
                                         const cxpr_model_import* imports,
                                         size_t import_count,
                                         cxpr_error* err) {
    if (!program || import_count == 0u) return true;
    if (!imports) return false;
    if (!program->registry) {
        program->registry = cxpr_registry_new();
        if (!program->registry) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    program->children = (cxpr_model_child_program*)calloc(import_count, sizeof(*program->children));
    if (!program->children) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    program->child_count = import_count;
    for (size_t i = 0u; i < import_count; ++i) {
        const cxpr_model_program* child = imports[i].program;
        const char* namespace_name = cxpr_model_import_namespace_for(model, imports[i].name);
        cxpr_func_entry* entry;
        size_t exposed_input_count;
        size_t exposed_param_count;
        if (!imports[i].name || !child || child->output_count == 0u) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model import", 0, 0);
            return false;
        }
        exposed_input_count =
            cxpr_model_program_inputs_are_implicit_market(child) ? 0u : child->input_count;
        exposed_param_count = cxpr_model_program_exposed_param_count(child);
        for (size_t prev = 0u; prev < i; ++prev) {
            if (program->children[prev].name &&
                cxpr_model_names_match(program->children[prev].name, namespace_name)) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Duplicate import namespace", 0, 0);
                return false;
            }
        }
        program->children[i].name = cxpr_strdup(namespace_name);
        program->children[i].program = child;
        program->children[i].registry_index = i;
        program->children[i].source_input_index = (size_t)-1;
        if (!program->children[i].name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        if (child->source_arg) {
            program->children[i].source_arg = cxpr_strdup(child->source_arg);
            if (!program->children[i].source_arg) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            for (size_t in_i = 0u; in_i < child->input_count; ++in_i) {
                if (cxpr_model_names_match(child->inputs[in_i], child->source_arg)) {
                    program->children[i].source_input_index = in_i;
                    break;
                }
            }
        }
        entry = cxpr_registry_find(program->registry, namespace_name);
        if (entry) {
            cxpr_registry_clear_owned_entry(entry);
        } else {
            if (program->registry->count >= program->registry->capacity &&
                !cxpr_registry_grow(program->registry)) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            entry = &program->registry->entries[program->registry->count++];
            cxpr_registry_prepare_entry(entry, namespace_name);
            if (!entry->name) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
        }
        entry->model_producer = cxpr_model_eval_child_producer;
        entry->model_producer_userdata = &program->children[i];
        entry->min_args = 0u;
        entry->max_args = exposed_input_count + exposed_param_count;
        entry->return_type = CXPR_VALUE_STRUCT;
        entry->has_return_type = true;
        entry->defined_return_field_names = cxpr_registry_clone_param_names(
            (const char* const*)child->outputs, child->output_count);
        entry->defined_param_count = entry->max_args;
        if (entry->defined_param_count > 0u) {
            size_t name_index = 0u;
            entry->defined_param_names = (char**)calloc(entry->defined_param_count, sizeof(char*));
            if (!entry->defined_param_names) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            for (size_t in_i = 0u; in_i < exposed_input_count; ++in_i) {
                entry->defined_param_names[name_index++] = cxpr_strdup(child->inputs[in_i]);
                if (!entry->defined_param_names[name_index - 1u]) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
            }
            for (size_t p = 0u; p < child->constant_count; ++p) {
                if (!cxpr_model_program_constant_is_exposed(child, p)) continue;
                entry->defined_param_names[name_index++] = cxpr_strdup(child->constants[p].name);
                if (!entry->defined_param_names[name_index - 1u]) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
            }
        }
        entry->defined_return_field_count = child->output_count;
        if (!entry->defined_return_field_names && child->output_count > 0u) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        program->registry->version++;
        if (child->registry) {
            for (size_t f = 0u; f < child->registry->count; ++f) {
                if (!cxpr_model_register_imported_defined_function(
                        program, namespace_name, child->registry,
                        &child->registry->entries[f], err)) {
                    return false;
                }
            }
        }
    }
    return true;
}

cxpr_model_program* cxpr_compile_model_with_imports(const cxpr_model* model,
                                                    const cxpr_registry* reg,
                                                    const cxpr_model_import* imports,
                                                    size_t import_count,
                                                    cxpr_error* err) {
    return cxpr_compile_model_with_imports_and_options(
        model, reg, imports, import_count, NULL, err);
}

cxpr_model_program* cxpr_compile_model_with_imports_and_options(
    const cxpr_model* model,
    const cxpr_registry* reg,
    const cxpr_model_import* imports,
    size_t import_count,
    const cxpr_model_compile_options* options,
    cxpr_error* err) {
    cxpr_model_program* program;
    cxpr_model inferred_model = {0};
    cxpr_model_compile_options compile_options;
    const cxpr_registry* compile_reg = reg;
    char** inferred_inputs = NULL;
    size_t inferred_input_count = 0u;
    char** required_defaults = NULL;
    size_t required_default_count = 0u;
    size_t* order = NULL;
    bool expanded_anonymous_outputs = false;

    if (err) *err = (cxpr_error){0};
    if (!cxpr_model_compile_options_resolve(options, &compile_options, err)) {
        return NULL;
    }
    if (model && model->anonymous_output_count > 0u) {
        if (!cxpr_model_copy_bindings_and_outputs(&inferred_model, model, err)) {
            cxpr_model_expanded_copy_free(&inferred_model);
            return NULL;
        }
        model = &inferred_model;
        if (!cxpr_model_expand_anonymous_outputs(&inferred_model, imports, import_count, err)) {
            cxpr_model_expanded_copy_free(&inferred_model);
            return NULL;
        }
        expanded_anonymous_outputs = true;
    }
    if (!cxpr_model_infer_inputs_for_compile(
            model, imports, import_count, &inferred_inputs, &inferred_input_count, err)) {
        if (expanded_anonymous_outputs) {
            cxpr_model_expanded_copy_free(&inferred_model);
        }
        return NULL;
    }
    if (inferred_input_count > 0u) {
        inferred_model = *model;
        inferred_model.inputs = inferred_inputs;
        inferred_model.input_count = inferred_input_count;
        model = &inferred_model;
    }
    if (!cxpr_model_validate(model, err)) {
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        if (expanded_anonymous_outputs) cxpr_model_expanded_copy_free(&inferred_model);
        return NULL;
    }
    if (!cxpr_model_collect_required_defaults(model, &required_defaults,
                                              &required_default_count, err)) {
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        if (expanded_anonymous_outputs) cxpr_model_expanded_copy_free(&inferred_model);
        return NULL;
    }
    program = (cxpr_model_program*)calloc(1, sizeof(cxpr_model_program));
    if (!program) {
        for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
        free(required_defaults);
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    program->requested_backend = compile_options.backend;
    program->selected_backend = CXPR_MODEL_BACKEND_AUTO;
    program->compile_fuse = compile_options.fuse;
    program->compile_trace = compile_options.enable_trace;
    program->lifetime = CXPR_MODEL_LIFETIME_SINGLETON;
    {
        bool saw_type = false;
        if (!cxpr_model_parse_lifetime_metadata(model, &program->lifetime, &saw_type, err)) {
            for (size_t d = 0; d < required_default_count; ++d) free(required_defaults[d]);
            free(required_defaults);
            for (size_t d = 0u; d < inferred_input_count; ++d) free(inferred_inputs[d]);
            free(inferred_inputs);
            cxpr_model_program_free(program);
            return NULL;
        }
        (void)saw_type;
    }
    program->source_arg = cxpr_model_parse_source_arg_metadata(model);
    {
        const char* guard = cxpr_model_model_field_value(model, "invalid_input_guard");
        if (guard) {
            program->invalid_input_guard =
                cxpr_model_dup_trimmed_metadata_value(guard);
            if (!program->invalid_input_guard) {
                for (size_t d = 0; d < required_default_count; ++d) {
                    free(required_defaults[d]);
                }
                free(required_defaults);
                for (size_t d = 0u; d < inferred_input_count; ++d) {
                    free(inferred_inputs[d]);
                }
                free(inferred_inputs);
                cxpr_model_program_free(program);
                cxpr_model_set_error(
                    err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (model && cxpr_model_metadata_count(model) > 0u && !program->source_arg) {
        for (size_t i = 0u; i < model->metadata_count; ++i) {
            if (model->metadatas[i].target_kind == CXPR_MODEL_METADATA_TARGET_MODEL &&
                cxpr_model_metadata_field_value(model, i, "source_arg")) {
                for (size_t d = 0; d < required_default_count; ++d) free(required_defaults[d]);
                free(required_defaults);
                for (size_t d = 0u; d < inferred_input_count; ++d) free(inferred_inputs[d]);
                free(inferred_inputs);
                cxpr_model_program_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (!cxpr_model_collect_lookbacks(model,
                                      &program->history_specs,
                                      &program->history_spec_count,
                                      err)) {
        for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
        free(required_defaults);
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        cxpr_model_program_free(program);
        return NULL;
    }
    if (model->function_count > 0 || model->record_function_count > 0u ||
        import_count > 0u ||
        (!reg && required_default_count > 0u) ||
        program->history_spec_count > 0u) {
        if (reg && program->history_spec_count > 0u) {
            for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
            free(required_defaults);
            for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
            free(inferred_inputs);
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "model lookback with external registry is not supported yet",
                                 0, 0);
            return NULL;
        }
        program->registry = cxpr_registry_new();
        if (!program->registry) {
            for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
            free(required_defaults);
            for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
            free(inferred_inputs);
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (program->history_spec_count > 0u) {
            cxpr_registry_set_lookback_resolver(
                program->registry, cxpr_model_lookback_resolver, NULL, NULL);
        }
        if (!cxpr_model_program_register_imports(program, model, imports, import_count, err)) {
            for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
            free(required_defaults);
            for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
            free(inferred_inputs);
            cxpr_model_program_free(program);
            return NULL;
        }
        for (size_t i = 0; i < required_default_count; ++i) {
            if (!cxpr_register_default_named(program->registry, required_defaults[i])) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_FUNCTION;
                    err->message = "Unknown function";
                }
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_program_free(program);
                return NULL;
            }
        }
        for (size_t i = 0; i < model->function_count; ++i) {
            cxpr_error fn_err = cxpr_registry_define_fn(program->registry, model->functions[i]);
            if (fn_err.code != CXPR_OK) {
                if (err) *err = fn_err;
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_program_free(program);
                return NULL;
            }
        }
        for (size_t i = 0; i < model->record_function_count; ++i) {
            const char** field_names;
            const cxpr_ast** field_bodies;
            cxpr_error fn_err;
            field_names = (const char**)calloc(model->record_functions[i].field_count,
                                               sizeof(char*));
            field_bodies = (const cxpr_ast**)calloc(model->record_functions[i].field_count,
                                                    sizeof(cxpr_ast*));
            if (!field_names || !field_bodies) {
                free(field_names);
                free(field_bodies);
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_program_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
                field_names[f] = model->record_functions[i].fields[f].name;
                field_bodies[f] = model->record_functions[i].fields[f].expr;
            }
            fn_err = cxpr_registry_define_record_fn(
                program->registry,
                model->record_functions[i].name,
                (const char* const*)model->record_functions[i].params,
                model->record_functions[i].param_count,
                (const char* const*)field_names,
                (const cxpr_ast* const*)field_bodies,
                model->record_functions[i].field_count);
            free(field_names);
            free(field_bodies);
            if (fn_err.code != CXPR_OK) {
                if (err) *err = fn_err;
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_program_free(program);
                return NULL;
            }
        }
        compile_reg = program->registry;
    }

    for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
    free(required_defaults);

    if (model->constant_count > 0) {
        program->constants =
            (cxpr_model_compiled_binding*)calloc(model->constant_count,
                                                 sizeof(cxpr_model_compiled_binding));
        if (!program->constants) {
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->constant_count = model->constant_count;
        for (size_t i = 0; i < model->constant_count; ++i) {
            program->constants[i].name = cxpr_strdup(model->constants[i].name);
            program->constants[i].source = cxpr_strdup(model->constants[i].source);
            program->constants[i].name_hash = cxpr_hash_string(model->constants[i].name);
            program->constants[i].ast = cxpr_ast_clone(model->constants[i].expr);
            program->constants[i].result_kind =
                cxpr_model_infer_result_kind(program->constants[i].ast, compile_reg);
            program->constants[i].is_call_param = model->constants[i].is_call_param;
            for (size_t m = 0u; m < cxpr_model_metadata_count(model); ++m) {
                const char* target;
                if (cxpr_model_metadata_target_kind_at(model, m) !=
                    CXPR_MODEL_METADATA_TARGET_PARAM) continue;
                target = cxpr_model_metadata_target_name(model, m);
                if (!target || !cxpr_model_names_match(target, model->constants[i].name)) continue;
                program->constants[i].has_min_value =
                    cxpr_model_metadata_field_number(
                        model, m, "min", &program->constants[i].min_value);
                program->constants[i].has_max_value =
                    cxpr_model_metadata_field_number(
                        model, m, "max", &program->constants[i].max_value);
                break;
            }
            if (!program->constants[i].name ||
                !program->constants[i].source ||
                !program->constants[i].ast) {
                cxpr_model_program_free(program);
                if (err && err->code == CXPR_OK) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                }
                return NULL;
            }
        }
    }

    {
        size_t state_count = 0u;
        size_t executable_count = 0u;
        for (size_t i = 0; i < model->binding_count; ++i) {
            if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE) state_count++;
            else executable_count++;
        }

        if (state_count > 0u) {
            program->state_defaults =
                (cxpr_model_compiled_binding*)calloc(state_count,
                                                     sizeof(cxpr_model_compiled_binding));
            if (!program->state_defaults) {
                cxpr_model_program_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            program->state_default_count = state_count;
            for (size_t i = 0, out_i = 0; i < model->binding_count; ++i) {
                if (model->bindings[i].kind != CXPR_MODEL_BINDING_STATE) continue;
                program->state_defaults[out_i].kind = model->bindings[i].kind;
                program->state_defaults[out_i].name = cxpr_strdup(model->bindings[i].name);
                program->state_defaults[out_i].source = cxpr_strdup(model->bindings[i].source);
                program->state_defaults[out_i].name_hash = cxpr_hash_string(model->bindings[i].name);
                program->state_defaults[out_i].ast = cxpr_ast_clone(model->bindings[i].expr);
                program->state_defaults[out_i].result_kind =
                    cxpr_model_infer_result_kind(program->state_defaults[out_i].ast, compile_reg);
                if (!program->state_defaults[out_i].name ||
                    !program->state_defaults[out_i].source ||
                    !program->state_defaults[out_i].ast) {
                    cxpr_model_program_free(program);
                    if (err && err->code == CXPR_OK) {
                        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    }
                    return NULL;
                }
                out_i++;
            }
        }

        if (executable_count == 0u) goto compile_outputs;

        order = (size_t*)calloc(executable_count, sizeof(size_t));
        program->bindings =
            (cxpr_model_compiled_binding*)calloc(executable_count,
                                                 sizeof(cxpr_model_compiled_binding));
        if (!order || !program->bindings) {
            free(order);
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (!cxpr_model_executable_eval_order(model, order, executable_count, err)) {
            free(order);
            cxpr_model_program_free(program);
            return NULL;
        }
        program->binding_count = executable_count;
        for (size_t out_i = 0; out_i < executable_count; ++out_i) {
            size_t src_i = order[out_i];
            program->bindings[out_i].kind = model->bindings[src_i].kind;
            program->bindings[out_i].name = cxpr_strdup(model->bindings[src_i].name);
            program->bindings[out_i].source = cxpr_strdup(model->bindings[src_i].source);
            program->bindings[out_i].name_hash = cxpr_hash_string(model->bindings[src_i].name);
            program->bindings[out_i].ast = cxpr_ast_clone(model->bindings[src_i].expr);
            program->bindings[out_i].result_kind =
                cxpr_model_infer_result_kind(program->bindings[out_i].ast, compile_reg);
            if (program->bindings[out_i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
                program->bindings[out_i].result_kind = cxpr_model_state_default_result_kind(
                    program, program->bindings[out_i].name);
            }
            if (!program->bindings[out_i].name ||
                !program->bindings[out_i].source ||
                !program->bindings[out_i].ast) {
                free(order);
                cxpr_model_program_free(program);
                if (err && err->code == CXPR_OK) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                }
                return NULL;
            }
        }
        free(order);
    }

compile_outputs:
    if (model->input_count > 0) {
        program->inputs = (char**)calloc(model->input_count, sizeof(char*));
        if (!program->inputs) {
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->input_count = model->input_count;
        for (size_t i = 0; i < model->input_count; ++i) {
            program->inputs[i] = cxpr_strdup(model->inputs[i]);
            if (!program->inputs[i]) {
                cxpr_model_program_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (model->output_count > 0) {
        program->outputs = (char**)calloc(model->output_count, sizeof(char*));
        if (!program->outputs) {
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->output_count = model->output_count;
        for (size_t i = 0; i < model->output_count; ++i) {
            program->outputs[i] = cxpr_strdup(model->outputs[i]);
            if (!program->outputs[i]) {
                cxpr_model_program_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }

    if (!cxpr_model_program_select_backend(program, model, compile_reg, &compile_options, err)) {
        cxpr_model_program_free(program);
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        return NULL;
    }

    for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
    free(inferred_inputs);
    if (expanded_anonymous_outputs) {
        cxpr_model_expanded_copy_free(&inferred_model);
    }
    if (err) err->code = CXPR_OK;
    return program;
}
