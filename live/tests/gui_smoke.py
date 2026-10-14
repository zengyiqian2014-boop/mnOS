#!/usr/bin/python3
# Run inside the built rootfs image: xvfb-run -a python3 /tests/gui_smoke.py
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.machinery
import importlib.util
import shutil
import sys
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
