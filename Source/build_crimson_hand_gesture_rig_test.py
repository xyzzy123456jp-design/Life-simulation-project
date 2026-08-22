import bpy
import math
from pathlib import Path
from mathutils import Vector


PROJECT_DIR = Path(__file__).resolve().parent.parent
IMPORT_DIR = PROJECT_DIR / "Source" / "Meshy_Crimson_Gaze_Import"
SOURCE_BLEND = IMPORT_DIR / "Crimson_Gaze_HeadRig_Test.blend"
OUTPUT_BLEND = IMPORT_DIR / "Crimson_Gaze_HandRig_Test.blend"
OUTPUT_FBX = IMPORT_DIR / "Crimson_Gaze_HandRig_Test.fbx"
PREVIEW_DIR = PROJECT_DIR / "Saved" / "HandGesturePreview"
MESH_NAME = "Crimson_Gaze_Jaw"
ARMATURE_NAME = "Crimson_Gaze_Armature"

# Character-right arm in mesh coordinates. Pivots follow the visible shoulder,
# elbow, wrist and palm centers of the neutral Crimson mesh.
SHOULDER = Vector((-0.255, 0.0, -0.205))
UPPER_START = Vector((-0.285, 0.0, -0.235))
ELBOW = Vector((-0.515, 0.0, -0.530))
WRIST = Vector((-0.715, 0.0, -0.805))
HAND_END = Vector((-0.800, 0.0, -0.930))
ARM_BONES = ("clavicle_r", "upperarm_r", "lowerarm_r", "hand_r")


def morph_signature(obj):
    basis = obj.data.shape_keys.key_blocks["Basis"]
    result = {}
    for key in obj.data.shape_keys.key_blocks:
        if key.name == "Basis":
            continue
        changed = 0
        max_delta = 0.0
        checksum = 0.0
        for index, point in enumerate(key.data):
            delta = point.co - basis.data[index].co
            if delta.length > 1.0e-8:
                changed += 1
                max_delta = max(max_delta, delta.length)
                checksum += (index + 1) * (delta.x + 3.0 * delta.y + 7.0 * delta.z)
        result[key.name] = (changed, max_delta, checksum)
    return result


def closest_segment_parameter(point, start, end):
    axis = end - start
    return max(0.0, min(1.0, (point - start).dot(axis) / axis.length_squared))


