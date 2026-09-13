#!/usr/bin/env python3

import os
import pathlib
import pty
import select
import sys
import time


def read_until(fd, needle, timeout=5):
    output = bytearray()
    deadline = time.monotonic() + timeout
    while needle not in output:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError(f"timed out waiting for {needle!r}: {output!r}")
        readable, _, _ = select.select([fd], [], [], remaining)
        if not readable:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        output.extend(chunk)
    return bytes(output)


def main():
    repository = pathlib.Path(__file__).resolve().parents[1]
    executable = (
        pathlib.Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else repository / "cpp-agent"
    )
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(repository)
        os.execv(executable, [str(executable), "--demo"])

    transcript = bytearray()
    try:
        transcript.extend(read_until(fd, b"> "))
        os.write(fd, b"/sta\t\n")
        transcript.extend(read_until(fd, b"provider=demo model=demo"))
        if b"provider=demo model=demo" not in transcript:
            raise AssertionError(f"Tab did not complete /status: {transcript!r}")

        os.write(fd, b"/skills cpp-\t\n")
        transcript.extend(read_until(fd, b"cpp-review - Review C++ code"))
        if b"cpp-review - Review C++ code" not in transcript:
            raise AssertionError("Tab did not complete a skill name")

        os.write(fd, "中文".encode() + b"\x7f\n")
        transcript.extend(read_until(fd, b"[turn] tokens=n/a"))
        decoded = transcript.decode(errors="replace")
        if "Demo model echo: 中" not in decoded:
            raise AssertionError("UTF-8 backspace did not remove one codepoint")
        if "[turn] tokens=n/a | context≈" not in decoded:
            raise AssertionError("turn status footer was not printed")

        os.write(fd, b"/quit\n")
        _, status = os.waitpid(pid, 0)
        if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
            raise AssertionError(f"CLI exited abnormally: status={status}")
    finally:
        try:
            os.close(fd)
        except OSError:
            pass

    print("CLI PTY test passed")


if __name__ == "__main__":
    main()
