#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the installed development stack as an ordinary user, without networking."""
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    with tempfile.TemporaryDirectory(prefix='mnos-development-') as temp:
        directory = Path(temp)
        os.chown(directory, 65534, 65534)
        def run(*args):
            command = ['runuser', '-u', 'nobody', '--', '/usr/bin/env',
                       'HOME=' + temp, 'XDG_CACHE_HOME=' + temp + '/cache',
                       'GOCACHE=' + temp + '/go-cache', 'GOTOOLCHAIN=local', 'GO111MODULE=off', *args]
            result = subprocess.run(command, cwd=temp, text=True, capture_output=True, timeout=60)
            assert result.returncode == 0, (args, result.returncode, result.stdout, result.stderr)
            return result.stdout
        sources = {
            'hello.c': '#include <stdio.h>\nint main(void) { puts("C ready"); }\n',
            'hello.cpp': '#include <iostream>\nint main() { std::cout << "C++ ready\\n"; }\n',
            'hello.rs': 'fn main() { println!("Rust ready"); }\n',
            'hello.go': 'package main\nimport "fmt"\nfunc main() { fmt.Println("Go ready") }\n',
            'Hello.java': 'class Hello { public static void main(String[] args) { System.out.println("Java ready"); } }\n',
        }
        for name, source in sources.items():
            path = directory / name
            path.write_text(source)
            os.chown(path, 65534, 65534)
            path.chmod(0o600)
        run('cc', 'hello.c', '-o', 'hello-c')
        assert run('./hello-c').strip() == 'C ready'
        run('c++', 'hello.cpp', '-o', 'hello-cpp')
        assert run('./hello-cpp').strip() == 'C++ ready'
        run('rustc', 'hello.rs', '-o', 'hello-rust')
        assert run('./hello-rust').strip() == 'Rust ready'
        assert run('go', 'run', 'hello.go').strip() == 'Go ready'
        run('javac', 'Hello.java')
        assert run('java', 'Hello').strip() == 'Java ready'
        assert run('node', '-e', 'console.log("JavaScript ready")').strip() == 'JavaScript ready'
        assert 'nano' in run('nano', '--version').lower()
        assert run('code', '--version').strip()
        run('code', '--list-extensions', '--user-data-dir', temp + '/code-data', '--extensions-dir', temp + '/extensions')
        for command in ('cargo', 'npm', 'cmake', 'ninja', 'clang', 'clangd', 'rg', 'shellcheck'):
            run(command, '--version')
        assert Path('/usr/share/applications/code.desktop').is_file()
        assert not any(path.stat().st_uid == 0 for path in directory.iterdir())
    print('Ordinary-user C/C++, Rust, Go, Java, JavaScript, nano and official VS Code CLI: PASS')


if __name__ == '__main__':
    main()
