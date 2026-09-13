#include "eval/snapshot/internal.h"

#include <math.h>
#include <stdlib.h>

bool cxpr_snapshot_json_string(FILE* out, const char* text) {
    const unsigned char* p = (const unsigned char*)(text ? text : "");

    if (fputc('"', out) == EOF) return false;
    while (*p) {
        switch (*p) {
            case '"':
                if (fputs("\\\"", out) == EOF) return false;
                break;
            case '\\':
                if (fputs("\\\\", out) == EOF) return false;
                break;
            case '\n':
                if (fputs("\\n", out) == EOF) return false;
                break;
            case '\r':
                if (fputs("\\r", out) == EOF) return false;
                break;
            case '\t':
                if (fputs("\\t", out) == EOF) return false;
                break;
            default:
                if (*p < 0x20u) {
                    if (fprintf(out, "\\u%04x", (unsigned int)*p) < 0) return false;
                } else if (fputc((int)*p, out) == EOF) {
                    return false;
                }
                break;
        }
        ++p;
    }
    return fputc('"', out) != EOF;
}

static const char* cxpr_snapshot_value_type_name(cxpr_value_type type) {
    switch (type) {
        case CXPR_VALUE_NUMBER: return "number";
        case CXPR_VALUE_BOOL: return "bool";
        case CXPR_VALUE_STRUCT: return "struct";
        case CXPR_VALUE_STRING: return "string";
        case CXPR_VALUE_NULL: return "null";
        case CXPR_VALUE_TIMESTAMP: return "timestamp";
        case CXPR_VALUE_DURATION: return "duration";
        case CXPR_VALUE_ARRAY: return "array";
        default: return "unknown";
    }
}

static bool cxpr_snapshot_write_value_json(FILE* out, const cxpr_value* value);

static bool cxpr_snapshot_write_number_json(FILE* out, double value) {
    if (isnan(value)) return fputs("\"special\": \"nan\"", out) != EOF;
    if (isinf(value)) {
        return fprintf(out, "\"special\": \"%s\"", value < 0.0 ? "-inf" : "inf") >= 0;
    }
    return fprintf(out, "\"number\": %.17g", value) >= 0;
}

static bool cxpr_snapshot_write_value_json(FILE* out, const cxpr_value* value) {
    if (!out) return false;
    if (!value) return fputs("null", out) != EOF;

    if (fputs("{ \"type\": ", out) == EOF) return false;
    if (!cxpr_snapshot_json_string(out, cxpr_snapshot_value_type_name(value->type))) return false;

    switch (value->type) {
        case CXPR_VALUE_NUMBER:
            if (fputs(", ", out) == EOF) return false;
            if (!cxpr_snapshot_write_number_json(out, value->d)) return false;
            break;
        case CXPR_VALUE_BOOL:
            if (fprintf(out, ", \"bool\": %s", value->b ? "true" : "false") < 0) return false;
            break;
        case CXPR_VALUE_STRING:
            if (fputs(", \"string\": ", out) == EOF) return false;
            if (!cxpr_snapshot_json_string(out, value->str)) return false;
            break;
        case CXPR_VALUE_NULL:
            if (fputs(", \"is_null\": true", out) == EOF) return false;
            break;
        case CXPR_VALUE_TIMESTAMP:
            if (fprintf(out, ", \"unix_ns\": %lld", (long long)value->i64) < 0) return false;
            break;
        case CXPR_VALUE_DURATION:
            if (fprintf(out, ", \"nanoseconds\": %lld", (long long)value->i64) < 0) return false;
            break;
        case CXPR_VALUE_ARRAY:
            if (fputs(", \"items\": [", out) == EOF) return false;
            if (value->a) {
                for (size_t i = 0; i < value->a->count; ++i) {
                    if (i > 0u && fputs(", ", out) == EOF) return false;
                    if (!cxpr_snapshot_write_value_json(out, &value->a->values[i])) return false;
                }
            }
            if (fputs("]", out) == EOF) return false;
            break;
        case CXPR_VALUE_STRUCT:
            if (fputs(", \"fields\": {", out) == EOF) return false;
            if (value->s) {
                for (size_t i = 0; i < value->s->field_count; ++i) {
                    if (i > 0u && fputs(", ", out) == EOF) return false;
                    if (!cxpr_snapshot_json_string(out, value->s->field_names[i])) return false;
                    if (fputs(": ", out) == EOF) return false;
                    if (!cxpr_snapshot_write_value_json(out, &value->s->field_values[i])) return false;
                }
            }
            if (fputs("}", out) == EOF) return false;
            break;
        default:
            break;
    }

    return fputs(" }", out) != EOF;
}

bool cxpr_snapshot_write_optional_value_fields(FILE* out,
                                               const cxpr_value* value,
                                               int has_value) {
    if (fprintf(out, ", \"has_value\": %s, \"value_type\": ",
                has_value ? "true" : "false") < 0) {
        return false;
    }
    if (!cxpr_snapshot_json_string(out, has_value ? cxpr_snapshot_value_type_name(value->type) : "")) {
        return false;
    }
    if (fputs(", \"typed_value\": ", out) == EOF) return false;
    return has_value ? cxpr_snapshot_write_value_json(out, value) : fputs("null", out) != EOF;
}

