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
import termios
import tempfile
import time

sys.dont_write_bytecode = True

from crepl_completion_test import (
    PtyReader,
    exit_code_from_wait_status,
    normalized,
    require_completed_line,
    wait_for_child,
    write_all,
)


RED_SGR = rb"\x1b\[(?:31|91|38;5;(?:1|9)|38;2;255;0+;0+)m"
RESET_SGR = rb"\x1b\[(?:0)?m"
RESUME_PROMPT = b"Press Enter to resume; press q or Q to quit.\r\n"
TERMINAL_INPUT_FLAGS = termios.ICANON | termios.ECHO


def require_red_message(output, message):
    expected = RED_SGR + re.escape(message) + RESET_SGR
    if re.search(expected, output) is None:
        raise AssertionError(
            f"expected red message {message!r}; received {output!r}")


def require_exact_progress_before_message(
        output, message, expected=None, minimum=None):
    match = re.search(
        rb"\[(\d+) steps so far\]\n" + re.escape(message),
        normalized(output))
    if match is None:
        raise AssertionError(
            "expected an exact progress line immediately before "
            f"{message!r}; received {output!r}")
    reductions = int(match.group(1))
    if expected is not None and reductions != expected:
        raise AssertionError(
            f"expected exact progress {expected}; received {reductions}")
    if minimum is not None and reductions < minimum:
        raise AssertionError(
            f"expected progress of at least {minimum}; "
            f"received {reductions}")
    return reductions


def terminal_input_flags(descriptor):
    return termios.tcgetattr(descriptor)[3] & TERMINAL_INPUT_FLAGS


