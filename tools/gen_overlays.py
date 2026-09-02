#!/usr/bin/env python3
"""Generate src/recomp_overlays.inl, the port's overlay section table.

librecomp needs one table row per section of the game so it can track which
overlay is resident at each address; N64Recomp's own recomp_overlays.inl only
lists the 32 sections that hold code or MIPS32 relocations. The port's table
is wider and narrower at once:

  * every ALLOC PROGBITS/NOBITS section of the ELF gets a row, data and bss
    included, in ELF section-header order, and `.index` is the section's
    true ELF header index -- that is the RELOC macro contract, the reason the
    ELF handed to N64Recomp (with its .rel.* sections interleaved) is the one
    to read here, not the plain link;
  * a bss row's rom_addr is the sentinel 0xF0000000 | index;
  * a PROGBITS row's rom_addr is what N64Recomp computes (the LOAD segment's
    physical address plus the section's offset inside it, less the lowest
    load address);
  * the function arrays are the recompiler's, minus librecomp's `*_recomp`
    reimplementations and the renamed entrypoint, with the toml's
    manual_funcs first (in reverse toml order) and the rest by offset;
  * relocations are not carried: the recompiled code is linked flat and the
    port resolves overlay addresses itself.

Inputs (defaults are the port root's conventions, see BUILDING.md):
  <elf>       the relocatable-sections ELF given to N64Recomp
  --funcs     N64Recomp's RecompiledFuncs/recomp_overlays.inl
  --toml      pokemonsnap.us.toml, for the manual_funcs order
  --syms      patches/pokemonsnap.syms.toml, only to warn when the ELF's
              code sections disagree with the tracked symbol files
  -o          output, src/recomp_overlays.inl

Usage, from the port root, after N64Recomp has written RecompiledFuncs:
    python3 tools/gen_overlays.py [pokemonsnap.relocs.elf] [-o OUT]
"""
import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

SHT_PROGBITS = 1
SHT_NOBITS = 8
SHF_ALLOC = 0x2
PT_LOAD = 1
BSS_SENTINEL = 0xF0000000


def read_elf(path):
    """Sections as (index, name, type, flags, addr, offset, size) and LOAD segments."""
    data = path.read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 2:
        raise SystemExit(f'{path}: not a 32-bit big-endian ELF')
    e_phoff, e_shoff = struct.unpack_from('>II', data, 0x1C)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('>HHHHH', data, 0x2A)

    headers = []
    for i in range(e_shnum):
        sh = struct.unpack_from('>IIIIIIIIII', data, e_shoff + i * e_shentsize)
        headers.append(sh)
    strtab_off = headers[e_shstrndx][4]

    def name_at(off):
        end = data.index(b'\0', strtab_off + off)
        return data[strtab_off + off:end].decode('ascii')

    sections = []
    for i, sh in enumerate(headers):
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size = sh[:6]
        sections.append((i, name_at(sh_name), sh_type, sh_flags, sh_addr, sh_offset, sh_size))

    segments = []
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from('>IIIIII', data, e_phoff + i * e_phentsize)
        if p_type == PT_LOAD:
            segments.append((p_offset, p_paddr, p_filesz))
    return sections, segments


def rom_address(offset, segments):
    """N64Recomp's rule: segment physical address + offset within the segment."""
    for p_offset, p_paddr, p_filesz in segments:
        if p_offset <= offset < p_offset + p_filesz:
            return p_paddr + (offset - p_offset)
    # A zero-length section can sit exactly at a segment's end.
    for p_offset, p_paddr, p_filesz in segments:
        if p_offset <= offset <= p_offset + p_filesz:
            return p_paddr + (offset - p_offset)
    return None


def recompiler_funcs(path):
    """N64Recomp's function arrays, keyed by ELF section index."""
    text = path.read_text(encoding='utf-8', errors='replace')
    arrays = {}
    for m in re.finditer(r'static FuncEntry (section_(\d+)_\w+_funcs)\[\] = \{(.*?)\n\};', text, re.S):
        name, index, body = m.group(1), int(m.group(2)), m.group(3)
        rows = re.findall(r'\{ \.func = (\w+), \.offset = (0x[0-9A-Fa-f]+), \.rom_size = (0x[0-9A-Fa-f]+) \}', body)
        arrays[index] = (name, [(f, int(o, 16), int(s, 16)) for f, o, s in rows])
    if not arrays:
        raise SystemExit(f'{path}: no FuncEntry arrays found')
    return arrays


def manual_func_names(path):
    text = path.read_text(encoding='utf-8')
    block = re.search(r'manual_funcs\s*=\s*\[(.*?)\n\]', text, re.S)
    if not block:
        return []
    return re.findall(r'name\s*=\s*"(\w+)"', block.group(1))


