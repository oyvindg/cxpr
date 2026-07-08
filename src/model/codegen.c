#include "model/internal.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* cxpr_model_program_function_to_c_function(const cxpr_model_program* program,
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

typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool oom;
} cxpr_model_c_buf;

static void cxpr_model_c_reserve(cxpr_model_c_buf* b, size_t extra) {
    if (!b || b->oom) return;
    if (b->len + extra + 1u > b->cap) {
        size_t cap = b->cap ? b->cap : 512u;
        char* grown;
        while (b->len + extra + 1u > cap) cap *= 2u;
        grown = (char*)realloc(b->data, cap);
        if (!grown) {
            b->oom = true;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
}

static void cxpr_model_c_puts(cxpr_model_c_buf* b, const char* s) {
    size_t n;
    if (!b || !s) return;
    n = strlen(s);
    cxpr_model_c_reserve(b, n);
    if (b->oom) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void cxpr_model_c_printf(cxpr_model_c_buf* b, const char* fmt, ...) {
    va_list ap;
    va_list ap_copy;
    int needed;
    if (!b || b->oom || !fmt) return;
    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        b->oom = true;
        va_end(ap_copy);
        return;
    }
    cxpr_model_c_reserve(b, (size_t)needed);
    if (!b->oom) {
        vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap_copy);
        b->len += (size_t)needed;
    }
    va_end(ap_copy);
}

static void cxpr_model_c_format_double(char* out, size_t out_size, double value) {
    if (isfinite(value) && floor(value) == value) {
        snprintf(out, out_size, "%.1f", value);
    } else {
        snprintf(out, out_size, "%.17g", value);
    }
}

static char* cxpr_model_c_safe_name(const char* name) {
    size_t len = name ? strlen(name) : 0u;
    char* out = (char*)malloc(len + 4u);
    size_t pos = 0u;
    if (!out) return NULL;
    if (len == 0u || (name[0] >= '0' && name[0] <= '9')) {
        memcpy(out, "cx_", 3u);
        pos = 3u;
    }
    for (size_t i = 0u; i < len; ++i) {
        unsigned char ch = (unsigned char)name[i];
        out[pos++] = ((ch >= 'a' && ch <= 'z') ||
                      (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') ||
                      ch == '_') ? (char)ch : '_';
    }
    out[pos] = '\0';
    return out;
}

static size_t cxpr_model_program_param_index(const cxpr_model_program* program,
                                             const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (cxpr_model_names_match(program->constants[i].name, name)) return i;
    }
    return (size_t)-1;
}

static const char* cxpr_model_c_binary_op(cxpr_opcode op) {
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

static bool cxpr_model_c_emit_native_call(cxpr_model_c_buf* b,
                                          const cxpr_ir_instr* instr,
                                          cxpr_error* err) {
    const char* name = instr && instr->func ? instr->func->name : NULL;
    if (!name) return false;
    if (instr->op == CXPR_OP_CALL_UNARY) {
        const char* fn = NULL;
        if (cxpr_model_names_match(name, "abs")) fn = "fabs";
        else if (cxpr_model_names_match(name, "sqrt")) fn = "sqrt";
        else if (cxpr_model_names_match(name, "floor")) fn = "floor";
        else if (cxpr_model_names_match(name, "ceil")) fn = "ceil";
        else if (cxpr_model_names_match(name, "round")) fn = "round";
        if (!fn) return false;
        cxpr_model_c_printf(b, "    _cx_s[_cx_sp - 1u] = %s(_cx_s[_cx_sp - 1u]);\n", fn);
        return true;
    }
    if (instr->op == CXPR_OP_CALL_BINARY) {
        const char* fn = NULL;
        if (cxpr_model_names_match(name, "min")) fn = "fmin";
        else if (cxpr_model_names_match(name, "max")) fn = "fmax";
        else if (cxpr_model_names_match(name, "pow")) fn = "pow";
        if (!fn) return false;
        cxpr_model_c_printf(
            b,
            "    { double _cx_b = _cx_s[--_cx_sp]; double _cx_a = _cx_s[--_cx_sp]; _cx_s[_cx_sp++] = %s(_cx_a, _cx_b); }\n",
            fn);
        return true;
    }
    (void)err;
    return false;
}

static char* cxpr_model_c_function_name(const char* name) {
    char raw[512];
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "cxpr_fn_%s", name) >= sizeof(raw)) return NULL;
    return cxpr_model_c_safe_name(raw);
}

static char* cxpr_model_c_scoped_function_name(const char* scope, const char* name) {
    char raw[768];
    if (!scope || !scope[0]) return cxpr_model_c_function_name(name);
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "cxpr_fn_%s_%s", scope, name) >= sizeof(raw)) {
        return NULL;
    }
    return cxpr_model_c_safe_name(raw);
}

static bool cxpr_model_c_emit_defined_call(cxpr_model_c_buf* b,
                                           const cxpr_ir_instr* instr,
                                           cxpr_error* err) {
    char* fn_name;
    if (!instr || !instr->func || !instr->func->name) return false;
    fn_name = cxpr_model_c_function_name(instr->func->name);
    if (!fn_name) return cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                              "Out of memory", 0, 0), false;
    cxpr_model_c_puts(b, "    { ");
    for (size_t i = instr->index; i > 0u; --i) {
        cxpr_model_c_printf(b, "double _cx_a%zu = _cx_s[--_cx_sp]; ", i - 1u);
    }
    cxpr_model_c_printf(b, "_cx_s[_cx_sp++] = %s(", fn_name);
    for (size_t i = 0u; i < instr->index; ++i) {
        if (i > 0u) cxpr_model_c_puts(b, ", ");
        cxpr_model_c_printf(b, "_cx_a%zu", i);
    }
    cxpr_model_c_puts(b, "); }\n");
    free(fn_name);
    return true;
}

static bool cxpr_model_c_emit_variadic_call(cxpr_model_c_buf* b,
                                            const cxpr_ir_instr* instr) {
    const char* name = instr && instr->func ? instr->func->name : NULL;
    const char* fn = NULL;
    if (!name || instr->index == 0u) return false;
    if (cxpr_model_names_match(name, "min")) fn = "fmin";
    else if (cxpr_model_names_match(name, "max")) fn = "fmax";
    if (!fn) return false;

    cxpr_model_c_puts(b, "    { ");
    for (size_t i = instr->index; i > 0u; --i) {
        cxpr_model_c_printf(b, "double _cx_a%zu = _cx_s[--_cx_sp]; ", i - 1u);
    }
    cxpr_model_c_puts(b, "double _cx_v = _cx_a0; ");
    for (size_t i = 1u; i < instr->index; ++i) {
        cxpr_model_c_printf(b, "_cx_v = %s(_cx_v, _cx_a%zu); ", fn, i);
    }
    cxpr_model_c_puts(b, "_cx_s[_cx_sp++] = _cx_v; }\n");
    return true;
}

static bool cxpr_model_c_emit_defined_functions(const cxpr_model_program* program,
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

typedef struct {
    const cxpr_model_program* program;
    char** param_names;
    char** param_exprs;
    size_t param_count;
    const char* inline_fn_name;
    const char* function_prefix;
    const double* literal_param_values;
    size_t literal_param_count;
} cxpr_model_ast_c_target;

typedef struct {
    cxpr_model_c_buf* declarations;
    const cxpr_c_target* target;
    size_t next_temp;
} cxpr_model_ast_temp_emit;

static bool cxpr_model_c_defined_function_used(const cxpr_model_program* program,
                                               const char* name,
                                               bool* used,
                                               cxpr_error* err);
static bool cxpr_model_c_collect_defined_function_refs(const cxpr_model_program* program,
                                                       const cxpr_ast* ast,
                                                       bool* used,
                                                       cxpr_error* err);
static char* cxpr_model_ast_c_emit_leaf(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        cxpr_error* err);
static char* cxpr_model_ast_c_emit_call(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        bool* handled,
                                        cxpr_error* err);
static char* cxpr_model_ast_expr_to_c(const cxpr_model_program* program,
                                      const cxpr_ast* ast,
                                      const char* function_prefix,
                                      const double* literal_param_values,
                                      size_t literal_param_count,
                                      cxpr_error* err);
static char* cxpr_model_ast_expr_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                 const cxpr_ast* ast,
                                                 cxpr_error* err);

static bool cxpr_model_c_symbol_is_input(const cxpr_model_program* program,
                                         const char* name,
                                         size_t* out_index) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        if (cxpr_model_names_match(program->fused_inputs[i].name, name)) {
            if (out_index) *out_index = i;
            return true;
        }
    }
    return false;
}