def require_raw_pause(descriptor, description):
    flags = terminal_input_flags(descriptor)
    if flags != 0:
        raise AssertionError(
            f"expected ICANON and ECHO off during {description}; "
            f"received local flags {flags:#x}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: crepl_terminal_color_test.py CREPL_EXECUTABLE")

    executable = os.path.abspath(sys.argv[1])
    working_directory = tempfile.TemporaryDirectory(
        prefix="crepl-terminal-color-")
    startup_gate_read, startup_gate_write = os.pipe()
    child, master = os.forkpty()
    if child == 0:
        os.close(startup_gate_write)
        os.read(startup_gate_read, 1)
        os.close(startup_gate_read)
        environment = os.environ.copy()
        environment["HOME"] = working_directory.name
        environment["INPUTRC"] = os.devnull
        environment["TERM"] = "xterm-256color"
        os.chdir(working_directory.name)
        os.execve(executable, [executable], environment)

    os.close(startup_gate_read)
    startup_input_flags = terminal_input_flags(master)
    write_all(startup_gate_write, b"1")
    os.close(startup_gate_write)

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

        write_all(master, b"references live\n")
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

        write_all(master, b"step limit 2\n")
        reader.read_until(b">")
        write_all(master, b"I(I(I(I(Ix))))\n")
        output = reader.read_until(RESUME_PROMPT)
        require_raw_pause(master, "ordinary step-limit pause")
        require_red_message(
            output, b"[step limit reached after 2 steps]")
        require_completed_line(output, b"I(I(Ix))\n")

        write_all(master, b"\n")
        output = reader.read_until(RESUME_PROMPT)
        require_red_message(
            output, b"[step limit reached after 2 steps]")
        require_completed_line(output, b"Ix\n")

        write_all(master, b"\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"x\n")

        write_all(master, b"I(I(Ix))\n")
        output = reader.read_until(RESUME_PROMPT)
        require_red_message(
            output, b"[step limit reached after 2 steps]")
        write_all(master, b"q")
        output = reader.read_until(b">")
        require_red_message(output, b"[cancelled]")

        write_all(master, b"step limit 1001\n")
        reader.read_until(b">")
        write_all(master, b"MM\n")
        output = reader.read_until(RESUME_PROMPT)
        require_raw_pause(master, "exact-progress step-limit pause")
        require_red_message(
            output, b"[step limit reached after 1001 steps]")
        require_exact_progress_before_message(
            output,
            b"[step limit reached after 1001 steps]",
            expected=1001)
        write_all(master, b"\n")
        output = reader.read_until(RESUME_PROMPT)
        require_red_message(
            output, b"[step limit reached after 1001 steps]")
        require_exact_progress_before_message(
            output,
            b"[step limit reached after 1001 steps]",
            expected=2002)
        write_all(master, b"q")
        output = reader.read_until(b">")
        require_red_message(output, b"[cancelled]")

        write_all(master, b"step limit 2\n")
        reader.read_until(b">")

        write_all(master, b"single step on\n")
        reader.read_until(b">")
        write_all(master, b"colorize on\n")
        reader.read_until(b">")
        write_all(master, b"I(I(I(I(I(Ix)))))\n")
        output = reader.read_until(RESUME_PROMPT)
        require_raw_pause(master, "colorized single-step limit pause")
        require_red_message(
            output, b"[step limit reached after 2 steps]")
        write_all(master, b"\n")
        output = reader.read_until(RESUME_PROMPT)
        require_red_message(
            output, b"[step limit reached after 2 steps]")
        write_all(master, b"\n")
        output = reader.read_until(b"\r\nx\r\n")
        output += reader.read_until(b">")
        require_completed_line(output, b"x\n")
        write_all(master, b"single step off\n")
        reader.read_until(b">")
        write_all(master, b"colorize off\n")
        reader.read_until(b">")

        write_all(master, b"key step on\n")
        reader.read_until(b">")
        write_all(master, b"I(I(I(I(Ix))))\n")
        reader.read_until(b"press q or Q to quit.\r\n")
        write_all(master, b"abcde")
        output = reader.read_until(b">")
        require_completed_line(output, b"x\n")

        write_all(master, b"colorize on\n")
        reader.read_until(b">")
        write_all(master, b"I(I(I(I(I(Ix)))))\n")
        reader.read_until(b"press q or Q to quit.\r\n")
        write_all(master, b"abcdef")
        output = reader.read_until(b"\r\nx\r\n")
        output += reader.read_until(b">")
        require_completed_line(output, b"x\n")

        write_all(master, b"colorize off\n")
        reader.read_until(b">")
        write_all(master, b"key step off\n")
        reader.read_until(b">")

        write_all(master, b"step limit off\n")
        reader.read_until(b">")
        write_all(master, b"MM\n")
        output = reader.read_until(b"[1000 steps so far]")
        time.sleep(0.01)
        os.kill(child, signal.SIGINT)
        output += reader.read_until(RESUME_PROMPT)
        require_raw_pause(master, "ordinary interrupt pause")
        require_red_message(output, b"[interrupted]")
        exact_interrupt_steps = require_exact_progress_before_message(
            output, b"[interrupted]", minimum=1000)
        if exact_interrupt_steps == 1000:
            raise AssertionError(
                "expected interruption progress to refresh beyond the "
                "observed 1000-step milestone")
        write_all(master, b"Q")
        output = reader.read_until(b">")
        require_red_message(output, b"[cancelled]")

        write_all(master, b"key step\n")
        reader.read_until(b">")
        write_all(master, b"Ix\n")
        reader.read_until(b"press q or Q to quit.\r\n")
        os.kill(child, signal.SIGINT)
        output = reader.read_until(RESUME_PROMPT)
        require_raw_pause(master, "key-step interrupt pause")
        require_red_message(output, b"[interrupted]")
        write_all(master, b"\n")
        write_all(master, b" ")
        output = reader.read_until(b">")
        require_completed_line(output, b"x\n")

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
        restored_input_flags = terminal_input_flags(master)
        if restored_input_flags != startup_input_flags:
            raise AssertionError(
                "expected CREPL to restore its startup ICANON/ECHO state "
                f"{startup_input_flags:#x} at exit; received "
                f"{restored_input_flags:#x}")
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
