#include "model/codegen/internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_c_append_output_leaf(cxpr_model_slot_ref** outputs,
                                            size_t* count,
                                            const char* name,
                                            cxpr_model_result_kind kind,
                                            cxpr_error* err) {
    cxpr_model_slot_ref* grown = (cxpr_model_slot_ref*)realloc(
        *outputs, (*count + 1u) * sizeof(**outputs));
    if (!grown) goto oom;
    *outputs = grown;
    memset(&grown[*count], 0, sizeof(grown[*count]));
    grown[*count].name = cxpr_strdup(name);
    grown[*count].hash = cxpr_hash_string(name);
    grown[*count].result_kind = kind == CXPR_MODEL_RESULT_UNKNOWN
        ? CXPR_MODEL_RESULT_NUMBER : kind;
    if (!grown[*count].name) goto oom;
    (*count)++;
    return true;
oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
    return false;
}

static bool cxpr_model_c_flatten_record(const cxpr_expr_ast* record,
                                        const char* prefix,
                                        cxpr_model_slot_ref** outputs,
                                        size_t* count,
                                        cxpr_error* err) {
    for (size_t i = 0u; i < cxpr_expr_ast_record_field_count(record); ++i) {
        const char* field = cxpr_expr_ast_record_field_name(record, i);
        const cxpr_expr_ast* value = cxpr_expr_ast_record_field_value(record, i);
        char name[512];
        if (!field || snprintf(name, sizeof(name), "%s.%s", prefix, field) < 0 ||
            strlen(name) >= sizeof(name) - 1u) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Record output path is too long", 0, 0);
            return false;
        }
        if (cxpr_expr_ast_kind_of(value) == CXPR_NODE_RECORD) {
            if (!cxpr_model_c_flatten_record(value, name, outputs, count, err)) return false;
        } else if (!cxpr_model_c_append_output_leaf(
                       outputs, count, name,
                       cxpr_expr_ast_kind_of(value) == CXPR_NODE_BOOL
                           ? CXPR_MODEL_RESULT_BOOL
                           : cxpr_expr_ast_kind_of(value) == CXPR_NODE_INT64
                                 ? CXPR_MODEL_RESULT_INT64
                                 : CXPR_MODEL_RESULT_NUMBER,
                       err)) {
            return false;
        }
    }
    return true;
}

bool cxpr_model_c_flatten_outputs(const cxpr_model_compiled* program,
                                  cxpr_model_slot_ref** out_outputs,
                                  size_t* out_count,
                                  cxpr_error* err) {
    cxpr_model_slot_ref* outputs = NULL;
    size_t count = 0u;
    if (!program || !out_outputs || !out_count) return false;
    for (size_t i = 0u; i < program->output_count; ++i) {
        const cxpr_model_compiled_binding* binding = NULL;
        for (size_t b = 0u; b < program->binding_count; ++b) {
            if (cxpr_model_names_match(program->bindings[b].name, program->outputs[i])) {
                binding = &program->bindings[b];
                break;
            }
        }
        if (binding && cxpr_expr_ast_kind_of(binding->ast) ==
                           CXPR_NODE_FUNCTION_CALL) {
            bool used_fused_shape = false;
            const size_t output_name_length = strlen(program->outputs[i]);
            for (size_t fused_i = 0u;
                 fused_i < program->fused_output_count; ++fused_i) {
                const char* fused_name = program->fused_outputs[fused_i].name;
                if (fused_name &&
                    strncmp(fused_name, program->outputs[i],
                            output_name_length) == 0 &&
                    fused_name[output_name_length] == '.') {
                    if (!cxpr_model_c_append_output_leaf(
                            &outputs, &count, fused_name,
                            program->fused_outputs[fused_i].result_kind, err)) {
                        goto fail;
                    }
                    used_fused_shape = true;
                }
            }
            if (used_fused_shape) continue;
        }
        if (binding && cxpr_expr_ast_kind_of(binding->ast) == CXPR_NODE_RECORD) {
            if (!cxpr_model_c_flatten_record(
                    binding->ast, program->outputs[i], &outputs, &count, err)) goto fail;
        } else {
            cxpr_model_result_kind kind = cxpr_model_compiled_output_result_kind(program, i);
            if (!cxpr_model_c_append_output_leaf(
                    &outputs, &count, program->outputs[i], kind, err)) goto fail;
        }
    }
    *out_outputs = outputs;
    *out_count = count;
    return true;
fail:
    cxpr_model_c_flatten_outputs_free(outputs, count);
    return false;
}

