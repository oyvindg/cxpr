#include <cxpr/model/compiled.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

int main(void) {
    cxpr_model_compile_options options = {
        .backend = CXPR_MODEL_BACKEND_AUTO,
        .fuse = true,
        .enable_trace = false,
    };

    assert(options.backend == CXPR_MODEL_BACKEND_AUTO);
    assert(options.fuse);
    assert(!options.enable_trace);
    assert(CXPR_MODEL_RESULT_NUMBER != CXPR_MODEL_RESULT_BOOL);

    printf("cxpr model header compile-only test passed.\n");
    return 0;
}
