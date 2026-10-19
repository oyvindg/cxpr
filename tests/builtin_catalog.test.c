#include <cxpr/cxpr.h>

#include <assert.h>
#include <string.h>

int main(void) {
    const cxpr_function_info* cross = cxpr_builtin_find("cross_above");
    const cxpr_function_info* window = cxpr_builtin_find("window");
    const cxpr_function_info* basket_sum = cxpr_builtin_find("sum");

    assert(cxpr_builtin_count() > 60u);
    assert(cross != NULL);
    assert(strcmp(cross->signature, "cross_above(left, right)") == 0);
    assert((cross->flags & CXPR_BUILTIN_TIMESERIES) != 0u);
    assert(window != NULL && (window->flags & CXPR_BUILTIN_SYNTAX) != 0u);
    assert(basket_sum != NULL && (basket_sum->flags & CXPR_BUILTIN_BASKET) != 0u);
    assert(cxpr_builtin_find("__cxpr_window_sum") == NULL);
    assert(cxpr_builtin_find("not_a_builtin") == NULL);
    assert(cxpr_builtin_at(cxpr_builtin_count()) == NULL);

    for (size_t i = 0u; i < cxpr_builtin_count(); ++i) {
        const cxpr_function_info* left = cxpr_builtin_at(i);
        assert(left != NULL);
        assert(left->name != NULL && left->name[0] != '\0');
        assert(left->signature != NULL && left->signature[0] != '\0');
        assert(left->documentation != NULL && left->documentation[0] != '\0');
        assert(left->min_args <= left->max_args);
        for (size_t j = i + 1u; j < cxpr_builtin_count(); ++j) {
            assert(strcmp(left->name, cxpr_builtin_at(j)->name) != 0);
        }
    }
    return 0;
}
