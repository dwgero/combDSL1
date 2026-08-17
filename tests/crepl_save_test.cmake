# C++ Combinator DSL
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

file(MAKE_DIRECTORY "${CREPL_WORKING_DIRECTORY}")

set(missing_input "${CREPL_WORKING_DIRECTORY}/missing-save-input.cmb")
file(WRITE "${missing_input}" "save\nexit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${missing_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
    RESULT_VARIABLE missing_result
    TIMEOUT 10
)
if(NOT missing_result EQUAL 0)
    message(FATAL_ERROR
        "missing filename exited with ${missing_result}\n"
        "stderr:\n${missing_error}")
endif()
if(NOT missing_output STREQUAL "")
    message(FATAL_ERROR
        "unexpected missing-filename output:\n${missing_output}")
endif()
set(expected_missing_error
    "Parse error at position 5: missing filename\n")
if(NOT missing_error STREQUAL expected_missing_error)
    message(FATAL_ERROR
        "unexpected missing-filename error\n"
        "expected:\n${expected_missing_error}"
        "actual:\n${missing_error}")
endif()

set(existing_file "${CREPL_WORKING_DIRECTORY}/existing definitions.cmb")
set(empty_input "${CREPL_WORKING_DIRECTORY}/empty-save-input.cmb")
file(WRITE "${existing_file}" "existing contents")
file(WRITE "${empty_input}" "save existing definitions.cmb\nexit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${empty_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE empty_output
    ERROR_VARIABLE empty_error
    RESULT_VARIABLE empty_result
    TIMEOUT 10
)
if(NOT empty_result EQUAL 0)
    message(FATAL_ERROR
        "empty save exited with ${empty_result}\nstderr:\n${empty_error}")
endif()
if(NOT empty_output STREQUAL "Nothing to save\n")
    message(FATAL_ERROR
        "unexpected empty-save output\n"
        "expected:\nNothing to save\n"
        "actual:\n${empty_output}")
endif()
if(NOT empty_error STREQUAL "")
    message(FATAL_ERROR "unexpected empty-save error:\n${empty_error}")
endif()
file(READ "${existing_file}" existing_contents)
if(NOT existing_contents STREQUAL "existing contents")
    message(FATAL_ERROR "empty save changed the existing destination file")
endif()

set(saved_file "${CREPL_WORKING_DIRECTORY}/saved definitions.cmb")
set(definitions_input
    "${CREPL_WORKING_DIRECTORY}/definitions-save-input.cmb")
file(WRITE "${saved_file}" "stale contents")
file(WRITE "${definitions_input}"
    "set SaveI = 1 I\n"
    "define SaveFlip xy = yx\n"
    "  save saved definitions.cmb  \n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${definitions_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE saved_output
    ERROR_VARIABLE saved_error
    RESULT_VARIABLE saved_result
    TIMEOUT 10
)
if(NOT saved_result EQUAL 0)
    message(FATAL_ERROR
        "save exited with ${saved_result}\nstderr:\n${saved_error}")
endif()
if(NOT saved_output STREQUAL "Saved saved definitions.cmb\n")
    message(FATAL_ERROR
        "unexpected save output\n"
        "expected:\nSaved saved definitions.cmb\n"
        "actual:\n${saved_output}")
endif()
if(NOT saved_error STREQUAL "")
    message(FATAL_ERROR "unexpected save error:\n${saved_error}")
endif()
file(READ "${saved_file}" saved_contents)
set(expected_contents
    "references captured\nset SaveI = 1 I\ndefine SaveFlip xy = yx")
if(NOT saved_contents STREQUAL expected_contents)
    message(FATAL_ERROR
        "unexpected saved definitions\n"
        "expected:\n${expected_contents}\n"
        "actual:\n${saved_contents}\n")
endif()

set(equals_file
    "${CREPL_WORKING_DIRECTORY}/equals definitions.cmb")
set(equals_input
    "${CREPL_WORKING_DIRECTORY}/equals-save-input.cmb")
file(REMOVE "${equals_file}")
file(WRITE "${equals_input}"
    "set = = 3 C\n"
    "set =bar = 3 C\n"
    "define = bar = rab\n"
    "=xyz\n"
    "=bar x y z\n"
    "save equals definitions.cmb\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${equals_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE equals_output
    ERROR_VARIABLE equals_error
    RESULT_VARIABLE equals_result
    TIMEOUT 10
)
if(NOT equals_result EQUAL 0)
    message(FATAL_ERROR
        "equals-name save exited with ${equals_result}\n"
        "stderr:\n${equals_error}")
endif()
set(expected_equals_output
    "zyx\nxzy\nSaved equals definitions.cmb\n")
if(NOT equals_output STREQUAL expected_equals_output)
    message(FATAL_ERROR
        "unexpected equals-name CREPL output\n"
        "expected:\n${expected_equals_output}"
        "actual:\n${equals_output}")
endif()
if(NOT equals_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected equals-name CREPL error:\n${equals_error}")
endif()
file(READ "${equals_file}" equals_contents)
string(CONCAT expected_equals_contents
    "references captured\n"
    "set = = 3 C\n"
    "set =bar = 3 C\n"
    "define = bar = rab")
if(NOT equals_contents STREQUAL expected_equals_contents)
    message(FATAL_ERROR
        "unexpected saved equals-name definitions\n"
        "expected:\n${expected_equals_contents}\n"
        "actual:\n${equals_contents}\n")
endif()

set(equals_load_input
    "${CREPL_WORKING_DIRECTORY}/equals-load-input.cmb")
file(WRITE "${equals_load_input}"
    "load equals definitions.cmb\n"
    "=xyz\n"
    "=bar x y z\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${equals_load_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE equals_load_output
    ERROR_VARIABLE equals_load_error
    RESULT_VARIABLE equals_load_result
    TIMEOUT 10
)
if(NOT equals_load_result EQUAL 0)
    message(FATAL_ERROR
        "equals-name load exited with ${equals_load_result}\n"
        "stderr:\n${equals_load_error}")
endif()
set(expected_equals_load_output
    "Loaded equals definitions.cmb\nzyx\nxzy\n")
if(NOT equals_load_output STREQUAL expected_equals_load_output)
    message(FATAL_ERROR
        "unexpected reloaded equals-name CREPL output\n"
        "expected:\n${expected_equals_load_output}"
        "actual:\n${equals_load_output}")
endif()
if(NOT equals_load_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected reloaded equals-name CREPL error:\n"
        "${equals_load_error}")
endif()

set(malformed_equals_input
    "${CREPL_WORKING_DIRECTORY}/malformed-equals-input.cmb")
file(WRITE "${malformed_equals_input}"
    "set = 3 C\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${malformed_equals_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE malformed_equals_output
    ERROR_VARIABLE malformed_equals_error
    RESULT_VARIABLE malformed_equals_result
    TIMEOUT 10
)
if(NOT malformed_equals_result EQUAL 0)
    message(FATAL_ERROR
        "malformed equals-name command exited with "
        "${malformed_equals_result}")
endif()
if(NOT malformed_equals_output STREQUAL "")
    message(FATAL_ERROR
        "malformed equals-name command produced output:\n"
        "${malformed_equals_output}")
endif()
if(NOT malformed_equals_error STREQUAL
        "Parse error at position 7: expected '='\n")
    message(FATAL_ERROR
        "unexpected malformed equals-name error\n"
        "expected:\nParse error at position 7: expected '='\n"
        "actual:\n${malformed_equals_error}")
endif()
