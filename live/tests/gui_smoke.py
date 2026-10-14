#!/usr/bin/python3
# Run inside the built rootfs image: xvfb-run -a python3 /tests/gui_smoke.py
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.machinery
import importlib.util
import shutil
import sys
import stat
from pathlib import Path
from unittest.mock import patch

errors = []
sys.excepthook = lambda kind, value, trace: errors.append(value)
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
print('GTK window, all six pages, tool availability and safe argument forwarding: PASS')
