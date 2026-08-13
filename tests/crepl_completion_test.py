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

import errno
import os
import re
import select
import signal
import sys
import tempfile
import time


class PtyReader:
    def __init__(self, descriptor):
        self.descriptor = descriptor
        self.buffer = bytearray()

    def read_until(self, marker, timeout=10.0):
        deadline = time.monotonic() + timeout
        while True:
            marker_position = self.buffer.find(marker)
            if marker_position != -1:
                end = marker_position + len(marker)
                result = bytes(self.buffer[:end])
                del self.buffer[:end]
                return result

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(
                    f"timed out waiting for {marker!r}; "
                    f"received {bytes(self.buffer)!r}")
            readable, _, _ = select.select(
                [self.descriptor], [], [], remaining)
            if not readable:
                continue
            try:
                chunk = os.read(self.descriptor, 4096)
            except OSError as error:
                if error.errno == errno.EIO:
                    chunk = b""
                else:
                    raise
            if not chunk:
                raise AssertionError(
                    f"CREPL exited while waiting for {marker!r}; "
                    f"received {bytes(self.buffer)!r}")
            self.buffer.extend(chunk)

    def read_to_end(self, timeout=10.0):
        result = bytearray(self.buffer)
        self.buffer.clear()
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(
                    f"timed out waiting for CREPL to exit; "
                    f"received {bytes(result)!r}")
            readable, _, _ = select.select(
                [self.descriptor], [], [], remaining)
            if not readable:
                continue
            try:
                chunk = os.read(self.descriptor, 4096)
            except OSError as error:
                if error.errno == errno.EIO:
                    return bytes(result)
                raise
            if not chunk:
                return bytes(result)
            result.extend(chunk)