def syms_sections(path):
    """Code sections of the tracked symbol file, name -> (vram, size)."""
    if not path.is_file():
        return {}
    text = path.read_text(encoding='utf-8')
    out = {}
    for m in re.finditer(r'\[\[section\]\]\nname = "([^"]+)"\nrom = 0x[0-9A-Fa-f]+\nvram = (0x[0-9A-Fa-f]+)\nsize = (0x[0-9A-Fa-f]+)', text):
        out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return out


def keep(func_name):
    return not func_name.endswith('_recomp') and func_name != 'recomp_entrypoint'


def generate(elf_path, funcs_path, toml_path, syms_path):
    sections, segments = read_elf(elf_path)
    arrays = recompiler_funcs(funcs_path)
    manual = manual_func_names(toml_path)
    manual_rank = {name: i for i, name in enumerate(reversed(manual))}
    syms = syms_sections(syms_path)

    rows = []
    for index, name, sh_type, flags, addr, offset, size in sections:
        if sh_type not in (SHT_PROGBITS, SHT_NOBITS) or not (flags & SHF_ALLOC):
            continue
        if sh_type == SHT_NOBITS:
            rom = BSS_SENTINEL | index
        else:
            rom = rom_address(offset, segments)
            if rom is None:
                raise SystemExit(f'section [{index}] {name}: file offset {offset:#x} is in no LOAD segment')
        rows.append([index, name, rom, addr, size])

    # N64Recomp reports ROM addresses relative to the lowest loaded one.
    base = min(r[2] for r in rows if not (r[2] & BSS_SENTINEL == BSS_SENTINEL))
    for r in rows:
        if r[2] & BSS_SENTINEL != BSS_SENTINEL:
            r[2] -= base

    warnings = []
    for index, name, rom, addr, size in rows:
        if name in syms and syms[name] != (addr, size):
            svram, ssize = syms[name]
            warnings.append(f'{name}: ELF vram {addr:#X} size {size:#X}, symbol file vram {svram:#X} size {ssize:#X}')

    out = []
    out.append('// Generated by tools/gen_overlays.py -- do not edit.')
    out.append('// Indices are true ELF section header indices (RELOC macro contract).')
    out.append('#include "librecomp/sections.h"')
    out.append('extern "C" {')
    out.append('#include "funcs.h"')
    out.append('}')
    out.append('')

    array_of = {}
    for index, name, rom, addr, size in rows:
        if index not in arrays:
            continue
        array_name, funcs = arrays[index]
        kept = [f for f in funcs if keep(f[0])]
        first = sorted((f for f in kept if f[0] in manual_rank), key=lambda f: manual_rank[f[0]])
        rest = sorted((f for f in kept if f[0] not in manual_rank), key=lambda f: f[1])
        array_of[index] = array_name
        out.append(f'static FuncEntry {array_name}[] = {{')
        for func_name, off, rom_size in first + rest:
            out.append(f'    {{ .func = {func_name}, .offset = 0x{off:X}, .rom_size = 0x{rom_size:X} }},')
        out.append('};')
        out.append('')

    out.append('static SectionTableEntry code_sections[] = {')
    for index, name, rom, addr, size in rows:
        out.append(f'    // [{index}] {name}')
        out.append(f'    {{ .rom_addr = 0x{rom:08X}, .ram_addr = 0x{addr:08X}, .size = 0x{size:X},')
        if index in array_of:
            out.append(f'      .funcs = {array_of[index]}, .num_funcs = ARRLEN({array_of[index]}),')
        else:
            out.append('      .funcs = nullptr, .num_funcs = 0,')
        out.append(f'      .relocs = nullptr, .num_relocs = 0, .index = {index} }},')
    out.append('};')
    out.append('')
    out.append('static constexpr size_t NUM_CODE_SECTIONS = ARRLEN(code_sections);')
    out.append(f'static constexpr size_t TOTAL_NUM_SECTIONS = {rows[-1][0] + 1};')
    return '\n'.join(out) + '\n', len(rows), len(array_of), warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('elf', nargs='?', default=ROOT / 'pokemonsnap.relocs.elf', type=pathlib.Path)
    parser.add_argument('--funcs', default=ROOT / 'RecompiledFuncs' / 'recomp_overlays.inl', type=pathlib.Path)
    parser.add_argument('--toml', default=ROOT / 'pokemonsnap.us.toml', type=pathlib.Path)
    parser.add_argument('--syms', default=ROOT / 'patches' / 'pokemonsnap.syms.toml', type=pathlib.Path)
    parser.add_argument('-o', '--output', default=ROOT / 'src' / 'recomp_overlays.inl', type=pathlib.Path)
    args = parser.parse_args()

    text, num_rows, num_code, warnings = generate(args.elf, args.funcs, args.toml, args.syms)
    for w in warnings:
        print(f'warning: {w}', file=sys.stderr)
    if warnings:
        print('warning: the ELF was linked from a different tree than the tracked symbol files;\n'
              '         an overlay table from it will not match the ROM the port runs. See BUILDING.md.',
              file=sys.stderr)
    args.output.write_text(text, encoding='utf-8', newline='\n')
    print(f'{args.output}: {num_rows} sections, {num_code} with code')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
