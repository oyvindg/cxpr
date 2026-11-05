/**
 * @file model/compile/infer.c
 * @brief Infer model inputs, result kinds, and anonymous outputs.
 */

#include "core.h"
#include "ast/internal.h"
#include "ir/compile/internal.h"
#include "model/internal.h"
#include "model/compile/internal.h"
#include <cxpr/source.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_input_name_exists(char* const* inputs, size_t count, const char* name) {
    if (!name) return false;
    for (size_t i = 0u; i < count; ++i) {
        if (cxpr_model_names_match(inputs[i], name)) return true;
    }
    return false;
}

static cxpr_model_result_kind cxpr_model_symbol_result_kind(
    const cxpr_model* model,
    const cxpr_model_compiled* program,
    const char* name,
    bool parameter) {
    if (!name) return CXPR_MODEL_RESULT_UNKNOWN;
    if (parameter && program) {
        for (size_t i = 0u; i < program->constant_count; ++i) {
            if (program->constants[i].name &&
                cxpr_model_names_match(program->constants[i].name, name))
                return program->constants[i].result_kind;
        }
        return CXPR_MODEL_RESULT_UNKNOWN;
    }
    if (model) {
        for (size_t i = 0u; i < model->input_count; ++i) {
            if (model->inputs[i] && cxpr_model_names_match(model->inputs[i], name)) {
                const cxpr_model_decl_type type = model->input_declared_types
                    ? model->input_declared_types[i] : CXPR_MODEL_DECL_INFERRED;
                if (type == CXPR_MODEL_DECL_INT) return CXPR_MODEL_RESULT_INT64;
                if (type == CXPR_MODEL_DECL_NUMBER) return CXPR_MODEL_RESULT_NUMBER;
                if (type == CXPR_MODEL_DECL_BOOL) return CXPR_MODEL_RESULT_BOOL;
                return CXPR_MODEL_RESULT_UNKNOWN;
            }
        }
    }
    if (program) {
        const cxpr_model_compiled_binding* groups[] = {
            program->bindings, program->state_defaults
        };
        const size_t counts[] = {
            program->binding_count, program->state_default_count
        };
        for (size_t group = 0u; group < 2u; ++group) {
            for (size_t i = 0u; i < counts[group]; ++i) {
                if (groups[group][i].name &&
                    cxpr_model_names_match(groups[group][i].name, name))
                    return groups[group][i].result_kind;
            }
        }
    }
    return CXPR_MODEL_RESULT_UNKNOWN;
}

static cxpr_model_result_kind cxpr_model_infer_result_kind_depth(
    const cxpr_expr_ast* ast,
    const cxpr_registry* reg,
    const cxpr_model* model,
    const cxpr_model_compiled* program,
    size_t depth) {
    cxpr_model_result_kind left;
    cxpr_model_result_kind right;

    if (!ast || depth > CXPR_IR_INFER_DEPTH_LIMIT) {
        return CXPR_MODEL_RESULT_UNKNOWN;
    }
    if (ast->type == CXPR_NODE_INT64) return CXPR_MODEL_RESULT_INT64;
    if (ast->type == CXPR_NODE_IDENTIFIER)
        return cxpr_model_symbol_result_kind(
            model, program, ast->data.identifier.name, false);
    if (ast->type == CXPR_NODE_VARIABLE)
        return cxpr_model_symbol_result_kind(
            model, program, ast->data.variable.name, true);
    if (ast->type == CXPR_NODE_FUNCTION_CALL &&
        ast->data.function_call.name &&
        cxpr_model_names_match(ast->data.function_call.name, "resample") &&
        ast->data.function_call.argc > 0u) {
        /* Resampling selects a materialized series; it does not itself perform
           a numeric aggregate, so the source element type is preserved. */
        return cxpr_model_infer_result_kind_depth(
            ast->data.function_call.args[0], reg, model, program, depth + 1u);
    }
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
            ast->data.index.target, reg, model, program, depth + 1u);
    }
    if (ast->type == CXPR_NODE_BINARY_OP) {
        left = cxpr_model_infer_result_kind_depth(
            ast->data.binary_op.left, reg, model, program, depth + 1u);
        right = cxpr_model_infer_result_kind_depth(
            ast->data.binary_op.right, reg, model, program, depth + 1u);
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_LT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GT:
        case CXPR_TOK_GTE:
            return CXPR_MODEL_RESULT_BOOL;
        case CXPR_TOK_AND:
        case CXPR_TOK_OR:
            return CXPR_MODEL_RESULT_BOOL;
        case CXPR_TOK_SLASH:
            return CXPR_MODEL_RESULT_NUMBER;
        case CXPR_TOK_PLUS:
        case CXPR_TOK_MINUS:
        case CXPR_TOK_STAR:
            if (left == CXPR_MODEL_RESULT_INT64 && right == CXPR_MODEL_RESULT_INT64)
                return CXPR_MODEL_RESULT_INT64;
            break;
        default:
            break;
        }
    }
    switch (cxpr_ir_infer_fast_result_kind(ast, reg, depth)) {
    case CXPR_IR_RESULT_DOUBLE: return CXPR_MODEL_RESULT_NUMBER;
    case CXPR_IR_RESULT_BOOL: return CXPR_MODEL_RESULT_BOOL;
    case CXPR_IR_RESULT_INT64: return CXPR_MODEL_RESULT_INT64;
    default: return CXPR_MODEL_RESULT_UNKNOWN;
    }
}

