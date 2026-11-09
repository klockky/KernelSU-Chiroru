#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Compile real manager_identity.h with minimal host mocks. No device access.

Usage: python3 baseline_checks.py /path/to/KernelSU-Chiroru
--regression returns nonzero on the original unverified manager seed.
Default runs characterization checks; success is NOT repair acceptance.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('repo', type=Path)
    parser.add_argument('--regression', action='store_true')
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    source = (args.repo / 'kernel/manager/throne_tracker.c').read_text()
    match = re.search(r'^uid_t ksu_manager_appid\s*=\s*([^;]+);', source, re.M)
    if not match:
        parser.error('Cannot identify the production manager initializer')
    seed = match.group(1).strip()
    if not re.fullmatch(r'(?:[0-9]+|KSU_INVALID_APPID)', seed):
        parser.error('Initializer requires manual review; refusing guessed value')
    header = args.repo / 'kernel/manager/manager_identity.h'
    with tempfile.TemporaryDirectory(prefix='ksu-identity-') as tmp:
        root = Path(tmp)
        (root / 'linux').mkdir()
        (root / 'linux/types.h').write_text(
            '#pragma once\n#include <stdbool.h>\n#include <sys/types.h>\n')
        (root / 'linux/cred.h').write_text(
            '#pragma once\n#include <linux/types.h>\n'
            'struct fake_kuid { uid_t val; };\n'
            'extern struct fake_kuid test_current_uid;\n'
            '#define current_uid() (test_current_uid)\n'
            '#define unlikely(x) (x)\n')
        executable = root / 'identity-test'
        subprocess.run([
            args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
            # Existing kernel header uses unsigned uid_t versus -1 sentinel.
            # Suppress only this pre-existing warning; preserve production bytes.
            '-Wno-sign-compare', '-UNDEBUG',
            '-O1', '-g', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', f'-DINITIAL_MANAGER_APPID={seed}',
            '-I', str(root), '-I', str(header.parent),
            str(Path(__file__).with_name('baseline_identity_test.c')),
            '-o', str(executable),
        ], check=True)
        print(f'Production initializer: {seed}', flush=True)
        result = subprocess.run([str(executable)] +
                                (['--regression'] if args.regression else []))
        return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
