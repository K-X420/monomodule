#!/usr/bin/env python3
"""Runs Monomodule MD's headless checks (md-plugintest), one MD_<NAME>_TEST at a time, each in a fresh process.

    python tests/run_md_tests.py --md-os Elektron_SPS1-1UW_OS1.63.syx [--kits MD_presets.syx] [--build build]

The library the checks write to is a temporary folder (MNM_LIBRARY_DIR), never your own. ARROW needs a kit dump
(--kits, any MD .syx with kits); without one it is skipped. Exit code: the number of failed checks.
"""
import argparse, glob, os, subprocess, sys, tempfile

SUITES = ['GROUP', 'SEQ', 'EDIT', 'GRID', 'REC', 'SONGED', 'MIDI', 'MIXER', 'CTR', 'PARITY', 'OSCHECK', 'RATE', 'FOUR',
          'RAM', 'ARROW']


def find_exe(build):
    for pattern in ('**/md-plugintest', '**/md-plugintest.exe', '**/MnmMdTest', '**/MnmMdTest.exe'):
        hits = [h for h in glob.glob(os.path.join(build, pattern), recursive=True) if os.path.isfile(h)]
        if hits:
            return max(hits, key=os.path.getmtime)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--md-os', required=True, help='the Machinedrum UW OS 1.63 .syx')
    ap.add_argument('--kits', help='an MD kit dump (.syx) for ARROW')
    ap.add_argument('--build', default='build', help='the CMake build folder (default: build)')
    ap.add_argument('--only', nargs='*', help='just these suites (e.g. SEQ GRID)')
    args = ap.parse_args()
    exe = find_exe(args.build)
    if not exe:
        sys.exit('md-plugintest not found under ' + args.build + ' (build the MnmMdTest target)')
    failed = 0
    with tempfile.TemporaryDirectory(prefix='md-tests-') as tmp:
        base = dict(os.environ, MNM_LIBRARY_DIR=os.path.join(tmp, 'library'))
        for name in args.only or SUITES:
            value = '1'
            if name == 'ARROW':
                if not args.kits:
                    print(f'{name:8} skipped (no --kits)')
                    continue
                value = args.kits
            env = dict(base, **{f'MD_{name}_TEST': value})
            r = subprocess.run([exe, args.md_os, os.path.join(tmp, name + '.wav')], env=env,
                               capture_output=True, text=True, errors='replace')
            out = r.stdout + r.stderr
            bad = r.returncode != 0 or 'FAIL' in out
            failed += bad
            if bad:
                print(f'{name:8} FAILED (exit {r.returncode})')
                for line in out.splitlines():
                    if 'FAIL' in line:
                        print('         ' + line.strip())
            else:
                ok = [l.strip() for l in out.splitlines() if ' OK' in l]
                print(f'{name:8} ok' + (f'  ({ok[-1]})' if ok else ''))
    print(f'{failed} failed')
    sys.exit(failed)


if __name__ == '__main__':
    main()
