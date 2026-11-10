import unittest
from pathlib import Path

from check_portability import forbidden_symbols, licenses_unchanged, native_includes, patch_bytes


class PortabilityChecks(unittest.TestCase):
    def test_native_headers_are_checked_at_their_active_owner(self):
        root = Path("/project")
        output = (
            '# 12 "/project/tinyx/os/utils.c"\n'
            "#include <sys/wait.h>\n"
            '# 1 "/usr/include/stdlib.h"\n'
            "#include <sys/types.h>\n"
            '# 20 "/project/libX11/src/Macros.c"\n'
            "#include <pthread.h>\n"
        )
        self.assertEqual(native_includes(output, (root,)), {
            "/project/tinyx/os/utils.c:12: sys/wait.h",
            "/project/libX11/src/Macros.c:20: pthread.h",
        })

    def test_protocol_headers_cannot_hide_behind_system_include_paths(self):
        output = '# 1 "/usr/include/X11/Xos.h"\n#include <unistd.h>\n'
        self.assertEqual(len(native_includes(output, (Path("/project"),))), 1)

    def test_symbol_versions_and_archive_member_names(self):
        output = (
            "select.c.o:\n                 U malloc\n"
            "archive.a:utils.c.o:                 U fork\n"
            "                 U uname@GLIBC_2.2.5\n"
            "                 U XCreateSimpleWindow\n"
        )
        self.assertEqual(forbidden_symbols(output), {"fork", "uname"})
        self.assertEqual(forbidden_symbols("                 U fopen\n"), {"fopen"})

    def test_original_license_bytes_must_be_preserved(self):
        before = b"/* Copyright \xa9 Example; permission granted. */\nint x;\n"
        self.assertTrue(licenses_unchanged(before, before + b"int y;\n"))
        self.assertFalse(licenses_unchanged(before, before.replace(b"\xa9", b"?")))
        patch = patch_bytes("example.c", before, before + b"int y;\n")
        self.assertIn(b"--- a/example.c\n", patch)
        self.assertIn(b"+int y;", patch)


if __name__ == "__main__":
    unittest.main()