static bool cxpr_snapshot_hooks_have_host(const cxpr_snapshot_json_hooks* hooks) {
    return hooks && ((hooks->host_name && hooks->host_name[0]) ||
                     (hooks->host_schema && hooks->host_schema[0]));
}

bool cxpr_snapshot_write_document_prefix(
    FILE* out,
    const char* schema,
    const cxpr_snapshot_json_hooks* hooks) {
    if (fputs("{\n  \"schema\": ", out) == EOF) return false;
    if (!cxpr_snapshot_json_string(out, schema)) return false;
    if (cxpr_snapshot_hooks_have_host(hooks)) {
        if (fputs(",\n  \"host\": {", out) == EOF) return false;
        if (hooks->host_name && hooks->host_name[0]) {
            if (fputs(" \"name\": ", out) == EOF) return false;
            if (!cxpr_snapshot_json_string(out, hooks->host_name)) return false;
            if (hooks->host_schema && hooks->host_schema[0] &&
                fputs(",", out) == EOF) return false;
        }
        if (hooks->host_schema && hooks->host_schema[0]) {
            if (fputs(" \"schema\": ", out) == EOF) return false;
            if (!cxpr_snapshot_json_string(out, hooks->host_schema)) return false;
        }
        if (fputs(" }", out) == EOF) return false;
    }
    return true;
}

bool cxpr_eval_snapshot_write_json_ex(const cxpr_eval_snapshot* snapshot,
                                      const cxpr_snapshot_json_hooks* hooks,
                                      FILE* out) {
    if (!snapshot || !out) return false;

    if (!cxpr_snapshot_write_document_prefix(out, "cxpr.eval_snapshot.v2", hooks)) return false;
    if (fputs(",\n  \"expression\": ", out) == EOF) return false;
    if (!cxpr_snapshot_json_string(out, snapshot->expression)) return false;
    if (fputs(",\n  \"resolved\": ", out) == EOF) return false;
    if (!cxpr_snapshot_json_string(out, snapshot->resolved)) return false;
    if (fprintf(out, ",\n  \"state\": \"%s\",\n  \"elements\": {\n    \"nodes\": [\n",
                cxpr_snapshot_state_name(snapshot->state)) < 0) return false;

    for (size_t i = 0; i < snapshot->node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot->nodes[i];
        if (i > 0u && fputs(",\n", out) == EOF) return false;
        if (fprintf(out, "      { \"data\": { \"id\": \"n%zu\", \"numeric_id\": %zu, ",
                    node->id, node->id) < 0) return false;
        if (fputs("\"role\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->role)) return false;
        if (fputs(", \"kind\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->kind)) return false;
        if (fputs(", \"label\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->label)) return false;
        if (fputs(", \"display_label\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->display_label)) return false;
        if (fputs(", \"source\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->source)) return false;
        if (fputs(", \"resolved\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->resolved)) return false;
        if (fputs(", \"value\": ", out) == EOF) return false;
        if (!cxpr_snapshot_json_string(out, node->value_text)) return false;
        if (!cxpr_snapshot_write_optional_value_fields(out, &node->value, node->has_value)) {
            return false;
        }
        if (fprintf(out, ", \"active\": %s, \"state\": \"%s\"",
                    node->active ? "true" : "false",
                    cxpr_snapshot_state_name(node->state)) < 0) return false;
        if (hooks && hooks->write_ast_node_host_json) {
            if (fputs(", \"host\": ", out) == EOF) return false;
            if (!hooks->write_ast_node_host_json(out, snapshot, i, hooks->userdata)) return false;
        }
        if (fputs(" } }", out) == EOF) return false;
    }

    if (fputs("\n    ],\n    \"edges\": [\n", out) == EOF) return false;
    {
        int first = 1;
        for (size_t i = 0; i < snapshot->node_count; ++i) {
            const cxpr_snapshot_node* node = &snapshot->nodes[i];
            if (!node->has_parent) continue;
            if (!first && fputs(",\n", out) == EOF) return false;
            first = 0;
            if (fprintf(out, "      { \"data\": { \"id\": \"e%zu\", \"source\": \"n%zu\", \"target\": \"n%zu\", \"role\": ",
                        node->id, node->parent_id, node->id) < 0) return false;
            if (!cxpr_snapshot_json_string(out, node->role)) return false;
            if (fputs(" } }", out) == EOF) return false;
        }
    }
    return fputs("\n    ]\n  }\n}\n", out) != EOF;
}

bool cxpr_eval_snapshot_write_json(const cxpr_eval_snapshot* snapshot, FILE* out) {
    return cxpr_eval_snapshot_write_json_ex(snapshot, NULL, out);
}
