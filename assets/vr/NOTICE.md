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
Released projectiles and photo reconstructions use these same authored meshes
through projectiles.bin, exported by tools/vr_projectile_assets.py. Movement,
collisions and item effects remain the original game logic. No ROM geometry
or textures are included in projectiles.bin.

The native throw estimator in src/vr/vr_throw.h is adapted from the VR
Pokeball release logic in DramaticShapeVoxelMod/lib/CatchThrow.lua (local
DramaticShape vr-perf checkout). The initial peak-window/3x implementation
was retuned after controller feedback: an 80 ms velocity regression and a
smooth launch gain up to 2.3x reduce strength and release inconsistency.

Item hand poses and palm placement follow DramaticShapeVoxelMod/lib/HandProp.lua
and data/ball_grip.lua: static grip_4, per-hand wrist offsets, palm seats and
hold quaternions. Both hand rendering and native release sampling use this seat.