static bool cxpr_model_c_symbol_is_state(const cxpr_model_program* program,
                                         const char* name,
                                         size_t* out_slot) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            size_t slot = cxpr_model_fused_slot_find(
                program->fused_slot_names, program->fused_slot_count, name);
            if (out_slot) *out_slot = slot;
            return slot != (size_t)-1;
        }
    }
    return false;
}

static bool cxpr_model_c_symbol_is_binding(const cxpr_model_program* program,
                                           const char* name) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (cxpr_model_names_match(program->bindings[i].name, name)) return true;
    }
    return false;
}

static char* cxpr_model_c_prefixed_name(const char* prefix, const char* name);

static unsigned long cxpr_model_c_name_hash(const char* s) {
    unsigned long h = 5381u;
    if (!s) return h;
    while (*s) h = ((h << 5u) + h) ^ (unsigned char)*s++;
    return h;
}

static char* cxpr_model_c_child_tick_name(const char* function_prefix, size_t child_index) {
    char raw[128];
    snprintf(raw, sizeof(raw), "cxpr_c%08lx_%zu_tick",
             cxpr_model_c_name_hash(function_prefix) & 0xfffffffful,
             child_index);
    return cxpr_model_c_safe_name(raw);
}

static char* cxpr_model_c_child_field_name(const char* function_prefix,
                                           size_t child_index,
                                           size_t field_index) {
    char raw[128];
    snprintf(raw, sizeof(raw), "cxpr_c%08lx_%zu_f%zu",
             cxpr_model_c_name_hash(function_prefix) & 0xfffffffful,
             child_index,
             field_index);
    return cxpr_model_c_safe_name(raw);
}

static const cxpr_ast* cxpr_model_producer_arg_for_param(const cxpr_ast* ast,
                                                         const char* param_name,
                                                         size_t param_index) {
    if (!ast || cxpr_ast_type(ast) != CXPR_NODE_PRODUCER_ACCESS) return NULL;
    if (cxpr_ast_producer_has_named_args(ast)) {
        for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
            const char* arg_name = cxpr_ast_producer_arg_name(ast, i);
            if (arg_name && param_name && cxpr_model_names_match(arg_name, param_name)) {
                return cxpr_ast_producer_arg(ast, i);
            }
        }
        return NULL;
    }
    return param_index < cxpr_ast_producer_argc(ast)
               ? cxpr_ast_producer_arg(ast, param_index)
               : NULL;
}

static size_t cxpr_model_c_child_base_inline(const cxpr_model_program* program,
                                             size_t child_index) {
    size_t base;
    if (!program || child_index >= program->child_count) return (size_t)-1;
    base = program->state_default_count;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        base += 2u + program->history_specs[i].depth;
    }
    for (size_t i = 0u; i < child_index; ++i) {
        const cxpr_model_program* child = program->children[i].program;
        base += cxpr_model_program_c_slot_count(child) + 1u + child->output_count;
    }
    return base;
}

static size_t cxpr_model_c_child_index_for_entry(const cxpr_model_program* program,
                                                 const cxpr_func_entry* entry) {
    if (!program || !entry || !entry->model_producer_userdata) return (size_t)-1;
    for (size_t i = 0u; i < program->child_count; ++i) {
        if (&program->children[i] == entry->model_producer_userdata) return i;
    }
    return (size_t)-1;
}

static char* cxpr_model_ast_producer_access_to_c(const cxpr_model_program* program,
                                                 const cxpr_ast* ast,
                                                 const char* function_prefix,
                                                 const double* literal_param_values,
                                                 size_t literal_param_count,
                                                 cxpr_error* err) {
    cxpr_func_entry* entry;
    const char* producer;
    const char* field;
    size_t selected_field = (size_t)-1;
    char** arg_exprs = NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target target_data;
    cxpr_c_target target;

    if (!program || !program->registry || !ast ||
        cxpr_ast_type(ast) != CXPR_NODE_PRODUCER_ACCESS) {
        return NULL;
    }
    producer = cxpr_ast_producer_name(ast);
    field = cxpr_ast_producer_field(ast);
    entry = cxpr_registry_find(program->registry, producer);
    if (!entry || (!entry->model_producer && entry->defined_return_field_count == 0u) ||
        entry->defined_param_count != cxpr_ast_producer_argc(ast)) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            err->message = "Unsupported model C producer access";
        }
        return NULL;
    }
    for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
        if (entry->defined_return_field_names[i] &&
            field &&
            strcmp(entry->defined_return_field_names[i], field) == 0) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "Unknown producer field";
        }
        return NULL;
    }

    if (entry->model_producer) {
        size_t child_index = cxpr_model_c_child_index_for_entry(program, entry);
        size_t child_base = cxpr_model_c_child_base_inline(program, child_index);
        const cxpr_model_program* child =
            child_index == (size_t)-1 ? NULL : program->children[child_index].program;
        char* helper_name;
        cxpr_model_c_buf call = {0};
        if (!child || child_base == (size_t)-1) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unknown child model producer", 0, 0);
            return NULL;
        }
        (void)producer;
        helper_name = cxpr_model_c_child_field_name(function_prefix, child_index, selected_field);
        if (!helper_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        cxpr_model_c_printf(&call, "%s(_cx_slots + %zu", helper_name, child_base);
        free(helper_name);
        for (size_t i = 0u; i < child->input_count; ++i) {
            size_t input_index = 0u;
            if (!cxpr_model_c_symbol_is_input(program, child->inputs[i], &input_index)) {
                free(call.data);
                cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                     "Child model input is not a parent input", 0, 0);
                return NULL;
            }
            cxpr_model_c_printf(&call, ", _cx_input_%zu", input_index);
        }
        target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
        };
        target = (cxpr_c_target){
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .userdata = &target_data,
        };
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            const cxpr_ast* arg = cxpr_model_producer_arg_for_param(
                ast, entry->defined_param_names ? entry->defined_param_names[i] : NULL, i);
            char* arg_expr;
            if (!arg) {
                free(call.data);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Missing producer argument", 0, 0);
                return NULL;
            }
            arg_expr = cxpr_ast_to_c_at_offset(arg, 0u, &target, err);
            if (!arg_expr) {
                free(call.data);
                return NULL;
            }
            cxpr_model_c_printf(&call, ", %s", arg_expr);
            free(arg_expr);
        }
        cxpr_model_c_puts(&call, ")");
        if (call.oom) {
            free(call.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return call.data;
    }

    arg_exprs = (char**)calloc(entry->defined_param_count ? entry->defined_param_count : 1u,
                              sizeof(char*));
    if (!arg_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    target_data = (cxpr_model_ast_c_target){
        .program = program,
        .function_prefix = function_prefix,
        .literal_param_values = literal_param_values,
        .literal_param_count = literal_param_count,
    };
    target = (cxpr_c_target){
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = &target_data,
    };
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        const cxpr_ast* arg = cxpr_model_producer_arg_for_param(
            ast, entry->defined_param_names ? entry->defined_param_names[i] : NULL, i);
        if (!arg) {
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "Missing producer argument";
            }
            goto fail;
        }
        arg_exprs[i] = cxpr_ast_to_c_at_offset(arg, 0u, &target, err);
        if (!arg_exprs[i]) goto fail;
    }

    field_exprs = (char**)calloc(entry->defined_return_field_count, sizeof(char*));
    if (!field_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t name_count = entry->defined_param_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            names[i] = entry->defined_param_names[i];
            exprs[i] = arg_exprs[i];
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[entry->defined_param_count + i] = entry->defined_return_field_names[i];
            exprs[entry->defined_param_count + i] = field_exprs[i];
        }
        target_data.param_names = names;
        target_data.param_exprs = exprs;
        target_data.param_count = name_count;
        field_exprs[field_i] = cxpr_ast_to_c_at_offset(
            entry->defined_return_field_bodies[field_i], 0u, &target, err);
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }
    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
        free(arg_exprs);
        return out;
    }

fail:
    if (field_exprs) {
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
    }
    for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
    free(arg_exprs);
    return NULL;
}

static size_t cxpr_model_c_state_slot_for_name(const cxpr_model_program* program,
                                               const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) return i;
    }
    return (size_t)-1;
}

static size_t cxpr_model_c_state_slot_for_fused_slot(const cxpr_model_program* program,
                                                     size_t fused_slot) {
    if (!program || fused_slot >= program->fused_slot_count) return (size_t)-1;
    return cxpr_model_c_state_slot_for_name(program, program->fused_slot_names[fused_slot]);
}

static size_t cxpr_model_c_history_base(const cxpr_model_program* program,
                                        size_t history_index) {
    size_t base;
    if (!program) return (size_t)-1;
    base = program->state_default_count;
    for (size_t i = 0u; i < history_index && i < program->history_spec_count; ++i) {
        base += 2u + program->history_specs[i].depth;
    }
    return base;
}

