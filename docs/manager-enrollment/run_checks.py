#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Run host-only reference checks. No kernel build, device access or deployment."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='ksu-reference-') as directory:
        executable = Path(directory) / 'reference-test'
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion',
                 '-Wshadow', '-Wstrict-prototypes', '-UNDEBUG', '-O1', '-g']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run([args.cc, *flags, str(here / 'reference_test.c'),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
