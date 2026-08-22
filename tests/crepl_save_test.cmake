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
    "Parse error at position 5 of command: missing filename\n")
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

set(question_file
    "${CREPL_WORKING_DIRECTORY}/question definitions.cmb")
set(question_input
    "${CREPL_WORKING_DIRECTORY}/question-save-input.cmb")
file(REMOVE "${question_file}")
file(WRITE "${question_input}"
    "set ? = 3 C\n"
    "set ?bar = 3 C\n"
    "define ? bar = rab\n"
    "set ?foo=bar = 1 I\n"
    "set ?foo?bar = 1 I\n"
    "set ?x= = B\n"
    "set ?y= = C\n"
    "set &=1 I\n"
    "set &bar=1 I\n"
    "set &foo=bar\n"
    "set A&B=1 I\n"
    "?xyz\n"
    "?bar x y z\n"
    "?foo=bar x\n"
    "?foo?bar x\n"
    "&x\n"
    "&bar x\n"
    "A&B x\n"
    "find among S K ?x= ?y= ?z= ?y=\n"
    "save question definitions.cmb\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${question_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE question_output
    ERROR_VARIABLE question_error
    RESULT_VARIABLE question_result
    TIMEOUT 10
)
if(NOT question_result EQUAL 0)
    message(FATAL_ERROR
        "question-name save exited with ${question_result}\n"
        "stderr:\n${question_error}")
endif()
string(CONCAT expected_question_output
    "zyx\nxzy\nx\nx\nx\nx\nx\n"
    "?=K ?y=\n"
    "Saved question definitions.cmb\n")
if(NOT question_output STREQUAL expected_question_output)
    message(FATAL_ERROR
        "unexpected question-name CREPL output\n"
        "expected:\n${expected_question_output}"
        "actual:\n${question_output}")
