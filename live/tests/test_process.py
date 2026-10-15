#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the compiled process boundary, including closed stdin and binary input."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class ProcessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / 'process-test'
        source = Path(cls.temp.name) / 'process-test.cpp'
        source.write_text(r'''
#include "process.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    try {
        std::string mode = argv[1];
        if (mode == "closed") {
            close(STDIN_FILENO);
            return mnos::run({"/usr/bin/cat"}, std::string("a\0b\n", 4));
        }
        if (mode == "env") return mnos::run({"/usr/bin/env"});
        if (mode == "literal") return mnos::run({"/usr/bin/printf", "%s", "$(false);literal"});
        if (mode == "missing") return mnos::run({"/no/such/mnos-program"});
    } catch (const std::exception &) { return 17; }
    return 3;
}
''')
        include = Path(__file__).resolve().parents[1] / 'src'
        subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(include), str(source), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_mode(self, mode):
        return subprocess.run([str(self.binary), mode], capture_output=True, timeout=10,
                              env={**os.environ, 'MNOS_UNTRUSTED_ENV': 'must-not-inherit'})

    def test_input_survives_closed_stdin(self):
        result = self.run_mode('closed')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'a\0b\n')

    def test_environment_arguments_and_spawn_error(self):
        result = self.run_mode('env')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(set(result.stdout.splitlines()),
                         {b'PATH=/usr/sbin:/usr/bin:/sbin:/bin', b'LANG=C.UTF-8'})
        self.assertEqual(self.run_mode('literal').stdout, b'$(false);literal')
        self.assertEqual(self.run_mode('missing').returncode, 17)


if __name__ == '__main__':
    unittest.main()
