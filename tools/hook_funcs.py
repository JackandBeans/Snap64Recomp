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
    # Frame-interpolation object identity: renPrepareModelMatrix names the
    # object whose matrices are being built, hal_mtx_f2l names the address
    # each matrix lands at.
    'renPrepareModelMatrix',
    'hal_mtx_f2l',
    'hal_mtx_f2l_fixed_w',
]


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'RecompiledFuncs')
    if not root.is_dir():
        print(f'not a directory: {root}')
        return 1

    header = root / 'funcs.h'
    header_text = header.read_text(encoding='utf-8')
    renamed = 0

    for path in sorted(root.glob('funcs_*.c')):
        text = path.read_text(encoding='utf-8')
        original = text
        for name in HOOKED:
            text = re.sub(r'^RECOMP_FUNC void %s\(' % re.escape(name),
                          'RECOMP_FUNC void __real_%s(' % name,
                          text, flags=re.MULTILINE)
        if text != original:
            path.write_text(text, encoding='utf-8', newline='')
            renamed += 1

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
