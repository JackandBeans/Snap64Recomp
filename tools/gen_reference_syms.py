"""Emit the reference symbol file N64Recomp needs to recompile patches.

A patch is compiled and recompiled on its own, so the recompiler has to be
told where the game's own functions live in order to resolve calls out of the
patch into them. That is what func_reference_syms_file is: a toml listing the
game's executable sections and the functions inside them.

It also emits a linker symbol file, which the patch link needs for a
different reason: a patch calls into the game, so ld has to resolve those
names to the addresses they live at in the target.

It can also emit the data reference symbols a mod build needs: RecompModTool
takes them beside the function symbols (`data_reference_syms_files` in a
mod's `mod.toml`) so a mod can name the game's variables. Same shape as the
other recompilations' symbol repositories: one `[[section]]` per data
section with `symbols = [{ name, vram }]`.

Usage, from the port root:
    python tools/gen_reference_syms.py <decomp elf> <output toml> [<output ld> [<output data toml>]]
"""
import subprocess
import sys


def load_segments(elf):
    """The LOAD segments as (file offset, physical address, file size)."""
    out = subprocess.run(['readelf', '-lW', elf], capture_output=True, text=True, check=True).stdout
    segments = []
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 6 and fields[0] == 'LOAD':
            segments.append((int(fields[1], 16), int(fields[3], 16), int(fields[4], 16)))
    return segments


def rom_address(offset, segments):
    """N64Recomp's rule, the one the runtime's section table follows too: a
    section's ROM address is its LOAD segment's physical address plus its
    offset inside the segment. (The file offset itself is not a ROM address:
    .main sits at 0x20400 in the ELF and at 0x1000 in the ROM.)"""
    for p_offset, p_paddr, p_filesz in segments:
        if p_offset <= offset < p_offset + p_filesz:
            return p_paddr + (offset - p_offset)
    for p_offset, p_paddr, p_filesz in segments:
        if p_offset <= offset <= p_offset + p_filesz:
            return p_paddr + (offset - p_offset)
    return offset


def sections(elf):
    """Executable sections, as (index, name, vram, ROM address, size)."""
    segments = load_segments(elf)
    out = subprocess.run(['readelf', '-SW', elf], capture_output=True, text=True, check=True).stdout
    found = []
    for line in out.splitlines():
        line = line.strip()
        if not line.startswith('['):
            continue
        # [ 3] .main PROGBITS 80000400 020400 0452c0 00 WAX 0 0 32
        close = line.find(']')
        try:
            index = int(line[1:close].strip())
        except ValueError:
            continue
        fields = line[close + 1:].split()
        if len(fields) < 6 or fields[1] != 'PROGBITS':
            continue
        flags = fields[6] if len(fields) > 6 else ''
        if 'X' not in flags:
            continue
        found.append((index, fields[0], int(fields[2], 16), rom_address(int(fields[3], 16), segments), int(fields[4], 16)))
    return found


def functions(elf):
    """FUNC symbols with a size, grouped by section index."""
    out = subprocess.run(['readelf', '-sW', elf], capture_output=True, text=True, check=True).stdout
    entries = []
    for line in out.splitlines():
        fields = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(fields) < 8 or not fields[0].endswith(':'):
            continue
        if fields[3] != 'FUNC':
            continue
        try:
            vram, size, ndx = int(fields[1], 16), int(fields[2]), int(fields[6])
        except ValueError:
            continue
        name = fields[7]
        if vram & 3:
            continue
        entries.append((ndx, name, vram, size))

    # A weak alias carries no size of its own -- sinf aliasing __sinf, for
    # instance -- and dropping those leaves calls to them unresolvable. Give
    # each the size of the real symbol at the same address.
    size_by_vram = {}
    for _, _, vram, size in entries:
        if size > size_by_vram.get(vram, 0):
            size_by_vram[vram] = size

    by_section = {}
    for ndx, name, vram, size in entries:
        size = size or size_by_vram.get(vram, 0)
        if size == 0:
            continue
        by_section.setdefault(ndx, []).append((name, vram, size))
    return by_section


def linker_symbols(elf, output):
    """Data symbols as absolute addresses ld can resolve against.

    Only data. A call must stay an unresolved symbol so the link leaves a
    relocation on it, which is what lets the recompiler tie the call to the
    reference symbol of the same name; resolving calls here instead bakes in a
    bare address that it cannot attribute to anything.
    """
    out = subprocess.run(['readelf', '-sW', elf], capture_output=True, text=True, check=True).stdout
    seen = {}
    for line in out.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].endswith(':'):
            continue
        if fields[3] != 'OBJECT' or fields[6] == 'UND':
            continue
        name = fields[7]
        if '.' in name or name in seen:
            continue
        seen[name] = int(fields[1], 16)

    lines = ['/* Generated by tools/gen_reference_syms.py -- do not edit. */']
    lines += [f'PROVIDE({name} = 0x{addr:08X});' for name, addr in sorted(seen.items())]
    with open(output, 'w', encoding='utf-8', newline='') as f:
        f.write('\n'.join(lines) + '\n')

    print(f'{output}: {len(seen)} symbols')