static bool cxpr_model_c_is_power_of_two(size_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool cxpr_model_c_history_use_shift(size_t depth) {
    return depth <= 4u;
}

static size_t cxpr_model_c_history_find(const cxpr_model_program* program,
                                        const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (cxpr_model_names_match(program->history_specs[i].name, name)) return i;
    }
    return (size_t)-1;
}

static char* cxpr_model_c_current_symbol_expr(const cxpr_model_program* program,
                                              char** state_next_names,
                                              const char* name,
                                              cxpr_error* err) {
    size_t index = 0u;
    size_t slot = 0u;
    if (!program || !name) return NULL;
    if (cxpr_model_c_symbol_is_input(program, name, &index)) {
        char raw[64];
        snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
        return cxpr_strdup(raw);
    }
    if (cxpr_model_c_symbol_is_state(program, name, &slot)) {
        size_t c_slot = cxpr_model_c_state_slot_for_fused_slot(program, slot);
        if (c_slot == (size_t)-1) return NULL;
        if (state_next_names && state_next_names[slot]) {
            return cxpr_strdup(state_next_names[slot]);
        }
        (void)c_slot;
        return cxpr_model_c_prefixed_name("_cx_state_", name);
    }
    if (cxpr_model_c_symbol_is_binding(program, name)) {
        return cxpr_model_c_safe_name(name);
    }
    if (err) {
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        err->message = "Unknown model C history target";
    }
    return NULL;
}

static char* cxpr_model_c_prefixed_name(const char* prefix, const char* name) {
    char raw[512];
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "%s%s", prefix ? prefix : "", name) >= sizeof(raw)) {
        return NULL;
    }
    return cxpr_model_c_safe_name(raw);
}

static char* cxpr_model_ast_c_emit_leaf(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        cxpr_error* err) {
    cxpr_model_ast_c_target* target = (cxpr_model_ast_c_target*)userdata;
    const cxpr_model_program* program = target ? target->program : NULL;
    const char* name = NULL;
    size_t index = 0u;
    size_t slot = 0u;

    if (!program || !ast) return NULL;
    if (lookback_offset != 0u) {
        size_t hist_index;
        size_t base;
        size_t depth;
        char* key = NULL;
        if (!cxpr_model_lookback_target_key(ast, &key, err)) return NULL;
        name = key;
        hist_index = cxpr_model_c_history_find(program, name);
        if (hist_index == (size_t)-1) {
            free(key);
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown model C history target";
            }
            return NULL;
        }
        depth = program->history_specs[hist_index].depth;
        if (depth == 0u || lookback_offset == 0u || lookback_offset > depth) {
            char raw[32];
            free(key);
            snprintf(raw, sizeof(raw), "NAN");
            return cxpr_strdup(raw);
        }
        base = cxpr_model_c_history_base(program, hist_index);
        free(key);
        {
            char raw[256];
            char index_expr[128];
            size_t delta = depth - (size_t)lookback_offset;
            if (cxpr_model_c_history_use_shift(depth)) {
                snprintf(raw, sizeof(raw),
                         "_cx_slots[%zu]",
                         base + 1u + (size_t)lookback_offset);
                return cxpr_strdup(raw);
            }
            if (depth == 1u) {
                snprintf(index_expr, sizeof(index_expr), "0u");
            } else if (delta == 0u) {
                snprintf(index_expr, sizeof(index_expr), "_cx_history_next_%zu", hist_index);
            } else if (cxpr_model_c_is_power_of_two(depth)) {
                snprintf(index_expr, sizeof(index_expr),
                         "((_cx_history_next_%zu + %zuu) & %zuu)",
                         hist_index, delta, depth - 1u);
            } else {
                snprintf(index_expr, sizeof(index_expr),
                         "((_cx_history_next_%zu + %zuu) %% %zuu)",
                         hist_index, delta, depth);
            }
            snprintf(raw, sizeof(raw),
                     "_cx_slots[%zu + %s]",
                     base + 2u,
                     index_expr);
            return cxpr_strdup(raw);
        }
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_IDENTIFIER) {
        name = cxpr_ast_identifier_name(ast);
        for (size_t i = 0u; target && i < target->param_count; ++i) {
            if (cxpr_model_names_match(target->param_names[i], name)) {
                if (target->param_exprs && target->param_exprs[i]) {
                    return cxpr_strdup(target->param_exprs[i]);
                }
                return cxpr_model_c_safe_name(name);
            }
        }
        if (cxpr_model_c_symbol_is_input(program, name, &index)) {
            char raw[64];
            snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
            return cxpr_strdup(raw);
        }
        if (cxpr_model_c_symbol_is_state(program, name, &slot)) {
            return cxpr_model_c_prefixed_name("_cx_state_", name);
        }
        if (cxpr_model_c_symbol_is_binding(program, name)) {
            return cxpr_model_c_safe_name(name);
        }
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "Unknown model C identifier";
        }
        return NULL;
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_model_ast_producer_access_to_c(
            program,
            ast,
            target ? target->function_prefix : NULL,
            target ? target->literal_param_values : NULL,
            target ? target->literal_param_count : 0u,
            err);
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_VARIABLE) {
        name = cxpr_ast_variable_name(ast);
        index = cxpr_model_program_param_index(program, name);
        if (index == (size_t)-1) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown model C parameter";
            }
            return NULL;
        }
        if (target &&
            target->literal_param_values &&
            index < target->literal_param_count) {
            char raw[64];
            cxpr_model_c_format_double(raw, sizeof(raw),
                                       target->literal_param_values[index]);
            return cxpr_strdup(raw);
        }
        {
            char raw[64];
            snprintf(raw, sizeof(raw), "_cx_param_%zu", index);
            return cxpr_strdup(raw);
        }
    }
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = "Unsupported model C leaf";
    }
    return NULL;
}

static char* cxpr_model_ast_c_emit_call(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        bool* handled,
                                        cxpr_error* err) {
    const char* name = cxpr_ast_function_name(ast);
    size_t argc = cxpr_ast_function_argc(ast);
    cxpr_model_ast_c_target* target_data = (cxpr_model_ast_c_target*)userdata;
    const cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = userdata,
    };
    cxpr_model_c_buf b = {0};

    (void)target_data;
    if (handled) *handled = false;
    if (!name) return NULL;

    if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
        argc == 2u) {
        char* left;
        char* right;
        const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
        if (handled) *handled = true;
        left = cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 0u),
                                       lookback_offset, &target, err);
        right = left ? cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 1u),
                                               lookback_offset, &target, err) : NULL;
        if (!left || !right) {
            free(left);
            free(right);
            return NULL;
        }
        cxpr_model_c_printf(&b, "((%s %s %s) ? (%s) : (%s))",
                            left, op, right, left, right);
        free(left);
        free(right);
        if (b.oom) {
            free(b.data);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return NULL;
        }
        return b.data;
    }

    if (cxpr_model_names_match(name, "if") && argc == 3u) {
        char* cond;
        char* yes;
        char* no;
        if (handled) *handled = true;
        cond = cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 0u),
                                       lookback_offset, &target, err);
        yes = cond ? cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 1u),
                                             lookback_offset, &target, err) : NULL;
        no = yes ? cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 2u),
                                           lookback_offset, &target, err) : NULL;
        if (!cond || !yes || !no) {
            free(cond);
            free(yes);
            free(no);
            return NULL;
        }
        cxpr_model_c_printf(&b, "((%s) ? (%s) : (%s))", cond, yes, no);
        free(cond);
        free(yes);
        free(no);
        if (b.oom) {
            free(b.data);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return NULL;
        }
        return b.data;
    }

    if (target_data && target_data->program && target_data->program->registry) {
        cxpr_func_entry* entry = cxpr_registry_find(target_data->program->registry, name);
        if (entry && entry->defined_body && entry->defined_return_field_count == 0u) {
            char* fn_name;
            if (entry->defined_param_count != argc) {
                if (err) {
                    err->code = CXPR_ERR_SYNTAX;
                    err->message = "Model function arity mismatch";
                }
                return NULL;
            }
            if (handled) *handled = true;
            fn_name = cxpr_model_c_scoped_function_name(target_data->function_prefix,
                                                        entry->name);
            if (!fn_name) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            cxpr_model_c_printf(&b, "%s(", fn_name);
            free(fn_name);
            for (size_t i = 0u; i < argc; ++i) {
                char* arg;
                if (i > 0u) cxpr_model_c_puts(&b, ", ");
                arg = cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, i),
                                              lookback_offset, &target, err);
                if (!arg) {
                    free(b.data);
                    return NULL;
                }
                cxpr_model_c_puts(&b, arg);
                free(arg);
            }
            cxpr_model_c_puts(&b, ")");
            if (b.oom) {
                free(b.data);
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            return b.data;
        }
    }

    if (handled) *handled = false;
    return NULL;
}

