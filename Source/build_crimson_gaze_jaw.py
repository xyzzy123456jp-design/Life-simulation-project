import bpy
import math
import bmesh
from mathutils import Vector


SOURCE_FBX = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Meshy_AI_Crimson_Gaze_0816095605_texture.fbx"
OUTPUT_BLEND = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_Jaw.blend"
OUTPUT_FBX = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_Jaw.fbx"
PREVIEW_PNG = r"C:\Users\hueda\.codex\visualizations\2026\08\16\01a00973-6a77-7970-9c4c-6b51d55ff75d\crimson_jaw_open.png"
PREVIEW_CLOSED_PNG = r"C:\Users\hueda\.codex\visualizations\2026\08\16\01a00973-6a77-7970-9c4c-6b51d55ff75d\crimson_jaw_closed.png"


def smoothstep(edge0, edge1, value):
    if edge0 == edge1:
        return 0.0
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SOURCE_FBX)
obj = bpy.data.objects.get("mesh_node")
if obj is None or obj.type != "MESH":
    raise RuntimeError("mesh_node was not found")

obj.name = "Crimson_Gaze_Jaw"
mesh = obj.data
mesh.name = "Crimson_Gaze_Jaw"

# Meshy FBXに混入している既定Cube・Camera・Lightは書き出さない。
for candidate in list(bpy.data.objects):
    if candidate != obj:
        bpy.data.objects.remove(candidate, do_unlink=True)

bpy.context.view_layer.objects.active = obj
obj.select_set(True)
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

# 閉じた一枚面だった口の中央に、実際の開口部を作る。
bm = bmesh.new()
bm.from_mesh(mesh)
mouth_center_x = 0.035
mouth_center_z = 0.122
mouth_faces = []
for face in bm.faces:
    center = face.calc_center_median()
    ellipse = ((center.x - mouth_center_x) / 0.078) ** 2 + ((center.z - mouth_center_z) / 0.013) ** 2
    if center.y < -0.342 and ellipse < 1.0:
        mouth_faces.append(face)
if not mouth_faces:
    raise RuntimeError("Mouth opening did not select any faces")
bmesh.ops.delete(bm, geom=mouth_faces, context="FACES")
# 削除した穴の境界頂点を記録する。閉口Basisでは下側境界を上側へ重ね、
# jawOpen側だけ元の穴を残すことで、停止中の切断面を見えなくする。
bm.verts.index_update()
bm.edges.index_update()
mouth_boundary_indices = set()
for edge in bm.edges:
    if not edge.is_boundary:
        continue
    for vert in edge.verts:
        x, y, z = vert.co
        ellipse = ((x - mouth_center_x) / 0.095) ** 2 + ((z - mouth_center_z) / 0.024) ** 2
        if y < -0.335 and ellipse < 1.0:
            mouth_boundary_indices.add(vert.index)
bm.to_mesh(mesh)
bm.free()
mesh.update()
face_vertex_count = len(mesh.vertices)

# 穴の後ろに暗い口腔を置く。
cavity_mat = bpy.data.materials.new("M_Crimson_MouthCavity")
cavity_mat.diffuse_color = (0.055, 0.012, 0.016, 1.0)
cavity_mat.metallic = 0.0
cavity_mat.roughness = 0.72
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, location=(mouth_center_x, -0.315, 0.108))
cavity = bpy.context.object
cavity.name = "Crimson_MouthCavity"
cavity.scale = (0.092, 0.030, 0.040)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
cavity.data.materials.append(cavity_mat)

