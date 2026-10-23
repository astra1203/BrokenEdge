"""Relationship failures must remain visible rather than silently activate parts."""
import tempfile
import unittest
import zipfile
from pathlib import Path

from audit_weapon_mechanics import Audit, assembly_graph


def row(index, parent=0, socket=0, child=0, parent_socket=0):
    return dict(Index=index, ItemId=100 + index, ParentIndex=parent,
                ParentSocketId=parent_socket, SocketId=socket, Child=child)


class AssemblyTests(unittest.TestCase):
    def test_repeated_parent_rows_are_edges_not_duplicate_parts(self):
        graph = assembly_graph({'a': row(1, socket=3, child=2), 'b': row(1, socket=4, child=3),
                                'c': row(2, 1, parent_socket=3), 'd': row(3, 1, parent_socket=4)})
        self.assertEqual(len(graph['nodes']), 3)
        self.assertEqual(len(graph['edges']), 2)
        self.assertEqual(graph['issues'], [])

    def test_duplicate_connection(self):
        graph = assembly_graph({'a': row(1, socket=3, child=2), 'b': row(1, socket=3, child=2),
                                'c': row(2, 1, parent_socket=3)})
        self.assertTrue(any(x.startswith('duplicate_connection:') for x in graph['issues']))

    def test_missing_and_orphan(self):
        graph = assembly_graph({'a': row(1, socket=3, child=9), 'b': row(2, 7)})
        self.assertIn('missing_child:a', graph['issues'])
        self.assertIn('orphan:2', graph['issues'])

    def test_cycle(self):
        graph = assembly_graph({'a': row(1, 2, 3, 2, 4), 'b': row(2, 1, 4, 1, 3)})
        self.assertTrue(any(x.startswith('cycle:') for x in graph['issues']))

    def test_parent_socket_mismatch(self):
        graph = assembly_graph({'a': row(1, socket=3, child=2), 'b': row(2, 1, parent_socket=4)})
        self.assertIn('parent_socket_mismatch:a', graph['issues'])


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.archives = self.root / '网盘上传压缩包'
        self.archives.mkdir()

    def tearDown(self):
        self.directory.cleanup()

    def archive(self, name, members):
        with zipfile.ZipFile(self.archives / name, 'w') as archive:
            for member, content in members.items():
                archive.writestr(member, content)

    def test_duplicate_table_candidates(self):
        self.archive('DataTables_001.zip', {'a/T.json': '{"Rows":{}}', 'b/T.json': '{"Rows":{}}'})
        audit = Audit(self.root)
        try:
            self.assertEqual(audit.table('T'), (None, {}))
            self.assertEqual(audit.issues[0]['kind'], 'table_candidates')
        finally:
            audit.close()

    def test_partial_missing_rows(self):
        self.archive('DataTables_001.zip', {'a/T.json': '{"Rows":{"r":{"Id":1}}}'})
        audit = Audit(self.root)
        try:
            audit.select('T', 'Id', [1, 2])
            self.assertEqual(audit.issues[0]['values'], [2])
        finally:
            audit.close()

    def test_explicit_curve_resolution_and_mode_rejection(self):
        member = 'assets/DeltaForce/Content/DataTables/MP/C.json'
        self.archive('DataTables_001.zip', {member: '{"Type":"CurveFloat","FloatCurve":{"Keys":[]}}'})
        audit = Audit(self.root)
        try:
            conflict = audit.curve({'ObjectPath': 'DeltaForce/Content/DataTables/MP/C.0'}, 'test', '/FloatCurve')
            self.assertEqual(conflict['status'], 'mode_conflict')
            missing = audit.curve({'AssetPathName': '/Game/DataTables/Missing.Missing'}, 'test', '/FloatCurve')
            self.assertEqual(missing['status'], 'missing_reference')
            self.assertEqual(audit.evidence, {})
        finally:
            audit.close()

    def test_curve_retains_tangents_and_rejects_ambiguity(self):
        member = 'assets/DeltaForce/Content/DataTables/C.json'
        content = '{"Type":"CurveFloat","FloatCurve":{"Keys":[{"Time":0,"ArriveTangent":2}],"PreInfinityExtrap":3}}'
        self.archive('DataTables_001.zip', {member: content})
        audit = Audit(self.root)
        try:
            result = audit.curve({'AssetPathName': '/Game/DataTables/C.C'}, 'test', '/FloatCurve')
            self.assertEqual(result['status'], 'reference_derived')
            self.assertEqual(audit.evidence[result['target']]['data']['FloatCurve']['Keys'][0]['ArriveTangent'], 2)
        finally:
            audit.close()
        self.archive('other.zip', {member: content})
        audit = Audit(self.root)
        try:
            result = audit.curve({'AssetPathName': '/Game/DataTables/C.C'}, 'test', '/FloatCurve')
            self.assertEqual(result['status'], 'ambiguous_reference')
        finally:
            audit.close()


if __name__ == '__main__':
    unittest.main()
