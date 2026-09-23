The hand geometry, skeleton, and animation data in hands.json were converted
from the local DramaticShapeVoxelMod HandSkinData.lua asset. Its source credits
the CC0 Godot XR Tools hands by DigitalN8m4r3, with Ash-style fingerless glove
colors and cuff geometry from DramaticShape. Preserve this attribution when
redistributing these assets.

Regenerate with tools/vr_assets.py, passing HandSkinData.lua as its source.
The native renderer uses meters and the source wrist-origin convention.

Camera, ZERO-ONE-inspired vehicle, apple and Pester Ball meshes are authored in
tools/build_vr_models.py, with an editable Blender source in snap_vr_props.blend.
props.json contains validated meter-scale triangles, normals, UVs, and colors.
Vehicle reference: the original Pokemon Snap ZERO-ONE official artwork;
Pokemon and ZERO-ONE designs belong to their respective owners.
Apple and Pester Ball meshes are used for the held and dispenser previews in
src/vr/vr_props.cpp. Individual portable exports are apple.glb and pester_ball.glb.
Pester Ball visual reference: original Pokemon Snap artwork, archived at
https://bulbapedia.bulbagarden.net/wiki/File:Pester_Ball.png
The meshes are newly authored; the reference image is not included in the assets.
The released projectiles use the player's ROM assets and original game logic.
