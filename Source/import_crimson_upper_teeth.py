import unreal

task = unreal.AssetImportTask()
task.filename = r"C:\Users\hueda\Documents\Unreal Projects\LifeSimulation 5.8\Source\Meshy_Crimson_Gaze_Import\Crimson_Gaze_UpperTeeth.fbx"
task.destination_path = "/Game/Meshy_Crimson_Gaze_Teeth"
task.destination_name = "SM_Crimson_Gaze_UpperTeeth"
task.automated = True
task.replace_existing = True
task.save = True
options = unreal.FbxImportUI()
options.import_as_skeletal = False
options.import_mesh = True
options.import_materials = True
options.import_textures = False
options.static_mesh_import_data.combine_meshes = True
options.static_mesh_import_data.generate_lightmap_u_vs = False
task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