# 上歯8本を口腔の手前上側に配置する。
teeth_mat = bpy.data.materials.new("M_Crimson_UpperTeeth")
teeth_mat.diffuse_color = (0.94, 0.91, 0.82, 1.0)
teeth_mat.metallic = 0.0
teeth_mat.roughness = 0.48
inner_objects = [cavity]
xs = (-0.069, -0.050, -0.029, -0.010, 0.010, 0.029, 0.050, 0.069)
widths = (0.030, 0.034, 0.038, 0.038, 0.038, 0.038, 0.034, 0.030)
for tooth_index, (x, width) in enumerate(zip(xs, widths), 1):
    # 閉口時は上唇の裏へ隠し、jawOpen側だけ下へ出す。
    bpy.ops.mesh.primitive_cube_add(location=(x + mouth_center_x, -0.350, 0.175))
    tooth = bpy.context.object
    tooth.name = f"Crimson_UpperTooth_{tooth_index:02d}"
    tooth.scale = (width * 0.5, 0.009, 0.014)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel = tooth.modifiers.new("Soft edges", "BEVEL")
    bevel.width = 0.003
    bevel.segments = 3
    bpy.context.view_layer.objects.active = tooth
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    tooth.data.materials.append(teeth_mat)
    inner_objects.append(tooth)

# 顔・口腔・歯を1つのSkeletal Meshにまとめる。口腔と歯は後述のMorph対象外。
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
for inner in inner_objects:
    inner.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.object.join()
mesh = obj.data

basis = obj.shape_key_add(name="Basis", from_mix=False)
jaw = obj.shape_key_add(name="jawOpen", from_mix=False)

# 結合後も歯だけを識別できるよう、歯マテリアルを使う頂点を収集する。
teeth_material_index = next(
    (i for i, material in enumerate(mesh.materials) if material and material.name == teeth_mat.name),
    -1,
)
teeth_vertices = set()
if teeth_material_index >= 0:
    for polygon in mesh.polygons:
        if polygon.material_index == teeth_material_index:
            teeth_vertices.update(polygon.vertices)
for index in teeth_vertices:
    # Basisでは口より十分上かつ奥に収納し、開口時だけ上歯として現れる。
    jaw.data[index].co.y -= 0.015
    jaw.data[index].co.z -= 0.050

affected = 0
max_weight = 0.0
thinned = 0
for index, vertex in enumerate(mesh.vertices):
    if index >= face_vertex_count:
        continue
    x, y, z = vertex.co
    # 正面(-Y)にある口から顎だけを選択。髪、首、頬上部は動かさない。
    front_weight = 1.0 - smoothstep(-0.36, -0.30, y)
    side_weight = 1.0 - smoothstep(0.09, 0.18, abs(x - mouth_center_x))
    upper_weight = 1.0 - smoothstep(0.125, 0.150, z)
    lower_weight = smoothstep(0.035, 0.085, z)
    weight = front_weight * side_weight * upper_weight * lower_weight
    if weight <= 0.0001:
        continue
    # 実際に切った口の下側を中心に動かす。中央の下唇はほぼ剛体移動させ、
    # 周辺だけ滑らかに減衰させることで膨張を防ぐ。
    jaw.data[index].co += Vector((0.0, 0.024 * weight, -0.038 * weight))

    # 下唇の上下厚だけを開口時に約30%圧縮する。顎全体の下方向移動量は
    # 変えないため、口の動きの大きさを保ったまま「太く膨らむ」見え方を抑える。
    lip_center_z = 0.112
    lip_band = (1.0 - smoothstep(0.018, 0.038, abs(z - lip_center_z)))
    lip_front = 1.0 - smoothstep(-0.375, -0.345, y)
    lip_side = 1.0 - smoothstep(0.055, 0.115, abs(x - mouth_center_x))
    thin_weight = lip_band * lip_front * lip_side
    if thin_weight > 0.0001:
        jaw.data[index].co.z -= (z - lip_center_z) * 0.84 * thin_weight
        thinned += 1
    affected += 1
    max_weight = max(max_weight, weight)

if affected == 0:
    raise RuntimeError("jawOpen did not affect any vertices")

