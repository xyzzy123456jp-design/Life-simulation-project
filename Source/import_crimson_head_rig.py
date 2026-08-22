import unreal


SOURCE_FBX = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_HeadRig_Test.fbx"
DESTINATION = "/Game/Meshy_Crimson_Gaze_HeadRig"
ASSET_NAME = "SK_Crimson_Gaze_HeadRig"

task = unreal.AssetImportTask()
task.filename = SOURCE_FBX
task.destination_path = DESTINATION
task.destination_name = ASSET_NAME
task.automated = True
task.replace_existing = True
task.save = True

options = unreal.FbxImportUI()
options.import_as_skeletal = True
options.import_mesh = True
options.import_animations = False
options.import_materials = False
options.import_textures = False
options.skeletal_mesh_import_data.set_editor_property("import_morph_targets", True)
task.options = options

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = unreal.load_asset(f"{DESTINATION}/{ASSET_NAME}.{ASSET_NAME}")
material = unreal.load_asset("/Game/Meshy_Crimson_Gaze/Material_001.Material_001")
if not mesh:
    raise RuntimeError("Crimson Head Rig skeletal mesh import failed")

materials = list(mesh.get_editor_property("materials"))
if materials and material:
    materials[0].set_editor_property("material_interface", material)
    mesh.set_editor_property("materials", materials)

unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
unreal.log_warning(f"CRIMSON_HEAD_RIG_IMPORT_DONE mesh={mesh.get_path_name()} slots={len(materials)}")
