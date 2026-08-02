# C++ combinator DSL
# Copyright (C) 2026  David W. Gero
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.20)

project(
    combDSL
    VERSION 1.0.0
    DESCRIPTION "A small, functional SKIY combinator DSL for modern C++"
    LANGUAGES C CXX
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

if(NOT EMSCRIPTEN)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)
    target_link_libraries(combdsl INTERFACE Threads::Threads)
endif()

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(COMBDSL_TOP_LEVEL ON)
else()
    set(COMBDSL_TOP_LEVEL OFF)
endif()

option(COMBDSL_BUILD_EXAMPLES "Build the combDSL example" ${COMBDSL_TOP_LEVEL})
option(COMBDSL_BUILD_TESTS "Build the combDSL tests" ${COMBDSL_TOP_LEVEL})
option(COMBDSL_BUILD_BROWSER "Build the browser WebAssembly application" OFF)

if(COMBDSL_BUILD_EXAMPLES)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    include(ExternalProject)
    find_program(COMBDSL_MAKE_EXECUTABLE NAMES make gmake REQUIRED)
    ExternalProject_Add(
        combdsl_ncurses
        URL
            "https://invisible-island.net/archives/ncurses/current/ncurses.tar.gz"
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ncurses"
        DOWNLOAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloads"
        TLS_VERIFY TRUE
        CONFIGURE_COMMAND
            <SOURCE_DIR>/configure
            "--bindir=${CMAKE_CURRENT_SOURCE_DIR}/bin"
            "--libdir=${CMAKE_CURRENT_SOURCE_DIR}/lib"
            "--includedir=${CMAKE_CURRENT_SOURCE_DIR}/include"
            --without-manpages
            --enable-termcap
            --enable-ext-colors
            --enable-rgb-color
        BUILD_COMMAND "${COMBDSL_MAKE_EXECUTABLE}"
        BUILD_IN_SOURCE TRUE
        INSTALL_COMMAND
            "${CMAKE_COMMAND}" -E env
            "PATH=${CMAKE_CURRENT_SOURCE_DIR}/bin:$ENV{PATH}"
            "${COMBDSL_MAKE_EXECUTABLE}" install
        TEST_COMMAND ""
        EXCLUDE_FROM_ALL TRUE
    )

    set(COMBDSL_READLINE_LDFLAGS
        "-L${CMAKE_CURRENT_SOURCE_DIR}/lib")
    if(APPLE)
        string(APPEND COMBDSL_READLINE_LDFLAGS
            " -Wl,-search_paths_first")
    endif()

    ExternalProject_Add(
        combdsl_readline
        GIT_REPOSITORY
            "git://git.git.savannah.gnu.org/readline.git"
        GIT_TAG 553d6bb272f26400d6d4d1cac7c1df84c447449b
        GIT_PROGRESS TRUE
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/readline"
        CONFIGURE_COMMAND
            "${CMAKE_COMMAND}" -E env
            "PATH=${CMAKE_CURRENT_SOURCE_DIR}/bin:$ENV{PATH}"
            "CPPFLAGS=-I${CMAKE_CURRENT_SOURCE_DIR}/include"
            "LDFLAGS=${COMBDSL_READLINE_LDFLAGS}"
            "bash_cv_termcap_lib=libncursesw"
            <SOURCE_DIR>/configure
            "--prefix=${CMAKE_CURRENT_SOURCE_DIR}"
            "--bindir=${CMAKE_CURRENT_SOURCE_DIR}/bin"
            "--libdir=${CMAKE_CURRENT_SOURCE_DIR}/lib"
            "--includedir=${CMAKE_CURRENT_SOURCE_DIR}/include"
            --disable-shared
            --disable-install-examples
            --enable-multibyte
            --with-curses
        BUILD_COMMAND "${COMBDSL_MAKE_EXECUTABLE}"
        BUILD_IN_SOURCE TRUE
        INSTALL_COMMAND
            "${CMAKE_COMMAND}" -E env
            "PATH=${CMAKE_CURRENT_SOURCE_DIR}/bin:$ENV{PATH}"
            "${COMBDSL_MAKE_EXECUTABLE}" install
        TEST_COMMAND ""
        DEPENDS combdsl_ncurses
        EXCLUDE_FROM_ALL TRUE
    )

    add_executable(crepl crepl.cpp)
    target_link_directories(crepl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    target_link_libraries(
        crepl
        PRIVATE
            combdsl::combdsl
            readline
            ncursesw
    )
    if(APPLE)
        target_link_options(crepl PRIVATE -Wl,-search_paths_first)
    endif()
    add_dependencies(crepl combdsl_readline)
    add_custom_command(
        TARGET crepl
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:crepl>"
            "${CMAKE_CURRENT_SOURCE_DIR}/bin/crepl"
        COMMENT "Copying crepl to ${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )

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
            -sINITIAL_HEAP=67108864
            -sMAXIMUM_MEMORY=4294967296
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

    foreach(
        asset
        IN ITEMS
            index.html
            app.js
            worker.js
            evaluation_watchdog.js
            input_history.js
    )
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/web/${asset}"
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/${asset}"
            COPYONLY
        )
    endforeach()

    set(
        COMBDSL_BROWSER_DOCS_DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
    add_custom_target(
        combdsl_browser_docs
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/index.html"
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}/index.html"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/app.js"
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}/app.js"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/worker.js"
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}/worker.js"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/evaluation_watchdog.js"
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}/evaluation_watchdog.js"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/input_history.js"
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}/input_history.js"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/combdsl.js"
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}/combdsl.js"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${COMBDSL_BROWSER_OUTPUT_DIRECTORY}/combdsl.wasm"
            "${COMBDSL_BROWSER_DOCS_DIRECTORY}/combdsl.wasm"
        DEPENDS combdsl_browser
        COMMENT "Refreshing the tracked browser application in docs"
        VERBATIM
    )
