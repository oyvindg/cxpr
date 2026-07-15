#include "model/internal.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void cxpr_model_metadatas_free(cxpr_model_metadata* metadatas, size_t count) {
    if (!metadatas) return;
    for (size_t i = 0u; i < count; ++i) {
        free(metadatas[i].name);
        free(metadatas[i].body);
        free(metadatas[i].target_name);
    }
    free(metadatas);
}

static void cxpr_model_host_blocks_free(cxpr_model_host_block* blocks, size_t count) {
    if (!blocks) return;
    for (size_t i = 0u; i < count; ++i) {
        free(blocks[i].kind);
        free(blocks[i].name);
        free(blocks[i].body);
        for (size_t j = 0u; j < blocks[i].field_count; ++j) {
            free(blocks[i].fields[j].key);
            free(blocks[i].fields[j].value);
        }
        free(blocks[i].fields);
        cxpr_model_host_blocks_free(blocks[i].children, blocks[i].child_count);
    }
    free(blocks);
}

void cxpr_model_record_fields_free(cxpr_model_record_field* fields, size_t count) {
    if (!fields) return;
    for (size_t i = 0; i < count; ++i) {
        free(fields[i].name);
        free(fields[i].source);
        cxpr_ast_free(fields[i].expr);
    }
    free(fields);
}

void cxpr_model_record_function_clear(cxpr_model_record_function* fn) {
    if (!fn) return;
    free(fn->name);
    for (size_t i = 0; i < fn->param_count; ++i) free(fn->params[i]);
    free(fn->params);
    cxpr_model_record_fields_free(fn->fields, fn->field_count);
    fn->name = NULL;
    fn->params = NULL;
    fn->param_count = 0u;
    fn->fields = NULL;
    fn->field_count = 0u;
}

void cxpr_model_free(cxpr_model* model) {
    if (!model) return;
    free(model->name);
    for (size_t i = 0; i < model->use_count; ++i) {
        free(model->uses[i]);
        free(model->use_aliases ? model->use_aliases[i] : NULL);
    }
    free(model->uses);
    free(model->use_aliases);
    for (size_t i = 0; i < model->function_count; ++i) free(model->functions[i]);
    free(model->functions);
    for (size_t i = 0; i < model->record_function_count; ++i) {
        cxpr_model_record_function_clear(&model->record_functions[i]);
    }
    free(model->record_functions);
    for (size_t i = 0; i < model->input_count; ++i) free(model->inputs[i]);
    free(model->inputs);
    for (size_t i = 0; i < model->constant_count; ++i) {
        free(model->constants[i].name);
        free(model->constants[i].source);
        cxpr_ast_free(model->constants[i].expr);
    }
    free(model->constants);
    for (size_t i = 0; i < model->binding_count; ++i) {
        free(model->bindings[i].name);
        free(model->bindings[i].source);
        cxpr_ast_free(model->bindings[i].expr);
    }
    free(model->bindings);
    for (size_t i = 0; i < model->output_count; ++i) free(model->outputs[i]);
    free(model->outputs);
    for (size_t i = 0; i < model->anonymous_output_count; ++i) {
        free(model->anonymous_outputs[i].source);
        cxpr_ast_free(model->anonymous_outputs[i].expr);
    }
    free(model->anonymous_outputs);
    cxpr_model_metadatas_free(model->metadatas, model->metadata_count);
    cxpr_model_host_blocks_free(model->host_blocks, model->host_block_count);
    free(model);
}

const char* cxpr_model_name(const cxpr_model* model) {
    return model ? model->name : NULL;
}

size_t cxpr_model_use_count(const cxpr_model* model) {
    return model ? model->use_count : 0;
}

const char* cxpr_model_use(const cxpr_model* model, size_t index) {
    return model && index < model->use_count ? model->uses[index] : NULL;
}

const char* cxpr_model_use_alias(const cxpr_model* model, size_t index) {
    return model && index < model->use_count && model->use_aliases
               ? model->use_aliases[index]
               : NULL;
}

size_t cxpr_model_input_count(const cxpr_model* model) {
    return model ? model->input_count : 0;
}

const char* cxpr_model_input(const cxpr_model* model, size_t index) {
    return model && index < model->input_count ? model->inputs[index] : NULL;
}

size_t cxpr_model_constant_count(const cxpr_model* model) {
    return model ? model->constant_count : 0;
}

