"""Import the five supplied reference images unchanged as presentation textures."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
assets = unreal.AssetToolsHelpers.get_asset_tools()
for name in ['RockycatLogo', 'RBAChampionBelt', 'ArenaPoster', 'RockyPortrait', 'ShadowPortrait']:
    task = unreal.AssetImportTask()
    task.filename = str(root / 'ArtSource' / 'Presentation' / (name + '.jpeg'))
    task.destination_path = '/Game/RockyKombat/Presentation'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    texture = unreal.load_asset(task.destination_path + '/' + name)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError('Failed to import ' + name)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('never_stream', True)
    texture.set_editor_property('max_texture_size', 2048)
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    unreal.log('RKC_PRESENTATION_IMPORTED ' + name)
