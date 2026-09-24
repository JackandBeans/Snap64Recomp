"""Export the authored item meshes to F3DEX2 lists for world/photo rendering.

No ROM data is consumed. 0x80F00000..0x81000000 is reserved for these immutable
assets, above the port UI arena and below librecomp's mod allocation space.
"""
import json
import math
from pathlib import Path
import struct

BASE = 0x80F00000
LIMIT = 0x100000


def build(models):
    blob = bytearray(16)
    lists = []
    for name in ("apple", "pester_ball"):
        mesh = models[name]
        data, indices = mesh["vertices"], mesh["indices"]
        if len(data) % 11 or len(indices) % 3:
            raise ValueError("Malformed item mesh")
        vertex_start = len(blob)
        for index in indices:
            v = data[index * 11:(index + 1) * 11]
            if len(v) != 11 or not all(math.isfinite(x) for x in v):
                raise ValueError("Invalid item vertex")
            # Original item's root scale is 0.1; 100 world units = one meter.
            xyz = [round(x * 1000) for x in v[:3]]
            shade = max(.35, min(1., .80 + sum(a*b for a, b in zip(v[3:6], (.12, .25, .18)))))
            rgb = [round(max(0., min(1., x * shade)) * 255) for x in v[6:9]]
            blob.extend(struct.pack(">hhhHhhBBBB", *xyz, 0, 0, 0, *rgb, 255))
        lists.append(BASE + len(blob))
        def command(a, b=0):
            blob.extend(struct.pack(">II", a, b))
        command(0xE7000000)  # pipe sync; retain caller's fog/depth render mode
        command(0xD7000000)  # texture off
        command(0xD9F1F9FF, 0x00200005)  # clear lighting/texgen/cull; smooth shade + Z
        command(0xFCFFFFFF, 0xFFFE793C)  # G_CC_SHADE in both cycles
        # Thirty vertices per batch, preserving triangle boundaries.
        for first in range(0, len(indices), 30):
            count = min(30, len(indices) - first)
            command(0x01000000 | count << 12 | count << 1, BASE + vertex_start + first * 16)
            for i in range(0, count, 3):
                command(0x05000000 | (i*2) << 16 | ((i+1)*2) << 8 | (i+2)*2)
        command(0xE7000000)
        command(0xD9FFFFFF, 0x00020000)  # same lighting restore as the ROM item
        command(0xDF000000)
    if len(blob) > LIMIT:
        raise ValueError("VR projectile assets exceed reserved arena")
    struct.pack_into(">4sIII", blob, 0, b"VRI1", len(blob), *lists)
    return blob


def export(root):
    assets = Path(root) / "assets/vr"
    models = json.loads((assets / "props.json").read_text())["models"]
    blob = build(models)
    (assets / "projectiles.bin").write_bytes(blob)
    print(f"VR projectile display lists: {len(blob)} bytes")


if __name__ == "__main__":
    export(Path(__file__).resolve().parents[1])
