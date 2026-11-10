#!/usr/bin/env python3
"""Run the same portable JWM archive against an isolated stock Xvfb."""

import argparse
import os
import selectors
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xvfb", required=True)
    parser.add_argument("--program", required=True)
    args = parser.parse_args()
    server = subprocess.Popen(
        [args.xvfb, "-displayfd", "1", "-screen", "0", "800x480x16",
         "-nolisten", "tcp", "-noreset", "-ac"],
        stdout=subprocess.PIPE,
        text=True,
    )
    try:
        if server.stdout is None:
            raise RuntimeError("Reference server stdout unavailable")
        with selectors.DefaultSelector() as selector:
            selector.register(server.stdout, selectors.EVENT_READ)
            if not selector.select(timeout=5):
                raise RuntimeError("Reference server did not become ready")
        number = server.stdout.readline().strip()
        if not number.isdigit() or server.poll() is not None:
            raise RuntimeError("Reference server failed to announce its display")
        environment = os.environ.copy()
        environment["DISPLAY"] = ":" + number
        result = subprocess.run([args.program], env=environment, timeout=10)
        return result.returncode
    finally:
        if server.poll() is None:
            server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
