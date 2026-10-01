"""Test the native PSA rotation convention independently of Unreal runtime."""
import ast, math, unittest
from pathlib import Path

class Quaternion(tuple):
    def __new__(cls,*values): return super().__new__(cls,values)

class Unreal:
    Quat=Quaternion

module=ast.parse(Path(__file__).with_name('repair_import.py').read_text(encoding='utf-8'))
namespace={'math':math,'unreal':Unreal}
functions=[node for node in module.body if isinstance(node,ast.FunctionDef) and node.name in ['quat','decode_rotation']]
exec(compile(ast.Module(body=functions,type_ignores=[]),'<decoder>','exec'),namespace)

class RotationTests(unittest.TestCase):
    def test_round_trip_exported_root_and_child(self):
        native=(.2,-.3,.4,math.sqrt(.71))
        for bone_index in [0,1,71]:
            exported=(native[0],-native[1],native[2],-native[3] if bone_index==0 else native[3])
            decoded=namespace['decode_rotation'](exported,bone_index)
            self.assertGreater(abs(sum(a*b for a,b in zip(native,decoded))),1-1e-7)

    def test_invalid_quaternion_is_rejected(self):
        for values in [(0,0,0,0),(float('nan'),0,0,1)]:
            with self.assertRaises(ValueError):namespace['decode_rotation'](values,1)

if __name__=='__main__':unittest.main()
