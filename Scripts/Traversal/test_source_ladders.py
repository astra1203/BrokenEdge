"""Regression checks for the source errors that displaced/misidentified ladders."""
import math
import unittest
from source_ladders import Exports, matrix, multiply, point, rebased


class SourceTransforms(unittest.TestCase):
    def assertPoint(self, actual, expected):
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=5)

    def test_unreal_yaw_scale_and_rebase(self):
        root = matrix(dict(RelativeLocation=dict(X=664000, Y=-451000, Z=-22400),
                           RelativeRotation=dict(Yaw=90), RelativeScale3D=dict(X=2, Y=3, Z=.5)))
        self.assertPoint(point(rebased(root, [664000, -451000, -22400]), [4,5,6]), [-15,8,3])

    def test_saved_ism_does_not_lose_parent_scale(self):
        parent = matrix(dict(RelativeLocation=dict(Z=237.784), RelativeScale3D=dict(Z=.947088)))
        instance = matrix(dict(Rotation=dict(X=0,Y=0,Z=0,W=.99830717), Translation=dict(Z=38), Scale3D={}), True)
        self.assertPoint(point(multiply(parent, instance)), [0,0,273.773344])

    def test_socket_is_between_child_and_parent_and_inherits_template(self):
        def ref(index): return dict(ObjectPath='/Game/Test.'+str(index))
        exports = Exports([dict(package='/Game/Test', objects=[
            dict(export_index=0, Type='SceneComponent', Properties=dict(RelativeLocation=dict(X=10,Y=20),
                 RelativeRotation=dict(Yaw=90), RelativeScale3D=dict(Z=2))),
            dict(export_index=1, Type='StaticMeshComponent', Properties=dict(AttachParent=ref(0), StaticMesh=dict(ObjectPath='/Game/Test.4'))),
            dict(export_index=2, Type='ArrowComponent', Template=ref(3), Properties=dict(AttachParent=ref(1), AttachSocketName='Exit')),
            dict(export_index=3, Type='ArrowComponent', Properties=dict(RelativeLocation=dict(Y=-30))),
            dict(export_index=4, Type='StaticMesh', Properties=dict(Sockets=[ref(5)])),
            dict(export_index=5, Type='StaticMeshSocket', Properties=dict(SocketName='Exit', RelativeLocation=dict(Y=97,Z=15)))])])
        self.assertPoint(point(exports.transform(exports.get(ref(2)))), [-57,20,30])
        exports.get(ref(2))['Properties']['AttachSocketName'] = 'Missing'
        with self.assertRaises(ValueError): exports.transform(exports.get(ref(2)))

    def test_attachment_cycle_is_not_silently_truncated(self):
        exports = Exports([dict(package='/Game/Test', objects=[dict(export_index=0, Type='SceneComponent',
            Properties=dict(AttachParent=dict(ObjectPath='/Game/Test.0')))])])
        with self.assertRaises(ValueError): exports.transform(exports.get(dict(ObjectPath='/Game/Test.0')))


if __name__ == '__main__':
    unittest.main()