static char* cxpr_model_ast_expr_to_c(const cxpr_model_program* program,
                                      const cxpr_ast* ast,
                                      const char* function_prefix,
                                      const double* literal_param_values,
                                      size_t literal_param_count,
                                      cxpr_error* err) {
    cxpr_model_ast_c_target userdata = {
        .program = program,
        .function_prefix = function_prefix,
        .literal_param_values = literal_param_values,
        .literal_param_count = literal_param_count,
    };
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = &userdata,
    };
    return cxpr_ast_to_c(ast, &target, err);
}

static char* cxpr_model_ast_temp_make(cxpr_model_ast_temp_emit* emit,
                                      const char* expr,
                                      cxpr_error* err) {
    char name[64];
    if (!emit || !emit->declarations || !expr) return NULL;
    snprintf(name, sizeof(name), "_cx_t%zu", emit->next_temp++);
    cxpr_model_c_printf(emit->declarations, "    const double %s = %s;\n", name, expr);
    if (emit->declarations->oom) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }
    return cxpr_strdup(name);
}

static char* cxpr_model_ast_binary_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                   const cxpr_ast* ast,
                                                   cxpr_error* err) {
    int op = cxpr_ast_operator(ast);
    const char* ops = NULL;
    char* left;
    char* right;
    cxpr_model_c_buf b = {0};

    switch (op) {
    case CXPR_TOK_PLUS: ops = "+"; break;
    case CXPR_TOK_MINUS: ops = "-"; break;
    case CXPR_TOK_STAR: ops = "*"; break;
    case CXPR_TOK_SLASH: ops = "/"; break;
    case CXPR_TOK_EQ: ops = "=="; break;
    case CXPR_TOK_NEQ: ops = "!="; break;
    case CXPR_TOK_LT: ops = "<"; break;
    case CXPR_TOK_GT: ops = ">"; break;
    case CXPR_TOK_LTE: ops = "<="; break;
    case CXPR_TOK_GTE: ops = ">="; break;
    case CXPR_TOK_AND: ops = "&&"; break;
    case CXPR_TOK_OR: ops = "||"; break;
    default:
        return cxpr_ast_to_c(ast, emit ? emit->target : NULL, err);
    }

    left = cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_ast_left(ast), err);
    right = left ? cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_ast_right(ast), err) : NULL;
    if (!left || !right) {
        free(left);
        free(right);
        return NULL;
    }
    cxpr_model_c_printf(&b, "(%s %s %s)", left, ops, right);
    free(left);
    free(right);
    if (b.oom) {
        free(b.data);
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }
    return b.data;
}

static char* cxpr_model_ast_expr_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                 const cxpr_ast* ast,
                                                 cxpr_error* err) {
    if (!emit || !ast) return NULL;
    if (cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_ast_function_name(ast);
        const size_t argc = cxpr_ast_function_argc(ast);
        if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
            argc == 2u) {
            const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
            char* left = cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_ast_function_arg(ast, 0u), err);
            char* right = left ? cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_ast_function_arg(ast, 1u), err) : NULL;
            char* left_temp;
            char* right_temp;
            char* out;
            cxpr_model_c_buf b = {0};
            if (!left || !right) {
                free(left);
                free(right);
                return NULL;
            }
            left_temp = cxpr_model_ast_temp_make(emit, left, err);
            right_temp = left_temp ? cxpr_model_ast_temp_make(emit, right, err) : NULL;
            free(left);
            free(right);
            if (!left_temp || !right_temp) {
                free(left_temp);
                free(right_temp);
                return NULL;
            }
            cxpr_model_c_printf(&b, "((%s %s %s) ? %s : %s)",
                                left_temp, op, right_temp, left_temp, right_temp);
            free(left_temp);
            free(right_temp);
            if (b.oom) {
                free(b.data);
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            out = cxpr_model_ast_temp_make(emit, b.data, err);
            free(b.data);
            return out;
        }
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP) {
        return cxpr_model_ast_binary_to_c_with_temps(emit, ast, err);
    }
    return cxpr_ast_to_c(ast, emit->target, err);
}

static bool cxpr_model_c_defined_function_used(const cxpr_model_program* program,
                                               const char* name,
                                               bool* used,
                                               cxpr_error* err) {
    if (!program || !program->registry || !name || !used) return true;
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        if (cxpr_model_names_match(entry->name, name) &&
            entry->defined_body &&
            entry->defined_return_field_count == 0u) {
            if (used[i]) return true;
            used[i] = true;
            return cxpr_model_c_collect_defined_function_refs(
                program, entry->defined_body, used, err);
        }
    }
    return true;
}

static bool cxpr_model_c_collect_defined_function_refs(const cxpr_model_program* program,
                                                       const cxpr_ast* ast,
                                                       bool* used,
                                                       cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_ast_left(ast),
                                                          used, err) &&
               cxpr_model_c_collect_defined_function_refs(program, cxpr_ast_right(ast),
                                                          used, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_ast_operand(ast),
                                                          used, err);
    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = cxpr_ast_function_name(ast);
        size_t argc = cxpr_ast_function_argc(ast);
        if (!cxpr_model_c_defined_function_used(program, name, used, err)) return false;
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_ast_function_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_PRODUCER_ACCESS:
        {
            cxpr_func_entry* entry = program && program->registry
                ? cxpr_registry_find(program->registry, cxpr_ast_producer_name(ast))
                : NULL;
            if (entry && !entry->model_producer && entry->defined_return_field_count > 0u) {
                const char* field = cxpr_ast_producer_field(ast);
                for (size_t f = 0u; f < entry->defined_return_field_count; ++f) {
                    if (entry->defined_return_field_names[f] &&
                        field &&
                        strcmp(entry->defined_return_field_names[f], field) == 0) {
                        if (!cxpr_model_c_collect_defined_function_refs(
                                program, entry->defined_return_field_bodies[f], used, err)) {
                            return false;
                        }
                        break;
                    }
                }
            }
        }
        for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_ast_producer_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_lookback_target(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_lookback_index(ast), used, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_ternary_condition(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_ternary_true_branch(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_ternary_false_branch(ast), used, err);
    default:
        return true;
    }
}

static char* cxpr_model_ast_defined_fn_to_c(const cxpr_model_program* program,
                                            const cxpr_func_entry* entry,
                                            const char* function_prefix,
                                            cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* fn_name;
    cxpr_model_ast_c_target userdata = {0};
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = &userdata,
    };
    if (!entry || !entry->defined_body) return NULL;
    userdata.program = program;
    userdata.param_names = entry->defined_param_names;
    userdata.param_exprs = NULL;
    userdata.param_count = entry->defined_param_count;
    userdata.inline_fn_name = entry->name;
    userdata.function_prefix = function_prefix;
    fn_name = cxpr_model_c_scoped_function_name(function_prefix, entry->name);
    if (!fn_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    if (function_prefix && function_prefix[0]) {
        cxpr_model_c_printf(&b, "/* Source function: %s (scope: %s) */\n",
                            entry->name ? entry->name : "(unnamed)",
                            function_prefix);
    } else {
        cxpr_model_c_printf(&b, "/* Source function: %s */\n",
                            entry->name ? entry->name : "(unnamed)");
    }
    cxpr_model_c_printf(&b, "static inline double %s(", fn_name);
    free(fn_name);
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        char* param_name = cxpr_model_c_safe_name(entry->defined_param_names[i]);
        if (!param_name) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (i > 0u) cxpr_model_c_puts(&b, ", ");
        cxpr_model_c_printf(&b, "double %s", param_name);
        free(param_name);
    }
    if (entry->defined_param_count == 0u) cxpr_model_c_puts(&b, "void");
    cxpr_model_c_puts(&b, ") { return ");
    {
        cxpr_model_c_buf declarations = {0};
        cxpr_model_ast_temp_emit temp_emit = {
            .declarations = &declarations,
            .target = &target,
            .next_temp = 0u,
        };
        char* expr = cxpr_model_ast_expr_to_c_with_temps(&temp_emit,
                                                         entry->defined_body,
                                                         err);
        if (!expr) {
            free(declarations.data);
            free(b.data);
            return NULL;
        }
        if (declarations.data && declarations.len > 0u) {
            size_t prefix_len = b.len;
            cxpr_model_c_buf nb = {0};
            if (b.len >= strlen(" { return ") &&
                strcmp(b.data + b.len - strlen(" { return "), " { return ") == 0) {
                prefix_len = b.len - strlen("return ");
            }
            cxpr_model_c_reserve(&nb, prefix_len + declarations.len + strlen("    return ") + strlen(expr) + 16u);
            if (!nb.oom) {
                memcpy(nb.data, b.data, prefix_len);
                nb.len = prefix_len;
                nb.data[nb.len] = '\0';
                cxpr_model_c_puts(&nb, "\n");
                cxpr_model_c_puts(&nb, declarations.data);
                cxpr_model_c_puts(&nb, "    return ");
                free(b.data);
                b = nb;
            }
        }
        free(declarations.data);
        cxpr_model_c_puts(&b, expr);
        free(expr);
    }
    cxpr_model_c_puts(&b, "; }\n\n");
    if (b.oom) {
        free(b.data);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    return b.data;
}

