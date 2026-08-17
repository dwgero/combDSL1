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


def child_environment(home, inputrc=os.devnull):
    environment = os.environ.copy()
    environment["HOME"] = home
    environment["INPUTRC"] = inputrc
    environment["TERM"] = "xterm-256color"
    return environment


def start_session(
        executable, working_directory, home, inputrc=os.devnull):
    child, master = os.forkpty()
    if child == 0:
        os.chdir(working_directory)
        environment = child_environment(home, inputrc)
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


def send_bracketed_paste(master, reader, source):
    write_all(master, b"\x1b[200~" + source + b"\x1b[201~\n")
    return reader.read_until(b">")


def strip_readline_controls(output):
    return normalized(output).replace(
        b"\x1b[?2004h", b"").replace(b"\x1b[?2004l", b"")


def require_no_terminal_bell(output, context):
    if b"\x07" in output:
        raise AssertionError(
            f"{context} unexpectedly rang the terminal bell; "
            f"received {output!r}")


def require_terminal_bell(output, context):
    if b"\x07" not in output:
        raise AssertionError(
            f"{context} did not retain Readline's terminal bell; "
            f"received {output!r}")


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


def check_ctrl_d_removes_in_memory_history(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        for command in (b"show I", b"show K", b"show S"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # Preserve a draft, recall the middle entry, and remove it.  The
        # next newer entry replaces it and is the command Enter executes.
        write_all(master, b"show all\x10\x10\x04\n")
        output = reader.read_until(b">")
        require_no_terminal_bell(
            output, "successful in-memory history deletion")
        if b"S is a fundamental name with arity:3\n" not in normalized(
                output):
            raise AssertionError(
                "Ctrl-D did not reveal the next newer in-memory entry; "
                f"received {output!r}")

        output = send_command(master, reader, b"show K")
        require_completed_line(output, b"show K\n")

        # A replacement history entry remains pristine.  Remove show S,
        # then remove its show K replacement with a second Ctrl-D.  With no
        # newer entry left, Readline must restore the saved live draft.
        write_all(master, b"show all\x10\x10\x04\x04\n")
        output = reader.read_until(b">")
        require_no_terminal_bell(
            output, "repeated successful in-memory history deletion")
        if b"Nothing to show\n" not in normalized(output):
            raise AssertionError(
                "repeated Ctrl-D did not restore the saved draft; "
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

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "show I\nshow all\n":
        raise AssertionError(
            "Ctrl-D did not persist repeated in-memory removals: "
            f"{history!r}")


def check_ctrl_d_removes_loaded_history(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    os.makedirs(state_directory)
    history_path = os.path.join(state_directory, "history")
    with open(history_path, "w", encoding="utf-8") as history_file:
        history_file.write("show I\nshow K\nshow S\n")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        write_all(master, b"show all\x10\x10\x04\n")
        output = reader.read_until(b">")
        require_no_terminal_bell(
            output, "successful startup-loaded history deletion")
        if b"S is a fundamental name with arity:3\n" not in normalized(
                output):
            raise AssertionError(
                "Ctrl-D did not reveal the next newer loaded entry; "
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

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "show I\nshow S\n":
        raise AssertionError(
            "Ctrl-D did not rewrite startup-loaded persistent history: "
            f"{history!r}")


def check_rewrite_preserves_unrelated_history_bytes(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    os.makedirs(state_directory)
    history_path = os.path.join(state_directory, "history")
    with open(history_path, "wb") as history_file:
        history_file.write(b"show I\r\nshow K\n")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")

        # Removing show K must not normalize the unrelated CRLF-terminated
        # show I line while rewriting the persistent history file.
        write_all(master, b"\x10\x04")
        finish_session(child, master, reader, b"exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise

    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != b"show I\r\n":
        raise AssertionError(
            "deleting one entry changed unrelated history bytes: "
            f"{history!r}")

    with open(history_path, "wb") as history_file:
        history_file.write(b"show I\r\nshow K\n")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")

        # GNU Readline presents the CRLF-loaded entry semantically as show I.
        # Its canonical source must still map back to the raw show I\r disk
        # line when that entry itself is removed, leaving show K unchanged.
        write_all(master, b"\x10\x10\x04")
        finish_session(child, master, reader, b"\x15exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise

    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != b"show K\n":
        raise AssertionError(
            "semantic CRLF history did not map to its raw disk entry: "
            f"{history!r}")


def check_rewrite_preserves_history_symlink(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    os.makedirs(state_directory)
    target_path = os.path.join(home, "history-target")
    history_path = os.path.join(state_directory, "history")
    with open(target_path, "wb") as target_file:
        target_file.write(b"show I\nshow K\n")
    link_target = os.path.join("..", "history-target")
    try:
        os.symlink(link_target, history_path)
    except (NotImplementedError, OSError):
        return

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        write_all(master, b"\x10\x04")
        finish_session(child, master, reader, b"exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise

    if not os.path.islink(history_path):
        raise AssertionError(
            "rewriting history replaced its symbolic link")
    if os.readlink(history_path) != link_target:
        raise AssertionError(
            "rewriting history changed its symbolic-link target")
    with open(target_path, "rb") as target_file:
        target = target_file.read()
    if target != b"show I\n":
        raise AssertionError(
            "history symlink target did not receive the deletion: "
            f"{target!r}")


def check_multiline_history_block_removal(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"show K")
        require_completed_line(output, b"show K\n")

        # Bracketed paste keeps the embedded newline in one logical Readline
        # entry.  An exact adjacent duplicate must still be suppressed.
        for _ in range(2):
            output = send_bracketed_paste(master, reader, b"show\nI")
            if b"I is a fundamental name with arity:1\n" not in normalized(
                    output):
                raise AssertionError(
                    "embedded-newline bracketed paste was not submitted "
                    f"as one command; received {output!r}")

        with open(history_path, "rb") as history_file:
            history = history_file.read()
        if history != b"show K\nshow\nI\n":
            raise AssertionError(
                "embedded-newline duplicate suppression changed its "
                f"physical history block: {history!r}")

        # Remove the pristine multiline recall, then recall and execute show K
        # to prove the complete logical entry left Readline's in-memory list.
        write_all(master, b"\x10\x04\x10\n")
        output = reader.read_until(b">")
        if b"K is a fundamental name with arity:2\n" not in normalized(
                output):
            raise AssertionError(
                "Ctrl-D did not remove the multiline in-memory entry; "
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

    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != b"show K\n":
        raise AssertionError(
            "Ctrl-D did not remove the complete multiline disk block: "
            f"{history!r}")


def check_concurrent_append_survives_multiline_removal(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child_a, master_a, reader_a = start_session(
        executable, working_directory, home)
    a_closed = False
    child_b = None
    master_b = None
    reader_b = None
    b_closed = False
    try:
        reader_a.read_until(b">")
        output = send_command(master_a, reader_a, b"show K")
        require_completed_line(output, b"show K\n")
        output = send_bracketed_paste(master_a, reader_a, b"show\nI")
        if b"I is a fundamental name with arity:1\n" not in normalized(
                output):
            raise AssertionError(
                f"multiline setup failed; received {output!r}")

        child_b, master_b, reader_b = start_session(
            executable, working_directory, home)
        reader_b.read_until(b">")
        output = send_command(master_b, reader_b, b"show S")
        require_completed_line(output, b"show S\n")

        # A loaded no knowledge of B's later append.  Removing A's multiline
        # recall must delete both of its physical lines while retaining B's
        # independently appended show S.
        write_all(master_a, b"\x10\x04")
        a_closed = True
        finish_session(child_a, master_a, reader_a, b"exit")
        b_closed = True
        finish_session(child_b, master_b, reader_b)
    finally:
        if not a_closed:
            try:
                os.close(master_a)
            except OSError:
                pass
            try:
                os.kill(child_a, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_a, 0)
            except ChildProcessError:
                pass
        if child_b is not None and not b_closed:
            try:
                os.close(master_b)
            except OSError:
                pass
            try:
                os.kill(child_b, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_b, 0)
            except ChildProcessError:
                pass

    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != b"show K\nshow S\n":
        raise AssertionError(
            "multiline removal lost a concurrent append or left a partial "
            f"entry: {history!r}")


def check_long_multiline_concurrent_append_survives_removal(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    long_source = b"show" + (b"\n" * 1025) + b"I"
    if long_source.count(b"\n") != 1025:
        raise AssertionError("long multiline test source has wrong size")

    child_a, master_a, reader_a = start_session(
        executable, working_directory, home)
    a_closed = False
    child_b = None
    master_b = None
    reader_b = None
    b_closed = False
    try:
        reader_a.read_until(b">")
        output = send_command(master_a, reader_a, b"show K")
        require_completed_line(output, b"show K\n")

        child_b, master_b, reader_b = start_session(
            executable, working_directory, home)
        reader_b.read_until(b">")
        write_all(
            master_b,
            b"\x1b[200~" + long_source + b"\x1b[201~\n",
        )
        # A very tall Readline redisplay may redraw the prompt while the paste
        # is still being ingested, so wait for semantic completion before the
        # final prompt rather than treating the first `>` byte as completion.
        completion = b"I is a fundamental name with arity:1"
        output = reader_b.read_until(completion)
        output += reader_b.read_until(b">")
        if completion not in output:
            raise AssertionError(
                "long concurrent bracketed paste was not submitted as "
                f"one command; received {output!r}")

        # A loaded only show K.  B's later logical entry expands to 1,026
        # physical file lines.  A's deletion must still align with show K and
        # preserve every byte of B's unmatched long append.
        write_all(master_a, b"\x10\x04")
        a_closed = True
        finish_session(child_a, master_a, reader_a, b"exit")
        b_closed = True
        finish_session(child_b, master_b, reader_b)
    finally:
        if not a_closed:
            try:
                os.close(master_a)
            except OSError:
                pass
            try:
                os.kill(child_a, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_a, 0)
            except ChildProcessError:
                pass
        if child_b is not None and not b_closed:
            try:
                os.close(master_b)
            except OSError:
                pass
            try:
                os.kill(child_b, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_b, 0)
            except ChildProcessError:
                pass

    expected = long_source + b"\n"
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != expected:
        raise AssertionError(
            "removing an older entry changed a 1,025-newline concurrent "
            f"append: expected {len(expected)} bytes, got {len(history)}")


def check_mixed_long_divergence_removal(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    long_source = b"show" + (b"\n" * 1025) + b"I"

    child_a, master_a, reader_a = start_session(
        executable, working_directory, home)
    a_closed = False
    child_b = None
    master_b = None
    reader_b = None
    b_closed = False
    try:
        reader_a.read_until(b">")
        output = send_command(master_a, reader_a, b"show I")
        require_completed_line(output, b"show I\n")
        output = send_command(master_a, reader_a, b"show K")
        require_completed_line(output, b"show K\n")

        child_b, master_b, reader_b = start_session(
            executable, working_directory, home)
        reader_b.read_until(b">")

        # B removes A's older show I, clears the show K recall that Ctrl-D
        # reveals, then appends one logical entry with 1,025 embedded
        # newlines.  A's canonical history remains [show I, show K], while
        # the disk now starts with only show K followed by the long block.
        write_all(
            master_b,
            b"\x10\x10\x04\x15\x1b[200~"
            + long_source
            + b"\x1b[201~\n",
        )
        completion = b"I is a fundamental name with arity:1"
        output = reader_b.read_until(completion)
        output += reader_b.read_until(b">")
        if completion not in output:
            raise AssertionError(
                "mixed-divergence long paste was not submitted as one "
                f"command; received {output!r}")

        setup = b"show K\n" + long_source + b"\n"
        with open(history_path, "rb") as history_file:
            history = history_file.read()
        if history != setup:
            raise AssertionError(
                "unexpected mixed-divergence history setup: expected "
                f"{len(setup)} bytes, got {len(history)}")

        # The 1,026-line divergence exceeds the bounded Myers alignment.
        # Deleting A's still-pristine show K must use the whole-entry
        # fallback and preserve the concurrent block byte-for-byte.
        write_all(master_a, b"\x10\x04")
        a_closed = True
        finish_session(child_a, master_a, reader_a, b"exit")
        b_closed = True
        finish_session(child_b, master_b, reader_b)
    finally:
        if not a_closed:
            try:
                os.close(master_a)
            except OSError:
                pass
            try:
                os.kill(child_a, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_a, 0)
            except ChildProcessError:
                pass
        if child_b is not None and not b_closed:
            try:
                os.close(master_b)
            except OSError:
                pass
            try:
                os.kill(child_b, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_b, 0)
            except ChildProcessError:
                pass

    expected = long_source + b"\n"
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != expected:
        raise AssertionError(
            "mixed-divergence removal changed the 1,025-newline block: "
            f"expected {len(expected)} bytes, got {len(history)}")


def check_scattered_multiline_match_is_not_removed(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child_a, master_a, reader_a = start_session(
        executable, working_directory, home)
    a_closed = False
    child_b = None
    master_b = None
    reader_b = None
    b_closed = False
    try:
        reader_a.read_until(b">")
        output = send_command(master_a, reader_a, b"show K")
        require_completed_line(output, b"show K\n")
        output = send_bracketed_paste(master_a, reader_a, b"show\nI")
        if b"I is a fundamental name with arity:1\n" not in normalized(
                output):
            raise AssertionError(
                f"split-block setup failed; received {output!r}")

        child_b, master_b, reader_b = start_session(
            executable, working_directory, home)
        reader_b.read_until(b">")

        # B sees A's physical show and I lines as separate loaded entries.
        # Remove I, then append show S and a new unrelated I so the two text
        # components of A's original multiline source exist only as a
        # scattered subsequence with show S between them.
        write_all(master_b, b"\x10\x04show S\n")
        completion = b"S is a fundamental name with arity:3"
        reader_b.read_until(completion)
        reader_b.read_until(b">")
        output = send_command(master_b, reader_b, b"I")
        if b"No further reductions\n" not in normalized(output):
            raise AssertionError(
                f"split-block replacement setup failed; received {output!r}")

        expected = b"show K\nshow\nshow S\nI\n"
        with open(history_path, "rb") as history_file:
            history = history_file.read()
        if history != expected:
            raise AssertionError(
                "unexpected split-block setup history: "
                f"{history!r}")

        # A's stale logical show\nI no longer owns one contiguous disk block.
        # Ctrl-D may remove it from A's memory, but must not use a scattered
        # match to delete B's unrelated physical records.
        write_all(master_a, b"\x10\x04")
        a_closed = True
        finish_session(child_a, master_a, reader_a, b"exit")
        b_closed = True
        finish_session(child_b, master_b, reader_b)
    finally:
        if not a_closed:
            try:
                os.close(master_a)
            except OSError:
                pass
            try:
                os.kill(child_a, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_a, 0)
            except ChildProcessError:
                pass
        if child_b is not None and not b_closed:
            try:
                os.close(master_b)
            except OSError:
                pass
            try:
                os.kill(child_b, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_b, 0)
            except ChildProcessError:
                pass

    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != expected:
        raise AssertionError(
            "stale multiline removal deleted scattered unrelated lines: "
            f"{history!r}")


def check_timestamp_history_block_removal(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    os.makedirs(state_directory)
    history_path = os.path.join(state_directory, "history")
    original = (
        b"#1700000000\nshow I\n"
        b"#1700000001\nshow K\n"
    )

    # Removing the newest entry removes its preceding timestamp too.
    with open(history_path, "wb") as history_file:
        history_file.write(original)
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        write_all(master, b"\x10\x04")
        finish_session(child, master, reader, b"exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != b"#1700000000\nshow I\n":
        raise AssertionError(
            "removing newest timestamped history left an orphan: "
            f"{history!r}")

    # Removing the oldest entry likewise removes only its timestamp block.
    with open(history_path, "wb") as history_file:
        history_file.write(original)
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        write_all(master, b"\x10\x10\x04")
        finish_session(child, master, reader, b"\x15exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != b"#1700000001\nshow K\n":
        raise AssertionError(
            "removing oldest timestamped history changed another block: "
            f"{history!r}")

    # Equal sources remain distinct timestamp-owned blocks; deleting the
    # newest duplicate must not consume its older twin.
    duplicate_blocks = (
        b"#1700000002\nshow I\n"
        b"#1700000003\nshow I\n"
    )
    with open(history_path, "wb") as history_file:
        history_file.write(duplicate_blocks)
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        write_all(master, b"\x10\x04")
        finish_session(child, master, reader, b"exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != b"#1700000002\nshow I\n":
        raise AssertionError(
            "removing a timestamped duplicate changed the wrong block: "
            f"{history!r}")


def check_concurrent_append_survives_timestamp_removal(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    os.makedirs(state_directory)
    history_path = os.path.join(state_directory, "history")
    with open(history_path, "wb") as history_file:
        history_file.write(
            b"#1700000000\nshow I\n"
            b"#1700000001\nshow K\n")

    child_a, master_a, reader_a = start_session(
        executable, working_directory, home)
    a_closed = False
    child_b = None
    master_b = None
    reader_b = None
    b_closed = False
    try:
        reader_a.read_until(b">")
        child_b, master_b, reader_b = start_session(
            executable, working_directory, home)
        reader_b.read_until(b">")
        output = send_command(master_b, reader_b, b"show S")
        require_completed_line(output, b"show S\n")

        write_all(master_a, b"\x10\x04")
        a_closed = True
        finish_session(child_a, master_a, reader_a, b"exit")
        b_closed = True
        finish_session(child_b, master_b, reader_b)
    finally:
        if not a_closed:
            try:
                os.close(master_a)
            except OSError:
                pass
            try:
                os.kill(child_a, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_a, 0)
            except ChildProcessError:
                pass
        if child_b is not None and not b_closed:
            try:
                os.close(master_b)
            except OSError:
                pass
            try:
                os.kill(child_b, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_b, 0)
            except ChildProcessError:
                pass

    expected = b"#1700000000\nshow I\nshow S\n"
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != expected:
        raise AssertionError(
            "timestamped removal lost a concurrent append or orphaned "
            f"metadata: {history!r}")


def check_logical_history_cap_preserves_blocks(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    os.makedirs(state_directory)
    history_path = os.path.join(state_directory, "history")

    plain_entries = b"".join(
        f"entry{index:03d}\n".encode("ascii")
        for index in range(499))
    with open(history_path, "wb") as history_file:
        history_file.write(plain_entries)

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        output = send_bracketed_paste(master, reader, b"show\nI")
        if b"I is a fundamental name with arity:1\n" not in normalized(
                output):
            raise AssertionError(
                f"multiline cap setup failed; received {output!r}")
        with open(history_path, "rb") as history_file:
            history_at_limit = history_file.read()
        if history_at_limit != plain_entries + b"show\nI\n":
            raise AssertionError(
                "the 500-entry cap counted multiline physical lines: "
                f"{len(history_at_limit)} bytes")

        output = send_command(master, reader, b"show K")
        require_completed_line(output, b"show K\n")
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

    expected = (
        b"".join(
            f"entry{index:03d}\n".encode("ascii")
            for index in range(1, 499)) +
        b"show\nI\nshow K\n"
    )
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != expected:
        raise AssertionError(
            "logical cap split a multiline entry or removed the wrong "
            f"block: {len(history)} bytes")

    timestamp_blocks = b"".join(
        f"#{1700000000 + index}\nentry{index:03d}\n".encode("ascii")
        for index in range(500))
    with open(history_path, "wb") as history_file:
        history_file.write(timestamp_blocks)
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"show K")
        require_completed_line(output, b"show K\n")
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

    expected = (
        b"".join(
            f"#{1700000000 + index}\nentry{index:03d}\n".encode("ascii")
            for index in range(1, 500)) +
        b"show K\n"
    )
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != expected:
        raise AssertionError(
            "logical cap orphaned a timestamp or removed the wrong block: "
            f"{len(history)} bytes")


def check_opaque_run_cap_converges(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    history_path = os.path.join(state_directory, "history")
    opaque_entries = b"".join(
        f"entry{index:03d}\n".encode("ascii")
        for index in range(500))

    # A starts with no canonical knowledge of the history file.  The 500
    # physical lines that appear while it is idle are therefore one opaque
    # concurrent run: they might be 500 commands, or components of fewer
    # multiline commands.  Appending K must preserve all of them rather than
    # risk enforcing the cap by splitting an unknown logical entry.
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        os.makedirs(state_directory, exist_ok=True)
        with open(history_path, "wb") as history_file:
            history_file.write(opaque_entries)
        output = send_command(master, reader, b"K")
        require_completed_line(output, b"K\n")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
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

    preserved = opaque_entries + b"K\n"
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != preserved:
        raise AssertionError(
            "stale append split or capped an opaque concurrent run: "
            f"expected {len(preserved)} bytes, got {len(history)}")

    # A fresh session knows every retained plain line as a distinct history
    # entry.  Its next append can safely converge to the 500-entry cap by
    # dropping the first two old entries without splitting any block.
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"S")
        require_completed_line(output, b"S\n")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
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

    expected = (
        b"".join(
            f"entry{index:03d}\n".encode("ascii")
            for index in range(2, 500)) +
        b"K\nS\n"
    )
    with open(history_path, "rb") as history_file:
        history = history_file.read()
    if history != expected:
        raise AssertionError(
            "fresh append did not safely converge an opaque run to the "
            f"500-entry cap: expected {len(expected)} bytes, "
            f"got {len(history)}")


def check_rewrite_preserves_unedited_history(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        for command in (b"show I", b"show K", b"show S"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # Readline temporarily stores edits made to recalled lines so they
        # survive navigation.  Edit show S, leave it, return to the edited
        # line, then leave it again and remove the different pristine show K.
        # Rewriting persistent history must use the original show S source,
        # not Readline's temporary show Sx editor state.
        write_all(master, b"\x10x\x10\x0e\x10\x04")
        finish_session(child, master, reader, b"\x15exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "show I\nshow S\n":
        raise AssertionError(
            "deleting another recall persisted Readline's temporary edit: "
            f"{history!r}")


def check_rewrite_preserves_unedited_history_on_submit(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        for command in (b"show I", b"show K", b"show S"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # Return show S's editor buffer to different valid source, navigate
        # to show K, delete show K, and submit the temporary replacement.
        # Both retained originals must remain intact before the submitted
        # show I is appended as a genuinely new non-adjacent command.
        write_all(master, b"\x10\x7fI\x10\x04\n")
        output = reader.read_until(b">")
        if b"I is a fundamental name with arity:1\n" not in normalized(
                output):
            raise AssertionError(
                "edited replacement was not submitted after removal; "
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

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    expected_history = "show I\nshow S\nshow I\n"
    if history != expected_history:
        raise AssertionError(
            "submitting a temporary edited recall corrupted originals: "
            f"{history!r}")


def check_dedup_uses_last_submitted_history(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        for command in (b"show I", b"show K", b"show S"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # Temporarily edit newest show S into show I, leave it non-current,
        # then edit and submit older show K as the same show I.  Deduplication
        # must compare with the actual last submitted command, show S, rather
        # than Readline's temporary show I copy, so this show I is appended.
        write_all(master, b"\x10\x7fI\x10\x7fI\n")
        output = reader.read_until(b">")
        if b"I is a fundamental name with arity:1\n" not in normalized(
                output):
            raise AssertionError(
                "edited older recall was not submitted; "
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

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    expected_history = "show I\nshow K\nshow S\nshow I\n"
    if history != expected_history:
        raise AssertionError(
            "temporary edits changed adjacent duplicate suppression: "
            f"{history!r}")


def check_rerecalled_undo_history_deletion(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        for command in (b"show I", b"show K", b"show S"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # Edit show S and return it to the exact original bytes, navigate
        # away and back so Readline attaches its temporary undo state to the
        # re-recalled entry, then delete that pristine re-recall.  Besides
        # checking semantics, sanitizer runs exercise cleanup of that undo
        # data for leaks and invalid frees.
        write_all(master, b"\x10\x7fS\x10\x0e\x04")
        finish_session(child, master, reader, b"exit")
    except Exception:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(child, 0)
        except ChildProcessError:
            pass
        raise

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "show I\nshow K\n":
        raise AssertionError(
            "deleting a re-recalled entry with undo history changed the "
            f"wrong persistent lines: {history!r}")


def check_concurrent_history_append_survives_removal(
        executable, working_directory, home):
    state_directory = os.path.join(home, ".crepl")
    os.makedirs(state_directory)
    history_path = os.path.join(state_directory, "history")
    with open(history_path, "w", encoding="utf-8") as history_file:
        history_file.write("show I\nshow K\n")

    child_a, master_a, reader_a = start_session(
        executable, working_directory, home)
    a_closed = False
    child_b = None
    master_b = None
    reader_b = None
    b_closed = False
    try:
        reader_a.read_until(b">")
        child_b, master_b, reader_b = start_session(
            executable, working_directory, home)
        reader_b.read_until(b">")

        # B appends after A has loaded its own snapshot.  A then removes its
        # recalled show K.  A's rewrite must merge against the current disk
        # file rather than discarding B's independently appended show S.
        output = send_command(master_b, reader_b, b"show S")
        require_completed_line(output, b"show S\n")
        write_all(master_a, b"\x10\x04")

        a_closed = True
        finish_session(child_a, master_a, reader_a, b"exit")
        b_closed = True
        finish_session(child_b, master_b, reader_b)
    finally:
        if not a_closed:
            try:
                os.close(master_a)
            except OSError:
                pass
            try:
                os.kill(child_a, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_a, 0)
            except ChildProcessError:
                pass
        if child_b is not None and not b_closed:
            try:
                os.close(master_b)
            except OSError:
                pass
            try:
                os.kill(child_b, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(child_b, 0)
            except ChildProcessError:
                pass

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "show I\nshow S\n":
        raise AssertionError(
            "one session's history removal lost another session's append: "
            f"{history!r}")


def check_history_deletion_keys_edit_normally(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")

    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"show I")
        require_completed_line(output, b"show I\n")

        # Editing disarms removal even if the edit restores the original
        # bytes.  Ctrl-D at end-of-line is then the normal no-op delete.
        write_all(master, b"\x10\x7fI\x04\n")
        output = reader.read_until(b">")
        require_terminal_bell(
            output, "native forward-delete at end of an edited line")
        if b"I is a fundamental name with arity:1\n" not in normalized(
                output):
            raise AssertionError(
                "Ctrl-D removed history after a Backspace edit; "
                f"received {output!r}")

        # Moving away and back must also disarm removal permanently for this
        # recall, despite returning to the original text and caret location.
        write_all(master, b"\x10\x02\x06\x04\n")
        output = reader.read_until(b">")
        require_terminal_bell(
            output, "native forward-delete after restored cursor movement")
        if b"I is a fundamental name with arity:1\n" not in normalized(
                output):
            raise AssertionError(
                "Ctrl-D removed history after the caret moved away and "
                f"back; received {output!r}")

        # With the caret left before I, Ctrl-D must perform a real forward
        # delete, producing the invalid command "show ".
        write_all(master, b"\x10\x02\x04\n")
        output = reader.read_until(b">")
        require_no_terminal_bell(
            output, "native forward-delete of an existing character")
        if b"Parse error" not in normalized(output):
            raise AssertionError(
                "Ctrl-D did not forward-delete after a caret move; "
                f"received {output!r}")

        output = send_command(master, reader, b"show K ")
        require_completed_line(output, b"show K \n")

        # DEL/Backspace and Ctrl-H edit recalled input; neither adopts the
        # pristine-recall removal behavior reserved for default Ctrl-D.
        write_all(master, b"\x10\x7f\n")
        output = reader.read_until(b">")
        if b"K is a fundamental name with arity:2\n" not in normalized(
                output):
            raise AssertionError(
                "Backspace removed a recalled history entry; "
                f"received {output!r}")
        write_all(master, b"\x10\x08\n")
        output = reader.read_until(b">")
        if b"Parse error" not in normalized(output):
            raise AssertionError(
                "Ctrl-H removed a recalled history entry; "
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

    expected_history = "show I\nshow \nshow K \nshow K\nshow \n"
    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != expected_history:
        raise AssertionError(
            "forward/backward deletion unexpectedly removed a history "
            f"entry: {history!r}")


def check_ctrl_o_operate_and_get_next(
        executable, working_directory, home):
    history_path = os.path.join(home, ".crepl", "history")
    child, master, reader = start_session(
        executable, working_directory, home)
    try:
        reader.read_until(b">")
        for command in (b"show I", b"show K", b"show S"):
            output = send_command(master, reader, command)
            require_completed_line(output, command + b"\n")

        # Ctrl-O must still accept show K and prepare its next newer entry,
        # show S, for the following prompt.
        write_all(master, b"\x10\x10\x0f")
        output = reader.read_until(b">")
        if b"K is a fundamental name with arity:2\n" not in normalized(
                output):
            raise AssertionError(
                "Ctrl-O did not operate on the recalled entry; "
                f"received {output!r}")

        # Ctrl-O preloads show S at the next prompt.  It counts as a fresh,
        # untouched recall, so Ctrl-D removes it and reveals the next newer
        # entry: the show K command that Ctrl-O just submitted.
        write_all(master, b"\x04\n")
        output = reader.read_until(b">")
        require_no_terminal_bell(
            output, "successful Ctrl-O-preloaded history deletion")
        if b"K is a fundamental name with arity:2\n" not in normalized(
                output):
            raise AssertionError(
                "Ctrl-D did not remove Ctrl-O's pristine preloaded entry; "
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

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "show I\nshow K\nshow K\n":
        raise AssertionError(
            "Ctrl-D did not persist removal of Ctrl-O's preloaded entry: "
            f"{history!r}")


def check_custom_ctrl_d_binding_is_preserved(
        executable, working_directory, home):
    inputrc = os.path.join(home, "custom.inputrc")
    with open(inputrc, "w", encoding="utf-8") as inputrc_file:
        inputrc_file.write(
            "set editing-mode emacs\n"
            '"\\C-d": backward-char\n')
    history_path = os.path.join(home, ".crepl", "history")

    child, master, reader = start_session(
        executable, working_directory, home, inputrc)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"I")
        require_completed_line(output, b"I\n")

        # The temporary inputrc maps Ctrl-D to backward-char.  On a pristine
        # recall the first press must move before I so the inserted K produces
        # KI, without removing the recalled I from history.  A second press at
        # the start of the line must retain that custom action's native bell.
        write_all(master, b"\x10\x04\x04K\n")
        output = reader.read_until(b">")
        require_terminal_bell(
            output, "invalid custom backward-char Ctrl-D mapping")
        if b"No further reductions\n" not in normalized(output):
            raise AssertionError(
                "the custom INPUTRC Ctrl-D mapping was not effective; "
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
        finish_session(child, master, reader, b"exit")

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "I\nKI\n":
        raise AssertionError(
            "a custom INPUTRC Ctrl-D mapping removed history or was "
            f"overwritten: {history!r}")


def check_multikey_ctrl_d_binding_is_preserved(
        executable, working_directory, home):
    inputrc = os.path.join(home, "multikey.inputrc")
    with open(inputrc, "w", encoding="utf-8") as inputrc_file:
        inputrc_file.write(
            "set editing-mode emacs\n"
            "set bell-style audible\n"
            '"\\C-x\\C-d": delete-char\n')
    history_path = os.path.join(home, ".crepl", "history")

    child, master, reader = start_session(
        executable, working_directory, home, inputrc)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"I")
        require_completed_line(output, b"I\n")

        # Ctrl-D is the final byte of a custom two-key sequence here.  Its
        # delete-char action at end-of-line must keep Readline's native bell;
        # CREPL may silence only a top-level Ctrl-D that it will replace with
        # successful pristine-history removal.
        write_all(master, b"\x10\x18\x04\n")
        output = reader.read_until(b">")
        require_terminal_bell(
            output, "custom multi-key sequence ending in Ctrl-D")
        if b"No further reductions\n" not in normalized(output):
            raise AssertionError(
                "the custom multi-key Ctrl-D sequence changed the recalled "
                f"entry; received {output!r}")
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
        finish_session(child, master, reader, b"exit")

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "I\n":
        raise AssertionError(
            "a custom multi-key Ctrl-D sequence removed or duplicated "
            f"history: {history!r}")


def check_vi_ctrl_d_binding_is_preserved(
        executable, working_directory, home):
    inputrc = os.path.join(home, "vi.inputrc")
    with open(inputrc, "w", encoding="utf-8") as inputrc_file:
        inputrc_file.write("set editing-mode vi\n")
    history_path = os.path.join(home, ".crepl", "history")

    child, master, reader = start_session(
        executable, working_directory, home, inputrc)
    try:
        reader.read_until(b">")
        output = send_command(master, reader, b"I")
        require_completed_line(output, b"I\n")

        # In vi insertion mode Ctrl-D retains Readline's rl_vi_eof_maybe
        # behavior.  On a nonempty recalled line it submits I; it must not
        # invoke CREPL's default-emacs forward-delete history removal.
        write_all(master, b"\x1b[A\x04")
        output = reader.read_until(b">")
        if b"No further reductions\n" not in normalized(output):
            raise AssertionError(
                "vi-mode Ctrl-D did not submit the recalled entry; "
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
        finish_session(child, master, reader, b"exit")

    with open(history_path, encoding="utf-8") as history_file:
        history = history_file.read()
    if history != "I\n":
        raise AssertionError(
            "vi-mode Ctrl-D removed or duplicated recalled history: "
            f"{history!r}")


def check_ctrl_d_empty_prompt_still_exits(
        executable, working_directory, home):
    child, master, reader = start_session(
        executable, working_directory, home)
    child_reaped = False
    try:
        reader.read_until(b">")
        write_all(master, b"\x04")
        reader.read_to_end()
        status = wait_for_child(child)
        child_reaped = True
        exit_code = exit_code_from_wait_status(status)
        if exit_code != 0:
            raise AssertionError(
                f"empty-prompt Ctrl-D exited with status {exit_code}")
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

        ctrl_d_memory_home = os.path.join(
            temporary, "ctrl-d-memory-home")
        os.makedirs(ctrl_d_memory_home)
        check_ctrl_d_removes_in_memory_history(
            executable, working_directory, ctrl_d_memory_home)

        ctrl_d_loaded_home = os.path.join(
            temporary, "ctrl-d-loaded-home")
        os.makedirs(ctrl_d_loaded_home)
        check_ctrl_d_removes_loaded_history(
            executable, working_directory, ctrl_d_loaded_home)

        exact_bytes_home = os.path.join(
            temporary, "exact-bytes-home")
        os.makedirs(exact_bytes_home)
        check_rewrite_preserves_unrelated_history_bytes(
            executable, working_directory, exact_bytes_home)

        symlink_home = os.path.join(
            temporary, "symlink-home")
        os.makedirs(symlink_home)
        check_rewrite_preserves_history_symlink(
            executable, working_directory, symlink_home)

        multiline_home = os.path.join(
            temporary, "multiline-home")
        os.makedirs(multiline_home)
        check_multiline_history_block_removal(
            executable, working_directory, multiline_home)

        multiline_concurrent_home = os.path.join(
            temporary, "multiline-concurrent-home")
        os.makedirs(multiline_concurrent_home)
        check_concurrent_append_survives_multiline_removal(
            executable, working_directory, multiline_concurrent_home)

        long_multiline_concurrent_home = os.path.join(
            temporary, "long-multiline-concurrent-home")
        os.makedirs(long_multiline_concurrent_home)
        check_long_multiline_concurrent_append_survives_removal(
            executable,
            working_directory,
            long_multiline_concurrent_home,
        )

        mixed_long_divergence_home = os.path.join(
            temporary, "mixed-long-divergence-home")
        os.makedirs(mixed_long_divergence_home)
        check_mixed_long_divergence_removal(
            executable,
            working_directory,
            mixed_long_divergence_home,
        )

        scattered_multiline_home = os.path.join(
            temporary, "scattered-multiline-home")
        os.makedirs(scattered_multiline_home)
        check_scattered_multiline_match_is_not_removed(
            executable,
            working_directory,
            scattered_multiline_home,
        )

        timestamp_home = os.path.join(
            temporary, "timestamp-home")
        os.makedirs(timestamp_home)
        check_timestamp_history_block_removal(
            executable, working_directory, timestamp_home)

        timestamp_concurrent_home = os.path.join(
            temporary, "timestamp-concurrent-home")
        os.makedirs(timestamp_concurrent_home)
        check_concurrent_append_survives_timestamp_removal(
            executable, working_directory, timestamp_concurrent_home)

        logical_cap_home = os.path.join(
            temporary, "logical-cap-home")
        os.makedirs(logical_cap_home)
        check_logical_history_cap_preserves_blocks(
            executable, working_directory, logical_cap_home)

        opaque_cap_home = os.path.join(
            temporary, "opaque-cap-home")
        os.makedirs(opaque_cap_home)
        check_opaque_run_cap_converges(
            executable, working_directory, opaque_cap_home)

        edited_recall_home = os.path.join(
            temporary, "edited-recall-home")
        os.makedirs(edited_recall_home)
        check_rewrite_preserves_unedited_history(
            executable, working_directory, edited_recall_home)

        edited_submit_home = os.path.join(
            temporary, "edited-submit-home")
        os.makedirs(edited_submit_home)
        check_rewrite_preserves_unedited_history_on_submit(
            executable, working_directory, edited_submit_home)

        edited_dedup_home = os.path.join(
            temporary, "edited-dedup-home")
        os.makedirs(edited_dedup_home)
        check_dedup_uses_last_submitted_history(
            executable, working_directory, edited_dedup_home)

        undo_recall_home = os.path.join(
            temporary, "undo-recall-home")
        os.makedirs(undo_recall_home)
        check_rerecalled_undo_history_deletion(
            executable, working_directory, undo_recall_home)

        concurrent_home = os.path.join(
            temporary, "concurrent-home")
        os.makedirs(concurrent_home)
        check_concurrent_history_append_survives_removal(
            executable, working_directory, concurrent_home)

        deletion_keys_home = os.path.join(
            temporary, "deletion-keys-home")
        os.makedirs(deletion_keys_home)
        check_history_deletion_keys_edit_normally(
            executable, working_directory, deletion_keys_home)

        ctrl_o_home = os.path.join(temporary, "ctrl-o-home")
        os.makedirs(ctrl_o_home)
        check_ctrl_o_operate_and_get_next(
            executable, working_directory, ctrl_o_home)

        custom_ctrl_d_home = os.path.join(
            temporary, "custom-ctrl-d-home")
        os.makedirs(custom_ctrl_d_home)
        check_custom_ctrl_d_binding_is_preserved(
            executable, working_directory, custom_ctrl_d_home)

        multikey_ctrl_d_home = os.path.join(
            temporary, "multikey-ctrl-d-home")
        os.makedirs(multikey_ctrl_d_home)
        check_multikey_ctrl_d_binding_is_preserved(
            executable, working_directory, multikey_ctrl_d_home)

        vi_ctrl_d_home = os.path.join(
            temporary, "vi-ctrl-d-home")
        os.makedirs(vi_ctrl_d_home)
        check_vi_ctrl_d_binding_is_preserved(
            executable, working_directory, vi_ctrl_d_home)

        ctrl_d_exit_home = os.path.join(
            temporary, "ctrl-d-exit-home")
        os.makedirs(ctrl_d_exit_home)
        check_ctrl_d_empty_prompt_still_exits(
            executable, working_directory, ctrl_d_exit_home)


if __name__ == "__main__":
    main()