endif()
if(NOT question_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected question-name CREPL error:\n${question_error}")
endif()
file(READ "${question_file}" question_contents)
string(CONCAT expected_question_contents
    "references captured\n"
    "set ? = 3 C\n"
    "set ?bar = 3 C\n"
    "define ? bar = rab\n"
    "set ?foo=bar = 1 I\n"
    "set ?foo?bar = 1 I\n"
    "set ?x= = 0 B\n"
    "set ?y= = 0 C\n"
    "set & = 1 I\n"
    "set &bar = 1 I\n"
    "set &foo = 0 bar\n"
    "set A&B = 1 I")
if(NOT question_contents STREQUAL expected_question_contents)
    message(FATAL_ERROR
        "unexpected saved question-name definitions\n"
        "expected:\n${expected_question_contents}\n"
        "actual:\n${question_contents}\n")
endif()

set(question_load_input
    "${CREPL_WORKING_DIRECTORY}/question-load-input.cmb")
file(WRITE "${question_load_input}"
    "load question definitions.cmb\n"
    "?xyz\n"
    "?bar x y z\n"
    "?foo=bar x\n"
    "?foo?bar x\n"
    "&x\n"
    "&bar x\n"
    "A&B x\n"
    "find among S K ?x= ?y= ?z= ?y=\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${question_load_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE question_load_output
    ERROR_VARIABLE question_load_error
    RESULT_VARIABLE question_load_result
    TIMEOUT 10
)
if(NOT question_load_result EQUAL 0)
    message(FATAL_ERROR
        "question-name load exited with ${question_load_result}\n"
        "stderr:\n${question_load_error}")
endif()
string(CONCAT expected_question_load_output
    "Loaded question definitions.cmb\nzyx\nxzy\nx\nx\nx\nx\nx\n"
    "?=K ?y=\n")
if(NOT question_load_output STREQUAL expected_question_load_output)
    message(FATAL_ERROR
        "unexpected reloaded question-name CREPL output\n"
        "expected:\n${expected_question_load_output}"
        "actual:\n${question_load_output}")
endif()
if(NOT question_load_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected reloaded question-name CREPL error:\n"
        "${question_load_error}")
endif()

set(backslash "\\")
set(quoted_backslash "\"\\\\\"")
set(backslash_file
    "${CREPL_WORKING_DIRECTORY}/backslash definitions.cmb")
set(backslash_input
    "${CREPL_WORKING_DIRECTORY}/backslash-save-input.cmb")
file(REMOVE "${backslash_file}")
file(WRITE "${backslash_input}"
    "I ${backslash}\n"
    "set ${backslash} = 3 C\n"
    "set ${backslash}foo = 1 I\n"
    "set ${backslash}12345678901234 = 1 I\n"
    "define ${backslash} bar = rab\n"
    "${backslash}xyz\n"
    "${backslash}foo x\n"
    "${backslash}12345678901234 x\n"
    "I ${quoted_backslash}\n"
    "save backslash definitions.cmb\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${backslash_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE backslash_output
    ERROR_VARIABLE backslash_error
    RESULT_VARIABLE backslash_result
    TIMEOUT 10
)
if(NOT backslash_result EQUAL 0)
    message(FATAL_ERROR
        "backslash-name save exited with ${backslash_result}\n"
        "stderr:\n${backslash_error}")
endif()
string(CONCAT expected_backslash_output
    "${quoted_backslash}\n"
    "zyx\n"
    "x\n"
    "x\n"
    "${quoted_backslash}\n"
    "Saved backslash definitions.cmb\n")
if(NOT backslash_output STREQUAL expected_backslash_output)
    message(FATAL_ERROR
        "unexpected backslash-name CREPL output\n"
        "expected:\n${expected_backslash_output}"
        "actual:\n${backslash_output}")
endif()
if(NOT backslash_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected backslash-name CREPL error:\n${backslash_error}")
endif()
file(READ "${backslash_file}" backslash_contents)
string(CONCAT expected_backslash_contents
    "references captured\n"
    "set ${backslash} = 3 C\n"
    "set ${backslash}foo = 1 I\n"
    "set ${backslash}12345678901234 = 1 I\n"
    "define ${backslash} bar = rab")
if(NOT backslash_contents STREQUAL expected_backslash_contents)
    message(FATAL_ERROR
        "unexpected saved backslash-name definitions\n"
        "expected:\n${expected_backslash_contents}\n"
        "actual:\n${backslash_contents}\n")
endif()

set(backslash_load_input
    "${CREPL_WORKING_DIRECTORY}/backslash-load-input.cmb")
file(WRITE "${backslash_load_input}"
    "load backslash definitions.cmb\n"
    "${backslash}xyz\n"
    "${backslash}foo x\n"
    "${backslash}12345678901234 x\n"
    "I ${quoted_backslash}\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${backslash_load_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE backslash_load_output
    ERROR_VARIABLE backslash_load_error
    RESULT_VARIABLE backslash_load_result
    TIMEOUT 10
)
if(NOT backslash_load_result EQUAL 0)
    message(FATAL_ERROR
        "backslash-name load exited with ${backslash_load_result}\n"
        "stderr:\n${backslash_load_error}")
endif()
string(CONCAT expected_backslash_load_output
    "Loaded backslash definitions.cmb\n"
    "zyx\n"
    "x\n"
    "x\n"
    "${quoted_backslash}\n")
if(NOT backslash_load_output STREQUAL expected_backslash_load_output)
    message(FATAL_ERROR
        "unexpected reloaded backslash-name CREPL output\n"
        "expected:\n${expected_backslash_load_output}"
        "actual:\n${backslash_load_output}")
endif()
if(NOT backslash_load_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected reloaded backslash-name CREPL error:\n"
        "${backslash_load_error}")
endif()

set(quoted_word_file
    "${CREPL_WORKING_DIRECTORY}/quoted word definitions.cmb")
set(quoted_word_input
    "${CREPL_WORKING_DIRECTORY}/quoted-word-save-input.cmb")
file(REMOVE "${quoted_word_file}")
string(CONCAT quoted_word_commands
    [=[set QuotedWord = 0 "fo\"\\o"]=] "\n"
    [=[set QuotedSlashWord = 0 "\\fo\"\\bar\\"]=] "\n"
    "QuotedWord\n"
    "QuotedSlashWord\n"
    "save quoted word definitions.cmb\n"
    "exit\n")
file(WRITE "${quoted_word_input}" "${quoted_word_commands}")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${quoted_word_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE quoted_word_output
    ERROR_VARIABLE quoted_word_error
    RESULT_VARIABLE quoted_word_result
    TIMEOUT 10
)
if(NOT quoted_word_result EQUAL 0)
    message(FATAL_ERROR
        "quoted-word save exited with ${quoted_word_result}\n"
        "stderr:\n${quoted_word_error}")
endif()
string(CONCAT expected_quoted_word_output
    [=[fo"\o]=] "\n"
    [=["\\fo\"\\bar\\"]=] "\n"
    "Saved quoted word definitions.cmb\n")
if(NOT quoted_word_output STREQUAL expected_quoted_word_output)
    message(FATAL_ERROR
        "unexpected quoted-word CREPL output\n"
        "expected:\n${expected_quoted_word_output}"
        "actual:\n${quoted_word_output}")
endif()
if(NOT quoted_word_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected quoted-word CREPL error:\n${quoted_word_error}")
endif()
file(READ "${quoted_word_file}" quoted_word_contents)
string(CONCAT expected_quoted_word_contents
    "references captured\n"
    [=[set QuotedWord = 0 "fo\"\\o"]=] "\n"
    [=[set QuotedSlashWord = 0 "\\fo\"\\bar\\"]=])
if(NOT quoted_word_contents STREQUAL expected_quoted_word_contents)
    message(FATAL_ERROR
        "unexpected saved quoted-word definitions\n"
        "expected:\n${expected_quoted_word_contents}\n"
        "actual:\n${quoted_word_contents}\n")
endif()

set(quoted_word_load_input
    "${CREPL_WORKING_DIRECTORY}/quoted-word-load-input.cmb")
file(WRITE "${quoted_word_load_input}"
    "load quoted word definitions.cmb\n"
    "QuotedWord\n"
    "QuotedSlashWord\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${quoted_word_load_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE quoted_word_load_output
    ERROR_VARIABLE quoted_word_load_error
    RESULT_VARIABLE quoted_word_load_result
    TIMEOUT 10
)
if(NOT quoted_word_load_result EQUAL 0)
    message(FATAL_ERROR
        "quoted-word load exited with ${quoted_word_load_result}\n"
        "stderr:\n${quoted_word_load_error}")
endif()
string(CONCAT expected_quoted_word_load_output
    "Loaded quoted word definitions.cmb\n"
    [=[fo"\o]=] "\n"
    [=["\\fo\"\\bar\\"]=] "\n")
if(NOT quoted_word_load_output STREQUAL
        expected_quoted_word_load_output)
    message(FATAL_ERROR
        "unexpected reloaded quoted-word CREPL output\n"
        "expected:\n${expected_quoted_word_load_output}"
        "actual:\n${quoted_word_load_output}")
endif()
if(NOT quoted_word_load_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected reloaded quoted-word CREPL error:\n"
        "${quoted_word_load_error}")
endif()

set(direct_string_pipe_input
    "${CREPL_WORKING_DIRECTORY}/direct-string-pipe-input.cmb")
file(WRITE "${direct_string_pipe_input}"
    [=[I "pipe\"\\word"]=] "\n"
    [=[I "bad\q"]=] "\n"
    [=[I "\\"]=] "\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${direct_string_pipe_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE direct_string_pipe_output
    ERROR_VARIABLE direct_string_pipe_error
    RESULT_VARIABLE direct_string_pipe_result
    TIMEOUT 10
)
if(NOT direct_string_pipe_result EQUAL 0)
    message(FATAL_ERROR
        "direct-string pipe input exited with "
        "${direct_string_pipe_result}\n"
        "stderr:\n${direct_string_pipe_error}")
endif()
string(CONCAT expected_direct_string_pipe_output
    [=[pipe"\word]=] "\n"
    [=["\\"]=] "\n")
if(NOT direct_string_pipe_output STREQUAL
        expected_direct_string_pipe_output)
    message(FATAL_ERROR
        "unexpected direct-string pipe output\n"
        "expected:\n${expected_direct_string_pipe_output}"
        "actual:\n${direct_string_pipe_output}")
endif()
string(FIND "${direct_string_pipe_error}"
    "Parse error at position 7: " direct_string_error_position)
string(REGEX MATCHALL "Parse error" direct_string_error_matches
    "${direct_string_pipe_error}")
list(LENGTH direct_string_error_matches direct_string_error_count)
if(NOT direct_string_error_position EQUAL 0 OR
        NOT direct_string_error_count EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one direct invalid-escape diagnostic:\n"
        "${direct_string_pipe_error}")
endif()

set(invalid_backslash_file
    "${CREPL_WORKING_DIRECTORY}/invalid backslash definitions.cmb")
file(WRITE "${invalid_backslash_file}"
    "set SlashLoadBefore = 1 K\n"
    "set ${backslash}123456789012345 = 1 I\n"
    "set SlashLoadAfter = 1 I\n")
set(invalid_backslash_load_input
    "${CREPL_WORKING_DIRECTORY}/invalid-backslash-load-input.cmb")
file(WRITE "${invalid_backslash_load_input}"
    "set SlashLoadKeep = 1 I\n"
    "load invalid backslash definitions.cmb\n"
    "SlashLoadKeep x\n"
    "show SlashLoadBefore\n"
    "show SlashLoadAfter\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${invalid_backslash_load_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE invalid_backslash_load_output
    ERROR_VARIABLE invalid_backslash_load_error
    RESULT_VARIABLE invalid_backslash_load_result
    TIMEOUT 10
)
if(NOT invalid_backslash_load_result EQUAL 0)
    message(FATAL_ERROR
        "invalid backslash-name load exited with "
        "${invalid_backslash_load_result}")
endif()
if(NOT invalid_backslash_load_output STREQUAL "x\n")
    message(FATAL_ERROR
        "backslash-name load did not roll back atomically\n"
        "actual output:\n${invalid_backslash_load_output}")
endif()
string(CONCAT expected_invalid_backslash_load_error
    "Parse error in file invalid backslash definitions.cmb on line 2 at "
    "position 20 of command: combdsl::basis names are limited to 15 characters\n"
    "Errors are preventing any changes from being made\n"
    "Parse error at position 6 of command: SlashLoadBefore is not a defined name\n"
    "Parse error at position 6 of command: SlashLoadAfter is not a defined name\n")
if(NOT invalid_backslash_load_error STREQUAL
        expected_invalid_backslash_load_error)
    message(FATAL_ERROR
        "unexpected invalid backslash-name load error\n"
        "expected:\n${expected_invalid_backslash_load_error}"
        "actual:\n${invalid_backslash_load_error}")
endif()

set(malformed_question_input
    "${CREPL_WORKING_DIRECTORY}/malformed-question-input.cmb")
file(WRITE "${malformed_question_input}"
    "set ? 3 C\n"
    "set ?? 3 C\n"
    "set ?= 3 C\n"
    "set ?bar=3 C\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${malformed_question_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE malformed_question_output
    ERROR_VARIABLE malformed_question_error
    RESULT_VARIABLE malformed_question_result
    TIMEOUT 10
)
if(NOT malformed_question_result EQUAL 0)
    message(FATAL_ERROR
        "malformed question-name command exited with "
        "${malformed_question_result}")
endif()
if(NOT malformed_question_output STREQUAL "")
    message(FATAL_ERROR
        "malformed question-name command produced output:\n"
        "${malformed_question_output}")
endif()
string(CONCAT expected_malformed_question_error
    "Parse error at position 7 of command: expected '='\n"
    "Parse error at position 8 of command: expected '='\n"
    "Parse error at position 8 of command: expected '='\n"
    "Parse error at position 12 of command: expected '='\n")
if(NOT malformed_question_error STREQUAL
        expected_malformed_question_error)
    message(FATAL_ERROR
        "unexpected malformed question-name error\n"
        "expected:\n"
        "Parse error at position 7 of command: expected '='\n"
        "Parse error at position 8 of command: expected '='\n"
        "Parse error at position 8 of command: expected '='\n"
        "Parse error at position 12 of command: expected '='\n"
        "actual:\n${malformed_question_error}")
endif()

set(unregistered_question_marker_input
    "${CREPL_WORKING_DIRECTORY}/unregistered-question-marker-input.cmb")
file(WRITE "${unregistered_question_marker_input}"
    "find among ?x=xx\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${unregistered_question_marker_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE unregistered_question_marker_output
    ERROR_VARIABLE unregistered_question_marker_error
    RESULT_VARIABLE unregistered_question_marker_result
    TIMEOUT 10
)
if(NOT unregistered_question_marker_result EQUAL 0 OR
        NOT unregistered_question_marker_output STREQUAL "" OR
        NOT unregistered_question_marker_error STREQUAL
            "Parse error at position 12 of command: expected at least one bird name\n")
    message(FATAL_ERROR
        "unexpected unregistered question-marker result\n"
        "stdout:\n${unregistered_question_marker_output}"
        "stderr:\n${unregistered_question_marker_error}")
endif()

set(registered_question_marker_input
    "${CREPL_WORKING_DIRECTORY}/registered-question-marker-input.cmb")
file(WRITE "${registered_question_marker_input}"
    "set ?x= = B\n"
    "set ?y= = C\n"
    "set ?foo=bar = 1 I\n"
    "find among S K ?x= ?y= ?z= ?y=\n"
    "find among ?x=xx\n"
    "find among ?x=x\n"
    "find among ?x= Missing ?z=zz\n"
    "remove ?x=\n"
    "find among ?x=xx\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${registered_question_marker_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE registered_question_marker_output
    ERROR_VARIABLE registered_question_marker_error
    RESULT_VARIABLE registered_question_marker_result
    TIMEOUT 10
)
if(NOT registered_question_marker_result EQUAL 0 OR
        NOT registered_question_marker_output STREQUAL "?=K ?y=\n" OR
        NOT registered_question_marker_error STREQUAL
            "Parse error at position 15 of command: xx is not a defined name\nParse error at position 15 of command: x is not a defined name\nParse error at position 17 of command: issing is not a defined name\nParse error at position 15 of command: xx is not a defined name\n")
    message(FATAL_ERROR
        "unexpected registered question-marker result\n"
        "stdout:\n${registered_question_marker_output}"
        "stderr:\n${registered_question_marker_error}")
endif()

set(spaced_question_marker_input
    "${CREPL_WORKING_DIRECTORY}/spaced-question-marker-input.cmb")
file(WRITE "${spaced_question_marker_input}"
    "set ?space = I\n"
    "find among I ?space = ?q=K\n"
    "remove ?space\n"
    "find among I ?space = ?q=K\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${spaced_question_marker_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE spaced_question_marker_output
    ERROR_VARIABLE spaced_question_marker_error
    RESULT_VARIABLE spaced_question_marker_result
    TIMEOUT 10
)
if(NOT spaced_question_marker_result EQUAL 0 OR
        NOT spaced_question_marker_output STREQUAL "" OR
        NOT spaced_question_marker_error STREQUAL
            "Parse error at position 21 of command: = is not a defined name\nParse error at position 21 of command: = is not a defined name\n")
    message(FATAL_ERROR
        "unexpected spaced question-marker result\n"
        "stdout:\n${spaced_question_marker_output}"
        "stderr:\n${spaced_question_marker_error}")
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
        "Parse error at position 7 of command: expected '='\n")
    message(FATAL_ERROR
        "unexpected malformed equals-name error\n"
        "expected:\nParse error at position 7 of command: expected '='\n"
        "actual:\n${malformed_equals_error}")
endif()

set(numeric_file
    "${CREPL_WORKING_DIRECTORY}/numeric-like definitions.cmb")
set(numeric_input
    "${CREPL_WORKING_DIRECTORY}/numeric-name-save-input.cmb")
file(REMOVE "${numeric_file}")
file(WRITE "${numeric_input}"
    "set +8 = 1 I\n"
    "set -8 = 1 I\n"
    "set 8.0 = 1 I\n"
    "set 8e2 = 1 I\n"
    "set 8x = 1 I\n"
    "set 0 = I\n"
    "set 000 = I\n"
    "set 123456789012345 = I\n"
    "define 0 = I\n"
    "remove 0\n"
    "revisions 000\n"
    "dependson 123456789012345\n"
    "show 0\n"
    "save numeric-like definitions.cmb\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${numeric_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE numeric_output
    ERROR_VARIABLE numeric_error
    RESULT_VARIABLE numeric_result
    TIMEOUT 10
)
if(NOT numeric_result EQUAL 0)
    message(FATAL_ERROR
        "numeric-name save exited with ${numeric_result}\n"
        "stderr:\n${numeric_error}")
endif()
if(NOT numeric_output STREQUAL
        "Saved numeric-like definitions.cmb\n")
    message(FATAL_ERROR
        "unexpected numeric-name CREPL output\n"
        "expected:\nSaved numeric-like definitions.cmb\n"
        "actual:\n${numeric_output}")
endif()
string(CONCAT expected_numeric_error
    "Parse error at position 5 of command: combdsl::basis names cannot be "
    "non-negative integer literals\n"
    "Parse error at position 5 of command: combdsl::basis names cannot be "
    "non-negative integer literals\n"
    "Parse error at position 5 of command: combdsl::basis names cannot be "
    "non-negative integer literals\n"
    "Parse error at position 8 of command: combdsl::basis names cannot be "
    "non-negative integer literals\n"
    "Parse error at position 8 of command: combdsl::basis names cannot be "
    "non-negative integer literals\n"
    "Parse error at position 11 of command: combdsl::basis names cannot be "
    "non-negative integer literals\n"
    "Parse error at position 11 of command: combdsl::basis names cannot be "
    "non-negative integer literals\n"
    "Parse error at position 6 of command: 0 is not a defined name\n")
if(NOT numeric_error STREQUAL expected_numeric_error)
    message(FATAL_ERROR
        "unexpected numeric-name CREPL error\n"
        "expected:\n${expected_numeric_error}"
        "actual:\n${numeric_error}")
endif()
file(READ "${numeric_file}" numeric_contents)
string(CONCAT expected_numeric_contents
    "references captured\n"
    "set +8 = 1 I\n"
    "set -8 = 1 I\n"
    "set 8.0 = 1 I\n"
    "set 8e2 = 1 I\n"
    "set 8x = 1 I")
if(NOT numeric_contents STREQUAL expected_numeric_contents)
    message(FATAL_ERROR
        "unexpected saved numeric-like definitions\n"
        "expected:\n${expected_numeric_contents}\n"
        "actual:\n${numeric_contents}\n")
endif()

set(numeric_load_input
    "${CREPL_WORKING_DIRECTORY}/numeric-name-load-input.cmb")
file(WRITE "${numeric_load_input}"
    "load numeric-like definitions.cmb\n"
    "+8 x\n"
    "-8 x\n"
    "8.0 x\n"
    "8e2 x\n"
    "8x x\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${numeric_load_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE numeric_load_output
    ERROR_VARIABLE numeric_load_error
    RESULT_VARIABLE numeric_load_result
    TIMEOUT 10
)
if(NOT numeric_load_result EQUAL 0)
    message(FATAL_ERROR
        "numeric-like name load exited with ${numeric_load_result}\n"
        "stderr:\n${numeric_load_error}")
endif()
if(NOT numeric_load_output STREQUAL
        "Loaded numeric-like definitions.cmb\nx\nx\nx\nx\nx\n")
    message(FATAL_ERROR
        "unexpected reloaded numeric-like name output\n"
        "actual:\n${numeric_load_output}")
endif()
if(NOT numeric_load_error STREQUAL "")
    message(FATAL_ERROR
        "unexpected reloaded numeric-like name error:\n"
        "${numeric_load_error}")
endif()

set(invalid_numeric_file
    "${CREPL_WORKING_DIRECTORY}/invalid numeric definitions.cmb")
file(WRITE "${invalid_numeric_file}"
    "set LoadBefore = 1 K\n"
    "set 000 = I\n"
    "set LoadAfter = 1 I\n")
set(invalid_numeric_load_input
    "${CREPL_WORKING_DIRECTORY}/invalid-numeric-load-input.cmb")
file(WRITE "${invalid_numeric_load_input}"
    "set LoadNumericKeep = 1 I\n"
    "load invalid numeric definitions.cmb\n"
    "LoadNumericKeep x\n"
    "show LoadBefore\n"
    "show LoadAfter\n"
    "exit\n")
execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${invalid_numeric_load_input}"
    WORKING_DIRECTORY "${CREPL_WORKING_DIRECTORY}"
    OUTPUT_VARIABLE invalid_numeric_load_output
    ERROR_VARIABLE invalid_numeric_load_error
    RESULT_VARIABLE invalid_numeric_load_result
    TIMEOUT 10
)
if(NOT invalid_numeric_load_result EQUAL 0)
    message(FATAL_ERROR
        "invalid numeric-name load exited with "
        "${invalid_numeric_load_result}")
endif()
if(NOT invalid_numeric_load_output STREQUAL "x\n")
    message(FATAL_ERROR
        "numeric-name load did not roll back atomically\n"
        "actual output:\n${invalid_numeric_load_output}")
endif()
string(CONCAT expected_invalid_numeric_load_error
    "Parse error in file invalid numeric definitions.cmb on line 2 at "
    "position 5 of command: combdsl::basis names cannot be non-negative integer literals\n"
    "Errors are preventing any changes from being made\n"
    "Parse error at position 6 of command: LoadBefore is not a defined name\n"
    "Parse error at position 6 of command: LoadAfter is not a defined name\n")
if(NOT invalid_numeric_load_error STREQUAL
        expected_invalid_numeric_load_error)
    message(FATAL_ERROR
        "unexpected invalid numeric-name load error\n"
        "expected:\n${expected_invalid_numeric_load_error}"
        "actual:\n${invalid_numeric_load_error}")
endif()