cxpr_model_result_kind cxpr_model_infer_result_kind(
    const cxpr_expr_ast* ast,
    const cxpr_registry* reg,
    const cxpr_model* model,
    const cxpr_model_compiled* program) {
    return cxpr_model_infer_result_kind_depth(ast, reg, model, program, 0u);
}

static const cxpr_expr_ast* cxpr_model_static_value_binding(
    const cxpr_model_compiled* program,
    const char* name) {
    const cxpr_model_compiled_binding* groups[2];
    size_t counts[2];
    if (!program || !name) return NULL;
    groups[0] = program->constants;
    groups[1] = program->bindings;
    counts[0] = program->constant_count;
    counts[1] = program->binding_count;
    for (size_t group = 0u; group < 2u; ++group) {
        for (size_t i = 0u; i < counts[group]; ++i) {
            if (groups[group][i].name && groups[group][i].ast &&
                cxpr_model_names_match(groups[group][i].name, name))
                return groups[group][i].ast;
        }
    }
    return NULL;
}

static cxpr_expr_ast* cxpr_model_project_static_shape(
    const cxpr_expr_ast* shape,
    const cxpr_expr_ast* access,
    cxpr_error* err) {
    if (!shape || !access) return NULL;
    if (shape->type == CXPR_NODE_RECORD) {
        size_t count = shape->data.record.field_count;
        const char** names = (const char**)calloc(count ? count : 1u, sizeof(*names));
        cxpr_expr_ast** values = (cxpr_expr_ast**)calloc(count ? count : 1u, sizeof(*values));
        cxpr_expr_ast* result;
        if (!names || !values) goto oom;
        for (size_t i = 0u; i < count; ++i) {
            cxpr_expr_ast* field_access = NULL;
            names[i] = shape->data.record.field_names[i];
            field_access = cxpr_expr_ast_field_expr_new(
                cxpr_expr_ast_clone(access), names[i]);
            if (!field_access) {
                for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(values[j]);
                free(values);
                free(names);
                goto oom;
            }
            values[i] = cxpr_model_project_static_shape(
                shape->data.record.field_values[i], field_access, err);
            cxpr_expr_ast_free(field_access);
            if (!values[i]) {
                for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(values[j]);
                free(values);
                free(names);
                return NULL;
            }
        }
        result = cxpr_expr_ast_record_new(names, values, count);
        free(names);
        if (!result) {
            for (size_t i = 0u; i < count; ++i) cxpr_expr_ast_free(values[i]);
            free(values);
            goto oom;
        }
        return result;
    }
    return cxpr_expr_ast_clone(access);

oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
    return NULL;
}