def smoothstep(a, b, value):
    t = max(0.0, min(1.0, (value - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def arm_weights(co):
    # Do not steal torso vertices. The boundary follows the diagonal underside
    # of the sleeve from armpit to elbow and includes skin plus clothing because
    # both are part of this same mesh.
    if co.x > -0.245 or co.z > -0.145:
        return None
    path = (UPPER_START, ELBOW, WRIST, HAND_END)
    candidates = []
    accumulated = 0.0
    total = sum((path[i + 1] - path[i]).length for i in range(3))
    for i in range(3):
        start, end = path[i], path[i + 1]
        t = closest_segment_parameter(co, start, end)
        closest = start.lerp(end, t)
        distance = (co - closest).length
        along = (accumulated + t * (end - start).length) / total
        candidates.append((distance, along))
        accumulated += (end - start).length
    distance, along = min(candidates, key=lambda item: item[0])
    radius = 0.19 if along < 0.72 else 0.16
    if distance > radius:
        return None

    # Smooth ownership along the chain. Shoulder begins blended with root;
    # elbow and wrist have short two-bone transitions instead of hard seams.
    root = max(0.0, 1.0 - smoothstep(0.00, 0.12, along))
    clavicle = max(0.0, 1.0 - abs(along - 0.08) / 0.13)
    upper = max(0.0, 1.0 - abs(along - 0.32) / 0.27)
    lower = max(0.0, 1.0 - abs(along - 0.68) / 0.28)
    hand = smoothstep(0.82, 0.95, along)
    values = [root, clavicle, upper, lower, hand]
    total_weight = sum(values)
    if total_weight <= 1.0e-6:
        return None
    return [value / total_weight for value in values]


def render_preview(obj, armature, filename, upper_x=0.0, upper_z=0.0, lower_x=0.0, lower_z=0.0):
    camera_data = bpy.data.cameras.get("HandPreviewCameraData") or bpy.data.cameras.new("HandPreviewCameraData")
    camera = bpy.data.objects.get("HandPreviewCamera") or bpy.data.objects.new("HandPreviewCamera", camera_data)
    if camera.name not in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.link(camera)
    camera.location = (0.0, -4.0, -0.20)
    camera.rotation_euler = (math.radians(90), 0.0, 0.0)
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 2.15
    bpy.context.scene.camera = camera
    bpy.context.scene.render.engine = "BLENDER_WORKBENCH"
    bpy.context.scene.display.shading.light = "STUDIO"
    bpy.context.scene.display.shading.color_type = "MATERIAL"
    bpy.context.scene.render.resolution_x = 800
    bpy.context.scene.render.resolution_y = 800
    bpy.context.scene.render.resolution_percentage = 100
    bpy.context.scene.render.image_settings.file_format = "PNG"
    upper = armature.pose.bones["upperarm_r"]
    lower = armature.pose.bones["lowerarm_r"]
    upper.rotation_mode = "XYZ"
    lower.rotation_mode = "XYZ"
    upper.rotation_euler = (math.radians(upper_x), 0.0, math.radians(upper_z))
    lower.rotation_euler = (math.radians(lower_x), 0.0, math.radians(lower_z))
    bpy.context.view_layer.update()
    bpy.context.scene.render.filepath = str(PREVIEW_DIR / filename)
    bpy.ops.render.render(write_still=True)
    upper.rotation_euler = (0.0, 0.0, 0.0)
    lower.rotation_euler = (0.0, 0.0, 0.0)


PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE_BLEND))
obj = bpy.data.objects[MESH_NAME]
armature = bpy.data.objects[ARMATURE_NAME]
before_signature = morph_signature(obj)
vertex_count = len(obj.data.vertices)

bpy.context.view_layer.objects.active = armature
armature.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
root = armature.data.edit_bones.get("root")
for name in ARM_BONES:
    old = armature.data.edit_bones.get(name)
    if old:
        armature.data.edit_bones.remove(old)

clavicle = armature.data.edit_bones.new("clavicle_r")
clavicle.head, clavicle.tail, clavicle.parent = Vector((-0.12, 0.0, -0.18)), SHOULDER, root
upper = armature.data.edit_bones.new("upperarm_r")
upper.head, upper.tail, upper.parent = UPPER_START, ELBOW, clavicle
lower = armature.data.edit_bones.new("lowerarm_r")
lower.head, lower.tail, lower.parent = ELBOW, WRIST, upper
hand = armature.data.edit_bones.new("hand_r")
hand.head, hand.tail, hand.parent = WRIST, HAND_END, lower
for bone in (clavicle, upper, lower, hand):
    bone.use_connect = False
bpy.ops.object.mode_set(mode="OBJECT")

root_group = obj.vertex_groups.get("root")
head_group = obj.vertex_groups.get("head")
if root_group is None or head_group is None:
    raise RuntimeError("Existing root/head weights are missing")
for name in ARM_BONES:
    old = obj.vertex_groups.get(name)
    if old:
        obj.vertex_groups.remove(old)
groups = [obj.vertex_groups.new(name=name) for name in ARM_BONES]

selected = blended = 0
counts = [0] * len(groups)
for vertex in obj.data.vertices:
    weights = arm_weights(vertex.co)
    if weights is None:
        continue
    selected += 1
    root_weight, *bone_weights = weights
    root_group.remove([vertex.index])
    head_group.remove([vertex.index])
    if root_weight > 1.0e-6:
        root_group.add([vertex.index], root_weight, "REPLACE")
    active = 0
    for i, weight in enumerate(bone_weights):
        if weight > 1.0e-6:
            groups[i].add([vertex.index], weight, "REPLACE")
            counts[i] += 1
            active += 1
    if active > 1 or (active and root_weight > 1.0e-6):
        blended += 1

if len(obj.data.vertices) != vertex_count or morph_signature(obj) != before_signature:
    raise RuntimeError("Vertex order/count or Morph Target deltas changed")

print(f"HAND_RIG_WEIGHTS selected={selected} blended={blended} counts={dict(zip(ARM_BONES, counts))}")
print(f"HAND_RIG_BONES root -> clavicle_r -> upperarm_r -> lowerarm_r -> hand_r")
for name, signature in sorted(before_signature.items()):
    print(f"HAND_RIG_MORPH {name}: changed={signature[0]} max_delta={signature[1]:.6f} checksum={signature[2]:.6f}")

render_preview(obj, armature, "crimson_handrig_neutral.png")
render_preview(obj, armature, "crimson_handrig_upper_x_pos20.png", upper_x=20)
render_preview(obj, armature, "crimson_handrig_upper_x_neg20.png", upper_x=-20)
render_preview(obj, armature, "crimson_handrig_upper_z_pos20.png", upper_z=20)
render_preview(obj, armature, "crimson_handrig_upper_z_neg20.png", upper_z=-20)

bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT_BLEND))
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
armature.select_set(True)
bpy.context.view_layer.objects.active = armature
bpy.ops.export_scene.fbx(
    filepath=str(OUTPUT_FBX), use_selection=True, use_mesh_modifiers=False,
    add_leaf_bones=False, bake_anim=False, path_mode="COPY", embed_textures=False,
)
print(f"CRIMSON_HAND_RIG_TEST_DONE blend={OUTPUT_BLEND} fbx={OUTPUT_FBX}")
