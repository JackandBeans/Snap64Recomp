# tools/

Scripts used to build, check and maintain the port. Each one's own
docstring says how to run it; `BUILDING.md` says where in the build it
belongs. Python 3.11 with Pillow, NumPy and SciPy covers all of them.

## Build pipeline

| Script | What it does |
| --- | --- |
| `build_quest.ps1`, `build_quest.py` | Builds and signs the standalone ARM64 Quest Release APK; `-Install -Launch` installs it through ADB and copies the user's ROM separately. See [QUEST.md](../docs/QUEST.md). |
| `fetch_deps.py` | Fetches the vendored trees a clean checkout does not carry (SDL, DirectX-Headers, RT64's third-party trees) at the recorded upstream commits, and verifies them. |
| `macos_bundle.py` | Lays the macOS application bundle out from a build directory's install tree, writes its `Info.plist`, signs it ad hoc and zips it with a START HERE text (BUILDING.md, step 15); `--dry-run` checks the layout on any machine. |
| `gen_reference_syms.py` | Writes `patches/game_syms.ld` and `patches/pokemonsnap.syms.toml`, the symbol names, addresses and sizes the patch build and the recompiler need, and, given a fourth path, the data symbols a mod build names variables with (`docs/MODS.md`). |
| `gen_overlays.py` | Generates `src/recomp_overlays.inl`, the port's overlay section table, from the recompiler's ELF input. |
| `hook_funcs.py` | Renames generated functions so the port can intercept them (MSVC has no linker `--wrap`). |
| `embed_bin.cmake`, `filter_map.cmake` | CMake helpers: embed the patches' data section as a C array; ship a linker map with build paths replaced. |

## Assets

| Script | What it does |
| --- | --- |
| `harvest_menu_font.py`, `extract_menu_sprite.py`, `extract_menu_dot.py`, `probe_font_order.py` | The Options page's sprite font and furniture: harvested from the game's own sprites at run time, these scripts were used to find and check the layout (`src/menu_harvest.cpp` does the run-time work). |
| `osd_font_gen.py` | Rasterises the capitals the Snap Station printer's on-screen lettering uses into `src/snap_station_osd_font.h` (Roboto, Apache 2.0; `licenses/roboto.txt`). |
| `icon_gen.py` | Composes the executable's icon, `src/snap64.ico`, and the macOS bundle's, `src/snap64.icns`, from the logo, `docs/logo.png`: the whole logo on a square tile of the logo's own outline blue at 32 px and above (with the Retina doubles up to 512 in the `.icns`); the film canister alone, cut out of the logo, at 24 and 16 (Pillow, NumPy). |
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
| `replays/beach.inputs`, `replays/eval.inputs`, `replays/station.inputs`, `replays/mods.inputs`, `replays/course.inputs`, `replays/course_exit.inputs`, `replays/menu_title.inputs`, `replays/menu_course.inputs`, `replays/mods_more.inputs` | The controller recordings the suite drives the executable with (`SNAP_REPLAY`; `BUILDING.md`, "Replays and the headless suite"): a Beach ride; a ride, the Camera Check and Oak's evaluation; and the route to a Snap Station print, synthesised from the other two. Twelve bytes per reading, no game data. `mods.inputs` is the Mods page's scripted visit: from the title to Options > Mods, down one row, A twice (off, then on), B; with two mods in `mods/` and `SNAP_PCAP_AT=1150,1330,1450 SNAP_PCAP_BURST=1` it photographs the page opened, after the first press and after the second. `course.inputs` is the Beach replay's own first 1,750 readings (into the ride, past the tutorial's first prompts) and then a visit to the pages from the pause menu: Start, three downs to Options, A, the Graphics page, back, three downs, the Mods page, back, back, B to resume; with a mod in `mods/` and `SNAP_PCAP_AT=1850,2040,2140,2360,2460,2580 SNAP_PCAP_BURST=1` it photographs the pause menu, the list, both pages, the pause menu again and the ride resumed (the pill, the list's dress and Exit Game's fifth row are in the same frames). `course_exit.inputs` is the same ride and pause, then the pill selected, the list, four downs to Exit Game, A (the question in the help box), B (withdrawn), up to Mods, A, and Start on the Mods page (everything closes, the ride resumes); with `SNAP_PCAP_AT=1850,1960,2040,2190,2260,2320,2440,2560` it photographs each of those. The pages from anywhere are opened by the host's key, which a tape cannot carry, so `SNAP_MENU_AT=<readings>` presses it: `menu_title.inputs` is the title replay's first 200 readings and then stick moves, A and B in the list (`SNAP_MENU_AT=300,700` opens the pages over the title's intro and closes them; captures at 350, 500, 600, 680, 760); `menu_course.inputs` is the course replay's first 1,750 readings and then two downs and A (`SNAP_MENU_AT=1800,2100` opens the pages straight over the paused ride and closes them, the ride resuming; captures at 1850, 1945, 2000, 2160, 2250). `eval.inputs` carries the key to the screens after a ride: `SNAP_MENU_AT=8650,8900` opens the pages over the evaluation and closes them (captures at 8700, 8800, 8950), `SNAP_MENU_AT=8900,9050` presses the key in the lab's first second, where the press waits for the screen to settle (captures at 8930, 9000, 9100), and `SNAP_MENU_AT=6000` on the photo check is refused with a `[SNAP-MENU]` line, that screen's display list having no room for the pages. `mods_more.inputs` is the title replay into the Mods page with the example mod in `mods/` twice under two ids, then Z (the first mod's options page), Right (its option changed), B, R (the mod moved down the order), three downs and B, B; captures at 1150, 1270, 1370, 1440, 1520, 1650. |
