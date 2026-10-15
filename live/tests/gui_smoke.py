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

errors = []
def record_exception(kind, value, trace):
    errors.append(value)
    traceback.print_exception(kind, value, trace)
sys.excepthook = record_exception
loader = importlib.machinery.SourceFileLoader('center', '/usr/bin/mnos-center')
spec = importlib.util.spec_from_loader(loader.name, loader)
center = importlib.util.module_from_spec(spec)
loader.exec_module(center)
application = center.Center()
application.register(None)
application.activate()
for name, button in application.nav:
    button.clicked()
    assert application.stack.get_visible_child_name() == name, name
    while center.Gtk.events_pending():
        center.Gtk.main_iteration_do(False)
assert len(application.nav) == 6
from gi.repository import GdkPixbuf
wallpaper = GdkPixbuf.Pixbuf.new_from_file_at_size('/usr/share/backgrounds/mnos/default.svg', 320, 180)
assert wallpaper.get_width() > 0 and wallpaper.get_height() > 0
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
# Enrollment must work against the installed dictionary, not just mocked calls.
import secrets
candidate = secrets.token_hex(16)
quality = center.subprocess.run(['pwscore', 'mnoscheck'], input=candidate + '\n',
                                text=True, capture_output=True)
assert quality.returncode == 0, quality.stderr
for group, entries in center.TOOLS.items():
    for _icon, title, _description, command in entries:
        executable = command[1] if command[0] == '@terminal' else command[0]
        assert shutil.which(executable), (group, title, executable)
        if executable == 'mousepad':
            assert Path(command[1]).is_file(), command[1]
with patch.object(center.subprocess, 'Popen') as launch:
    application.launch(['@terminal', 'python3', '-c', 'print("$HOME; do not execute")'])
    args, kwargs = launch.call_args
    assert args[0][0] == 'xfce4-terminal'
    assert not kwargs.get('shell')
application.window.destroy()
assert not errors, errors
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
run('runuser', '-u', 'nobody', '--', 'kernel', 'info', status=77)
assert 'kernel <指令>' in run('runuser', '-u', 'nobody', '--', 'kernel', 'help', '-cn')
for instruction in ('info', 'memory', 'cpu', 'gpu', 'devices'):
    run('kernel', instruction)
if Path('/proc/modules').exists():
    run('kernel', 'modules')
assert 'Linux' in run('kernel', 'sysctl', 'kernel.ostype')
for instruction, obj in [('cpu-offline', '0'), ('load', '--help'),
                         ('unload', 'x;touch /tmp/mnos-unsafe'), ('set', 'x'),
                         ('sysctl', '../proc'), ('performance', 'unknown')]:
    run('kernel', instruction, obj, status=2)
assert not Path('/tmp/mnos-unsafe').exists()
with tempfile.TemporaryDirectory() as temp:
    disk = str(Path(temp) / 'mnfs.img')
    run('mnfs', 'format', disk, '8')
    run('mnfs', disk, 'create', '/hello')
    run('mnfs', disk, 'write', '/hello', 'original-filesystem')
    assert run('mnfs', disk, 'cat', '/hello') == 'original-filesystem'
    run('mnfs', disk, 'check')
print('Bash help languages, root-only kernel commands, argument validation and MNFS image access: PASS')
print('GTK window, all six pages, tool availability and safe argument forwarding: PASS')
