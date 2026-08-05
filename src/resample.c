#include <cxpr/resample.h>
#include "ast/internal.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void set_error(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (!err) return;
    *err = (cxpr_error){0}; err->code = code; err->message = message;
}

static void locate_error(cxpr_error* err, const cxpr_expr_ast* ast) {
    cxpr_source_span span;
    if (!err || !cxpr_expr_ast_source_span(ast, &span)) return;
    err->position = span.start.offset;
    err->line = span.start.line;
    err->column = span.start.column;
}

static bool parse_unit(const char* unit, int64_t* factor, const char** canonical) {
    static const struct { const char* name; int64_t factor; } units[] = {
        {"ns", INT64_C(1)}, {"us", INT64_C(1000)}, {"ms", INT64_C(1000000)},
        {"s", INT64_C(1000000000)}, {"m", INT64_C(60000000000)},
        {"h", INT64_C(3600000000000)}, {"d", INT64_C(86400000000000)}
    };
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); ++i) {
        if (strcmp(unit, units[i].name) == 0) {
            *factor = units[i].factor; *canonical = units[i].name; return true;
        }
    }
    return false;
}

bool cxpr_parse_fixed_duration(const char* text, cxpr_resample_interval* out,
                               cxpr_error* err) {
    uint64_t count = 0u; size_t offset = 0u; int64_t factor;
    const char* unit; int written;
    if (err) *err = (cxpr_error){0};
    if (!text || !out || !text[0]) {
        set_error(err, CXPR_ERR_SYNTAX, "Fixed duration must be a positive integer and unit");
        return false;
    }
    while (text[offset] >= '0' && text[offset] <= '9') {
        unsigned digit = (unsigned)(text[offset] - '0');
        if (count > (UINT64_MAX - digit) / 10u) {
            set_error(err, CXPR_ERR_SYNTAX, "Fixed duration overflows"); return false;
        }
        count = count * 10u + digit; ++offset;
    }
    if (!offset || !count) {
        set_error(err, CXPR_ERR_SYNTAX, "Fixed duration must be greater than zero"); return false;
    }
    if (!parse_unit(text + offset, &factor, &unit)) {
        set_error(err, CXPR_ERR_SYNTAX,
                  "Unsupported fixed-duration unit; expected ns, us, ms, s, m, h, or d");
        return false;
    }
    if (count > (uint64_t)INT64_MAX / (uint64_t)factor) {
        set_error(err, CXPR_ERR_SYNTAX, "Fixed duration overflows nanoseconds"); return false;
    }
    out->duration_ns = (int64_t)(count * (uint64_t)factor);
    written = snprintf(out->canonical, sizeof(out->canonical), "%" PRIu64 "%s", count, unit);
    if (written < 0 || (size_t)written >= sizeof(out->canonical)) {
        set_error(err, CXPR_ERR_SYNTAX, "Canonical fixed duration is too long"); return false;
    }
    return true;
}

bool cxpr_resample_call_parse(const cxpr_expr_ast* ast, cxpr_resample_call* out,
                              cxpr_error* err) {
    const cxpr_expr_ast* interval; const char* first_name; const char* second_name;
    if (err) *err = (cxpr_error){0};
    if (!ast || !out || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL ||
        !cxpr_expr_ast_call_name(ast) || strcmp(cxpr_expr_ast_call_name(ast), "resample") != 0) {
        set_error(err, CXPR_ERR_TYPE_MISMATCH, "Expected a resample call"); return false;
    }
    if (cxpr_expr_ast_call_arg_count(ast) != 2u) {
        set_error(err, CXPR_ERR_WRONG_ARITY, "resample expects source and every"); return false;
    }
    first_name = cxpr_expr_ast_call_arg_name(ast, 0u);
    second_name = cxpr_expr_ast_call_arg_name(ast, 1u);
    if (first_name || (second_name && strcmp(second_name, "every") != 0)) {
        set_error(err, CXPR_ERR_SYNTAX,
                  "resample accepts a positional source and positional or named every argument");
        return false;
    }
    interval = cxpr_expr_ast_call_arg(ast, 1u);
    if (cxpr_expr_ast_kind_of(interval) != CXPR_NODE_STRING) {
        set_error(err, CXPR_ERR_TYPE_MISMATCH,
                  "resample every must be a compile-time string literal");
        locate_error(err, interval); return false;
    }
    if (!cxpr_parse_fixed_duration(cxpr_expr_ast_string_value(interval), &out->every, err)) {
        locate_error(err, interval); return false;
    }
    out->source = cxpr_expr_ast_call_arg(ast, 0u);
    switch (cxpr_expr_ast_kind_of(out->source)) {
        case CXPR_NODE_IDENTIFIER:
        case CXPR_NODE_FIELD_ACCESS:
        case CXPR_NODE_CHAIN_ACCESS:
        case CXPR_NODE_PRODUCER_ACCESS:
        case CXPR_NODE_FUNCTION_CALL:
            break;
        default:
            set_error(err, CXPR_ERR_TYPE_MISMATCH,
                      "resample source must be a provider/source-capable expression");
            locate_error(err, out->source);
            return false;
    }
    return true;
}