const char* cxpr_model_constant_name(const cxpr_model* model, size_t index) {
    return model && index < model->constant_count ? model->constants[index].name : NULL;
}

const cxpr_ast* cxpr_model_constant_expr(const cxpr_model* model, size_t index) {
    return model && index < model->constant_count ? model->constants[index].expr : NULL;
}

size_t cxpr_model_binding_count(const cxpr_model* model) {
    return model ? model->binding_count : 0;
}

cxpr_model_binding_kind cxpr_model_binding_kind_at(const cxpr_model* model, size_t index) {
    if (!model || index >= model->binding_count) return CXPR_MODEL_BINDING_EXPR;
    return model->bindings[index].kind;
}

const char* cxpr_model_binding_name(const cxpr_model* model, size_t index) {
    return model && index < model->binding_count ? model->bindings[index].name : NULL;
}

const cxpr_ast* cxpr_model_binding_expr(const cxpr_model* model, size_t index) {
    return model && index < model->binding_count ? model->bindings[index].expr : NULL;
}

size_t cxpr_model_output_count(const cxpr_model* model) {
    return model ? model->output_count : 0;
}

const char* cxpr_model_output(const cxpr_model* model, size_t index) {
    return model && index < model->output_count ? model->outputs[index] : NULL;
}

size_t cxpr_model_metadata_count(const cxpr_model* model) {
    return model ? model->metadata_count : 0u;
}

const char* cxpr_model_metadata_name(const cxpr_model* model, size_t index) {
    return model && index < model->metadata_count ? model->metadatas[index].name : NULL;
}

const char* cxpr_model_metadata_body(const cxpr_model* model, size_t index) {
    return model && index < model->metadata_count ? model->metadatas[index].body : NULL;
}

cxpr_model_metadata_target_kind cxpr_model_metadata_target_kind_at(
    const cxpr_model* model,
    size_t index) {
    if (!model || index >= model->metadata_count) {
        return CXPR_MODEL_METADATA_TARGET_BINDING;
    }
    return model->metadatas[index].target_kind;
}

const char* cxpr_model_metadata_target_name(const cxpr_model* model, size_t index) {
    return model && index < model->metadata_count ? model->metadatas[index].target_name : NULL;
}

static const char* cxpr_metadata_skip_ws(const char* cursor) {
    while (cursor && *cursor && isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static const char* cxpr_metadata_field_value_end(const char* cursor) {
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    char quote = '\0';

    while (cursor && *cursor) {
        if (quote) {
            if (*cursor == '\\' && cursor[1]) {
                cursor += 2;
                continue;
            }
            if (*cursor == quote) quote = '\0';
            cursor++;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'') {
            quote = *cursor++;
            continue;
        }
        if (*cursor == '(') paren_depth++;
        else if (*cursor == ')' && paren_depth > 0) paren_depth--;
        else if (*cursor == '[') bracket_depth++;
        else if (*cursor == ']' && bracket_depth > 0) bracket_depth--;
        else if (*cursor == '{') brace_depth++;
        else if (*cursor == '}' && brace_depth > 0) brace_depth--;
        else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
                 (*cursor == ',' || *cursor == '\n' || *cursor == '}')) {
            return cursor;
        }
        cursor++;
    }
    return cursor;
}

static bool cxpr_metadata_key_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_';
}

const char* cxpr_model_metadata_field_value(const cxpr_model* model,
                                            size_t index,
                                            const char* key) {
    const char* cursor;
    size_t key_len;

    if (!model || index >= model->metadata_count || !key) return NULL;
    cursor = model->metadatas[index].body;
    key_len = strlen(key);
    while (cursor && *cursor) {
        cursor = cxpr_metadata_skip_ws(cursor);
        if (!*cursor) break;
        if (strncmp(cursor, key, key_len) == 0 &&
            !cxpr_metadata_key_char(cursor[key_len])) {
            const char* probe = cxpr_metadata_skip_ws(cursor + key_len);
            if (*probe == '=') return cxpr_metadata_skip_ws(probe + 1);
        }
        {
            const char* line = strchr(cursor, '\n');
            const char* comma = strchr(cursor, ',');
            if (comma && (!line || comma < line)) cursor = comma + 1;
            else if (line) cursor = line + 1;
            else break;
        }
    }
    return NULL;
}

