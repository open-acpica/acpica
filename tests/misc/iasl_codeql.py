#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only
"""Run with: python3 iasl_codeql.py <path-to-iasl>."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


class IaslCodeqlTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def run_iasl(self, *arguments):
        result = subprocess.run(
            [str(self.iasl), *arguments],
            cwd=self.directory,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_comment_conversion_filenames(self):
        for filename in (
            "normal.asl",
            "short.a",
            "no_extension",
            "directory.with.dots/no_extension",
        ):
            with self.subTest(filename=filename):
                source = self.directory / filename
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text(
                    'DefinitionBlock ("", "SSDT", 2, "MSFT", "CODEQL", 1)\n'
                    "{\n"
                    "    // Filename allocation regression\n"
                    "    Name (TEST, 0x1234)\n"
                    "}\n",
                    encoding="ascii",
                )
                self.run_iasl("-p", "result", "-q", filename)
                compiled = (self.directory / "result.xxx").read_bytes()
                self.assertIn(
                    Path(filename).with_suffix(".dsl").as_posix().encode("ascii")
                    + b"\0",
                    compiled,
                )
                self.assertIn(b"Filename allocation regression", compiled)

    def test_mrrm_offset_crosses_64k(self):
        # Exercise table lengths immediately below, at, and above 64 KiB.
        for count in (2045, 2046, 2047, 2048):
            with self.subTest(entries=count):
                entry = struct.pack("<HHIQQHBBI", 0, 32, 0, 0, 4096, 0, 0, 0, 0)
                body = bytes([255, 0]) + bytes(26) + entry * count
                header = struct.pack(
                    "<4sIBB6s8sI4sI",
                    b"MRRM",
                    36 + len(body),
                    1,
                    0,
                    b"MSFT  ",
                    b"CODEQL  ",
                    1,
                    b"TEST",
                    1,
                )
                table = bytearray(header + body)
                table[9] = (-sum(table)) & 0xFF
                (self.directory / "mrrm.aml").write_bytes(table)
                self.run_iasl("-d", "mrrm.aml")
                disassembly = (self.directory / "mrrm.dsl").read_text(
                    encoding="ascii"
                )
                self.assertEqual(disassembly.count("System Address Base"), count)
                self.assertNotIn("terminates", disassembly)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iasl", type=Path)
    arguments = parser.parse_args()
    IaslCodeqlTests.iasl = arguments.iasl.resolve()
    unittest.main(argv=[__file__], verbosity=2)
