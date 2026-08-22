import bpy
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
BLEND = PROJECT_DIR / "Source" / "Meshy_Crimson_Gaze_Import" / "Crimson_Gaze_HeadRig_Test.blend"
MESH_NAME = "Crimson_Gaze_Jaw"
ARMATURE_NAME = "Crimson_Gaze_Armature"

bpy.ops.wm.open_mainfile(filepath=str(BLEND))
obj = bpy.data.objects.get(MESH_NAME)
armature = bpy.data.objects.get(ARMATURE_NAME)
if obj is None or armature is None:
    raise RuntimeError("Crimson Head Rig source objects not found")

coords = [v.co.copy() for v in obj.data.vertices]
mins = tuple(min(c[i] for c in coords) for i in range(3))
maxs = tuple(max(c[i] for c in coords) for i in range(3))
print(f"HAND_INSPECT mesh={obj.name} verts={len(coords)} bounds_min={mins} bounds_max={maxs}")
print(f"HAND_INSPECT mesh_objects={[o.name for o in bpy.data.objects if o.type == 'MESH']}")
print(f"HAND_INSPECT materials={[slot.material.name if slot.material else None for slot in obj.material_slots]}")
print(f"HAND_INSPECT bones={[b.name for b in armature.data.bones]}")

for group in obj.vertex_groups:
    count = 0
    weight_sum = 0.0
    for vertex in obj.data.vertices:
        for assignment in vertex.groups:
            if assignment.group == group.index and assignment.weight > 1.0e-6:
                count += 1
                weight_sum += assignment.weight
                break
    print(f"HAND_INSPECT group={group.name} vertices={count} weight_sum={weight_sum:.3f}")

# Spatial population table. The model's lateral axis is X and vertical axis is Z.
center_x = 0.035
for z0, z1 in [(-1.6, -1.2), (-1.2, -0.9), (-0.9, -0.6), (-0.6, -0.3), (-0.3, 0.0)]:
    band = [c for c in coords if z0 <= c.z < z1]
    if not band:
        continue
    left = sum(1 for c in band if c.x > center_x + 0.28)
    right = sum(1 for c in band if c.x < center_x - 0.28)
    torso = len(band) - left - right
    print(f"HAND_INSPECT zband={z0:.2f}:{z1:.2f} count={len(band)} left_side={left} torso={torso} right_side={right}")

# Verify that shape keys still cover the same single mesh and list their names.
keys = [] if obj.data.shape_keys is None else [k.name for k in obj.data.shape_keys.key_blocks]
print(f"HAND_INSPECT shape_keys={keys}")