static bool cxpr_model_c_emit_defined_functions_ast(const cxpr_model_program* program,
                                                    const char* function_prefix,
                                                    cxpr_model_c_buf* b,
                                                    cxpr_error* err) {
    bool* used = NULL;
    if (!program || !program->registry) return true;
    used = (bool*)calloc(program->registry->count ? program->registry->count : 1u,
                         sizeof(bool));
    if (!used && program->registry->count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (!cxpr_model_c_collect_defined_function_refs(
                program, program->bindings[i].program->ast, used, err)) {
            free(used);
            return false;
        }
    }
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        char* source;
        if (!used[i]) continue;
        if (!entry->defined_body || entry->defined_return_field_count > 0u) continue;
        source = cxpr_model_ast_defined_fn_to_c(program, entry, function_prefix, err);
        if (!source) {
            free(used);
            return false;
        }
        cxpr_model_c_puts(b, source);
        free(source);
        if (b->oom) {
            free(used);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    free(used);
    return true;
}

static bool cxpr_model_c_emit_child_model_helpers(const cxpr_model_program* program,
                                                  const char* function_prefix,
                                                  cxpr_model_c_buf* b,
                                                  cxpr_error* err) {
    if (!program || !function_prefix || !b) return true;
    for (size_t i = 0u; i < program->child_count; ++i) {
        const cxpr_model_program* child = program->children[i].program;
        char* tick_name;
        char* tick_source;
        if (!child) continue;
        tick_name = cxpr_model_c_child_tick_name(function_prefix, i);
        if (!tick_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "/* Source model tick: %s */\n",
                            program->children[i].name ? program->children[i].name : "(unnamed)");
        tick_source = cxpr_model_program_to_c_tick_function_select_outputs(
            child, "static inline", tick_name, NULL, 0u, err);
        if (!tick_source) {
            free(tick_name);
            return false;
        }
        cxpr_model_c_puts(b, tick_source);
        cxpr_model_c_puts(b, "\n");
        free(tick_source);

        for (size_t field_i = 0u; field_i < child->output_count; ++field_i) {
            char* helper_name;
            helper_name = cxpr_model_c_child_field_name(function_prefix, i, field_i);
            if (!helper_name) {
                free(tick_name);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_printf(b, "/* Source model field: %s.%s */\n",
                                program->children[i].name ? program->children[i].name : "(unnamed)",
                                child->outputs[field_i] ? child->outputs[field_i] : "(unnamed)");
            cxpr_model_c_printf(b, "static inline double %s(double* restrict _cx_child_slots",
                                helper_name);
            for (size_t in_i = 0u; in_i < child->input_count; ++in_i) {
                char* input_name = cxpr_model_c_safe_name(child->inputs[in_i]);
                if (!input_name) {
                    free(helper_name);
                    free(tick_name);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                cxpr_model_c_printf(b, ", double %s", input_name);
                free(input_name);
            }
            for (size_t p = 0u; p < child->constant_count; ++p) {
                char* param_name = cxpr_model_c_prefixed_name("param_", child->constants[p].name);
                if (!param_name) {
                    free(helper_name);
                    free(tick_name);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                cxpr_model_c_printf(b, ", double %s", param_name);
                free(param_name);
            }
            cxpr_model_c_puts(b, ") {\n");
            cxpr_model_c_printf(b, "    double _cx_child_inputs[%zu] = {",
                                child->input_count ? child->input_count : 1u);
            for (size_t in_i = 0u; in_i < child->input_count; ++in_i) {
                char* input_name = cxpr_model_c_safe_name(child->inputs[in_i]);
                if (in_i > 0u) cxpr_model_c_puts(b, ", ");
                cxpr_model_c_puts(b, input_name ? input_name : "0.0");
                free(input_name);
            }
            if (child->input_count == 0u) cxpr_model_c_puts(b, "0.0");
            cxpr_model_c_puts(b, "};\n");
            cxpr_model_c_printf(b, "    double _cx_child_params[%zu] = {",
                                child->constant_count ? child->constant_count : 1u);
            for (size_t p = 0u; p < child->constant_count; ++p) {
                char* param_name = cxpr_model_c_prefixed_name("param_", child->constants[p].name);
                if (p > 0u) cxpr_model_c_puts(b, ", ");
                cxpr_model_c_puts(b, param_name ? param_name : "0.0");
                free(param_name);
            }
            if (child->constant_count == 0u) cxpr_model_c_puts(b, "0.0");
            cxpr_model_c_puts(b, "};\n");
            cxpr_model_c_printf(b, "    double _cx_child_outputs[%zu] = {0};\n",
                                child->output_count ? child->output_count : 1u);
            cxpr_model_c_printf(b, "    if (_cx_child_slots[%zu] == 0.0) {\n",
                                cxpr_model_program_c_slot_count(child));
            cxpr_model_c_printf(b,
                                "        %s(_cx_child_slots, _cx_child_inputs, _cx_child_params, _cx_child_outputs);\n",
                                tick_name);
            for (size_t out_i = 0u; out_i < child->output_count; ++out_i) {
                cxpr_model_c_printf(b, "        _cx_child_slots[%zu] = _cx_child_outputs[%zu];\n",
                                    cxpr_model_program_c_slot_count(child) + 1u + out_i,
                                    out_i);
            }
            cxpr_model_c_printf(b, "        _cx_child_slots[%zu] = 1.0;\n",
                                cxpr_model_program_c_slot_count(child));
            cxpr_model_c_puts(b, "    }\n");
            cxpr_model_c_printf(b, "    return _cx_child_slots[%zu];\n",
                                cxpr_model_program_c_slot_count(child) + 1u + field_i);
            cxpr_model_c_puts(b, "}\n\n");
            free(helper_name);
        }
        free(tick_name);
        if (b->oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    return true;
}

static bool cxpr_model_program_to_c_tick_function_ast(const cxpr_model_program* program,
                                                      const char* qualifiers,
                                                      const char* function_name,
                                                      const double* literal_param_values,
                                                      size_t literal_param_count,
                                                      const size_t* output_indices,
                                                      size_t selected_output_count,
                                                      char** out_source,
                                                      cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* safe_name = NULL;
    char** state_next_names = NULL;
    bool* needed_bindings = NULL;

    if (out_source) *out_source = NULL;
    if (!program || !program->has_fused_layout || !function_name || !out_source) return false;
    state_next_names = (char**)calloc(program->fused_slot_count ? program->fused_slot_count : 1u,
                                      sizeof(char*));
    if (!state_next_names && program->fused_slot_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    needed_bindings = (bool*)calloc(program->binding_count ? program->binding_count : 1u,
                                    sizeof(bool));
    if (!needed_bindings && program->binding_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        free(state_next_names);
        return false;
    }
    if (!cxpr_model_program_mark_required_bindings(
            program,
            output_indices,
            output_indices ? selected_output_count : 0u,
            output_indices ? false : true,
            true,
            true,
            needed_bindings,
            err)) {
        goto fail;
    }
    cxpr_model_c_puts(&b,
                      "#ifndef CXPR_UNLIKELY\n"
                      "#if defined(__GNUC__) || defined(__clang__)\n"
                      "#define CXPR_UNLIKELY(x) __builtin_expect(!!(x), 0)\n"
                      "#else\n"
                      "#define CXPR_UNLIKELY(x) (x)\n"
                      "#endif\n"
                      "#endif\n\n");
    if (!cxpr_model_c_emit_child_model_helpers(program, function_name, &b, err)) goto fail;
    if (!cxpr_model_c_emit_defined_functions_ast(program, function_name, &b, err)) goto fail;

    safe_name = cxpr_model_c_safe_name(function_name);
    if (!safe_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    cxpr_model_c_printf(&b, "/* Source model tick: %s */\n", function_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(&b, "%s ", qualifiers);
    cxpr_model_c_printf(
        &b,
        "void %s(double* restrict _cx_slots, const double* restrict _cx_inputs, const double* restrict _cx_params, double* restrict _cx_outputs) {\n",
        safe_name);
    free(safe_name);
    safe_name = NULL;

    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        char* name = cxpr_model_c_prefixed_name("_cx_input_", program->fused_inputs[i].name);
        if (!name) goto oom;
        cxpr_model_c_printf(&b, "    const double _cx_input_%zu = _cx_inputs[%zu];\n", i, i);
        free(name);
    }
    if (!literal_param_values || literal_param_count < program->constant_count) {
        for (size_t i = 0u; i < program->constant_count; ++i) {
            cxpr_model_c_printf(&b, "    const double _cx_param_%zu = _cx_params[%zu];\n", i, i);
        }
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* name = cxpr_model_c_prefixed_name("_cx_state_", program->state_defaults[i].name);
        size_t c_slot = cxpr_model_c_state_slot_for_name(program,
                                                         program->state_defaults[i].name);
        if (!name || c_slot == (size_t)-1) {
            free(name);
            goto fail;
        }
        cxpr_model_c_printf(&b, "    const double %s = _cx_slots[%zu];\n", name, c_slot);
        free(name);
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t base = cxpr_model_c_history_base(program, i);
        size_t depth = program->history_specs[i].depth;
        if (depth == 0u) continue;
        cxpr_model_c_printf(&b, "    if (CXPR_UNLIKELY(_cx_slots[%zu] == 0.0)) {", base);
        for (size_t j = 0u; j < depth; ++j) {
            cxpr_model_c_printf(&b, " _cx_slots[%zu] = NAN;", base + 2u + j);
        }
        cxpr_model_c_printf(&b, " _cx_slots[%zu] = 1.0; }\n", base);
        if (depth > 1u && !cxpr_model_c_history_use_shift(depth)) {
            cxpr_model_c_printf(&b, "    const size_t _cx_history_next_%zu = (size_t)_cx_slots[%zu];\n",
                                i, base + 1u);
        }
    }
    for (size_t i = 0u; i < program->child_count; ++i) {
        size_t child_base = cxpr_model_c_child_base_inline(program, i);
        size_t child_slots = cxpr_model_program_c_slot_count(program->children[i].program);
        cxpr_model_c_printf(&b, "    _cx_slots[%zu] = 0.0;\n", child_base + child_slots);
    }
    for (size_t i = 0u; i < program->binding_count; ++i) {
        char* expr;
        char* name;
        bool owns_name = false;
        if (!needed_bindings[i]) continue;
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            size_t slot = cxpr_model_fused_slot_find(program->fused_slot_names,
                                                     program->fused_slot_count,
                                                     program->bindings[i].name);
            name = cxpr_model_c_prefixed_name("_cx_next_", program->bindings[i].name);
            if (slot == (size_t)-1) {
                free(name);
                name = NULL;
            } else if (name) {
                state_next_names[slot] = name;
            }
        } else {
            name = cxpr_model_c_safe_name(program->bindings[i].name);
            owns_name = true;
        }
        if (!name) goto oom;
        expr = cxpr_model_ast_expr_to_c(program,
                                        program->bindings[i].program->ast,
                                        function_name,
                                        literal_param_values,
                                        literal_param_count,
                                        err);
        if (!expr) {
            if (owns_name) free(name);
            goto fail;
        }
        cxpr_model_c_printf(&b, "    const double %s = %s;\n", name, expr);
        free(expr);
        if (owns_name) free(name);
    }
    for (size_t i = 0u; i < program->fused_commit_count; ++i) {
        char* next_name = state_next_names[program->fused_commits[i].state_slot];
        size_t c_slot = cxpr_model_c_state_slot_for_fused_slot(
            program, program->fused_commits[i].state_slot);
        if (!next_name) goto oom;
        if (c_slot == (size_t)-1) goto fail;
        cxpr_model_c_printf(&b, "    _cx_slots[%zu] = %s;\n",
                            c_slot, next_name);
    }
    for (size_t out_i = 0u; out_i < (output_indices ? selected_output_count : program->fused_output_count); ++out_i) {
        size_t i = output_indices ? output_indices[out_i] : out_i;
        const char* name;
        size_t state_slot = 0u;
        if (i >= program->fused_output_count) goto fail;
        name = program->fused_outputs[i].name;
        if (cxpr_model_c_symbol_is_state(program, name, &state_slot)) {
            char* next_name = state_next_names[state_slot];
            if (next_name) {
                cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = %s;\n", out_i, next_name);
            } else {
                size_t c_slot = cxpr_model_c_state_slot_for_fused_slot(program, state_slot);
                if (c_slot == (size_t)-1) goto fail;
                cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = _cx_slots[%zu];\n",
                                    out_i, c_slot);
            }
        } else {
            char* local_name = cxpr_model_c_safe_name(name);
            if (!local_name) goto oom;
            cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = %s;\n", out_i, local_name);
            free(local_name);
        }
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t base = cxpr_model_c_history_base(program, i);
        size_t depth = program->history_specs[i].depth;
        char* current = NULL;
        if (program->history_specs[i].target &&
            cxpr_ast_type(program->history_specs[i].target) == CXPR_NODE_PRODUCER_ACCESS) {
            current = cxpr_model_ast_expr_to_c(program,
                                               program->history_specs[i].target,
                                               function_name,
                                               literal_param_values,
                                               literal_param_count,
                                               err);
        } else {
            current = cxpr_model_c_current_symbol_expr(
                program, state_next_names, program->history_specs[i].name, err);
        }
        if (!current) goto fail;
        if (depth > 0u) {
            if (cxpr_model_c_history_use_shift(depth)) {
                for (size_t j = depth; j > 1u; --j) {
                    cxpr_model_c_printf(&b, "    _cx_slots[%zu] = _cx_slots[%zu];\n",
                                        base + 1u + j,
                                        base + j);
                }
                cxpr_model_c_printf(
                    &b,
                    "    _cx_slots[%zu] = %s;\n",
                    base + 2u,
                    current);
            } else if (cxpr_model_c_is_power_of_two(depth)) {
                cxpr_model_c_printf(
                    &b,
                    "    { const double _cx_history_value_%zu = %s; _cx_slots[%zu + _cx_history_next_%zu] = _cx_history_value_%zu; _cx_slots[%zu] = (double)((_cx_history_next_%zu + 1u) & %zuu); }\n",
                    i,
                    current,
                    base + 2u,
                    i,
                    i,
                    base + 1u,
                    i,
                    depth - 1u);
            } else {
                cxpr_model_c_printf(
                    &b,
                    "    { const double _cx_history_value_%zu = %s; _cx_slots[%zu + _cx_history_next_%zu] = _cx_history_value_%zu; _cx_slots[%zu] = (double)((_cx_history_next_%zu + 1u) %% %zuu); }\n",
                    i,
                    current,
                    base + 2u,
                    i,
                    i,
                    base + 1u,
                    i,
                    depth);
            }
        }
        free(current);
    }
    cxpr_model_c_puts(&b, "}\n");
    if (b.oom) goto oom;
    for (size_t i = 0u; i < program->fused_slot_count; ++i) free(state_next_names[i]);
    free(state_next_names);
    free(needed_bindings);
    *out_source = b.data;
    return true;

oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
fail:
    free(safe_name);
    if (state_next_names) {
        for (size_t i = 0u; i < program->fused_slot_count; ++i) free(state_next_names[i]);
        free(state_next_names);
    }
    free(needed_bindings);
    free(b.data);
    return false;
}

static bool cxpr_model_c_stack_effect(const cxpr_ir_instr* instr,
                                      size_t sp,
                                      size_t* next_sp,
                                      cxpr_error* err) {
    size_t pop = 0u;
    size_t push = 0u;
    if (!instr || !next_sp) return false;
    switch (instr->op) {
    case CXPR_OP_PUSH_CONST:
    case CXPR_OP_PUSH_BOOL:
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
        push = 1u;
        break;
    case CXPR_OP_ADD:
    case CXPR_OP_SUB:
    case CXPR_OP_MUL:
    case CXPR_OP_DIV:
    case CXPR_OP_MOD:
    case CXPR_OP_CMP_EQ:
    case CXPR_OP_CMP_NEQ:
    case CXPR_OP_CMP_LT:
    case CXPR_OP_CMP_LTE:
    case CXPR_OP_CMP_GT:
    case CXPR_OP_CMP_GTE:
    case CXPR_OP_POW:
        pop = 2u;
        push = 1u;
        break;
    case CXPR_OP_SQUARE:
    case CXPR_OP_NOT:
    case CXPR_OP_NEG:
    case CXPR_OP_SIGN:
    case CXPR_OP_SQRT:
    case CXPR_OP_ABS:
    case CXPR_OP_FLOOR:
    case CXPR_OP_CEIL:
    case CXPR_OP_ROUND:
        pop = 1u;
        push = 1u;
        break;
    case CXPR_OP_CLAMP:
        pop = 3u;
        push = 1u;
        break;
    case CXPR_OP_CALL_UNARY:
        pop = 1u;
        push = 1u;
        break;
    case CXPR_OP_CALL_BINARY:
        pop = 2u;
        push = 1u;
        break;
    case CXPR_OP_CALL_FUNC:
    case CXPR_OP_CALL_DEFINED:
        pop = instr->index;
        push = 1u;
        break;
    case CXPR_OP_STORE_LOCAL:
    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
    case CXPR_OP_RETURN:
        pop = 1u;
        break;
    case CXPR_OP_JUMP:
        break;
    default:
        {
            static CXPR_THREAD_LOCAL char msg[128];
            snprintf(msg, sizeof(msg), "Unsupported opcode in model C backend: %s",
                     cxpr_ir_opcode_name(instr->op));
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, msg, 0, 0);
        }
        return false;
    }
    if (sp < pop) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model C stack depth", 0, 0);
        return false;
    }
    *next_sp = sp - pop + push;
    return true;
}

static bool cxpr_model_c_set_depth(size_t* depths,
                                   bool* queued,
                                   size_t* queue,
                                   size_t* tail,
                                   size_t index,
                                   size_t depth,
                                   size_t count,
                                   cxpr_error* err) {
    if (index >= count) return true;
    if (depths[index] == (size_t)-1) {
        depths[index] = depth;
    } else if (depths[index] != depth) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Inconsistent model C stack depth", 0, 0);
        return false;
    }
    if (!queued[index]) {
        queued[index] = true;
        queue[(*tail)++] = index;
    }
    return true;
}

static bool cxpr_model_c_compute_stack_depths(const cxpr_ir_program* ir,
                                              size_t** out_depths,
                                              size_t* out_max_depth,
                                              cxpr_error* err) {
    size_t* depths;
    bool* queued;
    size_t* queue;
    size_t head = 0u;
    size_t tail = 0u;
    size_t max_depth = 0u;

    if (!ir || !out_depths || !out_max_depth) return false;
    *out_depths = NULL;
    *out_max_depth = 0u;
    depths = (size_t*)malloc(ir->count * sizeof(size_t));
    queued = (bool*)calloc(ir->count ? ir->count : 1u, sizeof(bool));
    queue = (size_t*)malloc(ir->count * sizeof(size_t));
    if ((ir->count > 0u && (!depths || !queued || !queue))) {
        free(depths);
        free(queued);
        free(queue);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < ir->count; ++i) depths[i] = (size_t)-1;
    if (ir->count > 0u &&
        !cxpr_model_c_set_depth(depths, queued, queue, &tail, 0u, 0u, ir->count, err)) {
        free(depths);
        free(queued);
        free(queue);
        return false;
    }
    while (head < tail) {
        size_t i = queue[head++];
        const cxpr_ir_instr* instr = &ir->code[i];
        size_t sp = depths[i];
        size_t next_sp = sp;
        if (sp > max_depth) max_depth = sp;
        if (!cxpr_model_c_stack_effect(instr, sp, &next_sp, err)) {
            free(depths);
            free(queued);
            free(queue);
            return false;
        }
        if (next_sp > max_depth) max_depth = next_sp;
        if (instr->op == CXPR_OP_JUMP) {
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        instr->index, next_sp, ir->count, err)) goto fail;
        } else if (instr->op == CXPR_OP_JUMP_IF_FALSE ||
                   instr->op == CXPR_OP_JUMP_IF_TRUE) {
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        instr->index, next_sp, ir->count, err)) goto fail;
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        i + 1u, next_sp, ir->count, err)) goto fail;
        } else if (instr->op != CXPR_OP_RETURN) {
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        i + 1u, next_sp, ir->count, err)) goto fail;
        }
    }
    free(queued);
    free(queue);
    *out_depths = depths;
    *out_max_depth = max_depth;
    return true;

