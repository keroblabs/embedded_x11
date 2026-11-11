#!/usr/bin/env python3
"""Verify the actual SDK opacity, compiler profiles, archive and application linkage."""
import argparse
import json
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def command(arguments: list[str], source: str | None = None,
            expected: bool = True) -> subprocess.CompletedProcess:
    result = subprocess.run(arguments, input=source, text=True, capture_output=True)
    if expected and result.returncode:
        raise RuntimeError(shlex.join(arguments) + "\n" + result.stderr[-6000:])
    return result


def compiler_flags(build: Path) -> list[str]:
    entry = next(item for item in json.loads((build / "compile_commands.json").read_text())
                 if "/wmb_opaque_macro_test.dir/" in item["command"])
    arguments = shlex.split(entry["command"])
    result = []
    index = 0
    while index < len(arguments):
        value = arguments[index]
        if value == "-o":
            index += 2
            continue
        if value == "-c" or value == entry["file"] or value.startswith("-std="):
            index += 1
            continue
        result.append(value)
        index += 1
    if any("_XSERVER64" in value or "/generated/tinyx/" in value or "-DWMB_DEVEL" in value
           for value in result):
        raise RuntimeError("Server ABI leaked into the public SDK")
    return result


def opaque_profiles(flags: list[str]) -> None:
    source = (ROOT / "transport/api/tests/all_macros.c").read_text()
    source = source.replace('#include "wmb_xlib_compat.h"', "#include <X11/Xlib.h>")
    for language, standard in (("c", "c11"), ("c++", "c++17")):
        for value in ("1", "0", ""):
            overrides = ["-UWMB_INPROC", "-UWMB_APP_OPAQUE",
                         "-DWMB_INPROC=" + value, "-DWMB_APP_OPAQUE=" + value]
            command(flags + overrides + ["-x", language, "-std=" + standard,
                                         "-fsyntax-only", "-"], source)
    for expression in ("sizeof(Display)", "sizeof(Screen)", "((Display *)0)->fd",
                       "((Screen *)0)->width"):
        result = command(flags + ["-x", "c", "-std=c11", "-fsyntax-only", "-"],
                         '#include "wmb_xlib_compat.h"\n'
                         f"int example(void) {{ return (int)({expression}); }}\n", False)
        if result.returncode == 0 or "incomplete" not in result.stderr:
            raise RuntimeError("Opaque SDK allowed a private layout access: " + expression)
    negatives = (
        ('#include "wmb_xlib_compat.h"\n',
         "cannot enable XLIB_ILLEGAL_ACCESS", ["-DXLIB_ILLEGAL_ACCESS"]),
        ('#include <X11/Xlibint.h>\n', "must not include private Xlib headers", []),
        ('#include "wmb_xlib_compat.h"\n', "require the matching WMB_INPROC", ["-UWMB_INPROC"]),
    )
    for code, diagnostic, overrides in negatives:
        result = command(flags + overrides + ["-x", "c", "-fsyntax-only", "-"], code, False)
        if result.returncode == 0 or diagnostic not in result.stderr:
            raise RuntimeError("SDK misuse failed without its intended diagnostic: " + diagnostic)
    print("PASS SDK: all 51 macros in C/C++, defined one/zero/empty, real Display/Screen opacity")


def wrapper_archive(archive: Path, flags: list[str]) -> None:
    definition = json.loads((ROOT / "transport/api/api.def").read_text())
    output = command(["nm", "-g", "--defined-only", str(archive)]).stdout
    symbols = {match[1] for match in re.finditer(r"^[0-9a-fA-F]+\s+[A-Z]\s+(\S+)$",
                                               output, re.MULTILINE)}
    expected = {"wmb_bind"} | {f"wba{index}" for index in range(len(definition["functions"]))}
    if symbols != expected:
        raise RuntimeError("Unexpected/missing SDK exports: " + str(symbols ^ expected))
    size = archive.stat().st_size
    if any("-fsanitize=" in value for value in flags):
        print(f"INFO sanitizer SDK archive={size} bytes; production byte gate is not this instrumented build")
    else:
        if size >= 16384:
            raise RuntimeError(f"Actual archive is {size} bytes, must be strictly below 16384")
        print(f"PASS actual libX11-app.a={size} bytes <16384, only 123 typed thunks plus wmb_bind")


def application(binary: Path) -> None:
    dynamic = command(["readelf", "-d", str(binary)]).stdout
    if re.search(r"Shared library: \[.*(?:X11|xcb|SDL|pthread)", dynamic):
        raise RuntimeError("External hello links the subject/system Xlib or a display/thread backend")
    symbols = command(["nm", "--defined-only", str(binary)]).stdout
    if re.search(r"\b(?:_XReply|_XFlush|_XRead|_XSend|_XGetRequest|XOpenDisplay)$",
                 symbols, re.MULTILINE):
        raise RuntimeError("The supposedly thin external app contains retained Xlib")
    print("PASS external executable: wrapper/scaffold/libc only, no full Xlib/TinyX/SDL")


def standalone_consumer() -> None:
    with tempfile.TemporaryDirectory(prefix="wmb-sdk-") as directory:
        root = Path(directory)
        checkout = root / "checkout with spaces"
        checkout.symlink_to(ROOT, target_is_directory=True)
        source, build = root / "consumer project", root / "build with spaces"
        source.mkdir()
        (source / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.16)
project(sdk_consumer C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-pedantic-errors)
add_subdirectory("${WMB_CHECKOUT}/wrapper/libX11-app" sdk)
add_executable(consumer_c main.c)
target_link_libraries(consumer_c PRIVATE wmb_app_wrapper)
add_executable(consumer_cpp main.cpp)
target_link_libraries(consumer_cpp PRIVATE wmb_app_wrapper)
add_executable(consumer_v1 main.c)
target_link_libraries(consumer_v1 PRIVATE wmb_app_wrapper_v1)
""")
        code = """#include <X11/Xlib.h>
#include "wmb_api.h"
#if !defined(__cplusplus) && __STDC_VERSION__ != 199901L
#error "The SDK must not change its consumer's C dialect"
#endif
int main(void) { return wmb_bind(NULL) == -1 ? 0 : 1; }
"""
        (source / "main.c").write_text(code)
        (source / "main.cpp").write_text(code)
        command(["cmake", "-S", str(source), "-B", str(build),
                 "-DWMB_CHECKOUT=" + str(checkout)])
        command(["cmake", "--build", str(build), "--target",
                 "consumer_c", "consumer_cpp", "consumer_v1", "-j", "2"])
        for name in ("consumer_c", "consumer_cpp", "consumer_v1"):
            command([str(build / name)])
    print("PASS standalone SDK: separate C99/C++11 project, both ABI generations, paths with spaces")


def wrong_thread(binary: Path) -> None:
    result = subprocess.run([str(binary), "--wrong-native-thread"],
                            env={**os.environ, "SDL_VIDEODRIVER": "dummy",
                                 "SDL_RENDER_DRIVER": "software"},
                            text=True, capture_output=True, timeout=10)
    if result.returncode != -6 or "native table requires the runtime thread" not in result.stderr:
        raise RuntimeError("Foreign caller entered the native table without its owner diagnostic\n"
                           + result.stderr[-4000:])
    print("PASS foreign misuse: rejected before any Xlib/fiber runtime entry")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--gateway", type=Path, required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    flags = compiler_flags(args.build_dir)
    opaque_profiles(flags)
    wrapper_archive(args.archive, flags)
    application(args.application)
    standalone_consumer()
    wrong_thread(args.gateway)


if __name__ == "__main__":
    main()
