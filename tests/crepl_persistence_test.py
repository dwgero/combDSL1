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
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

from crepl_completion_test import (
    PtyReader,
    exit_code_from_wait_status,
    normalized,
    require_completed_line,
    wait_for_child,
    write_all,
)


def child_environment(home):
    environment = os.environ.copy()
    environment["HOME"] = home
    environment["INPUTRC"] = os.devnull
    environment["TERM"] = "xterm-256color"
    return environment


def start_session(executable, working_directory, home):
    child, master = os.forkpty()
    if child == 0:
        os.chdir(working_directory)
        environment = child_environment(home)
        os.execve(executable, [executable], environment)
    return child, master, PtyReader(master)


def finish_session(child, master, reader, exit_command=None):
    child_reaped = False
    try:
        if exit_command is None:
            write_all(master, b"\x04")
        else:
            write_all(master, exit_command + b"\n")
        reader.read_to_end()
        status = wait_for_child(child)
        child_reaped = True
        exit_code = exit_code_from_wait_status(status)
        if exit_code != 0:
            raise AssertionError(
                f"CREPL exited with status {exit_code}")
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


def send_command(master, reader, command):
    write_all(master, command + b"\n")
    return reader.read_until(b">")


def strip_readline_controls(output):
    return normalized(output).replace(
        b"\x1b[?2004h", b"").replace(b"\x1b[?2004l", b"")


def populate_state(executable, working_directory, home):
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")

        output = send_command(
            master, reader, b"set Persisted = 1 I")
        require_completed_line(
            output, b"set Persisted = 1 I\n")

        output = send_command(
            master, reader, b"save remembered definitions.cmb")
        if b"Saved remembered definitions.cmb\n" not in normalized(
                output):
            raise AssertionError(
                f"expected successful save; received {output!r}")

        output = send_command(
            master, reader,
            b"load remembered load definitions.cmb")
        if (b"Loaded remembered load definitions.cmb\n"
                not in normalized(output)):
            raise AssertionError(
                f"expected successful load; received {output!r}")

        output = send_command(
            master, reader, b"save missing/failed save.cmb")
        if (b"Could not open missing/failed save.cmb for writing\n"
                not in normalized(output)):
            raise AssertionError(
                f"expected failed save; received {output!r}")

        output = send_command(
            master, reader, b"load missing load.cmb")
        if (b"Could not open missing load.cmb for reading\n"
                not in normalized(output)):
            raise AssertionError(
                f"expected failed load; received {output!r}")

        for command in (
                b"single step on",
                b"basis step on",
                b"colorize on",
                b"show I"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")
    except Exception:
        os.close(master)
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    else:
        finish_session(child, master, reader)


def check_noninteractive_isolation(
        executable, working_directory, home, state_directory):
    before = {
        path.name: path.read_bytes()
        for path in state_directory.iterdir()
    }
    commands = (
        "set Batch = 1 I\n"
        "save batch definitions.cmb\n"
        "load batch load definitions.cmb\n"
        "exit\n"
    )
    result = subprocess.run(
        [executable],
        input=commands,
        text=True,
        cwd=working_directory,
        env=child_environment(home),
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            "noninteractive CREPL failed with "
            f"{result.returncode}: {result.stderr!r}")
    after = {
        path.name: path.read_bytes()
        for path in state_directory.iterdir()
    }
    if after != before:
        raise AssertionError(
            "noninteractive input changed persistent state")


def check_exit_commands_not_persisted(
        executable, working_directory, home):
    expected_history = "show I\n"
    history_path = os.path.join(home, ".crepl", "history")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"show I")
        require_completed_line(output, b"show I\n")
    except Exception:
        os.close(master)
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    else:
        finish_session(child, master, reader, b"  exit  ")

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != expected_history:
        raise AssertionError(
            f"exit was added to command history: {history!r}")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
    except Exception:
        os.close(master)
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    else:
        finish_session(child, master, reader, b"\tquit\t")

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != expected_history:
        raise AssertionError(
            f"quit was added to command history: {history!r}")


