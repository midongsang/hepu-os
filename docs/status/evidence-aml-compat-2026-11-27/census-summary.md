# 结构普查汇总（34 张表）

> 口径：`build-*` 来自结构化外层遍历（可信度较高）；`eval-*` 来自全表原始字节启发式扫描，无法区分代码与数据，频次只作线索，不能单独作为缺口结论。

## A. 建库期外层结构中不支持/跳过的扩展对象

| 操作码 | 名称 | 口径 | 出现次数 | 涉及表数 |
|---|---|---|---|---|
| 0x13 | CreateField | unsupported | 4 | 2 |
| 0x22 | Sleep | eval-unsupported | 2 | 2 |
| 0x48 | UnknownExt48 | unsupported | 2 | 1 |
| 0xBF | UnknownExtBF | unsupported | 1 | 1 |
| 0x87 | BankField | build-skipped | 1 | 1 |
| 0x88 | DataRegion | unsupported | 1 | 1 |
| 0x86 | IndexField | build-skipped | 1 | 1 |

## B. 求值期扩展对象（启发式扫描，线索）

| 操作码 | 名称 | 口径 | 出现次数 | 涉及表数 |
|---|---|---|---|---|
| 0x12 | CondRefOf | eval-partial | 1293 | 14 |
| 0x22 | Sleep | eval-unsupported | 902 | 14 |
| 0x13 | CreateField | unsupported | 161 | 11 |
| 0x21 | Stall | eval-unsupported | 97 | 8 |
| 0x87 | BankField | build-skipped | 35 | 2 |
| 0x48 | UnknownExt48 | unsupported | 29 | 1 |
| 0x4D | UnknownExt4D | unsupported | 15 | 8 |
| 0x33 | Timer | unsupported | 13 | 2 |
| 0x10 | ShiftRightBit(obsolete) | unsupported | 11 | 2 |
| 0x86 | IndexField | build-skipped | 9 | 5 |
| 0x20 | Load | unsupported | 9 | 3 |
| 0x41 | UnknownExt41 | unsupported | 6 | 1 |
| 0x0A | UnknownExt0A | unsupported | 5 | 5 |
| 0x4F | UnknownExt4F | unsupported | 4 | 3 |
| 0x52 | UnknownExt52 | unsupported | 4 | 2 |
| 0x44 | UnknownExt44 | unsupported | 4 | 1 |
| 0x00 | UnknownExt00 | unsupported | 3 | 3 |
| 0x28 | FromBcd | unsupported | 3 | 2 |
| 0x24 | Signal | unsupported | 3 | 1 |
| 0x75 | UnknownExt75 | unsupported | 2 | 1 |
| 0x0B | UnknownExt0B | unsupported | 2 | 2 |
| 0x57 | UnknownExt57 | unsupported | 2 | 1 |
| 0x9E | UnknownExt9E | unsupported | 2 | 2 |
| 0x3E | UnknownExt3E | unsupported | 2 | 2 |
| 0x47 | UnknownExt47 | unsupported | 2 | 2 |
| 0xAA | UnknownExtAA | unsupported | 2 | 1 |
| 0x38 | UnknownExt38 | unsupported | 2 | 1 |
| 0x06 | UnknownExt06 | unsupported | 2 | 1 |
| 0x93 | UnknownExt93 | unsupported | 2 | 2 |
| 0x2C | UnknownExt2C | unsupported | 2 | 1 |
| 0x08 | UnknownExt08 | unsupported | 1 | 1 |
| 0x51 | UnknownExt51 | unsupported | 1 | 1 |
| 0x7F | UnknownExt7F | unsupported | 1 | 1 |
| 0x8F | UnknownExt8F | unsupported | 1 | 1 |
| 0xCF | UnknownExtCF | unsupported | 1 | 1 |
| 0x76 | UnknownExt76 | unsupported | 1 | 1 |
| 0x5B | UnknownExt5B | unsupported | 1 | 1 |
| 0x03 | UnknownExt03 | unsupported | 1 | 1 |
| 0xC1 | UnknownExtC1 | unsupported | 1 | 1 |
| 0x8A | UnknownExt8A | unsupported | 1 | 1 |
| 0x16 | UnknownExt16 | unsupported | 1 | 1 |
| 0xB8 | UnknownExtB8 | unsupported | 1 | 1 |
| 0xC3 | UnknownExtC3 | unsupported | 1 | 1 |
| 0xD0 | UnknownExtD0 | unsupported | 1 | 1 |
| 0x98 | UnknownExt98 | unsupported | 1 | 1 |
| 0x3A | UnknownExt3A | unsupported | 1 | 1 |
| 0x46 | UnknownExt46 | unsupported | 1 | 1 |
| 0x14 | UnknownExt14 | unsupported | 1 | 1 |
| 0x5C | UnknownExt5C | unsupported | 1 | 1 |
| 0x29 | ToBcd | unsupported | 1 | 1 |
| 0x53 | UnknownExt53 | unsupported | 1 | 1 |
| 0x2E | UnknownExt2E | unsupported | 1 | 1 |
| 0xE7 | UnknownExtE7 | unsupported | 1 | 1 |
| 0xD2 | UnknownExtD2 | unsupported | 1 | 1 |
| 0xC2 | UnknownExtC2 | unsupported | 1 | 1 |
| 0xAC | UnknownExtAC | unsupported | 1 | 1 |
| 0x45 | UnknownExt45 | unsupported | 1 | 1 |
| 0xB6 | UnknownExtB6 | unsupported | 1 | 1 |
| 0x8C | UnknownExt8C | unsupported | 1 | 1 |
| 0x78 | UnknownExt78 | unsupported | 1 | 1 |
| 0xB5 | UnknownExtB5 | unsupported | 1 | 1 |
| 0xE1 | UnknownExtE1 | unsupported | 1 | 1 |
| 0x07 | UnknownExt07 | unsupported | 1 | 1 |
| 0x7A | UnknownExt7A | unsupported | 1 | 1 |
| 0xCA | UnknownExtCA | unsupported | 1 | 1 |
| 0xCC | UnknownExtCC | unsupported | 1 | 1 |
| 0x11 | ShiftLeftBit(obsolete) | unsupported | 1 | 1 |
| 0x2F | UnknownExt2F | unsupported | 1 | 1 |
| 0x67 | UnknownExt67 | unsupported | 1 | 1 |
| 0x64 | UnknownExt64 | unsupported | 1 | 1 |
| 0xA4 | UnknownExtA4 | unsupported | 1 | 1 |
| 0xA0 | UnknownExtA0 | unsupported | 1 | 1 |
| 0x25 | Wait | eval-unsupported | 1 | 1 |
| 0x26 | Reset | unsupported | 1 | 1 |
| 0x88 | DataRegion | unsupported | 1 | 1 |

