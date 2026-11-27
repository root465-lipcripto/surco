cmake_minimum_required(VERSION 3.23)
project(surco VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
option(SURCO_ENABLE_NASM "Build the x86-64 Linux NASM routines" ON)
option(SURCO_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SURCO_ENABLE_LTO "Enable interprocedural optimization" OFF)
option(SURCO_FUZZ "Build a Clang libFuzzer decoder harness" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(SURCO_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT supported OUTPUT reason LANGUAGES C)
    if(NOT supported)
        message(FATAL_ERROR "LTO is unavailable: ${reason}")
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

add_compile_definitions(_FILE_OFFSET_BITS=64 _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wall> $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Wpedantic> $<$<COMPILE_LANGUAGE:C>:-Wconversion>
    $<$<COMPILE_LANGUAGE:C>:-Wshadow> $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
    $<$<COMPILE_LANGUAGE:C>:-Werror>)
if(SURCO_SANITIZE)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fsanitize=address,undefined>
        $<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer> $<$<COMPILE_LANGUAGE:C>:-g>)
    add_link_options(-fsanitize=address,undefined)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # Non-PIE test executables avoid intermittent ASan shadow-map collisions on Linux.
        add_link_options($<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-no-pie>)
    endif()
endif()
if(SURCO_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR NOT SURCO_SANITIZE)
        message(FATAL_ERROR "Fuzzing requires Clang and SURCO_SANITIZE=ON")
    endif()
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fsanitize=fuzzer-no-link>)
endif()

set(core src/codec.c src/platform.c src/storage.c)
if(SURCO_ENABLE_NASM)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        message(FATAL_ERROR "NASM backend requires Linux x86-64. Set SURCO_ENABLE_NASM=OFF for C only.")
    endif()
    enable_language(ASM_NASM)
    list(APPEND core asm/transforms.asm)
endif()
add_compile_definitions(SURCO_NASM=$<BOOL:${SURCO_ENABLE_NASM}>)

add_library(surco_objects OBJECT ${core})
set_target_properties(surco_objects PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_include_directories(surco_objects PRIVATE include)
add_library(surco_static STATIC $<TARGET_OBJECTS:surco_objects>)
add_library(surco_shared SHARED $<TARGET_OBJECTS:surco_objects>)
set_target_properties(surco_static surco_shared PROPERTIES OUTPUT_NAME surco LINKER_LANGUAGE C)
set_target_properties(surco_shared PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)
foreach(target surco_static surco_shared)
    target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endforeach()

add_executable(surco src/main.c src/csv.c src/capture.c src/benchmark.c)
target_link_libraries(surco PRIVATE surco_static)
add_custom_target(surco-docs
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/docs" "${CMAKE_CURRENT_BINARY_DIR}/docs"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_BINARY_DIR}/docs/"
    COMMENT "Assembling Surco reference documentation")
install(TARGETS surco surco_static surco_shared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/surco DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR} FILES_MATCHING PATTERN "*.md")
install(FILES README.md LICENSE DESTINATION .)
install(FILES SECURITY.md CONTRIBUTING.md CHANGELOG.md ROADMAP.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES assets/surco.svg DESTINATION assets)
install(DIRECTORY tools examples DESTINATION ${CMAKE_INSTALL_DATADIR}/surco
    FILES_MATCHING PATTERN "*.py" PATTERN "*.html" PATTERN "*.c" PATTERN "*.wsila")

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_subdirectory(tests)
endif()
if(SURCO_FUZZ)
    add_executable(surco_fuzz tests/fuzz_decoder.c)
    target_link_libraries(surco_fuzz PRIVATE surco_static)
    target_link_options(surco_fuzz PRIVATE -fsanitize=fuzzer)
endif()
