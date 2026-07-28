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

static char* cxpr_dup_range(const char* start, size_t len) {
    char* out;
    if (!start) return NULL;
    out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    if (len > 0u) memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char* cxpr_dup_host_block_value(const char* start, size_t len) {
    const char* end;
    if (!start) return NULL;
    while (len > 0u && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    end = start + len;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    if (end > start && ((*start == '"' && end[-1] == '"') || (*start == '\'' && end[-1] == '\''))) {
        start++;
        end--;
    }
    return cxpr_dup_range(start, (size_t)(end - start));
}

static bool cxpr_host_block_string_list_push(char*** values, size_t* count, char* value) {
    char** grown;
    if (!values || !count || !value) return false;
    grown = (char**)realloc(*values, (*count + 1u) * sizeof(**values));
    if (!grown) return false;
    *values = grown;
    (*values)[*count] = value;
    *count += 1u;
    return true;
}

static bool cxpr_parse_host_block_string_list(const char* raw,
                                              char*** out_values,
                                              size_t* out_count) {
    const char* cursor;
    char** values = NULL;
    size_t count = 0u;

    if (out_values) *out_values = NULL;
    if (out_count) *out_count = 0u;
    if (!raw || !out_values || !out_count) return false;

    cursor = raw;
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '[') cursor++;
    while (*cursor) {
        char* value;
        const char* end;

        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) cursor++;
        if (*cursor == ']') break;
        if (*cursor == '"' || *cursor == '\'') {
            char quote = *cursor++;
            end = strchr(cursor, quote);
            if (!end) goto fail;
            value = cxpr_dup_range(cursor, (size_t)(end - cursor));
            cursor = end + 1;
        } else {
            end = cursor;
            while (*end && *end != ',' && *end != ']') end++;
            value = cxpr_dup_host_block_value(cursor, (size_t)(end - cursor));
            cursor = end;
        }
        if (!value || value[0] == '\0' ||
            !cxpr_host_block_string_list_push(&values, &count, value)) {
            free(value);
            goto fail;
        }
    }

    *out_values = values;
    *out_count = count;
    return true;

fail:
    for (size_t i = 0u; i < count; ++i) free(values[i]);
    free(values);
    return false;
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
    free(model->use_spans);
    free(model->use_has_spans);
    for (size_t i = 0; i < model->function_count; ++i) free(model->functions[i]);
    free(model->functions);
    for (size_t i = 0; i < model->record_function_count; ++i) {
        cxpr_model_record_function_clear(&model->record_functions[i]);
    }
    free(model->record_functions);
    for (size_t i = 0; i < model->input_count; ++i) free(model->inputs[i]);
    free(model->inputs);
    free(model->input_spans);
    free(model->input_has_spans);
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
    free(model->output_spans);
    free(model->output_has_spans);
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

bool cxpr_model_name_source_span(const cxpr_model* model, cxpr_source_span* out_span) {
    if (!model || !model->has_name_span) return false;
    if (out_span) *out_span = model->name_span;
    return true;
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

bool cxpr_model_use_source_span(const cxpr_model* model,
                                size_t index,
                                cxpr_source_span* out_span) {
    if (!model || index >= model->use_count ||
        !model->use_has_spans || !model->use_has_spans[index]) {
        return false;
    }
    if (out_span) *out_span = model->use_spans[index];
    return true;
}

size_t cxpr_model_function_count(const cxpr_model* model) {
    return model ? model->function_count : 0u;
}

const char* cxpr_model_function_source(const cxpr_model* model, size_t index) {
    return model && index < model->function_count ? model->functions[index] : NULL;
}

bool cxpr_model_function_declaration_source(const cxpr_model* model,
                                            size_t index,
                                            char** out_source) {
    const char* source = cxpr_model_function_source(model, index);
    const char* separator;
    const char* body;
    size_t signature_len;
    size_t separator_len;
    size_t len;
    char* out;

    if (out_source) *out_source = NULL;
    if (!source || !out_source) return false;
    separator = strstr(source, "=>");
    separator_len = 2u;
    if (!separator) {
        separator = strchr(source, '=');
        separator_len = 1u;
    }
    if (!separator) return false;
    signature_len = (size_t)(separator - source);
    while (signature_len > 0u && isspace((unsigned char)source[signature_len - 1u])) {
        signature_len--;
    }
    body = separator + separator_len;
    while (*body && isspace((unsigned char)*body)) body++;
    len = strlen("fn ") + signature_len + strlen(" = ") + strlen(body);
    out = (char*)malloc(len + 1u);
    if (!out) return false;
    memcpy(out, "fn ", strlen("fn "));
    memcpy(out + strlen("fn "), source, signature_len);
    memcpy(out + strlen("fn ") + signature_len, " = ", strlen(" = "));
    memcpy(out + strlen("fn ") + signature_len + strlen(" = "), body, strlen(body));
    out[len] = '\0';
    *out_source = out;
    return true;
}

size_t cxpr_model_input_count(const cxpr_model* model) {
    return model ? model->input_count : 0;
}

const char* cxpr_model_input(const cxpr_model* model, size_t index) {
    return model && index < model->input_count ? model->inputs[index] : NULL;
}

bool cxpr_model_input_source_span(const cxpr_model* model,
                                  size_t index,
                                  cxpr_source_span* out_span) {
    if (!model || index >= model->input_count ||
        !model->input_has_spans || !model->input_has_spans[index]) {
        return false;
    }
    if (out_span) *out_span = model->input_spans[index];
    return true;
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

bool cxpr_model_constant_is_call_param(const cxpr_model* model, size_t index) {
    return model && index < model->constant_count
               ? model->constants[index].is_call_param
               : false;
}

size_t cxpr_model_call_param_count(const cxpr_model* model) {
    size_t count = 0u;
    if (!model) return 0u;
    for (size_t i = 0u; i < model->constant_count; ++i) {
        if (model->constants[i].is_call_param) ++count;
    }
    return count;
}

bool cxpr_model_constant_source_span(const cxpr_model* model,
                                     size_t index,
                                     cxpr_source_span* out_span) {
    if (!model || index >= model->constant_count || !model->constants[index].has_span) {
        return false;
    }
    if (out_span) *out_span = model->constants[index].span;
    return true;
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

bool cxpr_model_binding_source_span(const cxpr_model* model,
                                    size_t index,
                                    cxpr_source_span* out_span) {
    if (!model || index >= model->binding_count || !model->bindings[index].has_span) {
        return false;
    }
    if (out_span) *out_span = model->bindings[index].span;
    return true;
}

size_t cxpr_model_output_count(const cxpr_model* model) {
    return model ? model->output_count : 0;
}

const char* cxpr_model_output(const cxpr_model* model, size_t index) {
    return model && index < model->output_count ? model->outputs[index] : NULL;
}

bool cxpr_model_output_source_span(const cxpr_model* model,
                                   size_t index,
                                   cxpr_source_span* out_span) {
    if (!model || index >= model->output_count ||
        !model->output_has_spans || !model->output_has_spans[index]) {
        return false;
    }
    if (out_span) *out_span = model->output_spans[index];
    return true;
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

bool cxpr_model_metadata_source_span(const cxpr_model* model,
                                     size_t index,
                                     cxpr_source_span* out_span) {
    if (!model || index >= model->metadata_count || !model->metadatas[index].has_span) {
        return false;
    }
    if (out_span) *out_span = model->metadatas[index].span;
    return true;
}

static const char* cxpr_metadata_skip_ws(const char* cursor) {
    while (cursor && *cursor && isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static const char* cxpr_metadata_field_value_end_bounded(const char* cursor,
                                                         const char* limit) {
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    char quote = '\0';

    while (cursor && *cursor && (!limit || cursor < limit)) {
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

static const char* cxpr_metadata_field_value_end(const char* cursor) {
    return cxpr_metadata_field_value_end_bounded(cursor, NULL);
}

static const char* cxpr_metadata_block_body_end(const char* cursor,
                                                const char* limit) {
    int brace_depth = 0;
    char quote = '\0';

    while (cursor && *cursor && (!limit || cursor < limit)) {
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
        if (*cursor == '{') {
            brace_depth++;
        } else if (*cursor == '}') {
            if (brace_depth == 0) return cursor;
            brace_depth--;
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

static const char* cxpr_model_metadata_find_field_value(const char* body,
                                                        const char* limit,
                                                        const char* key,
                                                        size_t key_len,
                                                        bool* out_block) {
    const char* cursor = body;

    if (out_block) *out_block = false;
    while (cursor && *cursor && (!limit || cursor < limit)) {
        const char* probe;
        cursor = cxpr_metadata_skip_ws(cursor);
        while (cursor && *cursor == ',') cursor = cxpr_metadata_skip_ws(cursor + 1);
        if (!cursor || !*cursor || (limit && cursor >= limit)) break;
        if (*cursor == '}') break;
        if (strncmp(cursor, key, key_len) == 0 &&
            !cxpr_metadata_key_char(cursor[key_len])) {
            probe = cxpr_metadata_skip_ws(cursor + key_len);
            if (*probe == '=') {
                return cxpr_metadata_skip_ws(probe + 1);
            }
            if (*probe == '{') {
                if (out_block) *out_block = true;
                return cxpr_metadata_skip_ws(probe + 1);
            }
        }
        cursor = cxpr_metadata_field_value_end_bounded(cursor, limit);
        if (cursor && *cursor && (!limit || cursor < limit)) cursor++;
    }
    return NULL;
}

const char* cxpr_model_metadata_field_value(const cxpr_model* model,
                                            size_t index,
                                            const char* key) {
    const char* body;
    const char* limit = NULL;
    const char* cursor;
    const char* segment = key;

    if (!model || index >= model->metadata_count || !key) return NULL;
    body = model->metadatas[index].body;
    while (segment && *segment) {
        const char* dot = strchr(segment, '.');
        const size_t segment_len = dot ? (size_t)(dot - segment) : strlen(segment);
        bool block = false;
        cursor = cxpr_model_metadata_find_field_value(
            body, limit, segment, segment_len, &block);
        if (!cursor) return NULL;
        if (!dot) return cursor;
        if (!block) return NULL;
        {
            const char* block_end = cxpr_metadata_block_body_end(cursor, limit);
            body = cursor;
            limit = block_end;
            segment = dot + 1;
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

bool cxpr_model_metadata_field_number(const cxpr_model* model,
                                      size_t index,
                                      const char* key,
                                      double* out_value) {
    const char* cursor;
    const char* end;

    if (!out_value) return false;
    cursor = cxpr_model_metadata_field_value(model, index, key);
    if (!cursor) return false;
    end = cxpr_metadata_field_value_end(cursor);
    return cxpr_metadata_parse_double_token(cursor, end, out_value);
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

const cxpr_model_host_block* cxpr_model_host_block_by_kind(const cxpr_model* model,
                                                           const char* kind) {
    if (!model || !kind) return NULL;
    for (size_t i = 0u; i < model->host_block_count; ++i) {
        const char* block_kind = model->host_blocks[i].kind;
        if (block_kind && strcmp(block_kind, kind) == 0) return &model->host_blocks[i];
    }
    return NULL;
}

bool cxpr_model_host_block_source_span(const cxpr_model* model,
                                       size_t index,
                                       cxpr_source_span* out_span) {
    if (!model || index >= model->host_block_count || !model->host_blocks[index].has_span) {
        return false;
    }
    if (out_span) *out_span = model->host_blocks[index].span;
    return true;
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

bool cxpr_host_block_source_span(const cxpr_model_host_block* block,
                                 cxpr_source_span* out_span) {
    if (!block || !block->has_span) return false;
    if (out_span) *out_span = block->span;
    return true;
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

bool cxpr_host_block_field_is_bare_flag(const cxpr_model_host_block* block,
                                        size_t index) {
    const char* value = cxpr_host_block_field_value(block, index);
    return value && strcmp(value, "true") == 0;
}

bool cxpr_host_block_field_string_by_key(const cxpr_model_host_block* block,
                                         const char* key,
                                         char** out_value) {
    const char* raw;
    if (out_value) *out_value = NULL;
    if (!block || !key || !out_value) return false;
    raw = cxpr_host_block_field_value_by_key(block, key);
    if (!raw) return false;
    *out_value = cxpr_dup_host_block_value(raw, strlen(raw));
    return *out_value != NULL;
}

bool cxpr_host_block_field_string_list_by_key(const cxpr_model_host_block* block,
                                              const char* key,
                                              char*** out_values,
                                              size_t* out_count) {
    const char* raw;
    if (out_values) *out_values = NULL;
    if (out_count) *out_count = 0u;
    if (!block || !key || !out_values || !out_count) return false;
    raw = cxpr_host_block_field_value_by_key(block, key);
    if (!raw) return false;
    return cxpr_parse_host_block_string_list(raw, out_values, out_count);
}

size_t cxpr_host_block_child_count(const cxpr_model_host_block* block) {
    return block ? block->child_count : 0u;
}

const cxpr_model_host_block* cxpr_host_block_child(const cxpr_model_host_block* block,
                                                   size_t index) {
    return block && index < block->child_count ? &block->children[index] : NULL;
}

const cxpr_model_host_block* cxpr_host_block_child_by_kind(
    const cxpr_model_host_block* block,
    const char* kind) {
    if (!block || !kind) return NULL;
    for (size_t i = 0u; i < block->child_count; ++i) {
        const char* child_kind = block->children[i].kind;
        if (child_kind && strcmp(child_kind, kind) == 0) return &block->children[i];
    }
    return NULL;
}
