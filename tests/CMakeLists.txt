add_executable(surco_unit unit.c)
target_include_directories(surco_unit PRIVATE ../src)
target_link_libraries(surco_unit PRIVATE surco_static)
if(SURCO_ENABLE_NASM)
    target_sources(surco_unit PRIVATE abi.asm)
endif()
add_test(NAME unit_scalar COMMAND surco_unit scalar)
add_test(NAME unit_sse2 COMMAND surco_unit sse2)
set_tests_properties(unit_sse2 PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME cli_format_differential COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/integration.py" "$<TARGET_FILE:surco>")
add_test(NAME serial_pty COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/serial.py" "$<TARGET_FILE:surco>")
set_tests_properties(cli_format_differential serial_pty PROPERTIES TIMEOUT 60)
if(NOT SURCO_SANITIZE)
    add_test(NAME bounded_memory COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/bounded.py" "$<TARGET_FILE:surco>")
    set_tests_properties(bounded_memory PROPERTIES TIMEOUT 60)
endif()
