# libFuzzer targets. Requires a Clang toolchain that supports
# -fsanitize=fuzzer (i.e. CMAKE_C_COMPILER=clang). The CXPR_BUILD_FUZZERS
# option guards this so ordinary builds are unaffected.

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    message(FATAL_ERROR
        "CXPR_BUILD_FUZZERS requires Clang (libFuzzer). "
        "Re-run with -DCMAKE_C_COMPILER=clang, e.g. cmake --preset fuzz.")
endif()

add_executable(cxpr_fuzz_parse parse_fuzzer.c)
target_link_libraries(cxpr_fuzz_parse PRIVATE cxpr)
target_compile_options(cxpr_fuzz_parse PRIVATE -fsanitize=fuzzer,address,undefined -g)
target_link_options(cxpr_fuzz_parse PRIVATE -fsanitize=fuzzer,address,undefined)

# A small seed corpus gives the fuzzer a head start over the grammar.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/corpus)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/corpus/seed_arith "rsi < 30 and volume > $min_volume")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/corpus/seed_call  "clamp(ema(close, 14), 0.0, 1.0) ? a : b")
