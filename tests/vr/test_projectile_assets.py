"""Check actual exported F3DEX2 vertices, batch addressing and triangle counts."""
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from vr_projectile_assets import BASE, build


class ProjectileAssets(unittest.TestCase):
    def test_exported_meshes(self):
        models = json.loads((ROOT / "assets/vr/props.json").read_text())["models"]
        data = build(models)
        self.assertEqual(data, (ROOT / "assets/vr/projectiles.bin").read_bytes())
        magic, size, apple, pester = struct.unpack_from(">4sIII", data)
        self.assertEqual(magic, b"VRI1")
        self.assertEqual(size, len(data))
        for name, address in (("apple", apple), ("pester_ball", pester)):
            triangles = 0
            vertices = []
            count = 0
            cursor = address - BASE
            while True:
                a, b = struct.unpack_from(">II", data, cursor)
                cursor += 8
                opcode = a >> 24
                if opcode == 0xDF:
                    break
                if opcode == 1:
                    count = (a >> 12) & 255
                    self.assertLessEqual(count, 30)
                    self.assertEqual((a & 255) >> 1, count)
                    self.assertEqual(count % 3, 0)
                    for i in range(count):
                        vertices.append(struct.unpack_from(">hhh", data, b - BASE + i * 16))
                elif opcode == 5:
                    ids = [(a >> bit) & 255 for bit in (16, 8, 0)]
                    self.assertTrue(all(i % 2 == 0 and i // 2 < count for i in ids))
                    triangles += 1
            mesh = models[name]
            self.assertEqual(triangles, len(mesh["indices"]) // 3)
            for xyz, index in zip(vertices, mesh["indices"]):
                authored = mesh["vertices"][index * 11:index * 11 + 3]
                for actual, wanted in zip(xyz, authored):
                    # Root scale 0.1 and 100 units/m matches the held mesh.
                    self.assertLessEqual(abs(actual / 1000 - wanted), .000501)


if __name__ == "__main__":
    unittest.main()
