# tools/

Scripts used to build, check and maintain the port. Each one's own
docstring says how to run it; `BUILDING.md` says where in the build it
belongs. Python 3.11 with Pillow, NumPy and SciPy covers all of them.

## Build pipeline

| Script | What it does |
| --- | --- |
| `fetch_deps.py` | Fetches the vendored trees a clean checkout does not carry (SDL, DirectX-Headers, RT64's third-party trees) at the recorded upstream commits, and verifies them. |
| `gen_reference_syms.py` | Writes `patches/game_syms.ld` and `patches/pokemonsnap.syms.toml`, the symbol names, addresses and sizes the patch build and the recompiler need. |
| `gen_overlays.py` | Generates `src/recomp_overlays.inl`, the port's overlay section table, from the recompiler's ELF input. |
| `hook_funcs.py` | Renames generated functions so the port can intercept them (MSVC has no linker `--wrap`). |
| `embed_bin.cmake`, `filter_map.cmake` | CMake helpers: embed the patches' data section as a C array; ship a linker map with build paths replaced. |

## Assets

| Script | What it does |
| --- | --- |
| `harvest_menu_font.py`, `extract_menu_sprite.py`, `extract_menu_dot.py`, `probe_font_order.py` | The Options page's sprite font and furniture: harvested from the game's own sprites at run time, these scripts were used to find and check the layout (`src/menu_harvest.cpp` does the run-time work). |
| `osd_font_gen.py` | Rasterises the capitals the Snap Station printer's on-screen lettering uses into `src/snap_station_osd_font.h` (Roboto, Apache 2.0; `licenses/roboto.txt`). |
| `icon_gen.py` | Composes the executable's icon, `src/snap64.ico`, from the logo, `docs/logo.png`: the whole logo at 32 px and above, its film canister at 24 and 16. |
| `jynx_vc_preview.py` | Offline check and preview of the opt-in Jynx recolour against a Virtual Console capture. |

## Checking and debugging

| Script | What it does |
| --- | --- |
| `release_check.py` | The headless verification suite a release build goes through: windowed subsystem and icon, logging, the attract replay, pacing statistics, Oak's evaluation, the Options page, the settings file, the package, and the Snap Station print. |
| `dump_threads.py` | From a minidump: every thread's instruction pointer and stack return addresses, resolved through the linker map. |
| `unlock_save.py` | Writes an "everything unlocked" copy of a save file (all courses and items), for testing. |

## Replays

| File | What it is |
| --- | --- |
| `replays/beach.inputs`, `replays/eval.inputs`, `replays/station.inputs` | The controller recordings the suite drives the executable with (`SNAP_REPLAY`; `BUILDING.md`, "Replays and the headless suite"): a Beach ride; a ride, the Camera Check and Oak's evaluation; and the route to a Snap Station print, synthesised from the other two. Twelve bytes per reading, no game data. |
