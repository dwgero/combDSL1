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

import os
import re
import signal
import sys
import tempfile

sys.dont_write_bytecode = True

from crepl_completion_test import (
    PtyReader,
    exit_code_from_wait_status,
    wait_for_child,
    write_all,
)


RED_SGR = rb"\x1b\[(?:31|91|38;5;(?:1|9)|38;2;255;0+;0+)m"
RESET_SGR = rb"\x1b\[(?:0)?m"


def require_red_message(output, message):
    expected = RED_SGR + re.escape(message) + RESET_SGR
    if re.search(expected, output) is None:
        raise AssertionError(
            f"expected red message {message!r}; received {output!r}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: crepl_terminal_color_test.py CREPL_EXECUTABLE")

    executable = os.path.abspath(sys.argv[1])
    working_directory = tempfile.TemporaryDirectory(
        prefix="crepl-terminal-color-")
    child, master = os.forkpty()
    if child == 0:
        environment = os.environ.copy()
        environment["HOME"] = working_directory.name
        environment["INPUTRC"] = os.devnull
        environment["TERM"] = "xterm-256color"
        os.chdir(working_directory.name)
        os.execve(executable, [executable], environment)

    child_reaped = False
    try:
        reader = PtyReader(master)
        reader.read_until(b">")

        write_all(master, b"show all\n")
        output = reader.read_until(b">")
        require_red_message(output, b"Nothing to show")

        write_all(master, b"find ?x = MM\n")
        output = reader.read_until(b">")
        require_red_message(
            output, b"No match within search bounds")

        write_all(master, b"show x\n")
        output = reader.read_until(b">")
        require_red_message(
            output,
            b"Parse error at position 6: x is not a defined name")

        write_all(master, b"snapshot off\n")
        reader.read_until(b">")
        write_all(master, b"set RedCircleA = 0 I\n")
        reader.read_until(b">")
        write_all(master, b"set RedCircleB = 0 RedCircleA\n")
        reader.read_until(b">")
        write_all(master, b"set RedCircleA = 0 RedCircleB\n")
        output = reader.read_until(
            b"RedCircleA -> RedCircleB -> RedCircleA")
        output += reader.read_until(b">")
        require_red_message(
            output,
            b"Parse error at position 5: RedCircleA would have a "
            b"circular definition\r\n"
            b"RedCircleA -> RedCircleB -> RedCircleA")

        write_all(master, b"load missing.cmb\n")
        output = reader.read_until(b">")
        require_red_message(
            output,
            b"Could not open missing.cmb for reading")

        write_all(master, b"MM\n")
        reader.read_until(b"1000 steps")
        os.kill(child, signal.SIGINT)
        reader.read_until(b"then Enter to quit.\r\n")
        write_all(master, b"q\n")
        output = reader.read_until(b">")
        require_red_message(output, b"[cancelled]")

        write_all(master, b"key step\n")
        reader.read_until(b">")
        write_all(master, b"Ix\n")
        reader.read_until(b"press q or Q to quit.\r\n")
        write_all(master, b"q")
        output = reader.read_until(b">")
        require_red_message(output, b"[cancelled]")

        write_all(master, b"exit\n")
        reader.read_to_end()
        status = wait_for_child(child)
        child_reaped = True
        exit_code = exit_code_from_wait_status(status)
        if exit_code != 0:
            raise AssertionError(f"CREPL exited with status {exit_code}")
    finally:
        os.close(master)
        if not child_reaped:
            try:
                os.kill(child, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child, 0)
            except ChildProcessError:
                pass
        working_directory.cleanup()


if __name__ == "__main__":
    main()
