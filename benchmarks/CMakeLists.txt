add_executable(cxpr_bench_ir ir_bench.c)
target_link_libraries(cxpr_bench_ir PRIVATE cxpr)

add_executable(cxpr_bench_engine engine_bench.c)
target_link_libraries(cxpr_bench_engine PRIVATE cxpr)

set(CXPR_BENCH_RSI_MODEL_C "${CMAKE_CURRENT_BINARY_DIR}/rsi_state_model.gen.c")
set(CXPR_BENCH_RSI_MODEL_INLINE "${CMAKE_CURRENT_BINARY_DIR}/rsi_state_model.inline.inc")
add_custom_command(
    OUTPUT "${CXPR_BENCH_RSI_MODEL_C}"
    COMMAND cxpr_model_codegen
            --model "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/strategies/rsi_state_model.cxpr"
            --output "${CXPR_BENCH_RSI_MODEL_C}"
            --function "cxpr_bench_rsi_state_tick_c"
            --specialize-defaults
    DEPENDS cxpr_model_codegen
            "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/strategies/rsi_state_model.cxpr"
    VERBATIM
)
add_custom_command(
    OUTPUT "${CXPR_BENCH_RSI_MODEL_INLINE}"
    COMMAND cxpr_model_codegen
            --model "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/strategies/rsi_state_model.cxpr"
            --output "${CXPR_BENCH_RSI_MODEL_INLINE}"
            --qualifiers "static inline"
            --function "cxpr_bench_rsi_state_tick_inline_c"
            --specialize-defaults
    DEPENDS cxpr_model_codegen
            "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/strategies/rsi_state_model.cxpr"
    VERBATIM
)

add_executable(cxpr_bench_model model_bench.c "${CXPR_BENCH_RSI_MODEL_C}" "${CXPR_BENCH_RSI_MODEL_INLINE}")
target_link_libraries(cxpr_bench_model PRIVATE cxpr)
set_property(TARGET cxpr_bench_model PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
target_compile_definitions(cxpr_bench_model PRIVATE
    CXPR_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests"
    CXPR_BENCH_RSI_MODEL_INLINE="${CXPR_BENCH_RSI_MODEL_INLINE}")
