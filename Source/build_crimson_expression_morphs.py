import bpy
import math
from mathutils import Vector


SOURCE_BLEND = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_Jaw.blend"
OUTPUT_BLEND = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_Expressions.blend"
OUTPUT_FBX = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_Expressions.fbx"
PREVIEW_DIR = r"C:\Users\hueda\.codex\visualizations\2026\08\16\01a00973-6a77-7970-9c4c-6b51d55ff75d"


def smoothstep(a, b, value):
    if a == b:
        return 0.0
    t = max(0.0, min(1.0, (value - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def ellipse_weight(x, z, cx, cz, rx, rz):
    radius = math.sqrt(((x - cx) / rx) ** 2 + ((z - cz) / rz) ** 2)
    return 1.0 - smoothstep(0.45, 1.0, radius)


bpy.ops.wm.open_mainfile(filepath=SOURCE_BLEND)
obj = bpy.data.objects.get("Crimson_Gaze_Jaw")
if obj is None or obj.type != "MESH":
    raise RuntimeError("Crimson_Gaze_Jaw mesh was not found")

bpy.context.view_layer.objects.active = obj
obj.select_set(True)
mesh = obj.data
basis = mesh.shape_keys.key_blocks.get("Basis")
if basis is None or mesh.shape_keys.key_blocks.get("jawOpen") is None:
    raise RuntimeError("Basis/jawOpen is missing")

# The Meshy face points toward -Y. These landmarks were measured from the
# existing Crimson jaw source and deliberately use small displacements.
FACE_X = 0.035
MOUTH_Z = 0.122
# The earlier diagnostic values (0.236/0.285) were centered on the nose and
# upper cheek, so eye/brow expressions were effectively invisible in game.
# These heights match the actual Crimson eye line and brow ridge.
EYE_Z = 0.415
BROW_Z = 0.455


def side_factor(x, side):
    local_x = x - FACE_X
    if side == "left":
        return smoothstep(-0.012, 0.025, local_x)
    if side == "right":
        return 1.0 - smoothstep(-0.025, 0.012, local_x)
    return 1.0


def add_key(name, deform):
    old = mesh.shape_keys.key_blocks.get(name)
    if old is not None:
        obj.shape_key_remove(old)
    key = obj.shape_key_add(name=name, from_mix=False)
    changed = 0
    max_delta = 0.0
    for index, source in enumerate(basis.data):
        x, y, z = source.co
        # Exclude hair/back surfaces. Facial skin is the forward-most (-Y) shell.
        front = 1.0 - smoothstep(-0.345, -0.300, y)
        if front <= 0.0001:
            continue
        delta = deform(x, y, z, front)
        if delta.length_squared <= 1.0e-12:
            continue
        key.data[index].co += delta
        changed += 1
        max_delta = max(max_delta, delta.length)
    print(f"MORPH {name}: changed={changed} max_delta={max_delta:.5f}")
    return key


def smile(side):
    def deform(x, y, z, front):
        # Lift the corners without pulling the lip centre into a pointed grin.
        sx = FACE_X + (0.090 if side == "left" else -0.090)
        w = ellipse_weight(x, z, sx, MOUTH_Z + 0.002, 0.050, 0.030) * front * side_factor(x, side)
        outward = 0.003 if side == "left" else -0.003
        return Vector((outward * w, -0.002 * w, 0.014 * w))
    return deform


def cheek_squint(side):
    def deform(x, y, z, front):
        sx = FACE_X + (0.083 if side == "left" else -0.083)
        w = ellipse_weight(x, z, sx, EYE_Z - 0.035, 0.070, 0.055) * front * side_factor(x, side)
        return Vector((0.0, -0.010 * w, 0.014 * w))
    return deform


def eye_wide(side):
    def deform(x, y, z, front):
        sx = FACE_X + (0.095 if side == "left" else -0.095)
        rx, rz = 0.058, 0.035
        w = ellipse_weight(x, z, sx, EYE_Z, rx, rz) * front * side_factor(x, side)
        # Continuous signed falloff avoids a split/seam across the eye centre.
        offset = max(-1.0, min(1.0, (z - EYE_Z) / rz))
        return Vector((0.0, 0.001 * w, offset * 0.045 * w))
    return deform


def eye_squint(side):
    def deform(x, y, z, front):
        sx = FACE_X + (0.095 if side == "left" else -0.095)
        rx, rz = 0.060, 0.040
        w = ellipse_weight(x, z, sx, EYE_Z, rx, rz) * front * side_factor(x, side)
        offset = max(-1.0, min(1.0, (z - EYE_Z) / rz))
        return Vector((0.0, -0.001 * w, -offset * 0.028 * w))
    return deform


def eye_blink(side):
    def deform(x, y, z, front):
        sx = FACE_X + (0.095 if side == "left" else -0.095)
        # Keep the proven inner-eye range, extending only toward the outer
        # canthus. A symmetric wider ellipse also pulled the nose bridge.
        outward_distance = (x - sx) if side == "left" else (sx - x)
        rx = 0.115 if outward_distance > 0.0 else 0.060
        rz = 0.042
        # Use separable weights along/across the eyelid line. An elliptical
        # radius multiplies horizontal and vertical distance diagonally, which
        # reduced the measured outer-lid delta to only 8-16% of the centre.
        # Preserve full displacement through the outer eyelid; fade only in
        # the final 8% beyond the visible outer canthus.
        horizontal_weight = 1.0 - smoothstep(0.92, 1.0, abs(x - sx) / rx)
        vertical_weight = 1.0 - smoothstep(0.72, 1.0, abs(z - EYE_Z) / rz)
        w = horizontal_weight * vertical_weight * front * side_factor(x, side)
        # Close slightly below the eye centre, matching a natural upper-lid-led
        # blink. Signed convergence avoids a hard upper/lower split or seam.
        closure_z = EYE_Z - 0.004
        dz = (closure_z - z) * w
        return Vector((0.0, -0.004 * w, dz))
    return deform


def brow(side, down=False):
    def deform(x, y, z, front):
        sx = FACE_X + (0.052 if side == "left" else -0.052)
        w = ellipse_weight(x, z, sx, BROW_Z, 0.072, 0.034) * front * side_factor(x, side)
        inner = 1.0 - smoothstep(0.015, 0.075, abs(x - FACE_X))
        dz = (-0.032 if down else 0.034) * w * (0.55 + 0.45 * inner)
        dx = (-0.006 if side == "left" else 0.006) * w * inner if down else 0.0
        return Vector((dx, -0.004 * w, dz))
    return deform


def brow_inner_up(x, y, z, front):
    left = ellipse_weight(x, z, FACE_X + 0.080, BROW_Z, 0.055, 0.035)
    right = ellipse_weight(x, z, FACE_X - 0.080, BROW_Z, 0.055, 0.035)
    # The two brow ellipses do not reach the nose centre (0.080 > 0.055), so
    # they already leave the bridge fixed.  An additional X-only exclusion
    # incorrectly attenuates the actual inner brow endpoints.
    w = max(left, right) * front
    return Vector((0.0, 0.0, 0.024 * w))


def eye_region_weight(x, y, z, side):
    sx = FACE_X + (0.095 if side == "left" else -0.095)
    front = 1.0 - smoothstep(-0.345, -0.300, y)
    return ellipse_weight(x, z, sx, EYE_Z, 0.058, 0.035) * front * side_factor(x, side)


def brow_inner_region_weight(x, y, z):
    front = 1.0 - smoothstep(-0.345, -0.300, y)
    left = ellipse_weight(x, z, FACE_X + 0.080, BROW_Z, 0.055, 0.035)
    right = ellipse_weight(x, z, FACE_X - 0.080, BROW_Z, 0.055, 0.035)
    return max(left, right) * front


def mouth_frown(side):
    def deform(x, y, z, front):
        # Keep the deformation on the mouth corner.  The previous wide,
        # overlapping ellipses pulled the lip centre down and made a pout.
        sx = FACE_X + (0.090 if side == "left" else -0.090)
        w = ellipse_weight(x, z, sx, MOUTH_Z + 0.002, 0.042, 0.026) * front * side_factor(x, side)
        return Vector((0.0, 0.0, -0.010 * w))
    return deform


def mouth_shift(direction):
    def deform(x, y, z, front):
        w = ellipse_weight(x, z, FACE_X, MOUTH_Z, 0.125, 0.042) * front
        return Vector((0.020 * direction * w, 0.0, 0.0))
    return deform


def cheek_puff(x, y, z, front):
    left = ellipse_weight(x, z, FACE_X + 0.090, 0.170, 0.075, 0.070)
    right = ellipse_weight(x, z, FACE_X - 0.090, 0.170, 0.075, 0.070)
    w = max(left, right) * front
    return Vector((0.0, -0.018 * w, 0.0))


def mouth_press(side):
    def deform(x, y, z, front):
        w = ellipse_weight(x, z, FACE_X, MOUTH_Z, 0.120, 0.032) * front * side_factor(x, side)
        direction = -1.0 if z >= MOUTH_Z else 1.0
        return Vector((0.0, 0.004 * w, direction * 0.011 * w))
    return deform


add_key("mouthSmileLeft", smile("left"))
add_key("mouthSmileRight", smile("right"))
add_key("cheekSquintLeft", cheek_squint("left"))
add_key("cheekSquintRight", cheek_squint("right"))
add_key("browInnerUp", brow_inner_up)
add_key("eyeWideLeft", eye_wide("left"))
add_key("eyeWideRight", eye_wide("right"))
add_key("browDownLeft", brow("left", True))
add_key("browDownRight", brow("right", True))
add_key("mouthFrownLeft", mouth_frown("left"))
add_key("mouthFrownRight", mouth_frown("right"))
add_key("mouthLeft", mouth_shift(1.0))
add_key("mouthRight", mouth_shift(-1.0))
add_key("cheekPuff", cheek_puff)
add_key("eyeSquintLeft", eye_squint("left"))
add_key("eyeSquintRight", eye_squint("right"))
add_key("eyeBlinkLeft", eye_blink("left"))
add_key("eyeBlinkRight", eye_blink("right"))
add_key("mouthPressLeft", mouth_press("left"))
add_key("mouthPressRight", mouth_press("right"))


def print_blink_delta_stats(side, morph_name):
    sx = FACE_X + (0.095 if side == "left" else -0.095)
    key = mesh.shape_keys.key_blocks[morph_name]
    groups = {
        "center_upper": [], "center_lower": [],
        "outer_upper": [], "outer_lower": [],
    }
    for index, source in enumerate(basis.data):
        x, y, z = source.co
        front = 1.0 - smoothstep(-0.345, -0.300, y)
        vertical_distance = abs(z - EYE_Z)
        if front <= 0.5 or vertical_distance < 0.006 or vertical_distance > 0.040:
            continue
        signed_outward = (x - sx) if side == "left" else (sx - x)
        horizontal_distance = abs(x - sx)
        vertical_band = "upper" if z >= EYE_Z else "lower"
        if horizontal_distance <= 0.030:
            group_name = f"center_{vertical_band}"
        elif 0.035 <= signed_outward <= 0.090:
            group_name = f"outer_{vertical_band}"
        else:
            continue
        delta = key.data[index].co - source.co
        groups[group_name].append((abs(delta.z), delta.length))
    for group_name, deltas in groups.items():
        count = len(deltas)
        average_dz = sum(value[0] for value in deltas) / count if count else 0.0
        average_length = sum(value[1] for value in deltas) / count if count else 0.0
        maximum_length = max((value[1] for value in deltas), default=0.0)
        print(
            f"BLINK_DELTA {morph_name} {group_name}: count={count} "
            f"avg_abs_dz={average_dz:.5f} avg_len={average_length:.5f} "
            f"max_len={maximum_length:.5f}"
        )


print_blink_delta_stats("left", "eyeBlinkLeft")
print_blink_delta_stats("right", "eyeBlinkRight")

# Preview the six semantic expressions with the same coefficients as C++.
camera = bpy.data.objects.get("PreviewCamera")
if camera is None:
    camera_data = bpy.data.cameras.new("PreviewCamera")
    camera = bpy.data.objects.new("PreviewCamera", camera_data)
    bpy.context.scene.collection.objects.link(camera)
    camera.location = (0.0, -4.0, 0.0)
    camera.rotation_euler = (Vector((0.0, 0.0, 0.0)) - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 1.15
bpy.context.scene.camera = camera
bpy.context.scene.render.engine = "BLENDER_WORKBENCH"
bpy.context.scene.display.shading.light = "STUDIO"
bpy.context.scene.display.shading.color_type = "MATERIAL"
bpy.context.scene.render.resolution_x = 600
bpy.context.scene.render.resolution_y = 600
bpy.context.scene.render.resolution_percentage = 100
bpy.context.scene.render.image_settings.file_format = "PNG"

# Diagnostic gate: preserve the exact continuous weights as temporary Vertex
# Groups and render them as vertex colours before any FBX export. The groups and
# colour layer are removed below so they never enter the runtime asset.
debug_weight_sets = {
    "DBG_BrowInner": [brow_inner_region_weight(v.co.x, v.co.y, v.co.z) for v in mesh.vertices],
    "DBG_EyeLeft": [eye_region_weight(v.co.x, v.co.y, v.co.z, "left") for v in mesh.vertices],
    "DBG_EyeRight": [eye_region_weight(v.co.x, v.co.y, v.co.z, "right") for v in mesh.vertices],
}
for group_name, weights in debug_weight_sets.items():
    old_group = obj.vertex_groups.get(group_name)
    if old_group:
        obj.vertex_groups.remove(old_group)
    group = obj.vertex_groups.new(name=group_name)
    for vertex_index, weight in enumerate(weights):
        if weight > 0.0001:
            group.add([vertex_index], weight, "REPLACE")

    old_color = mesh.color_attributes.get("DBG_Weight")
    if old_color:
        mesh.color_attributes.remove(old_color)
    color_layer = mesh.color_attributes.new(name="DBG_Weight", type="BYTE_COLOR", domain="CORNER")
    for loop in mesh.loops:
        weight = weights[loop.vertex_index]
        # Blue=0, green=mid, red=1. This is the same continuous w used by deform().
        color_layer.data[loop.index].color = (
            weight,
            max(0.0, 1.0 - abs(weight - 0.5) * 2.0),
            1.0 - weight,
            1.0,
        )
    mesh.color_attributes.active_color = color_layer
    bpy.context.scene.display.shading.color_type = "VERTEX"
    bpy.context.scene.render.filepath = f"{PREVIEW_DIR}\\crimson_weight_{group_name}.png"
    bpy.ops.render.render(write_still=True)

bpy.context.scene.display.shading.color_type = "MATERIAL"

expressions = {
    "neutral": {},
    "happy": {"mouthSmileLeft": .7, "mouthSmileRight": .7, "cheekSquintLeft": .3, "cheekSquintRight": .3},
    "surprised": {"browInnerUp": .8, "eyeWideLeft": .6, "eyeWideRight": .6},
    "sad": {"browDownLeft": .5, "browDownRight": .5, "mouthFrownLeft": .6, "mouthFrownRight": .6},
    "confused": {"browDownLeft": .6, "mouthLeft": .4},
    "embarrassed": {"cheekPuff": .3, "eyeSquintLeft": .4, "eyeSquintRight": .4, "mouthPressLeft": .3, "mouthPressRight": .3},
}
for emotion, values in expressions.items():
    for key in mesh.shape_keys.key_blocks:
        if key.name != "Basis":
            key.value = 0.0
    for name, value in values.items():
        mesh.shape_keys.key_blocks[name].value = value
    bpy.context.scene.render.filepath = f"{PREVIEW_DIR}\\crimson_expression_{emotion}.png"
    bpy.ops.render.render(write_still=True)

# Required single-morph previews at Shape Key value 1.0.
for morph_name in ("browInnerUp", "eyeWideLeft", "eyeWideRight", "eyeBlinkLeft", "eyeBlinkRight"):
    for key in mesh.shape_keys.key_blocks:
        if key.name != "Basis":
            key.value = 0.0
    mesh.shape_keys.key_blocks[morph_name].value = 1.0
    bpy.context.scene.render.filepath = f"{PREVIEW_DIR}\\crimson_single_{morph_name}.png"
    bpy.ops.render.render(write_still=True)

for key in mesh.shape_keys.key_blocks:
    if key.name != "Basis":
        key.value = 0.0

for group_name in debug_weight_sets:
    group = obj.vertex_groups.get(group_name)
    if group:
        obj.vertex_groups.remove(group)
debug_color = mesh.color_attributes.get("DBG_Weight")
if debug_color:
    mesh.color_attributes.remove(debug_color)

bpy.ops.wm.save_as_mainfile(filepath=OUTPUT_BLEND)
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
armature = obj.parent
if armature:
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
else:
    bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.fbx(
    filepath=OUTPUT_FBX,
    use_selection=True,
    use_mesh_modifiers=False,
    add_leaf_bones=False,
    bake_anim=False,
    path_mode="COPY",
    embed_textures=False,
)
print(f"CRIMSON_EXPRESSIONS_DONE keys={len(mesh.shape_keys.key_blocks)} verts={len(mesh.vertices)}")
