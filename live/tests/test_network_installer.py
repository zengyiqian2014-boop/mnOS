# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import Mock, patch

SOURCE = Path(__file__).resolve().parents[1] / 'overlay/usr/lib/calamares/modules/mnosnetwork/main.py'


class NetworkInstallerTests(unittest.TestCase):
    def setUp(self):
        self.values = {'packagechooser_packagechooser': 'root', 'username': 'alice'}
        fake = types.ModuleType('libcalamares')
        fake.globalstorage = Mock()
        fake.globalstorage.value.side_effect = self.values.get
        utils = types.ModuleType('libcalamares.utils')
        self.execute = utils.check_target_env_call = Mock()
        with patch.dict(sys.modules, {'libcalamares': fake, 'libcalamares.utils': utils}):
            spec = importlib.util.spec_from_file_location('mnosnetwork', SOURCE)
            self.module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.module)

    def test_default_root_policy(self):
        self.assertIsNone(self.module.run())
        self.execute.assert_called_once_with(['/usr/sbin/mnos-network-policy', 'root'])

    def test_only_selected_user_receives_network_delegation(self):
        self.values['packagechooser_packagechooser'] = 'netadmin'
        self.assertIsNone(self.module.run())
        self.execute.assert_called_once_with(['/usr/sbin/mnos-network-policy', 'netadmin', 'alice'])

    def test_invalid_or_missing_choice_does_not_grant_permissions(self):
        for choice in (None, '', 'root,netadmin', 'netadmin;id'):
            self.values['packagechooser_packagechooser'] = choice
            self.assertIsNotNone(self.module.run())
        self.execute.assert_not_called()

    def test_invalid_user_does_not_reach_privileged_backend(self):
        self.values.update(packagechooser_packagechooser='netadmin', username='alice;id')
        self.assertIsNotNone(self.module.run())
        self.execute.assert_not_called()

    def test_capability_setup_failure_stops_installation_job(self):
        self.execute.side_effect = RuntimeError('target filesystem rejected capabilities')
        with self.assertRaises(RuntimeError):
            self.module.run()
