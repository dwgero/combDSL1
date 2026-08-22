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

set(missing_input "${CREPL_WORKING_DIRECTORY}/missing-load-input.cmb")
file(WRITE "${missing_input}" "load\nexit\n")
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
    "Parse error at position 5 of command: missing filename\n")
if(NOT missing_error STREQUAL expected_missing_error)
    message(FATAL_ERROR
        "unexpected missing-filename error\n"
        "expected:\n${expected_missing_error}"
        "actual:\n${missing_error}")
endif()

set(absent_file
    "${CREPL_WORKING_DIRECTORY}/missing definitions.cmb")
file(REMOVE "${absent_file}")
set(absent_input "${CREPL_WORKING_DIRECTORY}/absent-load-input.cmb")
file(WRITE "${absent_input}" "load missing definitions.cmb\nexit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${absent_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE absent_output
    ERROR_VARIABLE absent_error
    RESULT_VARIABLE absent_result
    TIMEOUT 10
)
if(NOT absent_result EQUAL 0)
    message(FATAL_ERROR
        "absent load exited with ${absent_result}\n"
        "stderr:\n${absent_error}")
endif()
if(NOT absent_output STREQUAL "")
    message(FATAL_ERROR
        "unexpected absent-load output:\n${absent_output}")
endif()
set(expected_absent_error
    "Could not open missing definitions.cmb for reading\n")
if(NOT absent_error STREQUAL expected_absent_error)
    message(FATAL_ERROR
        "unexpected absent-load error\n"
        "expected:\n${expected_absent_error}"
        "actual:\n${absent_error}")
endif()

set(load_file "${CREPL_WORKING_DIRECTORY}/loaded definitions.cmb")
set(round_trip_file
    "${CREPL_WORKING_DIRECTORY}/loaded round trip.cmb")
set(success_input
    "${CREPL_WORKING_DIRECTORY}/successful-load-input.cmb")
string(CONCAT loaded_definitions
    "set LoadI = 1 I\n"
    "define LoadFlip xy = yx\n"
    "set LoadI = 1 K")
string(CONCAT expected_definitions
    "references captured\n"
    "set LoadI = 1 I\n"
    "define LoadFlip xy = yx\n"
    "set LoadI = 1 K")
file(WRITE "${load_file}" "${loaded_definitions}")
file(REMOVE "${round_trip_file}")
file(WRITE "${success_input}"
    "load loaded definitions.cmb\n"
    "save loaded round trip.cmb\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${success_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE success_output
    ERROR_VARIABLE success_error
    RESULT_VARIABLE success_result
    TIMEOUT 10
)
if(NOT success_result EQUAL 0)
    message(FATAL_ERROR
        "successful load exited with ${success_result}\n"
        "stderr:\n${success_error}")
endif()
string(CONCAT expected_success_output
    "Loaded loaded definitions.cmb\n"
    "Saved loaded round trip.cmb\n")
if(NOT success_output STREQUAL expected_success_output)
    message(FATAL_ERROR
        "unexpected successful-load output\n"
        "expected:\n${expected_success_output}"
        "actual:\n${success_output}")
