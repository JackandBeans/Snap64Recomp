"""Rename recompiled functions the port needs to intercept.

MSVC has no linker --wrap, so interception works by renaming the generated
definition to __real_<name> and letting the port define <name> itself (see
src/overlay_hook.cpp, src/matrix_tags.cpp). Re-run this after regenerating
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
    # The photo-score screen rebuilding the photographed Pokemon as fresh
    # objects to re-render for pixel counting. The spawn fade must know it
    # is happening: fading a score-render object would collapse its depth
    # coverage and zero the photo's score.
    # The camera focus indicator: the game draws it into RDRAM, which HLE
    # presentation never shows, so the port observes it and redraws it.
    'PokemonDetector_PostProcessImage',
    # Names the object each sprite belongs to, in the display list, so the
    # renderer can find the same sprite in the previous frame. Texture
    # rectangles carry no matrix and no vertices, so this is the only thing
    # that can tell one from another. spX2Draw is the sprite; renDrawSprite
    # is the object that owns them, and closes the group afterwards.
    'spX2Draw',
    'renDrawSprite',
    # Measured only, to attribute the rectangles nothing has named yet to the
    # code that emits them. Each of these builds its commands through
    # gMainGfxPos, so the span between the write pointer before and after a
    # call is exactly what that call produced.
    'fx_draw',
    'Msg_DrawMessage',
    'func_8009E3D0',
    # The menu overlay's private copy of the sprite library, and the camera's
    # own background fills.
    'func_80373670_846E20',
    # Every volume that reaches a sound-player voice after it has started:
    # the patched auSetSoundVolume (positional sounds every tick, ambience
    # ramps) and the level-end global fades. Logged under SNAP_STATS so the
    # Effects slider can be proven at the voice, not at the slot.
    'alSndpSetVol',
    # The photo scorer's render-and-count routine: read its buffers back after
    # it returns, so the copy-mode low bit is checked in the game's own data.
    'func_800A007C',
    # The two ends of a course intro's hand-off glide, for measuring the
    # Cutscene Fix toggle in drawn frames (src/intro_probe.cpp).
    'PlayerModel_SetAnimation',
    'func_beach_802C5214',
    'func_802E2194_6C9F74',
    'func_803719B0_845160',
    'renInitCamera',
    'renInitCameraEx',
    # The viewfinder's scorer: one framebuffer tile copy per Pokemon drawn, up
    # to twenty a frame, and only while a course is running.
    'PokemonDetector_SaveRegion',
    # Every particle handed out gets a number, so a recycled address does not
    # name the particle that used to live there.
    'fx_createParticle',
    # Sprite slots get a generation number; om.c's free list reissues the
    # most recently freed address, so an address alone names two different
    # sprites within a frame of each other.
    'omGObjAddSprite',
]

# Calls inserted INSIDE a recompiled function, at a named guest address.
#
# Renaming a function only lets the port wrap it, which is useless when what
# needs intercepting happens in the middle of one. fx_draw builds a rectangle
# per particle inside three nested loops and calls nothing between the loop head
# and the end, so there is no seam a wrapper can reach -- and tagging the whole
# pass with one name would be worse than nothing, because every particle in the
# game would share an id whose rectangle count changes every frame, and the
# renderer refuses a pair whenever a count moves.
#
# The generated C carries a label for every guest address that anything branches
# to, so an address IS an insertion point. Each entry is
# (function, guest address, callback): the call is placed immediately after the
# label, where the registers hold exactly what they held at that instruction.
#
# Anchored to an address rather than to surrounding text, so a regeneration that
# moves the code still finds it -- and if the address stops being a label at all,
# this fails loudly instead of silently doing nothing.
INNER_HOOKS = [
    # 0x800A5158 is the one point every drawn particle passes through exactly
    # once, BEFORE any display-list cursor fetch that could bypass a tag.
    #
    # The previous insertion point, 0x800A5970 (the cursor fetch ahead of the
    # SetPrimColor that precedes each particle's rectangle), was NOT on every
    # path: the branch at 0x800A5158 --
    #   beql $t7, $s2, L_800A5974   ; if this particle's texture == the one
    #       lw $v0, 0x0($t1)        ; already loaded, skip the texture load
    # -- jumps straight past it, fetching the cursor in its own delay slot.
    # Any particle drawn with the same sprite frame as the particle drawn just
    # before it therefore emitted its rectangle with NO tag. Tumbling leaves
    # share a handful of animation frames, so during a gust half of them went
    # unnamed on any given frame, each unnamed frame breaking that leaf's
    # pairing for two ticks (the unnamed one, and the return with no history).
    # On screen: some leaves glide while others stand frozen for a tick and
    # jump -- the ghosting this port chased through eight identity fixes.
    #
    # 0x800A5158 is where the three TLUT paths converge and the texture-load
    # decision is made: a dominator of the rectangle emission, reached once per
    # drawn particle on every path. Both cursor fetches downstream (the delay
    # slot above and 0x800A5970 on the load path) read the pointer from memory
    # AFTER the tag advanced it, and the DP commands between the tag and the
    # rectangle leave the pending name untouched -- only a rectangle consumes
    # it. $s7 still holds the particle here; the pass index still sits at
    # sp+0x1F8.
    ('fx_draw', 0x800A5158, 'snap_fx_particle'),
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

    # Inner hooks: a call placed at a guest address inside a function body.
    #
    # Every existing call to the callback is stripped first, wherever it is.
    # This is what makes the pass idempotent AND lets an insertion point MOVE:
    # the earlier version only checked the target label before inserting, and
    # its check missed the call's own indentation -- so every re-run stacked
    # another call at the same label, which is how fx_draw came to tag each
    # particle twice.
    inner = 0
    for func_name, address, callback in INNER_HOOKS:
        label = 'L_%08X:' % address
        call = '    %s(rdram, ctx);' % callback
        call_line_re = re.compile(r'^[ \t]*%s\(rdram, ctx\);\r?\n' % re.escape(callback),
                                  flags=re.MULTILINE)
        placed = False

        for path in sorted(root.glob('funcs_*.c')):
            text = path.read_text(encoding='utf-8')
            stripped = call_line_re.sub('', text)
            if label not in stripped:
                if stripped != text:
                    path.write_text(stripped, encoding='utf-8', newline='')
                continue

            assert stripped.count(label) == 1, (
                f'{label} appears {stripped.count(label)} times in {path.name}; '
                f'an inner hook needs exactly one insertion point')

            head, _, tail = stripped.partition(label)
            text = head + label + '\n' + call + tail
            path.write_text(text, encoding='utf-8', newline='')
            inner += 1
            placed = True
            break

        assert placed, (
            f'inner hook for {func_name} at {address:#010x}: no label {label} found in '
            f'{root}. The address is no longer a branch target, so the insertion point '
            f'does not exist and the hook would silently do nothing.')

        decl = f'void {callback}(uint8_t* rdram, recomp_context* ctx);'
        if decl not in header_text:
            header_text = decl + '\n' + header_text

    header.write_text(header_text, encoding='utf-8', newline='')
    print(f'hooked {len(HOOKED)} functions across {renamed} file(s), '
          f'{len(INNER_HOOKS)} inner hook(s), {inner} inserted')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
