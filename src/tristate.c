/**
 * @file tristate.c
 * @brief Kleene three-valued partial evaluation. See tristate.h for semantics.
 */

#include <cxpr/tristate.h>
#include <cxpr/token.h>

/* ---- known-ness of a value sub-expression ---------------------------- */

static bool cx_tri_known(const cxpr_expr_ast* e,
                         cxpr_tristate_leaf_fn fn, void* ud) {
    if (!e) return false;
    switch (cxpr_expr_ast_kind_of(e)) {
        case CXPR_NODE_NUMBER:
        case CXPR_NODE_INT64:
        case CXPR_NODE_BOOL:
        case CXPR_NODE_STRING:
            return true;
        case CXPR_NODE_BINARY_OP:
            return cx_tri_known(cxpr_expr_ast_binary_left(e), fn, ud) &&
                   cx_tri_known(cxpr_expr_ast_binary_right(e), fn, ud);
        case CXPR_NODE_UNARY_OP:
            return cx_tri_known(cxpr_expr_ast_unary_operand(e), fn, ud);
        case CXPR_NODE_INDEX:
            return cx_tri_known(cxpr_expr_ast_index_target(e), fn, ud) &&
                   cx_tri_known(cxpr_expr_ast_index_expression(e), fn, ud);
        case CXPR_NODE_TERNARY:
            return cx_tri_known(cxpr_expr_ast_ternary_condition(e), fn, ud) &&
                   cx_tri_known(cxpr_expr_ast_ternary_true(e), fn, ud) &&
                   cx_tri_known(cxpr_expr_ast_ternary_false(e), fn, ud);
        case CXPR_NODE_IDENTIFIER:
        case CXPR_NODE_VARIABLE:
        case CXPR_NODE_FIELD_ACCESS:
        case CXPR_NODE_CHAIN_ACCESS:
        case CXPR_NODE_PRODUCER_ACCESS:
        case CXPR_NODE_FUNCTION_CALL:
            return fn && fn(e, ud) == CXPR_TRISTATE_KNOWN;
        default:
            return false;
    }
}

/* ---- folding boolean constructors ------------------------------------ */

static bool cx_tri_bool(const cxpr_expr_ast* e, bool* out) {
    if (e && cxpr_expr_ast_kind_of(e) == CXPR_NODE_BOOL) {
        *out = cxpr_expr_ast_bool_value(e);
        return true;
    }
    return false;
}

static cxpr_expr_ast* cx_tri_not(cxpr_expr_ast* a) {
    bool v;
    if (cx_tri_bool(a, &v)) {
        cxpr_expr_ast_free(a);
        return cxpr_expr_ast_bool_new(!v);
    }
    return cxpr_expr_ast_unary_new(CXPR_TOK_NOT, a);
}

static cxpr_expr_ast* cx_tri_and(cxpr_expr_ast* a, cxpr_expr_ast* b) {
    bool v;
    if (cx_tri_bool(a, &v)) {
        cxpr_expr_ast_free(a);
        if (!v) { cxpr_expr_ast_free(b); return cxpr_expr_ast_bool_new(false); }
        return b;
    }
    if (cx_tri_bool(b, &v)) {
        cxpr_expr_ast_free(b);
        if (!v) { cxpr_expr_ast_free(a); return cxpr_expr_ast_bool_new(false); }
        return a;
    }
    return cxpr_expr_ast_binary_new(CXPR_TOK_AND, a, b);
}

static cxpr_expr_ast* cx_tri_or(cxpr_expr_ast* a, cxpr_expr_ast* b) {
    bool v;
    if (cx_tri_bool(a, &v)) {
        cxpr_expr_ast_free(a);
        if (v) { cxpr_expr_ast_free(b); return cxpr_expr_ast_bool_new(true); }
        return b;
    }
    if (cx_tri_bool(b, &v)) {
        cxpr_expr_ast_free(b);
        if (v) { cxpr_expr_ast_free(a); return cxpr_expr_ast_bool_new(true); }
        return a;
    }
    return cxpr_expr_ast_binary_new(CXPR_TOK_OR, a, b);
}

/* ---- Kleene T/F transform -------------------------------------------- */

static bool cx_tri_is_comparison(int op) {
    switch (op) {
        case CXPR_TOK_EQ:
        case CXPR_TOK_NEQ:
        case CXPR_TOK_LT:
        case CXPR_TOK_GT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GTE:
            return true;
        default:
            return false;
    }
}

