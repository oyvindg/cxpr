#include "model/codegen/internal.h"
#include "registry/internal.h"
#include <stdlib.h>

char* cxpr_model_compiled_generate_function_c(const cxpr_model_compiled* program,
                                                const char* name,
                                                const char* qualifiers,
                                                const char* return_type,
                                                const char* function_name,
                                                cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!program || !program->registry) {
        cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                             "Model has no defined function registry", 0, 0);
        return NULL;
    }
    return cxpr_registry_defined_fn_to_c_function(program->registry,
                                                  name,
                                                  qualifiers,
                                                  return_type,
                                                  function_name,
                                                  err);
}

size_t cxpr_model_compiled_param_index(const cxpr_model_compiled* program,
                                      const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (cxpr_model_names_match(program->constants[i].name, name)) return i;
    }
    return (size_t)-1;
}

const char* cxpr_model_c_binary_op(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_ADD: return "+";
    case CXPR_OP_SUB: return "-";
    case CXPR_OP_MUL: return "*";
    case CXPR_OP_DIV: return "/";
    case CXPR_OP_CMP_EQ: return "==";
    case CXPR_OP_CMP_NEQ: return "!=";
    case CXPR_OP_CMP_LT: return "<";
    case CXPR_OP_CMP_LTE: return "<=";
    case CXPR_OP_CMP_GT: return ">";
    case CXPR_OP_CMP_GTE: return ">=";
    default: return NULL;
    }
}

bool cxpr_model_c_emit_defined_functions(const cxpr_model_compiled* program,
                                         cxpr_model_c_buf* b,
                                         cxpr_error* err) {
    if (!program || !program->registry) return true;
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        char* fn_name;
        char* source;
        if (!entry->defined_body || entry->defined_return_field_count > 0u) continue;
        fn_name = cxpr_model_c_function_name(entry->name);
        if (!fn_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        source = cxpr_registry_defined_fn_to_c_function(program->registry,
                                                        entry->name,
                                                        "static inline",
                                                        "double",
                                                        fn_name,
                                                        err);
        free(fn_name);
        if (!source) return false;
        cxpr_model_c_printf(b, "/* Source function: %s */\n",
                            entry->name ? entry->name : "(unnamed)");
        cxpr_model_c_puts(b, source);
        cxpr_model_c_puts(b, "\n");
        free(source);
        if (b->oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    return true;
}
