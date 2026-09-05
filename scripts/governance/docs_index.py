#!/usr/bin/env python3
"""Validate document lifecycle metadata and generate docs/INDEX.md (stdlib only)."""
import argparse
from datetime import date
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SCOPES = ('specs', 'tests', 'archive', 'audit', 'superpowers/plans', 'superpowers/specs')
LIFECYCLES = ('stable-current', 'reusable-verification', 'archive-legacy', 'working')
GENERATED = {'docs/tests/SPEC_CATALOG.md'}  # Checked by SpecCatalog.cmake, not authored Markdown.


def metadata(text):
    """Small documented frontmatter subset: scalar values and JSON string arrays."""
    if not text.startswith('---\n') or '\n---\n' not in text[4:]:
        raise ValueError('missing frontmatter')
    header, body = text[4:].split('\n---\n', 1)
    fields = {}
    for line in header.splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        key, sep, value = line.partition(':')
        if not sep or key in fields:
            raise ValueError(f'invalid or duplicate metadata field: {line}')
        value = value.strip()
        fields[key] = json.loads(value) if value.startswith(('[', '"')) else value
    return fields, body


def local_anchor(root, value):
    if not isinstance(value, str) or not value or '#' in value or Path(value).is_absolute():
        return False
    path = (root / value).resolve()
    return path.is_relative_to(root.resolve()) and path.exists()


def collect(root):
    rows, errors, canonical = [], [], {}
    paths = sorted({p for scope in SCOPES for p in (root / 'docs' / scope).rglob('*.md')})
    # Maintainers also keep ignored working notes in these directories. Only
    # public (tracked or unignored) documents belong in the published index.
    ignored = set()
    if (root / '.git').exists() and paths:
        result = subprocess.run(['git', 'check-ignore', '-z', '--stdin'], cwd=root,
                                input='\0'.join(p.relative_to(root).as_posix() for p in paths) + '\0',
                                capture_output=True, text=True, encoding='utf-8')
        if result.returncode not in (0, 1):
            return [], [f'git check-ignore failed: {result.stderr.strip()}']
        ignored = set(result.stdout.split('\0'))
    manifests = root / 'cmake/devtools/specs'
    target_names = set()
    for p in manifests.glob('*.cmake'):
        target_names.update(re.findall(r'miacode_add_spec\(\s*([\w-]+)', p.read_text(encoding="utf-8")))
    for scope in SCOPES:
        for p in sorted((root / 'docs' / scope).rglob('*.md')):
            relative = p.relative_to(root).as_posix()
            if relative in GENERATED or relative in ignored:
                continue
            try:
                fields, body = metadata(p.read_text(encoding="utf-8"))
                state = fields.get('lifecycle')
                if state not in LIFECYCLES:
                    raise ValueError('invalid lifecycle')
                cid = fields.get('canonical_id', '')
                if state in ('stable-current', 'reusable-verification'):
                    for key in ('owner', 'canonical_id'):
                        if not isinstance(fields.get(key), str) or not fields[key].strip():
                            raise ValueError(f'{key} is required')
                    if not re.fullmatch(r'[a-z0-9][a-z0-9.-]*', cid):
                        raise ValueError('canonical_id must be a lowercase stable ID')
                    if cid in canonical:
                        raise ValueError(f'duplicate canonical_id {cid}: {canonical[cid]}')
                    canonical[cid] = relative
                    if not local_anchor(root, fields['owner']):
                        raise ValueError('owner must name an existing repository module')
                if state == 'stable-current':
                    verified = fields.get('last_verified', '')
                    if not re.fullmatch(r'\d{4}-\d{2}-\d{2}', verified):
                        raise ValueError('last_verified must use YYYY-MM-DD')
                    date.fromisoformat(verified)
                    anchors = fields.get('code_anchors')
                    if not isinstance(anchors, list) or not anchors or any(not local_anchor(root, a) for a in anchors):
                        raise ValueError('code_anchors must contain existing repository paths, without line numbers')
                if state == 'reusable-verification':
                    targets = fields.get('test_targets')
                    if not isinstance(targets, list) or any(not isinstance(t, str) or t not in target_names for t in targets):
                        raise ValueError('test_targets must be a list of registered spec targets (empty for manual-only)')
                # Historical/working links may deliberately refer to removed code.
                if state in ('stable-current', 'reusable-verification'):
                    for link in re.findall(r'\[[^\]]*\]\(([^)]+)\)', body):
                        if '://' in link or link.startswith('#'):
                            continue
                        if not (p.parent / link.split('#')[0]).exists():
                            raise ValueError(f'broken document link: {link}')
                title = next((line[2:].strip() for line in body.splitlines() if line.startswith('# ')), p.stem)
                rows.append((state, relative, title.replace('|', '\\|'), cid))
            except (ValueError, TypeError) as exc:
                errors.append(f'{relative}: {exc}')
    return rows, errors


def render(rows):
    text = '# 文档索引\n\n由 `python3 scripts/governance/docs_index.py --sync` 从文档 frontmatter 生成；请勿手工编辑。\n\n'
    text += '入口与维护规则见 [README](README.md)；可执行规格见 [Spec 目录](tests/SPEC_CATALOG.md)。\n'
    for state in LIFECYCLES:
        selected = sorted(r for r in rows if r[0] == state)
        text += f'\n## {state}（{len(selected)}）\n\n| 文档 | Canonical ID |\n| --- | --- |\n'
        for _, path, title, cid in selected:
            text += f'| [{title}]({path.removeprefix("docs/")}) | {cid or "—"} |\n'
    return text


def run(root, sync=False):
    rows, errors = collect(root)
    if errors:
        return errors
    expected = render(rows)
    output = root / 'docs/INDEX.md'
    if sync:
        output.write_text(expected, encoding="utf-8")
    elif not output.exists() or output.read_text(encoding="utf-8") != expected:
        errors.append('docs/INDEX.md is stale; run docs_index.py --sync')
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
    print('Document lifecycle, current anchors and index are consistent.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
