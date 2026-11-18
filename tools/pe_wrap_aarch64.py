#!/usr/bin/env python3
"""ELF(aarch64, PIC) -> PE32+ EFI application

binutils 2.38 的 `objcopy --output-target=efi-app-aarch64` 不生成 optional header，
固件因此不认（PE 头 magic 读到 .text 的字节）。本脚本直接构造合规 PE。

用法: pe_wrap_aarch64.py <in.elf> <out.efi>
"""
import struct, sys
from elftools.elf.elffile import ELFFile

FILE_ALIGN = 0x200
SECT_ALIGN = 0x1000
IMAGE_BASE = 0x0
SHF_ALLOC  = 0x2

def align(v, a):
    return (v + a - 1) // a * a

def main(src, dst):
    # ---- 全部解析都在 with 内完成，出去只带纯数据 ----
    with open(src, 'rb') as f:
        elf = ELFFile(f)
        secs = []
        for sec in elf.iter_sections():
            if not (sec['sh_flags'] & SHF_ALLOC):
                continue
            if sec['sh_type'] in ('SHT_NULL', 'SHT_NOBITS'):
                continue
            if sec.name in ('.dynamic', '.dynsym', '.dynstr', '.hash', '.gnu.hash'):
                continue
            if sec.name.startswith('.rela') or sec.name.startswith('.rel'):
                continue   # 重定位源，已转成 .reloc
            secs.append(sec)
        data = {s.name: s.data() for s in secs}
        sizes = {s.name: s['sh_size'] for s in secs}
        relocs = {}          # target_section_index -> [(offset, type, addend)]
        for sec in elf.iter_sections():
            if sec['sh_type'] not in ('SHT_RELA', 'SHT_REL'):
                continue
            items = []
            for r in sec.iter_relocations():
                add = r['r_addend'] if r.is_RELA() else 0
                items.append((r['r_offset'], r['r_info_type'], add))
            if items:
                relocs[sec['sh_info']] = items
        sec_names = {i: s.name for i, s in enumerate(elf.iter_sections())}

    # ---- 布局 ----
    names = [s.name for s in secs]
    order = ['.text'] + [n for n in names if n != '.text']
    rva, foff, layout = {}, {}, []
    cur_rva, cur_foff = SECT_ALIGN, FILE_ALIGN
    for n in order:
        cur_rva = align(cur_rva, SECT_ALIGN)
        cur_foff = align(cur_foff, FILE_ALIGN)
        rva[n], foff[n] = cur_rva, cur_foff
        layout.append([n, cur_rva, cur_foff, sizes[n]])
        cur_rva += sizes[n]
        cur_foff += sizes[n]

    # ---- 重定位 -> .reloc（按页分块）----
    reloc_block = b''
    if relocs:
        pages = {}
        for tgt_idx, items in relocs.items():
            tname = sec_names.get(tgt_idx)
            base = rva.get(tname, 0)
            for (off, typ, add) in items:
                if typ in (1027, 257, 1025):      # RELATIVE / ABS64 / ABS32
                    r = base + off
                    pages.setdefault(r & ~0xFFF, []).append(r & 0xFFF)
        for page in sorted(pages):
            buf = b''.join(struct.pack('<HH', o, 10) for o in sorted(pages[page]))
            buf += b'\x00\x00'
            size = align(len(buf) + 8, 4)
            buf = buf.ljust(size - 8, b'\x00')
            reloc_block += struct.pack('<II', page, size) + buf
        if reloc_block:
            reloc_rva = align(cur_rva, SECT_ALIGN)
            reloc_foff = align(cur_foff, FILE_ALIGN)
            layout.append(['.reloc', reloc_rva, reloc_foff, len(reloc_block)])
            cur_rva = reloc_rva + len(reloc_block)
            cur_foff = reloc_foff + len(reloc_block)

    size_of_image = align(cur_rva, SECT_ALIGN)
    num_sec = len(layout)
    text_sz = sizes.get('.text', 0)

    # ---- DOS 头 ----
    dos = bytearray(0x40)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3c, 0x40)

    # ---- COFF 头（20B）----
    coff = struct.pack('<HHIIIHH', 0xAA64, num_sec, 0, 0, 0, 0xF0, 0x0022)

    # ---- PE32+ 可选头（0xF0 = 112 + 16*8 数据目录）----
    opt = bytearray(0xF0)
    struct.pack_into('<H', opt, 0, 0x20B)
    struct.pack_into('<I', opt, 4, text_sz)              # SizeOfCode
    struct.pack_into('<I', opt, 16, rva['.text'])        # AddressOfEntryPoint
    struct.pack_into('<I', opt, 20, rva['.text'])        # BaseOfCode
    struct.pack_into('<Q', opt, 24, IMAGE_BASE)
    struct.pack_into('<I', opt, 32, SECT_ALIGN)
    struct.pack_into('<I', opt, 36, FILE_ALIGN)
    struct.pack_into('<H', opt, 40, 0)
    struct.pack_into('<I', opt, 56, size_of_image)       # SizeOfImage
    struct.pack_into('<I', opt, 60, FILE_ALIGN)          # SizeOfHeaders
    struct.pack_into('<H', opt, 68, 0xA)                 # Subsystem: EFI app
    struct.pack_into('<H', opt, 70, 0)                   # DllCharacteristics
    struct.pack_into('<Q', opt, 72, 0x100000)
    struct.pack_into('<Q', opt, 80, 0x1000)
    struct.pack_into('<Q', opt, 88, 0x100000)
    struct.pack_into('<Q', opt, 96, 0x1000)
    struct.pack_into('<I', opt, 108, 16)                 # NumberOfRvaAndSizes

    # ---- 节表 ----
    shdrs = b''
    for (n, r, f, sz) in layout:
        chars = 0x42000040 if n == '.reloc' else 0x40000040
        shdrs += struct.pack('<8sIIIIIIHHI',
                             n.encode()[:8], align(sz, SECT_ALIGN) or SECT_ALIGN,
                             r, align(sz, FILE_ALIGN) or FILE_ALIGN, f,
                             0, 0, 0, 0, chars)

    headers = (bytes(dos) + b'PE\x00\x00' + coff + bytes(opt) + shdrs).ljust(FILE_ALIGN, b'\x00')

    out = bytearray(headers)
    for (n, r, f, sz) in layout:
        body = reloc_block if n == '.reloc' else data.get(n, b'')
        if len(out) < f:
            out += b'\x00' * (f - len(out))
        out += body
        pad = f + align(len(body), FILE_ALIGN)
        if len(out) < pad:
            out += b'\x00' * (pad - len(out))

    with open(dst, 'wb') as g:
        g.write(out)
    print(f"[pe_wrap] {dst}: {len(out)} B, {num_sec} sections, SizeOfImage {hex(size_of_image)}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