# Basisだけを完全閉口にする。上下の境界をX順で一対一に対応させ、
# 両方を中間位置へ寄せる。下側だけを上側まで持ち上げる方式よりも
# 唇の膨らみを抑えながら、歯や口腔が見える隙間を閉じられる。
upper_boundary = [
    index for index in mouth_boundary_indices
    if index < face_vertex_count and basis.data[index].co.z >= mouth_center_z
]
lower_boundary = [
    index for index in mouth_boundary_indices
    if index < face_vertex_count and basis.data[index].co.z < mouth_center_z
]
if not upper_boundary or not lower_boundary:
    raise RuntimeError("Mouth boundary pairing failed")
upper_boundary.sort(key=lambda index: basis.data[index].co.x)
lower_boundary.sort(key=lambda index: basis.data[index].co.x)
pair_count = min(len(upper_boundary), len(lower_boundary))
for pair_index in range(pair_count):
    upper_pos = round(pair_index * (len(upper_boundary) - 1) / max(pair_count - 1, 1))
    lower_pos = round(pair_index * (len(lower_boundary) - 1) / max(pair_count - 1, 1))
    upper_index = upper_boundary[upper_pos]
    lower_index = lower_boundary[lower_pos]
    seam = (basis.data[upper_index].co + basis.data[lower_index].co) * 0.5
    basis.data[upper_index].co = seam.copy()
    basis.data[lower_index].co = seam.copy()

# UnrealへSkeletal Meshとして読み込ませるため、全頂点をrootボーンへ100%ウェイトする。
armature_data = bpy.data.armatures.new("Crimson_Gaze_Armature")
armature = bpy.data.objects.new("Crimson_Gaze_Armature", armature_data)
bpy.context.collection.objects.link(armature)
bpy.context.view_layer.objects.active = armature
bpy.ops.object.mode_set(mode="EDIT")
root = armature_data.edit_bones.new("root")
root.head = (0.0, 0.0, -0.95)
root.tail = (0.0, 0.0, -0.75)
bpy.ops.object.mode_set(mode="OBJECT")

group = obj.vertex_groups.new(name="root")
group.add(range(len(mesh.vertices)), 1.0, "REPLACE")
modifier = obj.modifiers.new(name="Armature", type="ARMATURE")
modifier.object = armature
obj.parent = armature

bpy.ops.wm.save_as_mainfile(filepath=OUTPUT_BLEND)

bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
armature.select_set(True)
bpy.context.view_layer.objects.active = armature
bpy.ops.export_scene.fbx(
    filepath=OUTPUT_FBX,
    use_selection=True,
    use_mesh_modifiers=False,
    add_leaf_bones=False,
    bake_anim=False,
    path_mode="COPY",
    embed_textures=False,
)

# 閉口とjawOpen=1の正面プレビューを保存する。
jaw.value = 0.0
camera_data = bpy.data.cameras.new("PreviewCamera")
camera = bpy.data.objects.new("PreviewCamera", camera_data)
bpy.context.scene.collection.objects.link(camera)
camera.location = (0.0, -4.0, 0.0)
camera.rotation_euler = (Vector((0.0, 0.0, 0.0)) - camera.location).to_track_quat("-Z", "Y").to_euler()
camera_data.type = "ORTHO"
camera_data.ortho_scale = 2.15
bpy.context.scene.camera = camera
bpy.context.scene.render.engine = "BLENDER_WORKBENCH"
bpy.context.scene.display.shading.light = "STUDIO"
bpy.context.scene.display.shading.color_type = "MATERIAL"
bpy.context.scene.render.resolution_x = 800
bpy.context.scene.render.resolution_y = 900
bpy.context.scene.render.resolution_percentage = 100
bpy.context.scene.render.image_settings.file_format = "PNG"
bpy.context.scene.render.filepath = PREVIEW_PNG
bpy.context.scene.render.filepath = PREVIEW_CLOSED_PNG
bpy.ops.render.render(write_still=True)
jaw.value = 1.0
bpy.context.scene.render.filepath = PREVIEW_PNG
bpy.ops.render.render(write_still=True)

print(f"CRIMSON_JAW_DONE affected={affected} thinned={thinned} max_weight={max_weight:.4f} vertices={len(mesh.vertices)}")