static cxpr_expr_ast* cxpr_model_imported_static_shape(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* access,
    cxpr_error* err) {
    const cxpr_expr_ast* base;
    const cxpr_expr_ast* call = NULL;
    const cxpr_expr_ast* shape;
    const char* object;
    const char* field;
    cxpr_func_entry* entry;
    const cxpr_model_child_program* child;
    if (!program || !access) return NULL;
    if (access->type == CXPR_NODE_CHAIN_ACCESS) {
        if (access->data.chain_access.depth != 2u) return NULL;
        object = access->data.chain_access.path[0];
        field = access->data.chain_access.path[1];
        call = cxpr_model_static_value_binding(program, object);
    } else if (access->type == CXPR_NODE_FIELD_ACCESS) {
        base = access->data.field_access.base;
        object = access->data.field_access.object;
        field = access->data.field_access.field;
        if (base && base->type == CXPR_NODE_IDENTIFIER)
            call = cxpr_model_static_value_binding(program, base->data.identifier.name);
        else if (!base && object)
            call = cxpr_model_static_value_binding(program, object);
    } else {
        return NULL;
    }
    if (!call || call->type != CXPR_NODE_FUNCTION_CALL || !field) return NULL;
    entry = program->registry
        ? cxpr_registry_find(program->registry, call->data.function_call.name) : NULL;
    child = entry && entry->model_producer
        ? (const cxpr_model_child_program*)entry->model_producer_userdata : NULL;
    if (!child || !child->program) return NULL;
    shape = cxpr_model_static_value_binding(child->program, field);
    if (!shape || shape->type != CXPR_NODE_RECORD) return NULL;
    return cxpr_model_project_static_shape(shape, access, err);
}

static bool cxpr_model_materialize_static_value(
    cxpr_expr_ast** value,
    const cxpr_model_compiled* program,
    size_t depth,
    cxpr_error* err) {
    const cxpr_expr_ast* replacement;
    cxpr_expr_ast* clone;
    if (!value || !*value || depth > CXPR_IR_INFER_DEPTH_LIMIT) return true;
    if ((*value)->type == CXPR_NODE_FIELD_ACCESS ||
        (*value)->type == CXPR_NODE_CHAIN_ACCESS) {
        clone = cxpr_model_imported_static_shape(program, *value, err);
        if (clone) {
            cxpr_expr_ast_free(*value);
            *value = clone;
        } else if (err && err->code != CXPR_OK) {
            return false;
        }
    }
    if ((*value)->type == CXPR_NODE_IDENTIFIER &&
        (replacement = cxpr_model_static_value_binding(
             program, (*value)->data.identifier.name)) != NULL) {
        clone = cxpr_expr_ast_clone(replacement);
        if (!clone) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                 "Out of memory", 0u, 0u);
            return false;
        }
        cxpr_expr_ast_free(*value);
        *value = clone;
    }
    if ((*value)->type == CXPR_NODE_ARRAY) {
        for (size_t i = 0u; i < (*value)->data.array.count; ++i) {
            if (!cxpr_model_materialize_static_value(
                    &(*value)->data.array.elements[i], program, depth + 1u, err))
                return false;
        }
    } else if ((*value)->type == CXPR_NODE_RECORD) {
        for (size_t i = 0u; i < (*value)->data.record.field_count; ++i) {
            if (!cxpr_model_materialize_static_value(
                    &(*value)->data.record.field_values[i], program, depth + 1u, err))
                return false;
        }
    }
    return true;
}

static bool cxpr_model_inline_static_collection_source(
    cxpr_expr_ast** source,
    const cxpr_model_compiled* program,
    cxpr_error* err) {
    if (!source || !*source) return true;
    if (!cxpr_model_materialize_static_value(source, program, 0u, err))
        return false;
    if ((*source)->type == CXPR_NODE_FUNCTION_CALL &&
        (*source)->data.function_call.name &&
        strcmp((*source)->data.function_call.name, "map") == 0 &&
        (*source)->data.function_call.argc == 2u) {
        return cxpr_model_inline_static_collection_source(
            &(*source)->data.function_call.args[0], program, err);
    }
    return true;
}