static char* cxpr_metadata_dup_trimmed_value(const char* start, const char* end) {
    char* out;
    size_t len;
    if (!start || !end || end < start) return NULL;
    while (start < end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    if (end > start && ((*start == '"' && end[-1] == '"') ||
                        (*start == '\'' && end[-1] == '\''))) {
        start++;
        end--;
    }
    len = (size_t)(end - start);
    out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    if (len > 0u) memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static bool cxpr_metadata_parse_double_token(const char* start,
                                             const char* end,
                                             double* out) {
    char* owned;
    char* parse_end = NULL;
    double value;

    if (!start || !end || !out) return false;
    owned = cxpr_metadata_dup_trimmed_value(start, end);
    if (!owned) return false;
    value = strtod(owned, &parse_end);
    if (parse_end == owned) {
        free(owned);
        return false;
    }
    while (parse_end && *parse_end && isspace((unsigned char)*parse_end)) parse_end++;
    if (parse_end && *parse_end != '\0') {
        free(owned);
        return false;
    }
    *out = value;
    free(owned);
    return true;
}

bool cxpr_model_metadata_field_number_list(const cxpr_model* model,
                                           size_t index,
                                           const char* key,
                                           double** out_values,
                                           size_t* out_count) {
    const char* cursor;
    const char* end;
    double* values = NULL;
    size_t count = 0u;

    if (out_values) *out_values = NULL;
    if (out_count) *out_count = 0u;
    if (!out_values || !out_count) return false;
    cursor = cxpr_model_metadata_field_value(model, index, key);
    if (!cursor) return false;
    end = cxpr_metadata_field_value_end(cursor);
    cursor = cxpr_metadata_skip_ws(cursor);
    if (cursor < end && *cursor == '[') cursor++;
    while (cursor < end) {
        const char* item_end;
        double value;
        double* grown;

        while (cursor < end &&
               (isspace((unsigned char)*cursor) || *cursor == ',' || *cursor == ']')) {
            cursor++;
        }
        if (cursor >= end) break;
        item_end = cursor;
        while (item_end < end && *item_end != ',' && *item_end != ']') item_end++;
        if (!cxpr_metadata_parse_double_token(cursor, item_end, &value)) {
            free(values);
            return false;
        }
        grown = (double*)realloc(values, (count + 1u) * sizeof(*values));
        if (!grown) {
            free(values);
            return false;
        }
        values = grown;
        values[count++] = value;
        cursor = item_end;
    }
    *out_values = values;
    *out_count = count;
    return true;
}

size_t cxpr_model_host_block_count(const cxpr_model* model) {
    return model ? model->host_block_count : 0u;
}

const char* cxpr_model_host_block_kind(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? model->host_blocks[index].kind : NULL;
}

const char* cxpr_model_host_block_name(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? model->host_blocks[index].name : NULL;
}

const char* cxpr_model_host_block_body(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? model->host_blocks[index].body : NULL;
}

const cxpr_model_host_block* cxpr_model_host_block_at(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? &model->host_blocks[index] : NULL;
}

const char* cxpr_host_block_kind(const cxpr_model_host_block* block) {
    return block ? block->kind : NULL;
}

const char* cxpr_host_block_name(const cxpr_model_host_block* block) {
    return block ? block->name : NULL;
}

const char* cxpr_host_block_body(const cxpr_model_host_block* block) {
    return block ? block->body : NULL;
}

size_t cxpr_host_block_field_count(const cxpr_model_host_block* block) {
    return block ? block->field_count : 0u;
}

const char* cxpr_host_block_field_key(const cxpr_model_host_block* block, size_t index) {
    return block && index < block->field_count ? block->fields[index].key : NULL;
}

const char* cxpr_host_block_field_value(const cxpr_model_host_block* block, size_t index) {
    return block && index < block->field_count ? block->fields[index].value : NULL;
}

const char* cxpr_host_block_field_value_by_key(const cxpr_model_host_block* block,
                                               const char* key) {
    if (!block || !key) return NULL;
    for (size_t i = 0u; i < block->field_count; ++i) {
        if (block->fields[i].key && strcmp(block->fields[i].key, key) == 0) {
            return block->fields[i].value;
        }
    }
    return NULL;
}

size_t cxpr_host_block_child_count(const cxpr_model_host_block* block) {
    return block ? block->child_count : 0u;
}

const cxpr_model_host_block* cxpr_host_block_child(const cxpr_model_host_block* block,
                                                   size_t index) {
    return block && index < block->child_count ? &block->children[index] : NULL;
}
