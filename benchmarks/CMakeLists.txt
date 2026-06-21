add_executable(cxpr_bench_ir ir_bench.c)
target_link_libraries(cxpr_bench_ir PRIVATE cxpr)

add_executable(cxpr_bench_engine engine_bench.c)
target_link_libraries(cxpr_bench_engine PRIVATE cxpr)