endif()

if(COMBDSL_BUILD_TESTS)
    enable_testing()
    add_executable(
        combdsl_tests
        tests/combinators_test.cpp
        tests/registered_basis_test_support.cpp
    )
    target_link_libraries(combdsl_tests PRIVATE combdsl::combdsl)

    add_executable(
        combdsl_browser_load_tests
        tests/browser_load_test.cpp
    )
    target_link_libraries(
        combdsl_browser_load_tests
        PRIVATE combdsl::combdsl
    )
    target_include_directories(
        combdsl_browser_load_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(
        combdsl_define_check_tests
        tests/define_check_test.cpp
    )
    target_link_libraries(
        combdsl_define_check_tests
        PRIVATE combdsl::combdsl
    )

    if(MSVC)
        target_compile_options(
            combdsl_tests
            PRIVATE /W4 /permissive-
        )
        target_compile_options(
            combdsl_browser_load_tests
            PRIVATE /W4 /permissive-
        )
        target_compile_options(
            combdsl_define_check_tests
            PRIVATE /W4 /permissive-
        )
    else()
        target_compile_options(
            combdsl_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
        target_compile_options(
            combdsl_browser_load_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
        target_compile_options(
            combdsl_define_check_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(NAME combinators COMMAND combdsl_tests)
    add_test(
        NAME browser_load
        COMMAND combdsl_browser_load_tests
    )
    add_test(
        NAME define_check
        COMMAND combdsl_define_check_tests
    )

    if(TARGET crepl)
        add_test(
            NAME crepl_single_step
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_single_step_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_single_step_expected.txt"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_key_step
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_key_step_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_key_step_expected.txt"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_basis_step
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_basis_step_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_basis_step_expected.txt"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_colorize
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_colorize_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_colorize_expected.txt"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_about
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_about_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_about_expected.txt"
                "-DCREPL_MAX_LINE_LENGTH=80"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_birds
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_birds_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_birds_expected.txt"
                "-DCREPL_MAX_LINE_LENGTH=80"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_help
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_help_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_help_expected.txt"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_help_full
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_help_full_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_help_full_expected.txt"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_exit_commands
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_exit_commands_input.cmb"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_exit_commands_expected.txt"
                "-DCREPL_EXPECTED_ERROR_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_exit_commands_expected_error.txt"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        add_test(
            NAME crepl_version
            COMMAND
                "${CMAKE_COMMAND}"
                "-DCREPL_EXECUTABLE=$<TARGET_FILE:crepl>"
                "-DCREPL_ARGUMENTS=--version"
                "-DCREPL_EXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_about_expected.txt"
                "-DCREPL_MAX_LINE_LENGTH=80"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_command_test.cmake"
        )
        if(UNIX)
            find_package(Python3 QUIET COMPONENTS Interpreter)
            if(Python3_Interpreter_FOUND)
                add_test(
                    NAME crepl_completion
                    COMMAND
                        "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/crepl_completion_test.py"
                        "$<TARGET_FILE:crepl>"
                )
                set_tests_properties(
                    crepl_completion
                    PROPERTIES TIMEOUT 30
                )
            endif()
        endif()
    endif()

    find_program(COMBDSL_NODE_EXECUTABLE NAMES node nodejs)
    if(COMBDSL_NODE_EXECUTABLE)
        add_test(
            NAME evaluation_watchdog
            COMMAND
                "${COMBDSL_NODE_EXECUTABLE}"
                --test
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/evaluation_watchdog_test.mjs"
        )
        add_test(
            NAME input_history
            COMMAND
                "${COMBDSL_NODE_EXECUTABLE}"
                --test
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/input_history_test.mjs"
        )
        add_test(
            NAME worker_single_step
            COMMAND
                "${COMBDSL_NODE_EXECUTABLE}"
                --test
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/worker_single_step_test.mjs"
        )
    endif()
endif()