static bool cxpr_model_structural_sum_shapes_match(const cxpr_expr_ast* left,
                                                   const cxpr_expr_ast* right) {
    if (!left || !right || left->type != right->type) return false;
    if (left->type != CXPR_NODE_RECORD) return true;
    if (left->data.record.field_count != right->data.record.field_count) return false;
    for (size_t i = 0u; i < left->data.record.field_count; ++i) {
        if (strcmp(left->data.record.field_names[i],
                   right->data.record.field_names[i]) != 0 ||
            !cxpr_model_structural_sum_shapes_match(
                left->data.record.field_values[i],
                right->data.record.field_values[i])) return false;
    }
    return true;
}

static cxpr_expr_ast* cxpr_model_build_structural_sum_record(
    const cxpr_expr_ast* array,
    size_t depth,
    cxpr_error* err) {
    const cxpr_expr_ast* shape;
    const size_t count = array ? array->data.array.count : 0u;
    const char** names;
    cxpr_expr_ast** values;
    cxpr_expr_ast* result;

    if (!array || array->type != CXPR_NODE_ARRAY || count == 0u ||
        depth > CXPR_IR_INFER_DEPTH_LIMIT) {
        cxpr_model_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                             "Invalid structural sum shape", 0u, 0u);
        return NULL;
    }
    shape = array->data.array.elements[0];
    if (!shape || shape->type != CXPR_NODE_RECORD ||
        shape->data.record.field_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                             "Structural sum requires a non-empty record", 0u, 0u);
        return NULL;
    }
    for (size_t i = 1u; i < count; ++i) {
        if (!cxpr_model_structural_sum_shapes_match(shape, array->data.array.elements[i])) {
            cxpr_model_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                                 "sum requires identical record shapes and field order",
                                 0u, 0u);
            return NULL;
        }
    }
    names = (const char**)calloc(shape->data.record.field_count, sizeof(*names));
    values = (cxpr_expr_ast**)calloc(shape->data.record.field_count, sizeof(*values));
    if (!names || !values) goto oom;

    for (size_t field = 0u; field < shape->data.record.field_count; ++field) {
        cxpr_expr_ast** elements = (cxpr_expr_ast**)calloc(count, sizeof(*elements));
        cxpr_expr_ast* field_array;
        if (!elements) goto oom_fields;
        names[field] = shape->data.record.field_names[field];
        for (size_t i = 0u; i < count; ++i) {
            elements[i] = cxpr_expr_ast_clone(
                array->data.array.elements[i]->data.record.field_values[field]);
            if (!elements[i]) {
                for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(elements[j]);
                free(elements);
                goto oom_fields;
            }
        }
        field_array = cxpr_expr_ast_array_new(elements, count);
        if (!field_array) {
            for (size_t i = 0u; i < count; ++i) cxpr_expr_ast_free(elements[i]);
            free(elements);
            goto oom_fields;
        }
        if (shape->data.record.field_values[field]->type == CXPR_NODE_RECORD) {
            values[field] = cxpr_model_build_structural_sum_record(
                field_array, depth + 1u, err);
            cxpr_expr_ast_free(field_array);
            if (!values[field]) goto fail_fields;
        } else {
            cxpr_expr_ast** args = (cxpr_expr_ast**)calloc(1u, sizeof(*args));
            if (!args) { cxpr_expr_ast_free(field_array); goto oom_fields; }
            args[0] = field_array;
            values[field] = cxpr_expr_ast_call_new("sum", args, 1u);
            if (!values[field]) {
                cxpr_expr_ast_free(field_array);
                free(args);
                goto oom_fields;
            }
        }
    }
    result = cxpr_expr_ast_record_new(
        names, values, shape->data.record.field_count);
    free(names);
    if (!result) goto oom_values;
    return result;