fail:
    free(depths);
    free(queued);
    free(queue);
    return false;
}

static bool cxpr_model_c_validate_selected_outputs(const cxpr_model_program* program,
                                                   const size_t* output_indices,
                                                   size_t output_count,
                                                   cxpr_error* err) {
    if (!output_indices) return true;
    if (!program || output_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C selected-output backend requires outputs", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < output_count; ++i) {
        if (output_indices[i] >= program->fused_output_count) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Model C selected-output index out of range", 0, 0);
            return false;
        }
    }
    return true;
}

char* cxpr_model_program_to_c_tick_function_select_outputs(
    const cxpr_model_program* program,
    const char* qualifiers,
    const char* function_name,
    const size_t* output_indices,
    size_t output_count,
    cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* safe_name;
    size_t* depths = NULL;
    size_t max_depth = 0u;
    char* ast_source = NULL;
    cxpr_error ast_err = {0};

    if (err) *err = (cxpr_error){0};
    if (!program || (!program->has_fused_ir && !program->has_fused_layout) || !function_name) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C backend requires fused scalar IR", 0, 0);
        return NULL;
    }
    if (!cxpr_model_c_validate_selected_outputs(program, output_indices, output_count, err)) {
        return NULL;
    }
    if (cxpr_model_program_to_c_tick_function_ast(program, qualifiers, function_name,
                                                 NULL, 0u,
                                                 output_indices,
                                                 output_indices ? output_count : 0u,
                                                 &ast_source, &ast_err)) {
        return ast_source;
    }
    if (ast_err.code == CXPR_ERR_OUT_OF_MEMORY) {
        if (err) *err = ast_err;
        return NULL;
    }
    if (!program->has_fused_ir || !program->fused_ir.code) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C backend requires runnable fused scalar IR fallback", 0, 0);
        return NULL;
    }
    if (err) *err = (cxpr_error){0};

    safe_name = cxpr_model_c_safe_name(function_name);
    if (!safe_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    if (!cxpr_model_c_emit_defined_functions(program, &b, err)) goto fail;
    if (!cxpr_model_c_compute_stack_depths(&program->fused_ir, &depths, &max_depth, err)) {
        goto fail;
    }

    cxpr_model_c_printf(&b, "/* Source model tick: %s */\n", function_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(&b, "%s ", qualifiers);
    cxpr_model_c_printf(
        &b,
        "void %s(double* _cx_slots, const double* _cx_inputs, const double* _cx_params, double* _cx_outputs) {\n",
        safe_name);
    free(safe_name);
    safe_name = NULL;
    for (size_t i = 0u; i < max_depth + 1u; ++i) {
        cxpr_model_c_printf(&b, "    double _cx_v%zu;\n", i);
    }
    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        cxpr_model_c_printf(&b, "    _cx_slots[%zu] = _cx_inputs[%zu];\n",
                            program->fused_inputs[i].slot, i);
    }

    for (size_t i = 0u; i < program->fused_ir.count; ++i) {
        const cxpr_ir_instr* instr = &program->fused_ir.code[i];
        const char* op = cxpr_model_c_binary_op(instr->op);
        size_t sp = depths[i];
        cxpr_model_c_printf(&b, "L%zu:\n", i);
        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            {
                char raw[64];
                cxpr_model_c_format_double(raw, sizeof(raw), instr->value);
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s;\n", sp, raw);
            }
            break;
        case CXPR_OP_PUSH_BOOL:
            cxpr_model_c_printf(&b, "    _cx_v%zu = %.1f;\n", sp,
                                instr->value != 0.0 ? 1.0 : 0.0);
            break;
        case CXPR_OP_LOAD_LOCAL:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_slots[%zu];\n", sp, instr->index);
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_slots[%zu] * _cx_slots[%zu];\n",
                                sp, instr->index, instr->index);
            break;
        case CXPR_OP_LOAD_PARAM: {
            size_t param_index = cxpr_model_program_param_index(program, instr->name);
            if (param_index == (size_t)-1) {
                cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                     "Unknown model C parameter", 0, 0);
                goto fail;
            }
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_params[%zu];\n", sp, param_index);
            break;
        }
        case CXPR_OP_LOAD_PARAM_SQUARE: {
            size_t param_index = cxpr_model_program_param_index(program, instr->name);
            if (param_index == (size_t)-1) {
                cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                     "Unknown model C parameter", 0, 0);
                goto fail;
            }
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_params[%zu] * _cx_params[%zu];\n",
                                sp, param_index, param_index);
            break;
        }
        case CXPR_OP_ADD:
        case CXPR_OP_SUB:
        case CXPR_OP_MUL:
        case CXPR_OP_DIV:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_v%zu %s _cx_v%zu;\n",
                                sp - 2u, sp - 2u, op, sp - 1u);
            break;
        case CXPR_OP_CMP_EQ:
        case CXPR_OP_CMP_NEQ:
        case CXPR_OP_CMP_LT:
        case CXPR_OP_CMP_LTE:
        case CXPR_OP_CMP_GT:
        case CXPR_OP_CMP_GTE:
            cxpr_model_c_printf(&b, "    _cx_v%zu = (_cx_v%zu %s _cx_v%zu) ? 1.0 : 0.0;\n",
                                sp - 2u, sp - 2u, op, sp - 1u);
            break;
        case CXPR_OP_MOD:
            cxpr_model_c_printf(&b, "    _cx_v%zu = fmod(_cx_v%zu, _cx_v%zu);\n",
                                sp - 2u, sp - 2u, sp - 1u);
            break;
        case CXPR_OP_SQUARE:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_v%zu * _cx_v%zu;\n",
                                sp - 1u, sp - 1u, sp - 1u);
            break;
        case CXPR_OP_NOT:
            cxpr_model_c_printf(&b, "    _cx_v%zu = (_cx_v%zu == 0.0) ? 1.0 : 0.0;\n",
                                sp - 1u, sp - 1u);
            break;
        case CXPR_OP_NEG:
            cxpr_model_c_printf(&b, "    _cx_v%zu = -_cx_v%zu;\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_SIGN:
            cxpr_model_c_printf(&b, "    _cx_v%zu = (_cx_v%zu > 0.0) - (_cx_v%zu < 0.0);\n",
                                sp - 1u, sp - 1u, sp - 1u);
            break;
        case CXPR_OP_SQRT:
            cxpr_model_c_printf(&b, "    _cx_v%zu = sqrt(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_ABS:
            cxpr_model_c_printf(&b, "    _cx_v%zu = fabs(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_FLOOR:
            cxpr_model_c_printf(&b, "    _cx_v%zu = floor(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_CEIL:
            cxpr_model_c_printf(&b, "    _cx_v%zu = ceil(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_ROUND:
            cxpr_model_c_printf(&b, "    _cx_v%zu = round(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_POW:
            cxpr_model_c_printf(&b, "    _cx_v%zu = pow(_cx_v%zu, _cx_v%zu);\n",
                                sp - 2u, sp - 2u, sp - 1u);
            break;
        case CXPR_OP_CLAMP:
            cxpr_model_c_printf(&b, "    { double _cx_clamp = _cx_v%zu; if (_cx_clamp < _cx_v%zu) _cx_clamp = _cx_v%zu; if (_cx_clamp > _cx_v%zu) _cx_clamp = _cx_v%zu; _cx_v%zu = _cx_clamp; }\n",
                                sp - 3u, sp - 2u, sp - 2u, sp - 1u, sp - 1u, sp - 3u);
            break;
        case CXPR_OP_CALL_UNARY:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                const char* fn = NULL;
                if (cxpr_model_names_match(name, "abs")) fn = "fabs";
                else if (cxpr_model_names_match(name, "sqrt")) fn = "sqrt";
                else if (cxpr_model_names_match(name, "floor")) fn = "floor";
                else if (cxpr_model_names_match(name, "ceil")) fn = "ceil";
                else if (cxpr_model_names_match(name, "round")) fn = "round";
                if (!fn) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported native call in model C backend", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s(_cx_v%zu);\n",
                                    sp - 1u, fn, sp - 1u);
            }
            break;
        case CXPR_OP_CALL_BINARY:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                const char* fn = NULL;
                if (cxpr_model_names_match(name, "min")) fn = "fmin";
                else if (cxpr_model_names_match(name, "max")) fn = "fmax";
                else if (cxpr_model_names_match(name, "pow")) fn = "pow";
                if (!fn) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported native call in model C backend", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s(_cx_v%zu, _cx_v%zu);\n",
                                    sp - 2u, fn, sp - 2u, sp - 1u);
            }
            break;
        case CXPR_OP_CALL_FUNC:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                const char* fn = NULL;
                if (cxpr_model_names_match(name, "min")) fn = "fmin";
                else if (cxpr_model_names_match(name, "max")) fn = "fmax";
                if (!fn || instr->index == 0u) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported variadic call in model C backend", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_v%zu;\n",
                                    sp - instr->index, sp - instr->index);
                for (size_t arg = 1u; arg < instr->index; ++arg) {
                    cxpr_model_c_printf(&b, "    _cx_v%zu = %s(_cx_v%zu, _cx_v%zu);\n",
                                        sp - instr->index, fn,
                                        sp - instr->index,
                                        sp - instr->index + arg);
                }
            }
            break;
        case CXPR_OP_CALL_DEFINED:
            {
                char* fn_name;
                if (!instr->func || !instr->func->name) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported defined call in model C backend", 0, 0);
                    goto fail;
                }
                fn_name = cxpr_model_c_function_name(instr->func->name);
                if (!fn_name) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s(",
                                    sp - instr->index, fn_name);
                for (size_t arg = 0u; arg < instr->index; ++arg) {
                    if (arg > 0u) cxpr_model_c_puts(&b, ", ");
                    cxpr_model_c_printf(&b, "_cx_v%zu", sp - instr->index + arg);
                }
                cxpr_model_c_puts(&b, ");\n");
                free(fn_name);
            }
            break;
        case CXPR_OP_JUMP:
            cxpr_model_c_printf(&b, "    goto L%zu;\n", instr->index);
            break;
        case CXPR_OP_JUMP_IF_FALSE:
            cxpr_model_c_printf(&b, "    if (_cx_v%zu == 0.0) goto L%zu;\n",
                                sp - 1u, instr->index);
            break;
        case CXPR_OP_JUMP_IF_TRUE:
            cxpr_model_c_printf(&b, "    if (_cx_v%zu != 0.0) goto L%zu;\n",
                                sp - 1u, instr->index);
            break;
        case CXPR_OP_STORE_LOCAL:
            cxpr_model_c_printf(&b, "    _cx_slots[%zu] = _cx_v%zu;\n",
                                instr->index, sp - 1u);
            break;
        case CXPR_OP_RETURN:
            cxpr_model_c_puts(&b, "    goto _cx_done;\n");
            break;
        default:
            {
                static CXPR_THREAD_LOCAL char msg[128];
                snprintf(msg, sizeof(msg), "Unsupported opcode in model C backend: %s",
                         cxpr_ir_opcode_name(instr->op));
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, msg, 0, 0);
            }
            goto fail;
        }
        if (b.oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
    }
    cxpr_model_c_puts(&b, "_cx_done:\n");
    for (size_t i = 0u; i < program->fused_commit_count; ++i) {
        cxpr_model_c_printf(&b, "    _cx_slots[%zu] = _cx_slots[%zu];\n",
                            program->fused_commits[i].state_slot,
                            program->fused_commits[i].update_slot);
    }
    for (size_t out_i = 0u; out_i < (output_indices ? output_count : program->fused_output_count); ++out_i) {
        size_t i = output_indices ? output_indices[out_i] : out_i;
        cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = _cx_slots[%zu];\n",
                            out_i, program->fused_outputs[i].slot);
    }
    cxpr_model_c_puts(&b, "}\n");
    if (b.oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    if (err) err->code = CXPR_OK;
    free(depths);
    return b.data;

fail:
    free(safe_name);
    free(depths);
    free(b.data);
    return NULL;
}