## C. 求值期单字节操作码（启发式扫描，线索）

| 操作码 | 名称 | 口径 | 出现次数 | 涉及表数 |
|---|---|---|---|---|
| 0x73 | Concatenate | unsupported | 5052 | 17 |
| 0x78 | Divide | unsupported | 1949 | 17 |
| 0x86 | Notify | unsupported | 1629 | 15 |
| 0x8C | CreateByteField | unsupported | 867 | 12 |
| 0x81 | FindSetLeftBit | unsupported | 835 | 15 |
| 0x8A | CreateDWordField | unsupported | 788 | 13 |
| 0x8D | CreateBitField | unsupported | 676 | 10 |
| 0x98 | ToHexString | unsupported | 669 | 16 |
| 0x82 | FindSetRightBit | unsupported | 525 | 14 |
| 0x89 | Match | unsupported | 460 | 13 |
| 0x71 | RefOf | unsupported | 424 | 13 |
| 0x84 | ConcatenateTemplate | unsupported | 379 | 11 |
| 0x8B | CreateWordField | unsupported | 366 | 10 |
| 0x8E | ObjectType | unsupported | 336 | 9 |
| 0x8F | CreateQWordField | unsupported | 326 | 12 |
| 0x9E | Mid | unsupported | 252 | 11 |
| 0x9C | ToString | unsupported | 220 | 9 |
| 0x97 | ToDecimalString | unsupported | 218 | 10 |
| 0x9F | Continue | unsupported | 210 | 9 |
| 0x96 | ToBuffer | unsupported | 185 | 11 |
| 0xCC | BreakPoint | unsupported | 178 | 10 |