oom_fields:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
fail_fields:
    for (size_t i = 0u; i < shape->data.record.field_count; ++i)
        cxpr_expr_ast_free(values[i]);
    free(values);
    free(names);
    return NULL;
oom_values:
    for (size_t i = 0u; i < shape->data.record.field_count; ++i)
        cxpr_expr_ast_free(values[i]);
    free(values);
    return NULL;
oom:
    free(names);
    free(values);
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
    return NULL;
}

/**
 * Materialize named static collection bindings at native collection sites.
 *
 * Model bindings are scalar execution slots, so a compile-time array cannot
 * be carried through one as a runtime value. Keeping the expansion here also
 * gives map/reduce their concrete element records while they are typechecked
 * and lowered.
 */
cxpr_expr_ast* cxpr_model_inline_static_collections(
    cxpr_expr_ast* ast,
    const cxpr_model_compiled* program,
    cxpr_error* err) {
    if (!ast) return NULL;
    if (ast->type == CXPR_NODE_FUNCTION_CALL) {
        const char* name = ast->data.function_call.name;
        if (name && (strcmp(name, "map") == 0 || strcmp(name, "reduce") == 0) &&
            ast->data.function_call.argc > 0u &&
            !cxpr_model_inline_static_collection_source(
                &ast->data.function_call.args[0], program, err)) {
            cxpr_expr_ast_free(ast);
            return NULL;
        }
        if (name && (!strcmp(name, "sum") || !strcmp(name, "mean") ||
                     !strcmp(name, "min") || !strcmp(name, "max") ||
                     !strcmp(name, "any") || !strcmp(name, "all")) &&
            ast->data.function_call.argc > 0u &&
            !cxpr_model_inline_static_collection_source(
                &ast->data.function_call.args[0], program, err)) {
            cxpr_expr_ast_free(ast);
            return NULL;
        }
        if (name && (!strcmp(name, "sum") || !strcmp(name, "mean") ||
                     !strcmp(name, "min") || !strcmp(name, "max") ||
                     !strcmp(name, "any") || !strcmp(name, "all")) &&
            ast->data.function_call.argc > 0u &&
            ast->data.function_call.args[0]->type == CXPR_NODE_RECORD) {
            const cxpr_expr_ast* record = ast->data.function_call.args[0];
            size_t count = record->data.record.field_count;
            cxpr_expr_ast** elements = (cxpr_expr_ast**)calloc(
                count ? count : 1u, sizeof(*elements));
            cxpr_expr_ast* array;
            if (!elements) {
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                     "Out of memory", 0u, 0u);
                return NULL;
            }
            for (size_t i = 0u; i < count; ++i) {
                elements[i] = cxpr_expr_ast_clone(record->data.record.field_values[i]);
                if (!elements[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(elements[j]);
                    free(elements);
                    cxpr_expr_ast_free(ast);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                         "Out of memory", 0u, 0u);
                    return NULL;
                }
            }
            array = cxpr_expr_ast_array_new(elements, count);
            if (!array) {
                for (size_t i = 0u; i < count; ++i) cxpr_expr_ast_free(elements[i]);
                free(elements);
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                     "Out of memory", 0u, 0u);
                return NULL;
            }
            cxpr_expr_ast_free(ast->data.function_call.args[0]);
            ast->data.function_call.args[0] = array;
        }
        if (name && (!strcmp(name, "sum") || !strcmp(name, "mean") ||
                     !strcmp(name, "min") || !strcmp(name, "max") ||
                     !strcmp(name, "any") || !strcmp(name, "all")) &&
            ast->data.function_call.argc == 2u &&
            ast->data.function_call.args[0]->type == CXPR_NODE_ARRAY) {
            const cxpr_expr_ast* lambda = ast->data.function_call.args[1];
            const cxpr_expr_ast* parameter;
            const cxpr_expr_ast* body;
            const size_t count = ast->data.function_call.args[0]->data.array.count;
            cxpr_expr_ast** elements;
            cxpr_expr_ast** args;
            cxpr_expr_ast* lowered;
            if (!lambda || lambda->type != CXPR_NODE_FUNCTION_CALL ||
                strcmp(lambda->data.function_call.name, "__cxpr_lambda") != 0 ||
                lambda->data.function_call.argc != 2u ||
                lambda->data.function_call.args[0]->type != CXPR_NODE_IDENTIFIER) {
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                                     "Static reduction requires a one-parameter selector",
                                     0u, 0u);
                return NULL;
            }
            parameter = lambda->data.function_call.args[0];
            body = lambda->data.function_call.args[1];
            elements = (cxpr_expr_ast**)calloc(
                count ? count : 1u, sizeof(*elements));
            args = (cxpr_expr_ast**)calloc(1u, sizeof(*args));
            if (!elements || !args) {
                free(elements);
                free(args);
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                     "Out of memory", 0u, 0u);
                return NULL;
            }
            for (size_t i = 0u; i < count; ++i) {
                cxpr_model_local_binding local = {
                    .name = parameter->data.identifier.name,
                    .expr = ast->data.function_call.args[0]->data.array.elements[i],
                };
                elements[i] = cxpr_model_inline_locals(body, &local, 1u);
                if (!elements[i]) {
                    for (size_t j = 0u; j < i; ++j)
                        cxpr_expr_ast_free(elements[j]);
                    free(elements);
                    free(args);
                    cxpr_expr_ast_free(ast);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                         "Out of memory", 0u, 0u);
                    return NULL;
                }
            }
            args[0] = cxpr_expr_ast_array_new(elements, count);
            if (!args[0]) {
                for (size_t i = 0u; i < count; ++i)
                    cxpr_expr_ast_free(elements[i]);
                free(elements);
                free(args);
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                     "Out of memory", 0u, 0u);
                return NULL;
            }
            lowered = cxpr_expr_ast_call_new(name, args, 1u);
            if (!lowered) {
                cxpr_expr_ast_free(args[0]);
                free(args);
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                     "Out of memory", 0u, 0u);
                return NULL;
            }
            cxpr_expr_ast_free(ast);
            return cxpr_model_inline_static_collections(lowered, program, err);
        }
        if (name && strcmp(name, "map") == 0 &&
            ast->data.function_call.argc == 2u &&
            (ast->data.function_call.args[0]->type == CXPR_NODE_ARRAY ||
             ast->data.function_call.args[0]->type == CXPR_NODE_RECORD)) {
            cxpr_expr_ast* expanded = cxpr_model_inline_defined_calls(
                ast, program ? program->registry : NULL, err);
            if (!expanded) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
            cxpr_expr_ast_free(ast);
            ast = expanded;
            if (ast->type != CXPR_NODE_FUNCTION_CALL) {
                return cxpr_model_inline_static_collections(ast, program, err);
            }
            name = ast->data.function_call.name;
        }
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i) {
            ast->data.function_call.args[i] = cxpr_model_inline_static_collections(
                ast->data.function_call.args[i], program, err);
            if (!ast->data.function_call.args[i]) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
        }
        if (name && !strcmp(name, "sum") &&
            ast->data.function_call.argc == 1u &&
            ast->data.function_call.args[0]->type == CXPR_NODE_ARRAY &&
            ast->data.function_call.args[0]->data.array.count > 0u &&
            ast->data.function_call.args[0]->data.array.elements[0]->type ==
                CXPR_NODE_RECORD) {
            cxpr_expr_ast* lowered = cxpr_model_build_structural_sum_record(
                ast->data.function_call.args[0], 0u, err);
            if (!lowered) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
            cxpr_expr_ast_free(ast);
            return cxpr_model_inline_static_collections(lowered, program, err);
        }
    } else if (ast->type == CXPR_NODE_BINARY_OP) {
        ast->data.binary_op.left = cxpr_model_inline_static_collections(
            ast->data.binary_op.left, program, err);
        ast->data.binary_op.right = cxpr_model_inline_static_collections(
            ast->data.binary_op.right, program, err);
        if (!ast->data.binary_op.left || !ast->data.binary_op.right) {
            cxpr_expr_ast_free(ast);
            return NULL;
        }
    } else if (ast->type == CXPR_NODE_UNARY_OP) {
        ast->data.unary_op.operand = cxpr_model_inline_static_collections(
            ast->data.unary_op.operand, program, err);
        if (!ast->data.unary_op.operand) {
            cxpr_expr_ast_free(ast);
            return NULL;
        }
    } else if (ast->type == CXPR_NODE_ARRAY) {
        for (size_t i = 0u; i < ast->data.array.count; ++i) {
            ast->data.array.elements[i] = cxpr_model_inline_static_collections(
                ast->data.array.elements[i], program, err);
            if (!ast->data.array.elements[i]) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
        }
    } else if (ast->type == CXPR_NODE_RECORD) {
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            ast->data.record.field_values[i] = cxpr_model_inline_static_collections(
                ast->data.record.field_values[i], program, err);
            if (!ast->data.record.field_values[i]) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
        }
    } else if (ast->type == CXPR_NODE_INDEX) {
        ast->data.index.target = cxpr_model_inline_static_collections(
            ast->data.index.target, program, err);
        ast->data.index.index = cxpr_model_inline_static_collections(
            ast->data.index.index, program, err);
        if (!ast->data.index.target || !ast->data.index.index) {
            cxpr_expr_ast_free(ast);
            return NULL;
        }
    } else if (ast->type == CXPR_NODE_TERNARY) {
        ast->data.ternary.condition = cxpr_model_inline_static_collections(
            ast->data.ternary.condition, program, err);
        ast->data.ternary.true_branch = cxpr_model_inline_static_collections(
            ast->data.ternary.true_branch, program, err);
        ast->data.ternary.false_branch = cxpr_model_inline_static_collections(
            ast->data.ternary.false_branch, program, err);
        if (!ast->data.ternary.condition || !ast->data.ternary.true_branch ||
            !ast->data.ternary.false_branch) {
            cxpr_expr_ast_free(ast);
            return NULL;
        }
    }
    return ast;
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
    model->bindings[model->binding_count].prelowered_expr = NULL;
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
            dst->bindings[i].prelowered_expr = src->bindings[i].prelowered_expr
                ? cxpr_expr_ast_clone(src->bindings[i].prelowered_expr) : NULL;
            dst->bindings[i].span = src->bindings[i].span;
            dst->bindings[i].has_span = src->bindings[i].has_span;
            if (!dst->bindings[i].name ||
                (src->bindings[i].source && !dst->bindings[i].source) ||
                (src->bindings[i].expr && !dst->bindings[i].expr) ||
                (src->bindings[i].prelowered_expr &&
                 !dst->bindings[i].prelowered_expr)) {
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
        cxpr_expr_ast_free(model->bindings[i].prelowered_expr);
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
    if (out && call->data.function_call.instance_id &&
        !cxpr_expr_ast_call_set_instance_id(out, call->data.function_call.instance_id)) {
        cxpr_expr_ast_free(out);
        out = NULL;
    }
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
    const char* dot;
    if (!import_name) return NULL;
    dot = strrchr(import_name, '.');
    return dot && dot[1] != '\0' ? dot + 1 : import_name;
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

static bool cxpr_model_child_accepts_named_arg(const cxpr_model_compiled* child,
                                               const char* name) {
    size_t explicit_param_count = 0u;

    if (!child || !name) return false;
    if (child->pipe_target && cxpr_model_names_match(child->pipe_target, name)) return true;
    for (size_t i = 0u; i < child->input_count; ++i) {
        if (cxpr_model_names_match(child->inputs[i], name)) return true;
    }
    for (size_t i = 0u; i < child->constant_count; ++i) {
        if (child->constants[i].is_call_param) ++explicit_param_count;
    }
    for (size_t i = 0u; i < child->constant_count; ++i) {
        if ((explicit_param_count == 0u || child->constants[i].is_call_param) &&
            cxpr_model_names_match(child->constants[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool cxpr_model_child_input_is_rooted_source(
    const cxpr_model_compiled* child,
    const char* input) {
    size_t root_len;
    if (!child || !child->pipe_target || !input) return false;
    for (size_t i = 0u; i < child->input_count; ++i)
        if (cxpr_model_names_match(child->inputs[i], child->pipe_target)) return false;
    root_len = strlen(child->pipe_target);
    return strncmp(input, child->pipe_target, root_len) == 0 &&
           input[root_len] == '.' && input[root_len + 1u] != '\0';
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
                for (size_t arg_i = 0u; arg_i < ast->data.producer_access.argc; ++arg_i) {
                    const char* arg_name = cxpr_expr_ast_producer_arg_name(ast, arg_i);
                    if (arg_name && !cxpr_model_child_accepts_named_arg(child, arg_name)) {
                        cxpr_model_set_error(
                            err, CXPR_ERR_SYNTAX,
                            "Unknown named argument for imported model", 0, 0);
                        return false;
                    }
                }
                if (child->pipe_target) {
                    if (ast->data.producer_access.argc == child->constant_count + 1u &&
                        !cxpr_expr_ast_producer_has_named_args(ast)) {
                        call_supplies_source = true;
                    }
                    if (ast->data.producer_access.argc > 0u &&
                        !cxpr_expr_ast_producer_arg_name(ast, 0u)) {
                        call_supplies_source = true;
                    }
                    for (size_t arg_i = 0u; arg_i < ast->data.producer_access.argc; ++arg_i) {
                        const char* arg_name = cxpr_expr_ast_producer_arg_name(ast, arg_i);
                        if (arg_name && cxpr_model_names_match(arg_name, child->pipe_target)) {
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
                        child->pipe_target &&
                        (cxpr_model_names_match(child->inputs[i], child->pipe_target) ||
                         cxpr_model_child_input_is_rooted_source(
                             child, child->inputs[i]))) {
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

static bool cxpr_model_expr_binds_reference(const cxpr_expr_ast* ast,
                                            const char* reference) {
    if (!ast || !reference) return false;
    if (ast->type == CXPR_NODE_FUNCTION_CALL) {
        const char* name = ast->data.function_call.name;
        if (name && (!strcmp(name, "__cxpr_lambda") ||
                     !strcmp(name, "__cxpr_let")) &&
            ast->data.function_call.argc >= 2u &&
            ast->data.function_call.args[0]->type == CXPR_NODE_IDENTIFIER &&
            cxpr_model_names_match(
                ast->data.function_call.args[0]->data.identifier.name,
                reference)) return true;
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i)
            if (cxpr_model_expr_binds_reference(
                    ast->data.function_call.args[i], reference)) return true;
        return false;
    }
    switch (ast->type) {
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i)
            if (cxpr_model_expr_binds_reference(
                    ast->data.array.elements[i], reference)) return true;
        return false;
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i)
            if (cxpr_model_expr_binds_reference(
                    ast->data.record.field_values[i], reference)) return true;
        return false;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_expr_binds_reference(ast->data.binary_op.left, reference) ||
               cxpr_model_expr_binds_reference(ast->data.binary_op.right, reference);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_expr_binds_reference(ast->data.unary_op.operand, reference);
    case CXPR_NODE_INDEX:
        return cxpr_model_expr_binds_reference(ast->data.index.target, reference) ||
               cxpr_model_expr_binds_reference(ast->data.index.index, reference);
    case CXPR_NODE_TERNARY:
        return cxpr_model_expr_binds_reference(ast->data.ternary.condition, reference) ||
               cxpr_model_expr_binds_reference(ast->data.ternary.true_branch, reference) ||
               cxpr_model_expr_binds_reference(ast->data.ternary.false_branch, reference);
    default:
        return false;
    }
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
            if (cxpr_model_expr_binds_reference(
                    model->bindings[i].expr, refs[j])) continue;
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
