"""Behavioral regression checks for document indexing and generated guide copies."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import docs_index
import sync_guides


class GovernanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'src/app').mkdir(parents=True)
        (self.root / 'docs/specs/ui').mkdir(parents=True)
        (self.root / 'cmake/devtools/specs').mkdir(parents=True)
        (self.root / 'cmake/devtools/specs/ui.cmake').write_text('miacode_add_spec(ui_spec)\n', encoding="utf-8")
        self.doc = self.root / 'docs/specs/ui/current.md'
        self.doc.write_text('''---
lifecycle: stable-current
owner: src/app
canonical_id: ui.current
last_verified: 2026-09-06
code_anchors: ["src/app"]
---
# Current
''', encoding="utf-8")
        self.guide = self.root / sync_guides.SOURCE
        self.guide.mkdir(parents=True)
        (self.guide / 'SKILL.md').write_text('# Guide\n`src/app`\n', encoding="utf-8")

    def assert_error(self, errors, phrase):
        self.assertTrue(any(phrase in e for e in errors), errors)

    def test_index_sync_and_check_are_deterministic(self):
        self.assertEqual(docs_index.run(self.root, True), [])
        before = (self.root / 'docs/INDEX.md').read_bytes()
        self.assertEqual(docs_index.run(self.root), [])
        self.assertEqual(docs_index.run(self.root, True), [])
        self.assertEqual((self.root / 'docs/INDEX.md').read_bytes(), before)

    def test_new_document_without_metadata_fails(self):
        (self.doc.parent / 'new.md').write_text('# New\n', encoding="utf-8")
        self.assert_error(docs_index.run(self.root, True), 'missing frontmatter')
        self.assertFalse((self.root / 'docs/INDEX.md').exists())

    def test_duplicate_canonical_id_fails(self):
        (self.doc.parent / 'duplicate.md').write_text(self.doc.read_text(encoding="utf-8"), encoding="utf-8")
        self.assert_error(docs_index.run(self.root), 'duplicate canonical_id')

    def test_missing_code_anchor_fails(self):
        self.doc.write_text(self.doc.read_text(encoding="utf-8").replace('["src/app"]', '["src/missing"]'), encoding="utf-8")
        self.assert_error(docs_index.run(self.root), 'code_anchors')

    def test_invalid_calendar_date_fails(self):
        self.doc.write_text(self.doc.read_text(encoding="utf-8").replace('2026-09-06', '2026-02-31'), encoding="utf-8")
        self.assertTrue(docs_index.run(self.root))

    def test_unknown_verification_target_fails(self):
        self.doc.write_text('''---
lifecycle: reusable-verification
owner: src/app
canonical_id: verify.ui
test_targets: ["missing_spec"]
---
# Verify
''', encoding="utf-8")
        self.assert_error(docs_index.run(self.root), 'test_targets')

    def test_stale_index_fails_without_rewriting(self):
        self.assertEqual(docs_index.run(self.root, True), [])
        output = self.root / 'docs/INDEX.md'
        output.write_text('stale', encoding="utf-8")
        self.assert_error(docs_index.run(self.root), 'stale')
        self.assertEqual(output.read_text(encoding="utf-8"), 'stale')

    @unittest.skipUnless(shutil.which('git'), 'Git is required')
    def test_ignored_private_notes_are_not_published(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        (self.root / '.gitignore').write_text('docs/specs/ui/内部记录.md\n', encoding="utf-8")
        (self.doc.parent / '内部记录.md').write_text('---\nlifecycle: working\n---\n# Secret local note\n', encoding="utf-8")
        self.assertEqual(docs_index.run(self.root, True), [])
        self.assertNotIn('Secret', (self.root / 'docs/INDEX.md').read_text(encoding="utf-8"))

    def test_mirror_drift_and_obsolete_files(self):
        self.assertEqual(sync_guides.run(self.root, True), [])
        mirror = self.root / sync_guides.MIRRORS[0]
        (mirror / 'SKILL.md').write_text('drift', encoding="utf-8")
        (mirror / 'old.md').write_text('obsolete', encoding="utf-8")
        self.assert_error(sync_guides.run(self.root), 'drift')
        self.assertEqual((mirror / 'SKILL.md').read_text(encoding="utf-8"), 'drift')
        self.assertEqual(sync_guides.run(self.root, True), [])
        self.assertFalse((mirror / 'old.md').exists())
        self.assertEqual(sync_guides.snapshot(mirror), sync_guides.snapshot(self.guide))

    def test_missing_guide_reference_prevents_sync(self):
        (self.guide / 'SKILL.md').write_text('# Guide\n[Missing](missing.md)\n', encoding="utf-8")
        self.assert_error(sync_guides.run(self.root, True), 'broken link')
        self.assertFalse((self.root / sync_guides.MIRRORS[0]).exists())


if __name__ == '__main__':
    unittest.main()
