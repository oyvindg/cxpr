add_executable(cxpr_bench_ir ir_bench.c)
target_link_libraries(cxpr_bench_ir PRIVATE cxpr)
target_compile_definitions(cxpr_bench_ir PRIVATE
    CXPR_BENCH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(CXPR_BENCH_IR_MODELS
    simple_arith
    nested_expr
    function_call
    defined_fn
    defined_chain
    deep_defined
    complex_signal
    large_arith
    large_branch
    large_math
    mixed_expr
    mixed_pipe
    context_churn
    struct_scalar_mul
    scalar_struct_mul
    struct_struct_mul
    struct_struct_add
    struct_scalar_mul_all_fields
    scalar_struct_mul_all_fields
    struct_struct_mul_all_fields
    struct_struct_add_all_fields
    lookback_leaf
    lookback_mixed)
set(CXPR_BENCH_IR_MODEL_INCLUDES)
foreach(model_name IN LISTS CXPR_BENCH_IR_MODELS)
    set(model_source "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ir_${model_name}.cxpr")
    set(model_output "${CMAKE_CURRENT_BINARY_DIR}/ir_${model_name}.inline.inc")
    string(TOUPPER "${model_name}" model_define_name)
    add_custom_command(
        OUTPUT "${model_output}"
        COMMAND cxpr_model_codegen
                --model "${model_source}"
                --output "${model_output}"
                --qualifiers "static inline"
                --function "cxpr_bench_ir_${model_name}"
                --specialize-defaults
        DEPENDS cxpr_model_codegen "${model_source}"
        VERBATIM
    )
    list(APPEND CXPR_BENCH_IR_MODEL_INCLUDES "${model_output}")
    target_compile_definitions(cxpr_bench_ir PRIVATE
        "CXPR_BENCH_IR_${model_define_name}_INLINE=\"${model_output}\"")
endforeach()
target_sources(cxpr_bench_ir PRIVATE ${CXPR_BENCH_IR_MODEL_INCLUDES})

add_executable(cxpr_bench_engine engine_bench.c)
target_link_libraries(cxpr_bench_engine PRIVATE cxpr)

set(CXPR_BENCH_INDEX_HISTORY_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/index_history_generated.h")
set(CXPR_BENCH_PRODUCER_HISTORY_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/producer_history_generated.h")
set(CXPR_BENCH_INDEX_ARRAY_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/index_array_generated.h")
add_custom_command(
    OUTPUT "${CXPR_BENCH_INDEX_HISTORY_HEADER}"
    COMMAND cxpr_model_codegen
            --model "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/index_history_generated.cxpr"
            --output "${CXPR_BENCH_INDEX_HISTORY_HEADER}"
            --qualifiers "static inline"
            --function cxpr_bench_generated_close1
    DEPENDS cxpr_model_codegen
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/index_history_generated.cxpr"
    VERBATIM)
add_executable(cxpr_bench_model_codegen_tool model_c_codegen_tool.c)
target_link_libraries(cxpr_bench_model_codegen_tool PRIVATE cxpr)
add_custom_command(
    OUTPUT "${CXPR_BENCH_PRODUCER_HISTORY_HEADER}"
    COMMAND cxpr_bench_model_codegen_tool
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/producer_history_generated.cxpr"
            "${CXPR_BENCH_PRODUCER_HISTORY_HEADER}"
            "static inline"
            cxpr_bench_generated_producer_history
    DEPENDS cxpr_bench_model_codegen_tool
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/producer_history_generated.cxpr"
    VERBATIM)
add_executable(cxpr_bench_array_codegen_tool
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests/generated_array_codegen_tool.c")
target_link_libraries(cxpr_bench_array_codegen_tool PRIVATE cxpr)
add_custom_command(
    OUTPUT "${CXPR_BENCH_INDEX_ARRAY_HEADER}"
    COMMAND cxpr_bench_array_codegen_tool "${CXPR_BENCH_INDEX_ARRAY_HEADER}"
    DEPENDS cxpr_bench_array_codegen_tool
    VERBATIM)
add_executable(cxpr_bench_index_history
    index_history_bench.c
    "${CXPR_BENCH_INDEX_ARRAY_HEADER}"
    "${CXPR_BENCH_INDEX_HISTORY_HEADER}"
    "${CXPR_BENCH_PRODUCER_HISTORY_HEADER}")
target_link_libraries(cxpr_bench_index_history PRIVATE cxpr)
target_include_directories(cxpr_bench_index_history PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")

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
