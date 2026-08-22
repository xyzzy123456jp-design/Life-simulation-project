import bpy
import math
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
IMPORT_DIR = PROJECT_DIR / "Source" / "Meshy_Crimson_Gaze_Import"
SOURCE_BLEND = IMPORT_DIR / "Crimson_Gaze_Expressions.blend"
OUTPUT_BLEND = IMPORT_DIR / "Crimson_Gaze_HeadRig_Test.blend"
OUTPUT_FBX = IMPORT_DIR / "Crimson_Gaze_HeadRig_Test.fbx"
PREVIEW_DIR = PROJECT_DIR / "Saved" / "HeadRigPreview"

MESH_NAME = "Crimson_Gaze_Jaw"
ARMATURE_NAME = "Crimson_Gaze_Armature"
EXPECTED_MORPHS = {
    "jawOpen",
    "mouthSmileLeft", "mouthSmileRight",
    "cheekSquintLeft", "cheekSquintRight",
    "browInnerUp", "eyeWideLeft", "eyeWideRight",
    "browDownLeft", "browDownRight",
    "mouthFrownLeft", "mouthFrownRight",
    "mouthLeft", "mouthRight", "cheekPuff",
    "eyeSquintLeft", "eyeSquintRight",
    "eyeBlinkLeft", "eyeBlinkRight",
    "mouthPressLeft", "mouthPressRight",
}