char* cxpr_model_program_to_c_tick_function(const cxpr_model_program* program,
                                            const char* qualifiers,
                                            const char* function_name,
                                            cxpr_error* err) {
    return cxpr_model_program_to_c_tick_function_select_outputs(
        program, qualifiers, function_name, NULL, 0u, err);
}

char* cxpr_model_program_to_c_tick_function_with_params(const cxpr_model_program* program,
                                                        const char* qualifiers,
                                                        const char* function_name,
                                                        const double* param_values,
                                                        size_t param_count,
                                                        cxpr_error* err) {
    return cxpr_model_program_to_c_tick_function_with_params_select_outputs(
        program, qualifiers, function_name, param_values, param_count, NULL, 0u, err);
}

char* cxpr_model_program_to_c_tick_function_with_params_select_outputs(
    const cxpr_model_program* program,
    const char* qualifiers,
    const char* function_name,
    const double* param_values,
    size_t param_count,
    const size_t* output_indices,
    size_t output_count,
    cxpr_error* err) {
    char* ast_source = NULL;
    cxpr_error ast_err = {0};

    if (err) *err = (cxpr_error){0};
    if (!program || (!program->has_fused_ir && !program->has_fused_layout) || !function_name) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C backend requires fused scalar IR", 0, 0);
        return NULL;
    }
    if (!param_values || param_count < program->constant_count) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C specialized backend requires all parameter values", 0, 0);
        return NULL;
    }
    if (!cxpr_model_c_validate_selected_outputs(program, output_indices, output_count, err)) {
        return NULL;
    }
    if (cxpr_model_program_to_c_tick_function_ast(program, qualifiers, function_name,
                                                 param_values, param_count,
                                                 output_indices,
                                                 output_indices ? output_count : 0u,
                                                 &ast_source, &ast_err)) {
        return ast_source;
    }
    if (ast_err.code == CXPR_ERR_OUT_OF_MEMORY) {
        if (err) *err = ast_err;
        return NULL;
    }
    return cxpr_model_program_to_c_tick_function_select_outputs(
        program, qualifiers, function_name, output_indices, output_count, err);
}