void cxpr_model_c_flatten_outputs_free(cxpr_model_slot_ref* outputs, size_t count) {
    if (!outputs) return;
    for (size_t i = 0u; i < count; ++i) free(outputs[i].name);
    free(outputs);
}

bool cxpr_model_c_used_child_outputs(const cxpr_model_compiled* program,
                                     size_t child_index,
                                     const cxpr_model_slot_ref* flat_outputs,
                                     size_t flat_output_count,
                                     size_t** out_indices,
                                     size_t* out_count,
                                     cxpr_error* err) {
    size_t* indices = NULL;
    size_t count = 0u;
    bool found_child_binding = false;
    if (!program || child_index >= program->child_count || !out_indices || !out_count) {
        return false;
    }
    indices = (size_t*)calloc(flat_output_count ? flat_output_count : 1u, sizeof(*indices));
    if (!indices) goto oom;
    for (size_t root_i = 0u; root_i < program->binding_count; ++root_i) {
        const cxpr_model_compiled_binding* root = &program->bindings[root_i];
        bool materialized_record = false;
        if (!root->ast || cxpr_expr_ast_kind_of(root->ast) != CXPR_NODE_FUNCTION_CALL ||
            !cxpr_model_names_match(cxpr_expr_ast_call_name(root->ast),
                                    program->children[child_index].name)) {
            continue;
        }
        found_child_binding = true;
        for (size_t record_i = 0u; record_i < program->fused_record_count; ++record_i) {
            if (cxpr_model_names_match(
                    program->fused_records[record_i].name, root->name)) {
                materialized_record = true;
                break;
            }
        }
        for (size_t field_i = 0u; field_i < flat_output_count; ++field_i) {
            char needle[768];
            size_t root_prefix_len;
            bool used = false;
            if (!flat_outputs[field_i].name ||
                snprintf(needle, sizeof(needle), "%s.%s", root->name,
                         flat_outputs[field_i].name) < 0) {
                continue;
            }
            root_prefix_len = strlen(root->name) + 1u;
            for (size_t binding_i = 0u; binding_i < program->binding_count; ++binding_i) {
                char* source = cxpr_expr_ast_to_string(program->bindings[binding_i].ast);
                used = source && strstr(source, needle) != NULL;
                if (!used && source && flat_outputs[field_i].name) {
                    const char* dot = strchr(flat_outputs[field_i].name, '.');
                    while (dot && !used) {
                        size_t field_prefix_len = (size_t)(dot - flat_outputs[field_i].name);
                        size_t prefix_len = root_prefix_len + field_prefix_len;
                        if (prefix_len < sizeof(needle)) {
                            memcpy(needle, root->name, root_prefix_len - 1u);
                            needle[root_prefix_len - 1u] = '.';
                            memcpy(needle + root_prefix_len,
                                   flat_outputs[field_i].name, field_prefix_len);
                            needle[prefix_len] = '\0';
                            used = strstr(source, needle) != NULL;
                        }
                        dot = strchr(dot + 1, '.');
                    }
                }
                free(source);
                if (used) break;
            }
            if (!used && !materialized_record) continue;
            bool duplicate = false;
            for (size_t i = 0u; i < count; ++i) {
                if (indices[i] == field_i) duplicate = true;
            }
            if (!duplicate) indices[count++] = field_i;
        }
    }
    if (!found_child_binding || count == 0u) {
        for (size_t i = 0u; i < flat_output_count; ++i) indices[count++] = i;
    }
    *out_indices = indices;
    *out_count = count;
    return true;
oom:
    free(indices);
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
    return false;
}

