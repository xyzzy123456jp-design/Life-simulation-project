import bpy

OUTPUT = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_UpperTeeth.fbx"
bpy.ops.wm.read_factory_settings(use_empty=True)
white = bpy.data.materials.new("M_Crimson_UpperTeeth_White")
white.diffuse_color = (0.82, 0.80, 0.72, 1.0)
white.metallic = 0.0
white.roughness = 0.48
objects = []
xs = (-0.069, -0.050, -0.029, -0.010, 0.010, 0.029, 0.050, 0.069)
widths = (0.030, 0.034, 0.038, 0.038, 0.038, 0.038, 0.034, 0.030)
for i, (x, width) in enumerate(zip(xs, widths), 1):
    bpy.ops.mesh.primitive_cube_add(location=(x, -0.353, 0.082))
    tooth = bpy.context.object
    tooth.name = f"Crimson_UpperTooth_{i:02d}"
    tooth.scale = (width * 0.5, 0.007, 0.010)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel = tooth.modifiers.new("Soft edges", "BEVEL")
    bevel.width = 0.003
    bevel.segments = 3
    tooth.data.materials.append(white)
    objects.append(tooth)
bpy.ops.object.select_all(action="DESELECT")
for obj in objects: obj.select_set(True)
bpy.context.view_layer.objects.active = objects[0]
bpy.ops.object.convert(target="MESH")
bpy.ops.object.join()
bpy.context.object.name = "SM_Crimson_Gaze_UpperTeeth"
bpy.ops.export_scene.fbx(filepath=OUTPUT, use_selection=True, apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_ALL", object_types={'MESH'}, add_leaf_bones=False,
    bake_anim=False, path_mode="AUTO")
print(f"EXPORTED_CRIMSON_TEETH {OUTPUT}")