endif()
if(NOT success_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected successful-load error:\n${success_error}")
endif()
file(READ "${round_trip_file}" round_trip_contents)
if(NOT round_trip_contents STREQUAL expected_definitions)
    message(FATAL_ERROR
        "unexpected round-trip definitions\n"
        "expected:\n${expected_definitions}\n"
        "actual:\n${round_trip_contents}\n")
endif()

set(broken_file "${CREPL_WORKING_DIRECTORY}/broken definitions.cmb")
set(rollback_file "${CREPL_WORKING_DIRECTORY}/rollback result.cmb")
set(rollback_input "${CREPL_WORKING_DIRECTORY}/rollback-load-input.cmb")
file(WRITE "${broken_file}"
    "set ShouldRollBack = 0 I\n"
    "@\n"
    "set AlsoRollBack = 0 K")
file(REMOVE "${rollback_file}")
file(WRITE "${rollback_input}"
    "set Existing = 0 I\n"
    "load broken definitions.cmb\n"
    "save rollback result.cmb\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${rollback_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE rollback_output
    ERROR_VARIABLE rollback_error
    RESULT_VARIABLE rollback_result
    TIMEOUT 10
)
if(NOT rollback_result EQUAL 0)
    message(FATAL_ERROR
        "rollback load exited with ${rollback_result}\n"
        "stderr:\n${rollback_error}")
endif()
if(NOT rollback_output STREQUAL "Saved rollback result.cmb\n")
    message(FATAL_ERROR
        "unexpected rollback-load output:\n${rollback_output}")
endif()
string(CONCAT expected_rollback_error
    "Parse error in file broken definitions.cmb on line 2 at position 1: unknown operand\n"
    "Errors are preventing any changes from being made\n")
if(NOT rollback_error STREQUAL expected_rollback_error)
    message(FATAL_ERROR
        "unexpected rollback-load error\n"
        "expected:\n${expected_rollback_error}"
        "actual:\n${rollback_error}")
endif()
file(READ "${rollback_file}" rollback_contents)
if(NOT rollback_contents STREQUAL
        "references captured\nset Existing = 0 I")
    message(FATAL_ERROR
        "failed load was not rolled back\n"
        "expected:\nreferences captured\nset Existing = 0 I\n"
        "actual:\n${rollback_contents}\n")
endif()

set(lowercase_broken_file
    "${CREPL_WORKING_DIRECTORY}/broken lowercase definitions.cmb")
set(lowercase_rollback_file
    "${CREPL_WORKING_DIRECTORY}/lowercase rollback result.cmb")
set(lowercase_rollback_input
    "${CREPL_WORKING_DIRECTORY}/lowercase-rollback-load-input.cmb")
file(WRITE "${lowercase_broken_file}"
    "set LowerBefore = 0 I\n"
    "set lower = 1 I\n"
    "define anotherlower x=x\n"
    "set LowerAfter = 0 K")
file(REMOVE "${lowercase_rollback_file}")
file(WRITE "${lowercase_rollback_input}"
    "set LowerKeep = 0 I\n"
    "load broken lowercase definitions.cmb\n"
    "save lowercase rollback result.cmb\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${lowercase_rollback_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE lowercase_rollback_output
    ERROR_VARIABLE lowercase_rollback_error
    RESULT_VARIABLE lowercase_rollback_result
    TIMEOUT 10
)
if(NOT lowercase_rollback_result EQUAL 0)
    message(FATAL_ERROR
        "lowercase-name rollback load exited with "
        "${lowercase_rollback_result}\n"
        "stderr:\n${lowercase_rollback_error}")
endif()
if(NOT lowercase_rollback_output STREQUAL
        "Saved lowercase rollback result.cmb\n")
    message(FATAL_ERROR
        "unexpected lowercase-name rollback-load output:\n"
        "${lowercase_rollback_output}")
endif()
string(CONCAT expected_lowercase_rollback_error
    "Parse error in file broken lowercase definitions.cmb on line 2 at position 5 of command: combdsl::basis names cannot begin with a lowercase ASCII letter\n"
    "Parse error in file broken lowercase definitions.cmb on line 3 at position 8 of command: combdsl::basis names cannot begin with a lowercase ASCII letter\n"
    "Errors are preventing any changes from being made\n")
if(NOT lowercase_rollback_error STREQUAL
        expected_lowercase_rollback_error)
    message(FATAL_ERROR
        "unexpected lowercase-name rollback error\n"
        "expected:\n${expected_lowercase_rollback_error}"
        "actual:\n${lowercase_rollback_error}")
endif()
file(READ "${lowercase_rollback_file}" lowercase_rollback_contents)
if(NOT lowercase_rollback_contents STREQUAL
        "references captured\nset LowerKeep = 0 I")
    message(FATAL_ERROR
        "lowercase-name failed load was not rolled back\n"
        "expected:\nreferences captured\nset LowerKeep = 0 I\n"
        "actual:\n${lowercase_rollback_contents}\n")
endif()

set(question_broken_file
    "${CREPL_WORKING_DIRECTORY}/broken question definitions.cmb")
set(question_rollback_input
    "${CREPL_WORKING_DIRECTORY}/question-rollback-load-input.cmb")
file(WRITE "${question_broken_file}"
    "set ?LoadBefore = 1 I\n"
    "set ?Bad@ = 1 I\n"
    "set ?LoadAfter = 1 K\n")
file(WRITE "${question_rollback_input}"
    "set QuestionKeep = 1 I\n"
    "load broken question definitions.cmb\n"
    "QuestionKeep x\n"
    "show ?LoadBefore\n"
    "show ?LoadAfter\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${question_rollback_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE question_rollback_output
    ERROR_VARIABLE question_rollback_error
    RESULT_VARIABLE question_rollback_result
    TIMEOUT 10
)
if(NOT question_rollback_result EQUAL 0)
    message(FATAL_ERROR
        "question-name rollback load exited with "
        "${question_rollback_result}\n"
        "stderr:\n${question_rollback_error}")
endif()
if(NOT question_rollback_output STREQUAL "x\n")
    message(FATAL_ERROR
        "question-name failed load was not rolled back atomically\n"
        "actual output:\n${question_rollback_output}")
endif()
string(CONCAT expected_question_rollback_error
    "Parse error in file broken question definitions.cmb on line 2 at "
    "position 5 of command: combdsl::basis names cannot end with @\n"
    "Errors are preventing any changes from being made\n"
    "Parse error at position 6 of command: ?LoadBefore is not a defined name\n"
    "Parse error at position 6 of command: ?LoadAfter is not a defined name\n")
if(NOT question_rollback_error STREQUAL
        expected_question_rollback_error)
    message(FATAL_ERROR
        "unexpected question-name rollback error\n"
        "expected:\n${expected_question_rollback_error}"
        "actual:\n${question_rollback_error}")
endif()
