#!/usr/bin/env python3
"""Select CI duties from changed paths; explicit/release builds always qualify all."""
import argparse
import json
import os
import subprocess
from pathlib import Path

CRATES = ('mode2-profile', 'abl-tzmap', 'canoe-bootmgr', 'canoe-fs', 'canoe-image', 'canoe-provision')
PORTABLE = ('mode2-profile', 'canoe-image', 'abl-tzmap', 'canoe-provision')
DEPENDENTS = {
    'canoe-fs': {'canoe-bootmgr', 'canoe-image', 'mode2-profile'},
    'mode2-profile': {'canoe-bootmgr', 'canoe-image'},
    'abl-tzmap': {'canoe-bootmgr', 'canoe-image'},
}


def classify(paths, full=False):
    duties = {key: False for key in ('uefi_tests', 'patcher_tests', 'module_tests', 'script_tests', 'firmware', 'versions')}
    crates = set()
    portable = set()
    for path in paths:
        parts = Path(path).parts
        # Documentation cannot alter a compiled firmware image. Policy-only
        # editing is reviewed; it does not justify rebuilding the firmware.
        if path.endswith(('.md', '.rst')) or path.startswith(('wiki/', 'docs/')):
            continue
        if path.startswith('submodules/uefi/'):
            duties['uefi_tests'] = True
            is_test = path.startswith('submodules/uefi/tests/') or Path(path).name in ('TestBootRoot.c', 'TestLastLaunch.c')
            duties['firmware'] |= not is_test
        elif path.startswith('submodules/patcher/'):
            duties['patcher_tests'] = True
            if not path.startswith('submodules/patcher/tests/'):
                crates.add('canoe-image')
                portable.add('canoe-image')
        elif path.startswith('submodules/ablfvextractor/'):
            crates.add('canoe-image')
            if not path.startswith('submodules/ablfvextractor/tests/'):
                portable.add('canoe-image')
        elif len(parts) > 1 and parts[0] == 'tools' and parts[1] in CRATES:
            crate = parts[1]
            crates.add(crate)
            # Tests do not change dependent producers; source/manifest/lock
            # changes need their consumers' boundary checks too.
            if len(parts) < 3 or parts[2] != 'tests':
                affected = {crate, *DEPENDENTS.get(crate, ())}
                crates.update(affected)
                portable.update(affected.intersection(PORTABLE))
        elif path.startswith('targets/magisk_module/'):
            duties['module_tests'] = True
        elif path.startswith('scripts/tests/'):
            duties['script_tests'] = True
        elif path == 'scripts/firmware_release.py':
            duties['script_tests'] = duties['firmware'] = duties['versions'] = True
        elif path.startswith(('tools/canoe-ext4/', 'tools/vbmetafixer/')):
            # Retired adapters are outside the current packaged firmware.
            continue
        else:
            # Build orchestration, imports, CI and new/unclassified source
            # paths receive the complete check until their duty is known.
            full = True
    if full:
        duties = {key: True for key in duties}
        crates.update(CRATES)
        portable.update(PORTABLE)
    duties['versions'] |= duties['firmware']
    packages = set()
    if duties['uefi_tests'] or duties['patcher_tests'] or crates:
        packages.add('build-essential')
    if duties['uefi_tests'] or 'canoe-provision' in crates:
        packages.add('e2fsprogs')
    if 'canoe-provision' in crates or 'canoe-provision' in portable:
        packages.add('dosfstools')
    if 'canoe-image' in portable:
        packages.update(('clang', 'lld'))
    # The canonical EFI build gets its toolchain inside Docker. Host Python
    # checks use the standard library; no pytest or EFI SDK host packages.
    return {**duties, 'rust_crates': ' '.join(c for c in CRATES if c in crates),
            'portable_crates': ' '.join(c for c in PORTABLE if c in portable),
            'apt_packages': ' '.join(sorted(packages))}


def changed_paths(event):
    if event.get('pull_request'):
        base = event['pull_request']['base']['sha']
        head = event['pull_request']['head']['sha']
        # Review only this branch, not concurrent base-branch changes.
        span = f'{base}...{head}'
    else:
        base = event.get('before', '')
        if not base or set(base) == {'0'}:
            return None
        span = f'{base}..HEAD'
    try:
        output = subprocess.check_output(['git', 'diff', '--name-only', '-z', span])
    except subprocess.CalledProcessError:
        # A force-pushed baseline may no longer be reachable after checkout.
        # Missing comparison evidence means full qualification, never no work.
        return None
    return output.decode().strip('\0').split('\0') if output else []


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--full', action='store_true')
    parser.add_argument('paths', nargs='*', help='Explicit paths for local inspection; otherwise use the GitHub event')
    args = parser.parse_args()
    full = args.full or bool(os.environ.get('SOURCE_REF')) or os.environ.get('GITHUB_EVENT_NAME') == 'workflow_dispatch'
    paths = args.paths
    if not paths and not full:
        event_path = os.environ.get('GITHUB_EVENT_PATH')
        paths = changed_paths(json.loads(Path(event_path).read_text())) if event_path else None
        full = paths is None
    duties = classify(paths or [], full)
    print(json.dumps(duties, indent=2))
    if output := os.environ.get('GITHUB_OUTPUT'):
        with open(output, 'a') as stream:
            for key, value in duties.items():
                stream.write(f'{key}={str(value).lower() if isinstance(value, bool) else value}\n')


if __name__ == '__main__':
    main()