def check_exact_consecutive_history_deduplication(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        for command in (
                b"show I",
                b"show K",
                b"show K",
                b"show K ",
                b"show K ",
                b"show S",
                b"show K"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # With adjacent duplicates absent from readline's in-memory history,
        # the fourth previous entry is the earlier, whitespace-free show K.
        write_all(master, b"\x10\x10\x10\x10\n")
        output = reader.read_until(b">")
        if b"K is a fundamental name with arity:2\n" not in normalized(
                output):
            raise AssertionError(
                "expected readline navigation to execute show K; "
                f"received {output!r}")
    except Exception:
        os.close(master)
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    else:
        finish_session(child, master, reader)

    expected_history = (
        "show I\n"
        "show K\n"
        "show K \n"
        "show S\n"
        "show K\n"
    )
    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != expected_history:
        raise AssertionError(
            "consecutive duplicate suppression changed exact, "
            f"whitespace-distinct, or non-adjacent history: {history!r}")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")

        # The loaded history ends with show K, so the first command must be
        # treated as an adjacent duplicate across the startup boundary.
        for command in (
                b"show K",
                b"show K ",
                b"show K ",
                b"show I"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # This also verifies that startup-boundary and current-session
        # duplicates were omitted from readline's in-memory history.
        write_all(master, b"\x10\x10\x10\x10\n")
        output = reader.read_until(b">")
        if b"S is a fundamental name with arity:3\n" not in normalized(
                output):
            raise AssertionError(
                "expected startup-loaded navigation to execute show S; "
                f"received {output!r}")

        output = send_command(master, reader, b"show I")
        require_completed_line(output, b"show I\n")
        send_command(master, reader, b"")
        output = send_command(master, reader, b"show I")
        require_completed_line(output, b"show I\n")

        output = send_command(master, reader, b"   ")
        require_completed_line(output, b"   \n")
        output = send_command(master, reader, b"show I")
        require_completed_line(output, b"show I\n")

        for _ in range(2):
            output = send_command(master, reader, b"references")
            require_completed_line(output, b"references\n")
            if b"Parse error" not in normalized(output):
                raise AssertionError(
                    "expected bare references to fail; "
                    f"received {output!r}")
    except Exception:
        os.close(master)
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    else:
        finish_session(child, master, reader)

    expected_history += (
        "show K \n"
        "show I\n"
        "show S\n"
        "show I\n"
        "   \n"
        "show I\n"
        "references\n"
    )
    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != expected_history:
        raise AssertionError(
            "startup-loaded duplicate suppression changed exact, "
            f"whitespace-distinct, or non-adjacent history: {history!r}")


def check_restored_state(executable, working_directory, home):
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")

        write_all(master, b"\x10\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"show I\n")

        output = send_command(master, reader, b"show all")
        if b"Nothing to show\n" not in normalized(output):
            raise AssertionError(
                "user definitions were unexpectedly restored")

        output = send_command(master, reader, b"SKIx")
        plain_output = strip_readline_controls(output)
        if plain_output != b"SKIx\nx\n>":
            raise AssertionError(
                "stepping or color settings were unexpectedly restored: "
                f"{plain_output!r}")

        send_command(master, reader, b"single step on")
        output = send_command(master, reader, b"Mx")
        plain_output = strip_readline_controls(output)
        if plain_output != b"Mx\nxx\n>":
            raise AssertionError(
                "basis-step or color settings were unexpectedly restored: "
                f"{plain_output!r}")
        send_command(master, reader, b"single step off")

        write_all(master, b"save \t\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"save remembered definitions.cmb \n")

        write_all(master, b"load \t\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"load remembered load definitions.cmb \n")
        if (b"Loaded remembered load definitions.cmb\n"
                not in normalized(output)):
            raise AssertionError(
                f"expected remembered load; received {output!r}")
    except Exception:
        os.close(master)
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    else:
        finish_session(child, master, reader)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: crepl_persistence_test.py CREPL_EXECUTABLE")

    executable = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(
            prefix="crepl-persistence-") as temporary:
        home = os.path.join(temporary, "home")
        working_directory = os.path.join(temporary, "work")
        os.makedirs(home)
        os.makedirs(working_directory)

        with open(
                os.path.join(
                    working_directory,
                    "remembered load definitions.cmb"),
                "w", encoding="utf-8") as load_file:
            load_file.write("set RememberedLoad = 0 K")
        with open(
                os.path.join(
                    working_directory,
                    "batch load definitions.cmb"),
                "w", encoding="utf-8") as load_file:
            load_file.write("set BatchLoad = 0 K")

        populate_state(executable, working_directory, home)

        state_directory = os.path.join(home, ".crepl")
        state_entries = set(os.listdir(state_directory))
        if state_entries != {"history", "settings"}:
            raise AssertionError(
                f"unexpected persistent files: {state_entries!r}")

        settings_path = os.path.join(state_directory, "settings")
        history_path = os.path.join(state_directory, "history")
        with open(settings_path, encoding="utf-8") as settings_file:
            settings = settings_file.read()
        expected_settings = (
            "crepl-settings 1\n"
            "save \"remembered definitions.cmb\"\n"
            "load \"remembered load definitions.cmb\"\n"
        )
        if settings != expected_settings:
            raise AssertionError(
                f"unexpected settings contents: {settings!r}")

        with open(history_path, encoding="utf-8") as history_file:
            history = history_file.read()
        expected_history = (
            "set Persisted = 1 I\n"
            "save remembered definitions.cmb\n"
            "load remembered load definitions.cmb\n"
            "save missing/failed save.cmb\n"
            "load missing load.cmb\n"
            "single step on\n"
            "basis step on\n"
            "colorize on\n"
            "show I\n"
        )
        if history != expected_history:
            raise AssertionError(
                f"unexpected history contents: {history!r}")

        check_noninteractive_isolation(
            executable,
            working_directory,
            home,
            Path(state_directory),
        )
        check_restored_state(executable, working_directory, home)

        exit_home = os.path.join(temporary, "exit-home")
        os.makedirs(exit_home)
        check_exit_commands_not_persisted(
            executable, working_directory, exit_home)

        duplicate_home = os.path.join(temporary, "duplicate-home")
        os.makedirs(duplicate_home)
        check_exact_consecutive_history_deduplication(
            executable, working_directory, duplicate_home)


if __name__ == "__main__":
    main()
