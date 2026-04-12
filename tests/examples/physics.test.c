#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <cxpr/cxpr.h>
#include "../cxpr_test_internal.h"

#define EPSILON 1e-12

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_defaults(reg);

    cxpr_context_set(ctx, "mass", 2.0);
    cxpr_context_set(ctx, "velocity", 3.0);
    cxpr_context_set(ctx, "damping", 0.15);
    cxpr_context_set(ctx, "t", 1.25);
    cxpr_context_set(ctx, "omega", 2.5);
    cxpr_context_set(ctx, "acceleration", 7.0);
    cxpr_context_set(ctx, "temperature", 420.0);
    cxpr_context_set_param(ctx, "max_acceleration", 9.0);
    cxpr_context_set_param(ctx, "meltdown_limit", 500.0);

    const char* fields[] = {"x", "y", "vx", "vy"};
    double vals[] = {1.2, -0.5, 0.8, 1.1};
    cxpr_context_set_fields(ctx, "body", fields, vals, 4);

    cxpr_ast* energy = cxpr_parse(parser, "0.5 * mass * velocity^2", &err);
    cxpr_ast* speed = cxpr_parse(parser, "sqrt(body.vx^2 + body.vy^2)", &err);
    cxpr_ast* alarm = cxpr_parse(
        parser,
        "abs(acceleration) > $max_acceleration or temperature >= $meltdown_limit",
        &err
    );
    assert(energy);
    assert(speed);
    assert(alarm);

    assert(fabs(cxpr_test_eval_ast_number(energy, ctx, reg, &err) - 9.0) < EPSILON);
    assert(fabs(cxpr_test_eval_ast_number(speed, ctx, reg, &err) - sqrt(1.85)) < EPSILON);
    assert(cxpr_test_eval_ast_bool(alarm, ctx, reg, &err) == false);

    cxpr_context_set(ctx, "acceleration", 12.0);
    assert(cxpr_test_eval_ast_bool(alarm, ctx, reg, &err) == true);

    cxpr_ast_free(alarm);
    cxpr_ast_free(speed);
    cxpr_ast_free(energy);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);

    printf("  \342\234\223 physics example\n");
    return 0;
}
