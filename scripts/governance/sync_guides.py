#!/usr/bin/env python3
"""Generate client copies of the repository guide; --check is read-only."""
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('.agents/skills/miacode-dev-guide')
MIRRORS = (Path('.claude/skills/miacode-dev-guide'), Path('.codex/skills/miacode-dev-guide'))


def snapshot(directory):
    return {p.relative_to(directory): p.read_bytes() for p in directory.rglob('*') if p.is_file()}


def validate(root, files):
    errors = []
    if Path('SKILL.md') not in files:
        return ['canonical SKILL.md is missing']
    for name, data in files.items():
        if name.suffix != '.md':
            continue
        text = data.decode('utf-8')
        # The guide deliberately uses file/directory references, not line numbers.
        links = re.findall(r'\[[^\]]*\]\(([^)]+)\)', text)
        for link in links:
            if '://' not in link and not (root / SOURCE / name.parent / link.split('#')[0]).exists():
                errors.append(f'{name}: broken link {link}')
        for anchor in re.findall(r'`((?:src|docs|scripts|cmake|resources|\.agents)/[^`]+)`', text):
            if not (root / anchor).exists():
                errors.append(f'{name}: missing repository path {anchor}')
    return errors


def run(root, sync=False):
    files = snapshot(root / SOURCE)
    errors = validate(root, files)
    if errors:
        return errors
    for mirror in MIRRORS:
        actual = snapshot(root / mirror)
        if sync:
            for name in actual.keys() - files.keys():
                (root / mirror / name).unlink()
            for name, data in files.items():
                target = root / mirror / name
                if actual.get(name) != data:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
        elif actual != files:
            changed = sorted(str(n) for n in actual.keys() | files.keys() if actual.get(n) != files.get(n))
            errors.append(f'{mirror}: drift: {", ".join(changed)}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--sync', action='store_true')
    mode.add_argument('--check', action='store_true')
    args = parser.parse_args()
    errors = run(ROOT, args.sync)
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('Guide mirrors and references are consistent.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