static void cx_tri_tf(const cxpr_expr_ast* e, cxpr_tristate_leaf_fn fn, void* ud,
                      cxpr_expr_ast** out_true, cxpr_expr_ast** out_false) {
    if (!e) {
        *out_true = cxpr_expr_ast_bool_new(false);
        *out_false = cxpr_expr_ast_bool_new(false);
        return;
    }
    switch (cxpr_expr_ast_kind_of(e)) {
        case CXPR_NODE_BOOL: {
            bool v = cxpr_expr_ast_bool_value(e);
            *out_true = cxpr_expr_ast_bool_new(v);
            *out_false = cxpr_expr_ast_bool_new(!v);
            return;
        }
        case CXPR_NODE_UNARY_OP:
            if (cxpr_expr_ast_operator(e) == CXPR_TOK_NOT) {
                cxpr_expr_ast *ct, *cf;
                cx_tri_tf(cxpr_expr_ast_unary_operand(e), fn, ud, &ct, &cf);
                *out_true = cf; /* definitely-true(not x) = definitely-false(x) */
                *out_false = ct;
                return;
            }
            break;
        case CXPR_NODE_BINARY_OP: {
            int op = cxpr_expr_ast_operator(e);
            if (op == CXPR_TOK_AND || op == CXPR_TOK_OR) {
                cxpr_expr_ast *lt, *lf, *rt, *rf;
                cx_tri_tf(cxpr_expr_ast_binary_left(e), fn, ud, &lt, &lf);
                cx_tri_tf(cxpr_expr_ast_binary_right(e), fn, ud, &rt, &rf);
                if (op == CXPR_TOK_AND) {
                    *out_true = cx_tri_and(lt, rt);
                    *out_false = cx_tri_or(lf, rf);
                } else {
                    *out_true = cx_tri_or(lt, rt);
                    *out_false = cx_tri_and(lf, rf);
                }
                return;
            }
            if (cx_tri_is_comparison(op)) {
                if (cx_tri_known(cxpr_expr_ast_binary_left(e), fn, ud) &&
                    cx_tri_known(cxpr_expr_ast_binary_right(e), fn, ud)) {
                    *out_true = cxpr_expr_ast_clone(e);
                    *out_false = cx_tri_not(cxpr_expr_ast_clone(e));
                } else {
                    *out_true = cxpr_expr_ast_bool_new(false);
                    *out_false = cxpr_expr_ast_bool_new(false);
                }
                return;
            }
            break;
        }
        case CXPR_NODE_TERNARY: {
            cxpr_expr_ast *ct, *cf, *tt, *tf, *ft, *ff;
            cx_tri_tf(cxpr_expr_ast_ternary_condition(e), fn, ud, &ct, &cf);
            cx_tri_tf(cxpr_expr_ast_ternary_true(e), fn, ud, &tt, &tf);
            cx_tri_tf(cxpr_expr_ast_ternary_false(e), fn, ud, &ft, &ff);
            *out_true = cx_tri_or(cx_tri_and(cxpr_expr_ast_clone(ct), tt),
                                  cx_tri_and(cxpr_expr_ast_clone(cf), ft));
            *out_false = cx_tri_or(cx_tri_and(ct, tf), cx_tri_and(cf, ff));
            return;
        }
        default:
            break;
    }
    /* Reference atom in boolean position: keep it only when known. */
    if (cx_tri_known(e, fn, ud)) {
        *out_true = cxpr_expr_ast_clone(e);
        *out_false = cx_tri_not(cxpr_expr_ast_clone(e));
    } else {
        *out_true = cxpr_expr_ast_bool_new(false);
        *out_false = cxpr_expr_ast_bool_new(false);
    }
}

cxpr_expr_ast* cxpr_tristate_definitely_true(const cxpr_expr_ast* expr,
                                             cxpr_tristate_leaf_fn classify,
                                             void* userdata) {
    cxpr_expr_ast *t, *f;
    if (!expr) return cxpr_expr_ast_bool_new(false);
    cx_tri_tf(expr, classify, userdata, &t, &f);
    cxpr_expr_ast_free(f);
    return t;
}

bool cxpr_tristate_is_known(const cxpr_expr_ast* expr,
                            cxpr_tristate_leaf_fn classify,
                            void* userdata) {
    return cx_tri_known(expr, classify, userdata);
}

bool cxpr_tristate_has_true_term(const cxpr_expr_ast* expr,
                                 cxpr_tristate_leaf_fn classify,
                                 void* userdata) {
    bool v;
    cxpr_expr_ast* t = cxpr_tristate_definitely_true(expr, classify, userdata);
    bool eligible = !(cx_tri_bool(t, &v) && v == false);
    cxpr_expr_ast_free(t);
    return eligible;
}