void cxpr_model_c_reserve(cxpr_model_c_buf* b, size_t extra) {
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

void cxpr_model_c_puts(cxpr_model_c_buf* b, const char* s) {
    size_t n;
    if (!b || !s) return;
    n = strlen(s);
    cxpr_model_c_reserve(b, n);
    if (b->oom) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void cxpr_model_c_printf(cxpr_model_c_buf* b, const char* fmt, ...) {
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

void cxpr_model_c_format_double(char* out, size_t out_size, double value) {
    if (isfinite(value) && floor(value) == value) {
        snprintf(out, out_size, "%.1f", value);
    } else {
        char shortest[64];
        snprintf(shortest, sizeof(shortest), "%.15g", value);
        if (strtod(shortest, NULL) == value) {
            snprintf(out, out_size, "%s", shortest);
        } else {
            snprintf(out, out_size, "%.17g", value);
        }
    }
}

char* cxpr_model_c_safe_name(const char* name) {
    size_t len = name ? strlen(name) : 0u;
    static const char* const keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
        "union", "unsigned", "void", "volatile", "while", "_Alignas",
        "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
        "_Noreturn", "_Static_assert", "_Thread_local",
    };
    bool keyword = false;
    char* out;
    size_t pos = 0u;
    for (size_t i = 0u; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (name && strcmp(name, keywords[i]) == 0) {
            keyword = true;
            break;
        }
    }
    out = (char*)malloc(len + 4u);
    if (!out) return NULL;
    if (keyword || len == 0u || (name[0] >= '0' && name[0] <= '9')) {
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

char* cxpr_model_c_function_name(const char* name) {
    char raw[512];
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "cxpr_fn_%s", name) >= sizeof(raw)) return NULL;
    return cxpr_model_c_safe_name(raw);
}

char* cxpr_model_c_scoped_function_name(const char* scope, const char* name) {
    char raw[768];
    if (!scope || !scope[0]) return cxpr_model_c_function_name(name);
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "cxpr_fn_%s_%s", scope, name) >= sizeof(raw)) {
        return NULL;
    }
    return cxpr_model_c_safe_name(raw);
}

char* cxpr_model_c_prefixed_name(const char* prefix, const char* name) {
    char raw[512];
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "%s%s", prefix ? prefix : "", name) >= sizeof(raw)) {
        return NULL;
    }
    return cxpr_model_c_safe_name(raw);
}

unsigned long cxpr_model_c_name_hash(const char* s) {
    unsigned long h = 5381u;
    if (!s) return h;
    while (*s) h = ((h << 5u) + h) ^ (unsigned char)*s++;
    return h;
}

char* cxpr_model_c_child_tick_name(const char* function_prefix, size_t child_index) {
    char raw[128];
    snprintf(raw, sizeof(raw), "cxpr_c%08lx_%zu_tick",
             cxpr_model_c_name_hash(function_prefix) & 0xfffffffful,
             child_index);
    return cxpr_model_c_safe_name(raw);
}

char* cxpr_model_c_child_field_name(const char* function_prefix,
                                    size_t child_index,
                                    size_t field_index) {
    char raw[128];
    snprintf(raw, sizeof(raw), "cxpr_c%08lx_%zu_f%zu",
             cxpr_model_c_name_hash(function_prefix) & 0xfffffffful,
             child_index,
             field_index);
    return cxpr_model_c_safe_name(raw);
}

bool cxpr_model_c_is_power_of_two(size_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

bool cxpr_model_c_history_use_shift(size_t depth) {
    return depth <= 4u;
}

size_t cxpr_model_c_history_capacity(size_t depth) {
    size_t capacity = 1u;
    if (cxpr_model_c_history_use_shift(depth)) return depth;
    while (capacity < depth && capacity <= ((size_t)-1) / 2u) capacity *= 2u;
    return capacity < depth ? depth : capacity;
}
