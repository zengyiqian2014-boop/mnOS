#!/usr/bin/python3
# Run inside the built rootfs image: xvfb-run -a python3 /tests/gui_smoke.py
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.machinery
import importlib.util
import shutil
import sys
import stat
import subprocess
import tempfile
import traceback
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest.mock import patch

import struct
png = Path('/usr/share/backgrounds/mnos/coast.png').read_bytes()
assert png[:8] == b'\x89PNG\r\n\x1a\n'
width, height = struct.unpack('>II', png[16:24])
assert width >= 1600 and height >= 900
panel = ET.parse('/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml')
for plugin in panel.findall(".//property[@name='plugins']/property"):
    name = plugin.get('value')
    assert Path(f'/usr/share/xfce4/panel/plugins/{name}.desktop').is_file(), name
for command in ('dbus-launch', 'dbus-run-session', 'xfce4-session', 'xfwm4',
                'xfce4-panel', 'xfdesktop', 'fcitx5', 'firefox-esr', 'calamares'):
    assert shutil.which(command), command
for directory in ('/', '/usr', '/usr/bin', '/usr/share', '/etc'):
    assert stat.S_IMODE(Path(directory).stat().st_mode) & 0o005 == 0o005, directory
for public_file in ('/etc/lightdm/lightdm-gtk-greeter.conf',
                    '/usr/share/backgrounds/mnos/contour.svg',
                    '/usr/share/backgrounds/mnos/default.svg',
                    '/etc/tuned/profiles/mnos-virtual/tuned.conf'):
    assert Path(public_file).stat().st_mode & stat.S_IROTH, public_file
# Calamares' C++ contextualprocess bridges the validated installer choice.
installer = Path('/etc/calamares/settings.conf').read_text()
assert 'contextualprocess@mnosnetwork' in installer
assert 'module: contextualprocess' in installer
assert any(Path('/usr/lib').rglob('libcalamares_job_contextualprocess.so'))
chooser = Path('/etc/calamares/modules/packagechooser.conf').read_text()
assert 'default: root' in chooser and 'mode: required' in chooser
def run(*args, status=0):
    result = subprocess.run(args, capture_output=True, text=True, timeout=20)
    assert result.returncode == status, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout

# Test the actual Bash builtin collision in an interactive non-login shell.
assert 'command guide (English)' in run('bash', '-ic', 'help')
assert '中文命令帮助' in run('bash', '-ic', 'help -cn')
assert 'cd:' in run('bash', '-ic', 'help cd')
assert 'command guide (English)' in run('/usr/bin/help')
assert 'Root' not in run('kernel', 'help', '-cn')
assert '内核管理' not in run('kernel', 'help')  # Default output stays English.
assert 'kernel <instruction> [object]' in run('kernel', 'help')
assert 'kernel <指令> [对象]' in run('kernel', 'help', '-cn')
subprocess.run([sys.executable, str(Path(__file__).with_name('kernel_permissions.py'))], check=True)
with tempfile.TemporaryDirectory() as temp:
    disk = str(Path(temp) / 'mnfs.img')
    run('mnfs', 'format', disk, '8')
    run('mnfs', disk, 'create', '/hello')
    run('mnfs', disk, 'write', '/hello', 'original-filesystem')
    assert run('mnfs', disk, 'cat', '/hello') == 'original-filesystem'
    run('mnfs', disk, 'check')
print('Bash help languages and MNFS image access: PASS')
print('Desktop defaults, installed GUI tools and installer integration: PASS')
subprocess.run([sys.executable, str(Path(__file__).with_name('tool_launcher.py'))], check=True)
