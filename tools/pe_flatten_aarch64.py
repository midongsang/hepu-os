#!/usr/bin/env python3
"""aarch64 gnu-efi 应用：ELF -> EFI（不重造 PE 头）

gnu-efi 的 crt0-efi-aarch64.S 把完整的 PE/COFF 头（含 optional header 与节表）
写在 .text.head 里，并在 _start 里用 _DYNAMIC 自举重定位。因此 EFI 文件 =
.text（从偏移 0，含 PE 头） + .data（按节表写的 VirtualAddress 落位），无需另造头。

binutils 2.38 的 objcopy --output-target=efi-app-aarch64 不认这个布局（生成残缺 PE），
所以这里直接按节拼接。

用法: pe_flatten_aarch64.py <in.elf> <out.efi>
"""
import sys
from elftools.elf.elffile import ELFFile

def main(src, dst):
    with open(src, 'rb') as f:
        elf = ELFFile(f)
        items = []
        for sec in elf.iter_sections():
            if not (sec['sh_flags'] & 0x2):       # SHF_ALLOC
                continue
            if sec['sh_type'] in ('SHT_NULL', 'SHT_NOBITS'):
                continue
            items.append((sec['sh_addr'], sec['sh_offset'], sec['sh_size'], sec.name))
        data = {s.name: s.data() for s in elf.iter_sections()}
    # 只拼 .text 与 .data（crt0 的节表就这两节；.dynamic/.rela 都在 .text 尾部）
    out = bytearray()
    for (addr, off, size, name) in items:
        if name not in ('.text', '.data'):
            continue
        if len(out) < addr:
            out += b'\x00' * (addr - len(out))   # 按 VirtualAddress 落位
        out += data[name]
    # crt0 的 SizeOfImage 覆盖 .data 的 VirtualSize（含 .bss），文件必须补齐，
    # 否则固件判 "Unsupported"（要求文件大小 >= SizeOfImage）。
    import struct as _s
    if len(out) >= 0x100:
        e_lfanew = _s.unpack('<I', out[0x3c:0x40])[0]
        opt = e_lfanew + 24
        size_of_image = _s.unpack('<I', out[opt+56:opt+60])[0]
        if len(out) < size_of_image:
            out += b'\x00' * (size_of_image - len(out))
    with open(dst, 'wb') as g:
        g.write(out)
    print(f"[pe_flat] {dst}: {len(out)} B（.text 起于 0，.data 按 crt0 节表落位）")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
