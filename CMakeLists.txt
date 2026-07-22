cmake_minimum_required(VERSION 3.20)

project(
    combDSL
    VERSION 1.0.0
    DESCRIPTION "A small, functional SKIY combinator DSL for modern C++"
    LANGUAGES CXX
)

add_library(combdsl INTERFACE)
add_library(combdsl::combdsl ALIAS combdsl)

target_compile_features(combdsl INTERFACE cxx_std_20)
target_include_directories(
    combdsl
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(COMBDSL_TOP_LEVEL ON)
else()
    set(COMBDSL_TOP_LEVEL OFF)
endif()

option(COMBDSL_BUILD_EXAMPLES "Build the combDSL example" ${COMBDSL_TOP_LEVEL})
option(COMBDSL_BUILD_TESTS "Build the combDSL tests" ${COMBDSL_TOP_LEVEL})
option(COMBDSL_BUILD_BROWSER "Build the browser WebAssembly application" OFF)

if(COMBDSL_BUILD_EXAMPLES)
    add_executable(crepl crepl.cpp)
    target_link_libraries(crepl PRIVATE combdsl::combdsl)

    add_executable(combdsl_example examples/main.cpp)
    target_link_libraries(combdsl_example PRIVATE combdsl::combdsl)

    add_executable(combdsl_single_step examples/single_step.cpp)
    target_link_libraries(combdsl_single_step PRIVATE combdsl::combdsl)
endif()

if(COMBDSL_BUILD_BROWSER)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "COMBDSL_BUILD_BROWSER requires the Emscripten toolchain")
    endif()

    set(COMBDSL_BROWSER_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web")
    file(MAKE_DIRECTORY "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}")

    add_executable(combdsl_browser web/combdsl_browser.cpp)
    target_link_libraries(
        combdsl_browser
        PRIVATE
            combdsl::combdsl
            embind
    )
    target_compile_options(
        combdsl_browser
        PRIVATE
            -fwasm-exceptions
            -Wall
            -Wextra
            -Wpedantic
    )
    target_link_options(
        combdsl_browser
        PRIVATE
            -fwasm-exceptions
            --no-entry
            -sMODULARIZE=1
            -sEXPORT_NAME=createCombdslModule
            -sENVIRONMENT=worker
            -sALLOW_MEMORY_GROWTH=1
            -sNO_EXIT_RUNTIME=1
            -sDYNAMIC_EXECUTION=0
    )
    set_target_properties(
        combdsl_browser
        PROPERTIES
            OUTPUT_NAME combdsl
            SUFFIX .js
            RUNTIME_OUTPUT_DIRECTORY "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}"
    )

    foreach(asset IN ITEMS index.html app.js worker.js)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/web/${asset}"
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/${asset}"
            COPYONLY
        )
    endforeach()
endif()

if(COMBDSL_BUILD_TESTS)
    enable_testing()
    add_executable(
        combdsl_tests
        tests/combinators_test.cpp
        tests/registered_basis_test_support.cpp
    )
    target_link_libraries(combdsl_tests PRIVATE combdsl::combdsl)

    if(MSVC)
        target_compile_options(combdsl_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(combdsl_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME combinators COMMAND combdsl_tests)
endif()
