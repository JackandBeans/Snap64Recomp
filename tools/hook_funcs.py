"""Rename recompiled functions the port needs to intercept.

MSVC has no linker --wrap, so interception works by renaming the generated
definition to __real_<name> and letting the port define <name> itself (see
src/overlay_hook.cpp, src/matrix_ids.cpp). Re-run this after regenerating
RecompiledFuncs with N64Recomp; it is idempotent.

Usage: python tools/hook_funcs.py [RecompiledFuncs directory]
"""
import pathlib
import re
import sys

HOOKED = [
    # Overlay residency tracking and the RSP memory probes.
    'dmaLoadOverlay',
    'check_sp_imem',
    'check_sp_dmem',
    # Reads the animation system's own verdict about which poses are steps
    # rather than motion, so the renderer can snap those instead of drawing the
    # positions between. Read-only: the wrapper looks at the tree before and
    # after the real call and never writes to game memory.
    'animUpdateModelTreeAnimation',
    # Turns RT64's extended commands on through the display list write pointer
    # it receives. Object identity is a game patch now, not a hook.
    'renPrepareCameraMatrix',
    # Carries the distance the world origin moved by, so the previous frame can
    # be expressed in the new one rather than skipped.
    'bindCameraNextBlock',
    # Tells the renderer the world origin moved, which no frame can be
    # interpolated across.
    'enterNextBlock',
    # Stamps a serial into each matrix as it is handed out, so a recycled
    # address does not read as the same object it was last time.
    'omGetMtx',
    # Spawn-timing probes: log every Pokemon the world creates, with its
    # block and position, so a ride's log says whether one that visibly
    # "appeared" was created at that moment or had existed for a block.
    'pokemonAdd',
    'pokemonAddOne',
    # Reports how full the game's display list buffers get. The port emits a
    # matrix group ahead of every matrix, which the original never did, and
    # these buffers are small and fixed.
    'gtlCheckBuffers',
    # The two halves of the game's own frame, as its main loop names them:
    # every object's update and every object's process, then the pass that
    # builds the display list. Timed to say which half a slow frame is in,
    # and to read the game thread's own parked time, which can only be taken
    # on the thread that parked.
    'gtlUpdate',
    'gtlDraw',
    # One Pokemon coming into existence. A course block boundary runs this
    # once per Pokemon in the block being created, and that frame is the
    # worst of a ride.
    'Pokemon_SpawnOnGround',
    # The camera focus indicator: the game draws it into RDRAM, which HLE
    # presentation never shows, so the port observes it and redraws it.
    'PokemonDetector_PostProcessImage',
]


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'RecompiledFuncs')
    if not root.is_dir():
        print(f'not a directory: {root}')
        return 1

    header = root / 'funcs.h'
    header_text = header.read_text(encoding='utf-8')
    renamed = 0

    hooked = set(HOOKED)
    for path in sorted(root.glob('funcs_*.c')):
        text = path.read_text(encoding='utf-8')
        original = text
        for name in HOOKED:
            text = re.sub(r'^RECOMP_FUNC void %s\(' % re.escape(name),
                          'RECOMP_FUNC void __real_%s(' % name,
                          text, flags=re.MULTILINE)

        # Reversible: a function dropped from HOOKED gets its own name back,
        # otherwise nothing defines it and the link fails on every caller.
        def unhook(match):
            name = match.group(1)
            return match.group(0) if name in hooked else 'RECOMP_FUNC void %s(' % name

        text = re.sub(r'^RECOMP_FUNC void __real_(\w+)\(', unhook, text, flags=re.MULTILINE)
        if text != original:
            path.write_text(text, encoding='utf-8', newline='')
            renamed += 1

    # Drop __real_ declarations for functions no longer hooked.
    header_text = re.sub(r'^void __real_(\w+)\(uint8_t\* rdram, recomp_context\* ctx\);\n',
                         lambda m: m.group(0) if m.group(1) in hooked else '',
                         header_text, flags=re.MULTILINE)

    # Every hooked name needs both declarations: the port defines the plain
    # one, the recompiled tree defines (and callers reach) the __real_ one.
    for name in HOOKED:
        real_decl = f'void __real_{name}(uint8_t* rdram, recomp_context* ctx);'
        if real_decl not in header_text:
            plain_decl = f'void {name}(uint8_t* rdram, recomp_context* ctx);'
            assert plain_decl in header_text, f'{name} missing from funcs.h'
            header_text = header_text.replace(plain_decl, plain_decl + '\n' + real_decl, 1)

    header.write_text(header_text, encoding='utf-8', newline='')
    print(f'hooked {len(HOOKED)} functions across {renamed} file(s)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
