#!/usr/bin/env python3
"""The ACC object zap should have written, made from the ELF object it wrote.

    elf_to_acc.py <accobj.py's directory> <in.o> <out.o>

zap's ELF output is checked by binutils -- linked at two addresses and
compared with a flat assembly -- so it is a trustworthy account of what an
object holds. This reads that account and writes it again through acc's own
independent writer, test/accobj.py, which knows the ACC format from acc's
specification and not from zap. zap's ACC output for the same source has to be
these bytes exactly.

The mapping is the one docs/LIBRARIES.md describes: .text, .rodata and .data
go into acc's one text in that order, each started on its own alignment; .bss
is the bss; a relocation against a section symbol is against the text or the
bss with the section's place added to the addend; HIGH8, UPPER8 and a PCREL8
past a signed byte carry their addend in the entry, and every other kind
keeps it in its slot.
"""

import struct
import sys

sys.path.insert(0, sys.argv[1])
from accobj import Obj  # noqa: E402

KIND = {5: 'ABS24', 11: 'ABS16', 7: 'LOW8', 8: 'HIGH8', 9: 'UPPER8', 3: 'PCREL8'}
WIDTH = {'ABS24': 3, 'ABS16': 2, 'LOW8': 1, 'PCREL8': 1}
SHN_ABS = 0xFFF1


def log2(n):
    return max(n, 1).bit_length() - 1


def main():
    data = open(sys.argv[2], 'rb').read()
    shoff, = struct.unpack_from('<I', data, 32)
    shnum, shstrndx = struct.unpack_from('<HH', data, 48)
    sections = [struct.unpack_from('<10I', data, shoff + i * 40) for i in range(shnum)]
    names = sections[shstrndx]

    def cstr(off):
        return data[off:data.index(b'\0', off)].decode()

    by_name = {}
    for i, sh in enumerate(sections):
        by_name[cstr(names[4] + sh[0])] = (i, sh)

    def contents(name):
        _, sh = by_name[name]
        return bytearray(data[sh[4]:sh[4] + sh[5]])

    def align(name):
        return max(by_name[name][1][8], 1)

    base = {'.text': 0}
    base['.rodata'] = -(-len(contents('.text')) // align('.rodata')) * align('.rodata')
    end = base['.rodata'] + len(contents('.rodata'))
    base['.data'] = -(-end // align('.data')) * align('.data')

    text = bytearray()
    for name in ('.text', '.rodata', '.data'):
        text += bytes(base[name] - len(text))
        text += contents(name)

    index_name = {i: cstr(names[4] + sh[0]) for i, sh in enumerate(sections)}
    _, symtab = by_name['.symtab']
    strtab = sections[symtab[6]]
    syms = []
    for k in range(symtab[5] // 16):
        name, value, _, info, _, shndx = struct.unpack_from('<IIIBBH', data, symtab[4] + k * 16)
        syms.append((cstr(strtab[4] + name), value, info, shndx))

    o = Obj()
    o.item(0, align=log2(max(align('.text'), align('.rodata'), align('.data'))))
    o.bss_len = by_name['.bss'][1][5]
    o.bss_align = log2(align('.bss'))
    for name, value, info, shndx in syms:
        if info >> 4 != 1 or shndx == 0:
            continue
        if shndx == SHN_ABS:
            sys.exit('an ACC object has no absolute symbols: ' + name)
        section = index_name[shndx]
        if section == '.bss':
            o.define(name, value, bss=True)
        else:
            o.define(name, base[section] + value, func=section == '.text')

    for rname in ('.rela.text', '.rela.rodata', '.rela.data'):
        if rname not in by_name:
            continue
        section = rname[5:]
        _, sh = by_name[rname]
        for k in range(sh[5] // 12):
            off, info, addend = struct.unpack_from('<IIi', data, sh[4] + k * 12)
            at = base[section] + off
            kind = KIND[info & 0xFF]
            name, _, sinfo, shndx = syms[info >> 8]
            if sinfo & 0xF == 3:                    # a section symbol
                target = index_name[shndx]
                if target == '.bss':
                    target = 'bss'
                else:
                    addend += base[target]
                    target = 'text'
            else:
                target = name
            if kind == 'PCREL8':
                addend += 1                         # acc measures from P + 1
            if kind in ('HIGH8', 'UPPER8') or (kind == 'PCREL8' and not -128 <= addend <= 127):
                o.reloc(at, kind, target, addend)
            else:
                slot = (addend & 0xFFFFFF).to_bytes(3, 'little')[:WIDTH[kind]]
                text[at:at + len(slot)] = slot
                o.reloc(at, kind, target)

    o.text = text
    o.write(sys.argv[3])


if __name__ == '__main__':
    main()
