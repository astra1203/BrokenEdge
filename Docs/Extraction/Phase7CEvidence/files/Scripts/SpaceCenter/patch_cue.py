"""Add an explicit geometry snapshot hook; leave upstream parse failures visible.

Only the converter's allowlisted recovery path may use these snapshots. The
original mesh vertex/index buffers are retained before unsupported distance-field
metadata is read. No source package or vertex is patched.
"""
from source_packages import WORK

path = WORK / 'Tools/CUE4Parse/CUE4Parse/UE4/Assets/Exports/StaticMesh/FStaticMeshRenderData.cs'
text = path.read_text(encoding='utf-8')
if 'SpaceCenterGeometrySnapshots' not in text:
    declaration = 'public class FStaticMeshRenderData\n{'
    hook = '                    FDeltaForceStaticMeshRenderData.SkipDistanceFields(Ar, LODs.Length);'
    assert text.count(declaration) == text.count(hook) == 1
    text = text.replace(declaration, declaration + '\n    public static readonly System.Collections.Concurrent.ConcurrentDictionary<string, FStaticMeshRenderData> SpaceCenterGeometrySnapshots = new();')
    text = text.replace(hook, '                    SpaceCenterGeometrySnapshots[Ar.Name] = this;\n' + hook)
    path.write_text(text, encoding='utf-8')
print('CUE4Parse geometry snapshot hook ready')
