# SPDX-License-Identifier: GPL-3.0-or-later
import re
import libcalamares
from libcalamares.utils import check_target_env_call


def pretty_name():
    return 'Configure network-tool permissions'


def run():
    mode = libcalamares.globalstorage.value('packagechooser_packagechooser')
    if mode not in ('root', 'netadmin'):
        return ('Invalid network permission selection', 'Return to Network permissions and choose one policy.')
    command = ['/usr/sbin/mnos-network-policy', mode]
    if mode == 'netadmin':
        username = libcalamares.globalstorage.value('username')
        if not isinstance(username, str) or not re.fullmatch(r'[a-z][a-z0-9_-]{0,30}', username):
            return ('Invalid local account', 'Choose a valid local username before installing.')
        command.append(username)
    check_target_env_call(command)
    return None
