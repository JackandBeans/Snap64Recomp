"""Blender geometry checks shared by the prop generator and saved-file review.

Run against a saved source:
  blender --background assets/vr/snap_vr_props.blend --python-exit-code 1 \
      --python tools/vr_model_validation.py
"""
import bpy
from mathutils.bvhtree import BVHTree


def validate_camera_assembly(objects):
    """Require a physical path from every detail to the main camera casting.

    Surface intersections or actual containment count as attachment. Merely
    overlapping bounding boxes does not: that misses hollow rings and bevels.
    One-micron contact tolerance accommodates coincident faces, not visible gaps.
    """
    objects = list(objects)
    bpy.context.view_layer.update()
    vertices = [[obj.matrix_world @ v.co for v in obj.data.vertices] for obj in objects]
    trees = [BVHTree.FromPolygons(v, [list(f.vertices) for f in obj.data.polygons])
             for obj, v in zip(objects, vertices)]
    bounds = [([min(p[a] for p in v) for a in range(3)],
               [max(p[a] for p in v) for a in range(3)]) for v in vertices]
    graph = [set() for _ in objects]

    def inside(point, tree):
        nearest, normal, _, distance = tree.find_nearest(point)
        return nearest is not None and ((point-nearest).dot(normal) < -1e-7 or distance < 1e-6)

    for i in range(len(objects)):
        for j in range(i):
            if any(bounds[i][1][a] < bounds[j][0][a]-1e-6 or
                   bounds[j][1][a] < bounds[i][0][a]-1e-6 for a in range(3)):
                continue
            if (trees[i].overlap(trees[j]) or inside(vertices[i][0], trees[j]) or
                    inside(vertices[j][0], trees[i])):
                graph[i].add(j)
                graph[j].add(i)

    root = next(i for i, obj in enumerate(objects) if obj.name == 'Camera main casting')
    seen = {root}
    pending = [root]
    while pending:
        for index in graph[pending.pop()]-seen:
            seen.add(index)
            pending.append(index)
    detached = [objects[i].name for i in range(len(objects)) if i not in seen]
    if detached:
        raise ValueError('Detached camera components: ' + ', '.join(detached))
    print(f'Camera assembly: all {len(objects)} parts physically connect to the casting', flush=True)


if __name__ == '__main__':
    validate_camera_assembly(bpy.data.collections['camera'].objects)