def normalized(output):
    without_ansi = re.sub(
        rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", output)
    return without_ansi.replace(b"\r", b"")


def require_completed_line(output, expected):
    if expected not in normalized(output):
        raise AssertionError(
            f"expected completed line {expected!r}; received {output!r}")


def write_all(descriptor, data):
    while data:
        written = os.write(descriptor, data)
        data = data[written:]


def wait_for_child(child, timeout=10.0):
    deadline = time.monotonic() + timeout
    while True:
        completed, status = os.waitpid(child, os.WNOHANG)
        if completed != 0:
            return status
        if time.monotonic() >= deadline:
            raise AssertionError("timed out waiting for CREPL to exit")
        time.sleep(0.01)


def exit_code_from_wait_status(status):
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    if os.WIFSIGNALED(status):
        return -os.WTERMSIG(status)
    return status


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: crepl_completion_test.py CREPL_EXECUTABLE")

    executable = os.path.abspath(sys.argv[1])
    working_directory = tempfile.TemporaryDirectory(
        prefix="crepl-completion-")
    with open(
            os.path.join(working_directory.name, "set_list.cmb"),
            "w", encoding="utf-8") as load_file:
        load_file.write("set DefaultLoad = 0 I")
    with open(
            os.path.join(
                working_directory.name,
                "remembered load definitions.cmb"),
            "w", encoding="utf-8") as load_file:
        load_file.write("set RememberedLoad = 0 K")
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

        write_all(master, b"key   st\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"key   step \n")

        write_all(master, b"key   step   of\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"key   step   off \n")

        write_all(master, b"ste\tlim\t2\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"step limit 2\n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected successful step limit 2; received {output!r}")

        write_all(master, b"step   limit   o\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"step   limit   off \n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected successful step limit off; received {output!r}")

        write_all(master, b"ref\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"references \n")
        if b"Parse error" not in normalized(output):
            raise AssertionError(
                f"expected bare references parse error; received {output!r}")

        write_all(master, b"references   cap\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"references   captured \n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected successful references captured; received {output!r}")

        write_all(master, b"references   live\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"references   live \n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected successful references live; received {output!r}")

        write_all(master, b"sav\t\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"save set_list.cmb \n")
        if b"Saved set_list.cmb\n" not in normalized(output):
            raise AssertionError(
                f"expected references commands to be saved; received {output!r}")

        write_all(master, b"set CompleteGone = 1 I\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"set CompleteGone = 1 I\n")

        write_all(master, b"rem\tCompleteGone\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"remove CompleteGone\n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected successful remove; received {output!r}")

        write_all(master, b"set DepSource = 1 I\n")
        reader.read_until(b">")
        write_all(master, b"set DepUser = 1 DepSource\n")
        reader.read_until(b">")

        write_all(master, b"rev\tDepSource\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"revisions DepSource\n")
        normalized_output = normalized(output)
        if b"Parse error" in normalized_output:
            raise AssertionError(
                f"expected successful revisions; received {output!r}")
        if (b"DepSource@1 arity:1 I [live] [current]\n"
                not in normalized_output):
            raise AssertionError(
                f"expected revision listing; received {output!r}")

        write_all(master, b"dependso\tDepSource\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"dependson DepSource\n")
        normalized_output = normalized(output)
        if b"Parse error" in normalized_output:
            raise AssertionError(
                f"expected successful dependson; received {output!r}")
        if (b"DepSource is depended on by: DepUser\n"
                not in normalized_output):
            raise AssertionError(
                f"expected dependson result; received {output!r}")

        write_all(master, b"depends-\tDepSource\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"depends-on DepSource\n")
        normalized_output = normalized(output)
        if b"Parse error" in normalized_output:
            raise AssertionError(
                f"expected successful depends-on; received {output!r}")
        if (b"DepSource is depended on by: DepUser\n"
                not in normalized_output):
            raise AssertionError(
                f"expected depends-on result; received {output!r}")

        write_all(master, b"depends   o\tDepSource\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"depends   on DepSource\n")
        normalized_output = normalized(output)
        if b"Parse error" in normalized_output:
            raise AssertionError(
                f"expected successful depends on; received {output!r}")
        if (b"DepSource is depended on by: DepUser\n"
                not in normalized_output):
            raise AssertionError(
                f"expected depends on result; received {output!r}")

        write_all(master, b"usedb\tDepUser\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"usedby DepUser\n")
        normalized_output = normalized(output)
        if b"Parse error" in normalized_output:
            raise AssertionError(
                f"expected successful usedby; received {output!r}")
        if b"DepUser uses: DepSource\n" not in normalized_output:
            raise AssertionError(
                f"expected usedby result; received {output!r}")

        write_all(master, b"used-\tDepUser\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"used-by DepUser\n")
        normalized_output = normalized(output)
        if b"Parse error" in normalized_output:
            raise AssertionError(
                f"expected successful used-by; received {output!r}")
        if b"DepUser uses: DepSource\n" not in normalized_output:
            raise AssertionError(
                f"expected used-by result; received {output!r}")

        write_all(master, b"used   b\tDepUser\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"used   by DepUser\n")
        normalized_output = normalized(output)
        if b"Parse error" in normalized_output:
            raise AssertionError(
                f"expected successful used by; received {output!r}")
        if b"DepUser uses: DepSource\n" not in normalized_output:
            raise AssertionError(
                f"expected used by result; received {output!r}")

        write_all(master, b"set Remember = 1 I\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"set Remember = 1 I\n")

        write_all(master, b"sho\ta\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"show all \n")
        if b"set Remember = 1 I\n" not in normalized(output):
            raise AssertionError(
                f"expected show all output; received {output!r}")

        write_all(master, b"fin\t1 ?xy = x(yx)\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"find 1 ?xy = x(yx)\n")
        if b"?=A\n" not in normalized(output):
            raise AssertionError(
                f"expected Applicator match; received {output!r}")

        write_all(master, b"find a\t?x = MM\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"find all ?x = MM\n")
        if b"No match within search bounds\n" not in normalized(output):
            raise AssertionError(
                f"expected completed find all no-match; received {output!r}")

        write_all(master, b"find 2\t?x = MM\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"find 2 ?x = MM\n")
        if b"No match within search bounds\n" not in normalized(output):
            raise AssertionError(
                f"expected completed find 2 no-match; received {output!r}")

        write_all(master, b"find all 4\t?x = MM\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"find all 4 ?x = MM\n")
        if b"No match within search bounds\n" not in normalized(output):
            raise AssertionError(
                f"expected completed find all 4 no-match; received {output!r}")

        write_all(master, b"abs\t\txy = x(yx)\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"abstract ?xy = x(yx)\n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected completed abstract command; received {output!r}")
        if b"?=A\n" not in normalized(output):
            raise AssertionError(
                f"expected abstract result; received {output!r}")

        write_all(master, b"abstract   s\t  \tx = x\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"abstract   steps   ?x = x\n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected completed abstract steps command; received {output!r}")
        normalized_output = normalized(output)
        if (b"takeout x from x: I\n" not in normalized_output or
                b"?=I\n" not in normalized_output):
            raise AssertionError(
                f"expected abstract steps trace; received {output!r}")

        write_all(master, b"abstract   m\t  \txy = y(xy)\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"abstract   ministeps   ?xy = y(xy)\n")
        normalized_output = normalized(output)
        expected_ministeps = (
            b"takeout y from y(xy): O[takeout y from xy]\n"
            b"= Ox\n"
            b"takeout x from Ox: O\n"
            b"?=O\n"
        )
        if expected_ministeps not in normalized_output:
            raise AssertionError(
                f"expected abstract ministeps trace; received {output!r}")

        write_all(master, b"define   c\tTraceComplete x = x\n")
        output = reader.read_until(b">")
        require_completed_line(
            output,
            b"define   captured TraceComplete x = x\n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected captured define; received {output!r}")

        write_all(master, b"set   l\tLiveComplete = 1 TraceComplete\n")
        output = reader.read_until(b">")
        require_completed_line(
            output,
            b"set   live LiveComplete = 1 TraceComplete\n")
        if b"Parse error" in normalized(output):
            raise AssertionError(
                f"expected live set; received {output!r}")

        write_all(master, b"show TraceComplete\n")
        output = reader.read_until(b">")
        if b"arity:1 I\n" not in normalized(output):
            raise AssertionError(
                f"expected captured definition; received {output!r}")

        write_all(master, b"save remembered definitions.cmb\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"save remembered definitions.cmb\n")
        if b"Saved remembered definitions.cmb\n" not in normalized(output):
            raise AssertionError(
                f"expected successful save; received {output!r}")

        write_all(master, b"save \t\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"save remembered definitions.cmb \n")
        if b"Saved remembered definitions.cmb\n" not in normalized(output):
            raise AssertionError(
                f"expected remembered save; received {output!r}")

        write_all(master, b"loa\t\t\n")
        output = reader.read_until(b">")
        require_completed_line(output, b"load set_list.cmb \n")
        if b"Loaded set_list.cmb\n" not in normalized(output):
            raise AssertionError(
                f"expected default load; received {output!r}")

        write_all(master, b"load remembered load definitions.cmb\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"load remembered load definitions.cmb\n")
        if b"Loaded remembered load definitions.cmb\n" not in normalized(
                output):
            raise AssertionError(
                f"expected successful load; received {output!r}")

        write_all(master, b"load missing definitions.cmb\n")
        output = reader.read_until(b">")
        if (b"Could not open missing definitions.cmb for reading\n"
                not in normalized(output)):
            raise AssertionError(
                f"expected failed load; received {output!r}")

        write_all(master, b"load \t\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"load remembered load definitions.cmb \n")
        if b"Loaded remembered load definitions.cmb\n" not in normalized(
                output):
            raise AssertionError(
                f"expected remembered load; received {output!r}")

        write_all(master, b"save \t\n")
        output = reader.read_until(b">")
        require_completed_line(
            output, b"save remembered definitions.cmb \n")
        if b"Saved remembered definitions.cmb\n" not in normalized(output):
            raise AssertionError(
                f"expected independent remembered save; received {output!r}")

        write_all(master, b"exi\t\n")
        output = reader.read_to_end()
        require_completed_line(output, b"exit \n")

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
