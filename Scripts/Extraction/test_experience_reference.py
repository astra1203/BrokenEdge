"""Gate regression with synthetic bytes only; never official reference evidence."""
from pathlib import Path
import tempfile
import unittest

from experience_reference import REQUIREMENTS, digest, template, verify


class ReferenceGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.media = self.base / 'synthetic.bin'
        self.media.write_bytes(b'Synthetic gate fixture, not gameplay capture')
        self.document = template()
        self.document['baseline'].update(client_build='test-build', season='test', sampled_at='2026-10-07')
        self.document['capture_settings'].update(resolution='1920x1080', fov=90, input_settings='fixture')
        for name, observations in REQUIREMENTS.items():
            self.document['groups'][name]['evidence'] = [{
                'path': self.media.name, 'kind': 'video', 'client_build': 'test-build',
                'sha256': digest(self.media), 'covers': list(observations),
                'reviewed': True, 'review_note': 'Synthetic structural test only',
            }]

    def blocked(self, document):
        self.assertFalse(verify(document, self.base)['content_production_allowed'])

    def test_complete_synthetic_structure(self):
        self.assertTrue(verify(self.document, self.base)['content_production_allowed'])

    def test_empty_checklist(self):
        self.blocked(template())

    def test_omitted_gun(self):
        del self.document['groups']['weapon.MP5']
        self.blocked(self.document)

    def test_mixed_build(self):
        self.document['groups']['weapon.AKM']['evidence'][0]['client_build'] = 'other-build'
        self.blocked(self.document)

    def test_unreviewed(self):
        self.document['groups']['ui.hud']['evidence'][0]['reviewed'] = 'pending'
        self.blocked(self.document)

    def test_screenshots_cannot_replace_continuous_video(self):
        self.document['groups']['weapon.M4A1']['evidence'][0]['kind'] = 'screenshot'
        self.blocked(self.document)

    def test_changed_media(self):
        self.media.write_bytes(b'Changed after recorded review')
        self.blocked(self.document)

    def test_removing_declared_requirement_does_not_bypass_gate(self):
        group = self.document['groups']['weapon.MP5']
        group['required_observations'] = []
        group['evidence'][0]['covers'].remove('empty_reload')
        self.blocked(self.document)

    def test_unknown_claim(self):
        self.document['groups']['ui.hud']['evidence'][0]['covers'].append('invented_state')
        self.blocked(self.document)


if __name__ == '__main__':
    unittest.main()
