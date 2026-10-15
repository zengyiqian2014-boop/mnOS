#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise search, settings classification and help launching in the built desktop."""
import importlib.machinery
import importlib.util
import json
from pathlib import Path
import shlex
import shutil
import sys
import traceback
from unittest.mock import patch

errors = []
def record(kind, value, trace):
    errors.append(value)
    traceback.print_exception(kind, value, trace)
sys.excepthook = record
loader = importlib.machinery.SourceFileLoader('mnos_tools_test', '/usr/bin/mnos-tools')
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)
catalog = json.loads(module.CATALOG.read_text())
assert len({item['name'] for item in catalog}) == len(catalog)
for item in catalog:
    assert shutil.which(item['command'][0], path='/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin'), item
    assert item['category'] in module.CATEGORIES and item['icon']

app = module.Tools()
app.register(None)
app.activate()
assert len(app.rows) >= len(catalog)
assert any(row.tool['app'] and row.tool['app'].get_id() == 'code.desktop' for row in app.rows)
for query, expected in [('nmap', 'Nmap'), ('内核', '内核设置'), ('nano', 'Nano'), ('C++', 'G++')]:
    app.category = '全部'
    app.search.set_text(query)
    app.filter_rows()
    assert expected in [row.tool['name'] for row in app.rows if row.get_visible()], query
app.category = '系统设置'
app.search.set_text('')
app.filter_rows()
visible = [row.tool for row in app.rows if row.get_visible()]
assert all(tool['category'] == '系统设置' for tool in visible)
assert {'内核设置', 'xrandr', 'parted'} <= {tool['name'] for tool in visible}
desktop_ids = {tool['app'].get_id() for tool in visible if tool['app']}
assert {'xfce-display-settings.desktop', 'xfce-backdrop-settings.desktop'} <= desktop_ids, desktop_ids
app.search.set_text('no-such-application-5290384')
app.filter_rows()
assert not any(row.get_visible() for row in app.rows)
with patch.object(module.subprocess, 'Popen') as launch:
    nmap = next(row.tool for row in app.rows if row.tool['name'] == 'Nmap')
    app.launch(nmap)
    args, kwargs = launch.call_args
    assert args[0][:3] == ['xfce4-terminal', '--hold', '--command']
    assert shlex.split(args[0][3]) == ['/usr/bin/nmap', '--help']
    assert not kwargs.get('shell')
app.window.destroy()
assert not errors, errors
print(f'Application launcher: {len(catalog)} named CLI tools, installed GUI apps, search, settings categories and terminal help: PASS')
