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

execute_process(
    COMMAND "${CREPL_EXECUTABLE}"
    INPUT_FILE "${CREPL_INPUT_FILE}"
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    RESULT_VARIABLE result
    TIMEOUT 10
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "crepl exited with ${result}\nstderr:\n${actual_error}")
endif()

file(READ "${CREPL_EXPECTED_FILE}" expected_output)
string(ASCII 27 ansi_escape)
string(REPLACE "<ESC>" "${ansi_escape}" expected_output "${expected_output}")
if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "unexpected crepl output\n"
        "expected:\n${expected_output}"
        "actual:\n${actual_output}")
endif()

if(NOT actual_error STREQUAL "")
    message(FATAL_ERROR "unexpected crepl stderr:\n${actual_error}")
endif()
