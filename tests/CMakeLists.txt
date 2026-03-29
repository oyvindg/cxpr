file(GLOB CXPR_C_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.test.c")

set(CXPR_C_TEST_INTERNAL_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/lexer.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ir.test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/formula_ir.test.c"
)

foreach(test_file IN LISTS CXPR_C_TESTS)
    get_filename_component(test_name "${test_file}" NAME_WE)      # e.g. lexer.test
    string(REPLACE ".test" "" test_base "${test_name}")           # e.g. lexer
    set(target_name "test_${test_base}")

    add_executable("${target_name}" "${test_file}")
    target_link_libraries("${target_name}" PRIVATE cxpr)

    if(test_file IN_LIST CXPR_C_TEST_INTERNAL_INCLUDE)
        target_include_directories("${target_name}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    endif()

    add_test(NAME "${test_base}" COMMAND "$<TARGET_FILE:${target_name}>")
endforeach()
