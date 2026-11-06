"""Explicit reviewed kernel targets. An OS major version alone is insufficient."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
TARGETS = {
    'tahoe-25G332': {
        'build': '25G332', 'darwin': (25, 6), 'deployment': '26.0',
        'defines': ['E5_PROFILE_TAHOE_25G332=1'],
        'standalone': ROOT / 'standalone-profile.json', 'boot': ROOT / 'kernel-profile.json',
        'header': ROOT / 'src/profiles/Tahoe25G332.hpp', 'writer_offset': 0x1190,
    },
    'sequoia-24H232': {
        'build': '24H232', 'darwin': (24, 6), 'deployment': '15.0',
        'defines': ['E5_PROFILE_SEQUOIA_24H232=1'],
        'standalone': ROOT / 'profiles/sequoia-24H232/standalone.json',
        'boot': ROOT / 'profiles/sequoia-24H232/boot.json',
        'header': ROOT / 'src/profiles/Sequoia24H232.hpp', 'writer_offset': 0x1140,
    },
}


def select(name=None):
    if name is None:
        build = subprocess.check_output(['/usr/bin/sw_vers', '-buildVersion'], text=True).strip()
        names = [key for key, value in TARGETS.items() if value['build'] == build]
        if len(names) != 1:
            raise ValueError('No reviewed target for this build; use an explicit --target for cross-building.')
        name = names[0]
    if name not in TARGETS:
        raise ValueError('Unknown kernel target: ' + str(name))
    return name, TARGETS[name]
