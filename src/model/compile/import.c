/**
 * @file model/compile/import.c
 * @brief Register imported models and their namespaced functions.
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



static const char* cxpr_model_import_namespace_for(const cxpr_model* model,
                                                   const char* import_name) {
    return cxpr_model_import_namespace_name(model, import_name);
}

static size_t cxpr_model_compiled_exposed_param_count(const cxpr_model_compiled* program) {
    size_t explicit_count = 0u;
    if (!program) return 0u;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (program->constants[i].is_call_param) ++explicit_count;
    }
    return explicit_count > 0u ? explicit_count : program->constant_count;
}

static bool cxpr_model_compiled_param_is_exposed(const cxpr_model_compiled* program,
                                                   size_t index) {
    size_t explicit_count = 0u;
    if (!program || index >= program->constant_count) return false;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (program->constants[i].is_call_param) ++explicit_count;
    }
    return explicit_count == 0u || program->constants[index].is_call_param;
}

static bool cxpr_model_compiled_inputs_are_implicit_market(
    const cxpr_model_compiled* program) {
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
           (entry->defined_body || entry->defined_return_field_bodies);
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

static bool cxpr_model_namespace_imported_ast(cxpr_expr_ast* ast,
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
    case CXPR_NODE_INDEX:
        return cxpr_model_namespace_imported_ast(
                   ast->data.index.target, namespace_name, source_registry, err) &&
               cxpr_model_namespace_imported_ast(
                   ast->data.index.index, namespace_name, source_registry, err);
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

static bool cxpr_model_register_imported_defined_function(cxpr_model_compiled* program,
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
    entry->defined_body = cxpr_expr_ast_clone(src->defined_body);
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
            (cxpr_expr_ast**)calloc(src->defined_return_field_count, sizeof(cxpr_expr_ast*));
        if (!entry->defined_return_field_bodies) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        for (size_t i = 0u; i < src->defined_return_field_count; ++i) {
            entry->defined_return_field_bodies[i] =
                cxpr_expr_ast_clone(src->defined_return_field_bodies[i]);
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

bool cxpr_model_compiled_register_imports(cxpr_model_compiled* program,
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
        const cxpr_model_compiled* child = imports[i].program;
        const char* namespace_name = cxpr_model_import_namespace_for(model, imports[i].name);
        cxpr_func_entry* entry;
        size_t exposed_input_count;
        size_t exposed_param_count;
        if (!imports[i].name || !child || child->output_count == 0u) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model import", 0, 0);
            return false;
        }
        exposed_input_count =
            cxpr_model_compiled_inputs_are_implicit_market(child) ? 0u : child->input_count;
        exposed_param_count = cxpr_model_compiled_exposed_param_count(child);
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
                if (!cxpr_model_compiled_param_is_exposed(child, p)) continue;
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
                const cxpr_func_entry* imported_entry = &child->registry->entries[f];
                if (!cxpr_model_import_entry_is_namespaced(imported_entry) &&
                    imported_entry->name &&
                    !cxpr_registry_find(program->registry, imported_entry->name)) {
                    /* Imported expression functions may depend on core builtins.
                       Re-register known cxpr defaults, but never copy arbitrary
                       host callbacks across the import boundary. */
                    (void)cxpr_register_default_named(
                        program->registry, imported_entry->name);
                }
                if (!cxpr_model_register_imported_defined_function(
                        program, namespace_name, child->registry,
                        imported_entry, err)) {
                    return false;
                }
            }
        }
    }
    return true;
}
