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
import yaml
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
assert len(application.nav) == 5
from gi.repository import GdkPixbuf
wallpaper = GdkPixbuf.Pixbuf.new_from_file_at_size('/usr/share/backgrounds/mnos/default.svg', 320, 180)
assert wallpaper.get_width() > 0 and wallpaper.get_height() > 0
coast = GdkPixbuf.Pixbuf.new_from_file('/usr/share/backgrounds/mnos/coast.png')
assert coast.get_width() >= 1600 and coast.get_height() >= 900
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
        executable = command[1] if command[0] in ('@terminal', '@network') else command[0]
        assert shutil.which(executable), (group, title, executable)
        if executable == 'mousepad' and len(command) > 1:
            assert Path(command[1]).is_file(), command[1]
with patch.object(center.subprocess, 'Popen') as launch:
    application.launch(['@terminal', 'python3', '-c', 'print("$HOME; do not execute")'])
    args, kwargs = launch.call_args
    assert args[0][0] == 'xfce4-terminal'
    assert not kwargs.get('shell')
    with patch.object(center.Path, 'is_file', return_value=True), \
         patch.object(center.Path, 'read_text', return_value='netadmin\n'), \
         patch.object(center.os, 'access', return_value=True):
        application.launch(['@network', 'tcpdump', '-D'])
        assert launch.call_args.args[0][-1] == 'mnos-net tcpdump -D'
    with patch.object(center.Path, 'is_file', return_value=True), \
         patch.object(center.Path, 'read_text', return_value='root\n'):
        application.launch(['@network', 'tcpdump', '-D'])
        assert launch.call_args.args[0][-1] == 'sudo tcpdump -D'
application.window.destroy()
assert not errors, errors
installer = yaml.safe_load(Path('/etc/calamares/settings.conf').read_text())
chooser = yaml.safe_load(Path('/etc/calamares/modules/packagechooser.conf').read_text())
show = installer['sequence'][0]['show']
jobs = installer['sequence'][1]['exec']
assert show.index('users') < show.index('packagechooser') < show.index('summary')
assert jobs.index('users') < jobs.index('mnosnetwork') < jobs.index('umount')
assert chooser['default'] == 'root' and chooser['mode'] == 'required'
assert {item['id'] for item in chooser['items']} == {'root', 'netadmin'}
assert Path('/usr/lib/calamares/modules/mnosnetwork/main.py').is_file()
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
print('GTK window, all five pages, tool availability and safe argument forwarding: PASS')
subprocess.run([sys.executable, str(Path(__file__).with_name('tool_launcher.py'))], check=True)