def smoothstep(a, b, value):
    if a == b:
        return 0.0
    t = max(0.0, min(1.0, (value - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def head_weight(co):
    """Keep shoulders on root and blend only the neck boundary into head."""
    z = co.z
    if z >= -0.08:
        return 1.0
    if z <= -0.28:
        return 0.0
    vertical = smoothstep(-0.28, -0.08, z)
    # At the boundary, suppress broad shoulder/chest vertices. Above -0.08 all
    # hair/head vertices remain fully attached to head, including outer hair.
    lateral = 1.0 - smoothstep(0.30, 0.52, abs(co.x - 0.035))
    return vertical * lateral


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
            length = delta.length
            if length > 1.0e-8:
                changed += 1
                max_delta = max(max_delta, length)
                checksum += (index + 1) * (delta.x + 3.0 * delta.y + 7.0 * delta.z)
        result[key.name] = (changed, max_delta, checksum)
    return result


def render_preview(obj, armature, filename, nod_degrees):
    camera = bpy.data.objects.get("PreviewCamera")
    if camera is None:
        camera_data = bpy.data.cameras.new("PreviewCamera")
        camera = bpy.data.objects.new("PreviewCamera", camera_data)
        bpy.context.scene.collection.objects.link(camera)
        camera.location = (0.0, -4.0, 0.0)
        camera.rotation_euler = (0.0, 0.0, 0.0)
        camera_data.type = "ORTHO"
        camera_data.ortho_scale = 1.6
    bpy.context.scene.camera = camera
    bpy.context.scene.render.engine = "BLENDER_WORKBENCH"
    bpy.context.scene.display.shading.light = "STUDIO"
    bpy.context.scene.display.shading.color_type = "MATERIAL"
    bpy.context.scene.render.resolution_x = 700
    bpy.context.scene.render.resolution_y = 700
    bpy.context.scene.render.resolution_percentage = 100
    bpy.context.scene.render.image_settings.file_format = "PNG"
    pose_bone = armature.pose.bones["head"]
    pose_bone.rotation_mode = "XYZ"
    pose_bone.rotation_euler.x = math.radians(nod_degrees)
    bpy.context.view_layer.update()
    bpy.context.scene.render.filepath = str(PREVIEW_DIR / filename)
    bpy.ops.render.render(write_still=True)
    pose_bone.rotation_euler.x = 0.0
    bpy.context.view_layer.update()


PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(SOURCE_BLEND))
obj = bpy.data.objects.get(MESH_NAME)
armature = bpy.data.objects.get(ARMATURE_NAME)
if obj is None or obj.type != "MESH" or armature is None or armature.type != "ARMATURE":
    raise RuntimeError("Crimson expression mesh/armature was not found")
if obj.data.shape_keys is None:
    raise RuntimeError("Crimson Shape Keys are missing")

actual_morphs = {key.name for key in obj.data.shape_keys.key_blocks if key.name != "Basis"}
missing = sorted(EXPECTED_MORPHS - actual_morphs)
unexpected = sorted(actual_morphs - EXPECTED_MORPHS)
if missing:
    raise RuntimeError(f"Required existing Morphs are missing: {missing}")
if "mouthSeal" not in actual_morphs:
    print("HEAD_RIG_NOTE mouthSeal is not present in the current source asset; no new Morph is invented")
before_signature = morph_signature(obj)
vertex_count_before = len(obj.data.vertices)

# Preserve the current reference pose and add only the minimal hierarchy.
bpy.context.view_layer.objects.active = armature
armature.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
edit_bones = armature.data.edit_bones
root = edit_bones.get("root")
if root is None:
    raise RuntimeError("root bone is missing")
for name in ("neck_01", "neck_02", "head"):
    old = edit_bones.get(name)
    if old is not None:
        edit_bones.remove(old)

neck_01 = edit_bones.new("neck_01")
neck_01.head = (0.035, 0.0, -0.30)
neck_01.tail = (0.035, 0.0, -0.18)
neck_01.parent = root
neck_01.use_connect = False

neck_02 = edit_bones.new("neck_02")
neck_02.head = neck_01.tail
neck_02.tail = (0.035, 0.0, -0.08)
neck_02.parent = neck_01
neck_02.use_connect = True

head = edit_bones.new("head")
head.head = neck_02.tail
head.tail = (0.035, 0.0, 0.24)
head.parent = neck_02
head.use_connect = True
bpy.ops.object.mode_set(mode="OBJECT")

root_group = obj.vertex_groups.get("root") or obj.vertex_groups.new(name="root")
for name in ("neck_01", "neck_02", "head"):
    old_group = obj.vertex_groups.get(name)
    if old_group is not None:
        obj.vertex_groups.remove(old_group)
head_group = obj.vertex_groups.new(name="head")

# Explicitly rewrite root/head ownership. neck_01/neck_02 intentionally act as
# transform relays for this first 5-8 degree rig, per NOD_SPEC_v1.
head_count = root_count = blended_count = 0
for vertex in obj.data.vertices:
    hw = head_weight(vertex.co)
    rw = 1.0 - hw
    root_group.remove([vertex.index])
    if rw > 1.0e-6:
        root_group.add([vertex.index], rw, "REPLACE")
        root_count += 1
    if hw > 1.0e-6:
        head_group.add([vertex.index], hw, "REPLACE")
        head_count += 1
    if 1.0e-6 < hw < 1.0 - 1.0e-6:
        blended_count += 1

after_signature = morph_signature(obj)
if len(obj.data.vertices) != vertex_count_before or before_signature != after_signature:
    raise RuntimeError("Vertex order/count or Morph Target deltas changed during rigging")

for name, (changed, max_delta, checksum) in sorted(after_signature.items()):
    print(
        f"HEAD_RIG_MORPH {name}: changed={changed} "
        f"max_delta={max_delta:.6f} checksum={checksum:.6f}"
    )
print(
    f"HEAD_RIG_WEIGHTS verts={vertex_count_before} root={root_count} "
    f"head={head_count} blended={blended_count}"
)
print(
    "HEAD_RIG_BONES root -> neck_01 -> neck_02 -> head"
)

render_preview(obj, armature, "crimson_headrig_neutral.png", 0.0)
render_preview(obj, armature, "crimson_headrig_nod_7deg.png", 7.0)

bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT_BLEND))
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
armature.select_set(True)
bpy.context.view_layer.objects.active = armature
bpy.ops.export_scene.fbx(
    filepath=str(OUTPUT_FBX),
    use_selection=True,
    use_mesh_modifiers=False,
    add_leaf_bones=False,
    bake_anim=False,
    path_mode="COPY",
    embed_textures=False,
)
print(f"CRIMSON_HEAD_RIG_TEST_DONE blend={OUTPUT_BLEND} fbx={OUTPUT_FBX}")