## 逐表首个未支持对象（build / eval 上下文）

| 表 | 子类 | 大小 | build 首个 | eval 首个(启发式) |
|---|---|---|---|---|
| a16-11m-dsdt.dat | raw-binary | 279633 | ObjectType@0x0 | CreateByteField@0x25 |
| a16-11m-ssdt.dat | raw-binary | 304 | CreateDWordField@0x0 | - |
| acer-ec471g-dsdt.aml | raw-binary | 45759 | CreateBitField@0x0 | ToHexString@0x2f9 |
| acer-ec471g-ssdt1.aml | raw-binary | 2408 | ConcatenateTemplate@0x0 | FindSetRightBit@0x72 |
| acer-ec471g-ssdt2.aml | raw-binary | 2706 | - | ToHexString@0x51 |
| asus-gl552jx-dsdt.aml | raw-binary | 81441 | CreateDWordField@0x0 | CreateWordField@0x11ff |
| asus-gl552jx-ssdt-cpupm.aml | raw-binary | 2776 | - | ToHexString@0x3d |
| dell-xps9350-ssdt-cpupm.aml | raw-binary | 1761 | ToBuffer@0x0 | Concatenate@0x72 |
| lenovo-14asp10-dsdt.dat | raw-binary | 37773 | CreateBitField@0x0 | ObjectType@0xe2c |
| lenovo-14asp10-ssdt1.dat | raw-binary | 259 | - | - |
| lenovo-16afr10h-dsdt.dat | raw-binary | 114523 | UnknownExtBF@0x4 | ToString@0x26 |
| lenovo-16afr10h-ssdt1.dat | raw-binary | 1975 | Sleep@0x4f6 | Concatenate@0x7d |
| lenovo-16afr10h-ssdt2.dat | raw-binary | 33133 | FindSetLeftBit@0x0 | CreateWordField@0x5f6 |
| surface-book2-dsdt.aml | reassembled | 102989 | CreateDWordField@0x0 | CreateWordField@0x82e |
| surface-book2-ssdt1.aml | reassembled | 9448 | - | Match@0x25 |
| surface-laptop5-dsdt.aml | reassembled | 171439 | Notify@0x0 | ObjectType@0x25 |
| surface-laptop5-ssdt1.aml | reassembled | 14810 | - | FindSetRightBit@0x25 |
| surface-pro7-dsdt.aml | reassembled | 114974 | CreateDWordField@0x0 | CreateDWordField@0x25 |
| surface-pro7-ssdt1.aml | reassembled | 45690 | Concatenate@0x0 | Concatenate@0x2d7 |
| t450s-ssdt-batx.aml | raw-binary | 7580 | ToString@0x0 | CreateByteField@0x1e0 |
| t450s-ssdt-ocbatt.aml | raw-binary | 3866 | - | Divide@0x5e5 |
| x1c6-ssdt-battery.aml | raw-binary | 2559 | - | Divide@0x3d6 |
| SeaBIOS-bios.bin.dsdt@0x13f2d.aml | firmware | 4585 | - | ToHexString@0xfc |
| SeaBIOS-bios.bin.ssdt@0x13da9.aml | firmware | 388 | ConcatenateTemplate@0x0 | Divide@0x51 |
| SeaBIOS-bios.bin.ssdt@0xf280.aml | firmware | 121 | - | - |
| SeaBIOS-bios.bin.ssdt@0xf300.aml | firmware | 162 | - | - |
| syn-bankfield.aml | synthetic | 134 | Notify@0x0 | BankField@0x51 |
| syn-baseline.aml | synthetic | 115 | Concatenate@0x0 | - |
| syn-condrefof.aml | synthetic | 113 | RefOf@0x0 | CondRefOf@0x42 |
| syn-create-dword.aml | synthetic | 122 | - | CreateDWordField@0x4c |
| syn-ec-region.aml | synthetic | 152 | ToHexString@0x0 | - |
| syn-extfield.aml | synthetic | 118 | DataRegion@0x51 | DataRegion@0x51 |
| syn-indexfield.aml | synthetic | 146 | Mid@0x0 | IndexField@0x5d |
| syn-osi-branch.aml | synthetic | 116 | - | Concatenate@0x50 |