bool cxpr_resample_validate_ast(const cxpr_expr_ast* ast, cxpr_error* err) {
    if (!ast) return true;
    if (ast->type == CXPR_NODE_FUNCTION_CALL) {
        if (strcmp(ast->data.function_call.name, "resample") == 0) {
            cxpr_resample_call parsed;
            if (!cxpr_resample_call_parse(ast, &parsed, err)) return false;
        }
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i)
            if (!cxpr_resample_validate_ast(ast->data.function_call.args[i], err)) return false;
    } else if (ast->type == CXPR_NODE_PRODUCER_ACCESS) {
        for (size_t i = 0u; i < ast->data.producer_access.argc; ++i)
            if (!cxpr_resample_validate_ast(ast->data.producer_access.args[i], err)) return false;
    } else if (ast->type == CXPR_NODE_BINARY_OP) {
        return cxpr_resample_validate_ast(ast->data.binary_op.left, err) &&
               cxpr_resample_validate_ast(ast->data.binary_op.right, err);
    } else if (ast->type == CXPR_NODE_UNARY_OP) {
        return cxpr_resample_validate_ast(ast->data.unary_op.operand, err);
    } else if (ast->type == CXPR_NODE_INDEX) {
        if (ast->data.index.target &&
            ast->data.index.target->type == CXPR_NODE_FUNCTION_CALL &&
            ast->data.index.target->data.function_call.name &&
            strcmp(ast->data.index.target->data.function_call.name, "resample") == 0) {
            const cxpr_expr_ast* index = ast->data.index.index;
            double value = 0.0;
            bool literal = index && index->type == CXPR_NODE_NUMBER;
            if (literal) value = index->data.number.value;
            if (index && index->type == CXPR_NODE_UNARY_OP &&
                index->data.unary_op.operand &&
                index->data.unary_op.operand->type == CXPR_NODE_NUMBER) {
                literal = true;
                value = -index->data.unary_op.operand->data.number.value;
            }
            if (literal && (!isfinite(value) || value < 0.0 || floor(value) != value ||
                            value > (double)UINT32_MAX)) {
                set_error(err, CXPR_ERR_INVALID_INDEX,
                          "resample index must be a finite non-negative integer");
                return false;
            }
        }
        return cxpr_resample_validate_ast(ast->data.index.target, err) &&
               cxpr_resample_validate_ast(ast->data.index.index, err);
    } else if (ast->type == CXPR_NODE_TERNARY) {
        return cxpr_resample_validate_ast(ast->data.ternary.condition, err) &&
               cxpr_resample_validate_ast(ast->data.ternary.true_branch, err) &&
               cxpr_resample_validate_ast(ast->data.ternary.false_branch, err);
    } else if (ast->type == CXPR_NODE_ARRAY) {
        for (size_t i = 0u; i < ast->data.array.count; ++i)
            if (!cxpr_resample_validate_ast(ast->data.array.elements[i], err)) return false;
    } else if (ast->type == CXPR_NODE_RECORD) {
        for (size_t i = 0u; i < ast->data.record.field_count; ++i)
            if (!cxpr_resample_validate_ast(ast->data.record.field_values[i], err)) return false;
    } else if (ast->type == CXPR_NODE_FIELD_ACCESS && ast->data.field_access.base) {
        return cxpr_resample_validate_ast(ast->data.field_access.base, err);
    }
    return true;
}