def data_sections(elf):
    """Allocated sections, as (index, name, vram, ROM address or None, size, executable).

    The game keeps most of its globals inside the sections that also hold
    code (.main, .app_level and the rest are WAX), so those are listed too,
    for their OBJECT symbols only; the segment markers (NOTYPE) come from
    the data-only sections.
    """
    segments = load_segments(elf)
    out = subprocess.run(['readelf', '-SW', elf], capture_output=True, text=True, check=True).stdout
    found = []
    for line in out.splitlines():
        line = line.strip()
        if not line.startswith('['):
            continue
        close = line.find(']')
        try:
            index = int(line[1:close].strip())
        except ValueError:
            continue
        fields = line[close + 1:].split()
        if len(fields) < 7 or fields[1] not in ('PROGBITS', 'NOBITS'):
            continue
        flags = fields[6]
        if 'A' not in flags:
            continue
        rom = rom_address(int(fields[3], 16), segments) if fields[1] == 'PROGBITS' else None
        found.append((index, fields[0], int(fields[2], 16), rom, int(fields[4], 16), 'X' in flags))
    return found


def data_symbols(elf, output):
    """The data reference symbols for RecompModTool, one section at a time."""
    out = subprocess.run(['readelf', '-sW', elf], capture_output=True, text=True, check=True).stdout
    by_section = {}
    seen = set()
    for line in out.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].endswith(':'):
            continue
        if fields[3] not in ('OBJECT', 'NOTYPE') or fields[4] == 'LOCAL' and fields[3] == 'NOTYPE':
            continue
        try:
            vram, ndx = int(fields[1], 16), int(fields[6])
        except ValueError:
            continue  # ABS and UND carry no section
        name = fields[7]
        if '.' in name or name in seen:
            continue
        seen.add(name)
        by_section.setdefault(ndx, []).append((name, vram, fields[3]))

    lines = ['# Generated by tools/gen_reference_syms.py -- do not edit.', '']
    total = 0
    for index, name, vram, rom, size, executable in data_sections(elf):
        syms = [s for s in by_section.get(index, []) if s[2] == 'OBJECT' or not executable]
        syms = sorted(syms, key=lambda s: s[1])
        if not syms:
            continue
        lines.append('[[section]]')
        lines.append(f'name = "{name}"')
        if rom is not None:
            lines.append(f'rom = 0x{rom:X}')
        lines.append(f'vram = 0x{vram:X}')
        lines.append(f'size = 0x{size:X}')
        lines.append('symbols = [')
        for sym_name, sym_vram, _ in syms:
            lines.append(f'    {{ name = "{sym_name}", vram = 0x{sym_vram:X} }},')
        lines.append(']')
        lines.append('')
        total += len(syms)

    with open(output, 'w', encoding='utf-8', newline='') as f:
        f.write('\n'.join(lines))

    print(f'{output}: {total} data symbols across {len(data_sections(elf))} data sections')


def main():
    if len(sys.argv) not in (3, 4, 5):
        print(__doc__)
        return 1

    elf, output = sys.argv[1], sys.argv[2]
    by_section = functions(elf)

    lines = ['# Generated by tools/gen_reference_syms.py -- do not edit.', '']
    total = 0
    for index, name, vram, rom, size in sections(elf):
        funcs = sorted(by_section.get(index, []), key=lambda f: f[1])
        if not funcs:
            continue

        lines.append('[[section]]')
        lines.append(f'name = "{name}"')
        lines.append(f'rom = 0x{rom:X}')
        lines.append(f'vram = 0x{vram:X}')
        lines.append(f'size = 0x{size:X}')
        lines.append('functions = [')
        for func_name, func_vram, func_size in funcs:
            lines.append(f'    {{ name = "{func_name}", vram = 0x{func_vram:X}, size = 0x{func_size:X} }},')
        lines.append(']')
        lines.append('')
        total += len(funcs)

    with open(output, 'w', encoding='utf-8', newline='') as f:
        f.write('\n'.join(lines))

    print(f'{output}: {total} functions across {len(sections(elf))} executable sections')

    if len(sys.argv) >= 4:
        linker_symbols(elf, sys.argv[3])
    if len(sys.argv) == 5:
        data_symbols(elf, sys.argv[4])

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
