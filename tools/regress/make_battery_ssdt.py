#!/usr/bin/env python3
"""生成用于 QEMU 取证 / 主机单测的合成 SSDT（不是产品固件的一部分）。

用法:
  make_battery_ssdt.py <输出文件> [declared|valid|valid-bix|valid-acoff|absent|nobst|ec|ec-read] [百分比]
  make_battery_ssdt.py --header <输出头文件>

模式与预期:
  declared   仅声明 PNP0C0A + Name(_STA,0x0F)  -> DECLARED
  valid      _STA/_BIF/_BST 全实现（_BIF 路径） -> VALID
  valid-bix  同上但用 _BIX                       -> VALID
  absent     Method(_STA) 返回 0                 -> ABSENT
  nobst      只有声明，没有 _BST                  -> DECLARED（不可信）
  ec         _BST 读 EmbeddedControl 字段         -> DECLARED（明确不支持 EC）
  ec-read    EC 控制器 + EC 16 位字段 + _REG，_GLK=0 -> 主机模拟 EC 正路径
  ec-glk     同上但 _GLK=1                          -> 锁死 EC，仅诊断
  valid-acoff  同 valid，但 ACPI0003._PSR=0       -> 交流离线
  idxfield-io  SystemIO IndexField 8/16/32 granule + 跨 granule/非对齐
  idxfield-mem SystemMemory IndexField 正路径
  bankfield    BankField 多 bank 切换
  idxfield-ec  EC 上的 IndexField 电池             -> fail-closed，UNKNOWN
  bankfield-ec bank 寄存器在 EC                    -> UNSUP
  idxfield-bad 缺名/非字段/位宽溢出/越界等负路径
"""
import struct
import sys

# ---------------------------------------------------------------- AML 编码


def pkg(payload):
    """PkgLength 编码：长度包含长度字节自身。"""
    total = len(payload) + 1
    if total < 0x40:
        return bytes([total]) + payload
    total = len(payload) + 2
    if total < 0x1000:
        return bytes([0x40 | (total & 0x0F), (total >> 4) & 0xFF]) + payload
    total = len(payload) + 3
    if total < 0x100000:
        return bytes([0x80 | (total & 0x0F), (total >> 4) & 0xFF, (total >> 12) & 0xFF]) + payload
    raise ValueError("payload too large")


def bit_len(bits):
    """FieldList 里的位宽：PkgLength 值 = 位宽 + 长度字节数。"""
    total = bits + 1
    if total < 0x40:
        return bytes([total])
    total = bits + 2
    if total < 0x1000:
        return bytes([0x40 | (total & 0x0F), (total >> 4) & 0xFF])
    raise ValueError("field too wide")


def int_obj(v):
    if v == 0:
        return b"\x00"
    if v == 1:
        return b"\x01"
    if v == 0xFF:
        return b"\xff"
    if 0 <= v <= 0xFF:
        return b"\x0a" + bytes([v])
    if 0 <= v <= 0xFFFF:
        return b"\x0b" + struct.pack("<H", v)
    if 0 <= v <= 0xFFFFFFFF:
        return b"\x0c" + struct.pack("<I", v)
    return b"\x0e" + struct.pack("<Q", v)


def string_obj(s):
    b = s.encode()
    return b"\x0d" + bytes([len(b)]) + b + b"\x00"


def package(items):
    return b"\x12" + pkg(bytes([len(items)]) + b"".join(items))


def seg(n):
    b = n.encode()
    if len(b) > 4:
        raise ValueError("name seg too long: %s" % n)
    return b.ljust(4, b"_")


def name(n, value):
    return b"\x08" + seg(n) + value


def method(n, body, args=0, serialized=False):
    flags = args | (0x08 if serialized else 0)
    return b"\x14" + pkg(seg(n) + bytes([flags]) + body)


def device(n, body):
    return b"\x5b\x82" + pkg(seg(n) + body)


def ret(expr):
    return b"\xa4" + expr


def store(src, dst):
    return b"\x70" + src + dst


def index(base_name, i):
    return b"\x88" + base_name.encode() + int_obj(i)


def l_equal(a, b):
    return b"\x93" + a + b


def if_then(predicate, body):
    return b"\xa0" + pkg(predicate + body)


def else_body(body):
    return b"\xa1" + pkg(body)


def operation_region(n, space, off, length):
    return b"\x5b\x80" + pkg(n.encode() + bytes([space]) + int_obj(off) + int_obj(length))


def field(region, entries):
    payload = region.encode() + b"\x00"
    for e in entries:
        if e[0] is None:
            payload += b"\x00" + bit_len(e[1])
        else:
            payload += e[0].encode() + bit_len(e[1])
    return b"\x5b\x81" + pkg(payload)


def _fieldlist_payload(entries):
    payload = b""
    for e in entries:
        if e[0] is None:
            payload += b"\x00" + bit_len(e[1])
        else:
            payload += e[0].encode().ljust(4, b"_") + bit_len(e[1])
    return payload


def index_field(idx_name, data_name, entries, flags=0x00):
    """IndexField(IndexName, DataName, FieldFlags) { FieldList }。"""
    payload = (idx_name.encode().ljust(4, b"_")
               + data_name.encode().ljust(4, b"_")
               + bytes([flags]) + _fieldlist_payload(entries))
    return b"\x5b\x86" + pkg(payload)


def bank_field(region, bank_name, bank_value, entries, flags=0x00):
    """BankField(RegionName, BankName, BankValue, FieldFlags) { FieldList }。"""
    payload = (region.encode().ljust(4, b"_")
               + bank_name.encode().ljust(4, b"_")
               + int_obj(bank_value) + bytes([flags])
               + _fieldlist_payload(entries))
    return b"\x5b\x87" + pkg(payload)


def buffer_obj(bytes_):
    """Buffer 项：0x11 PkgLength LengthConst ByteList。"""
    raw = bytes(bytes_)
    return b"\x11" + pkg(int_obj(len(raw)) + raw)


def create_dword_field(buf_expr, byte_index, name):
    return b"\x8a" + buf_expr + int_obj(byte_index) + seg(name)


def create_word_field(buf_expr, byte_index, name):
    return b"\x8b" + buf_expr + int_obj(byte_index) + seg(name)


def create_byte_field(buf_expr, byte_index, name):
    return b"\x8c" + buf_expr + int_obj(byte_index) + seg(name)


def create_qword_field(buf_expr, byte_index, name):
    return b"\x8f" + buf_expr + int_obj(byte_index) + seg(name)


def create_field(buf_expr, bit_index, bit_width, name):
    return b"\x5b\x13" + buf_expr + int_obj(bit_index) + int_obj(bit_width) + seg(name)


def mutex(n, sync=0):
    return b"\x5b\x01" + seg(n) + bytes([sync])


def acquire(n, timeout=0xFFFF):
    return b"\x5b\x23" + seg(n) + int_obj(timeout)


def release(n):
    return b"\x5b\x27" + seg(n)


def osi(name):
    return b"_OSI" + string_obj(name)


def build_table(body, oem_table_id=b"BATDECL"):
    header = bytearray(36)
    header[0:4] = b"SSDT"
    struct.pack_into("<I", header, 4, 36 + len(body))
    header[8] = 2
    header[10:16] = b"HEPUTS"
    header[16:24] = oem_table_id[:8].ljust(8, b" ")
    header[24:28] = b"HEPU"
    struct.pack_into("<I", header, 28, 1)
    struct.pack_into("<I", header, 32, 1)
    checksum = (-sum(header) - sum(body)) & 0xFF
    header[9] = checksum
    return bytes(header) + body


# ---------------------------------------------------------------- 各模式


def make_declared(dev="BAT0"):
    return build_table(
        device(dev, name("_HID", string_obj("PNP0C0A")) + name("_STA", int_obj(0x0F))),
        oem_table_id=b"BATDECL",
    )


def valid_device(percent=57, use_bix=False, dev="BAT0", bst_state=2,
                 with_mutex=False, bst_rate=0):
    design = 10000
    full = 10000
    rem = design * percent // 100
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0), int_obj(0)]))
    bst_body = b""
    if with_mutex:
        bst_body += acquire("MUT0") + store(osi("Linux"), b"\x60")
    bst_body += (
        store(int_obj(bst_state), index("BST0", 0))
        + store(int_obj(bst_rate), index("BST0", 1))
        + store(int_obj(rem), index("BST0", 2))
        + store(int_obj(12000), index("BST0", 3))
    )
    if with_mutex:
        bst_body += release("MUT0")
    bst_body += ret(b"BST0")
    if use_bix:
        caps = method("_BIX", ret(package([int_obj(0), int_obj(0), int_obj(design),
                                           int_obj(full), int_obj(12000)])))
    else:
        caps = method("_BIF", ret(package([int_obj(0), int_obj(design), int_obj(full),
                                           int_obj(1), int_obj(12000)])))
    extra = (
        method("_TIF", if_then(l_equal(int_obj(0x0F), int_obj(0x0F)), ret(int_obj(1)))
               + else_body(ret(int_obj(0))) + ret(int_obj(3)))
        + method("_TAR", ret(b"\x72" + int_obj(0x10) + int_obj(0x20)))
    )
    body = name("_HID", string_obj("PNP0C0A")) + method("_STA", ret(int_obj(0x0F))) \
        + bst0 + method("_BST", bst_body) + caps + extra
    return device(dev, body)


def make_valid(percent=57, use_bix=False, dev="BAT0", ac_psr=1, bst_state=2,
               bst_rate=0):
    ac = device("AC00", name("_HID", string_obj("ACPI0003"))
                + method("_PSR", ret(int_obj(ac_psr))))
    return build_table(valid_device(percent, use_bix, dev, bst_state,
                                    bst_rate=bst_rate) + ac,
                       oem_table_id=b"BATVALID")


def make_mutex(percent=57, dev="MTX0"):
    ac = device("AC00", name("_HID", string_obj("ACPI0003"))
                + method("_PSR", ret(int_obj(1))))
    return build_table(mutex("MUT0") + valid_device(percent, dev=dev, with_mutex=True) + ac,
                       oem_table_id=b"BATMTX")


def make_dual(percent=57):
    # 用独立名字避免与 make_valid 的 BAT0 在同一主机测试命名空间里重名。
    abs0 = device("DUA0", name("_HID", string_obj("PNP0C0A"))
                  + method("_STA", ret(int_obj(0))))
    ac = device("AC00", name("_HID", string_obj("ACPI0003"))
                + method("_PSR", ret(int_obj(1))))
    return build_table(abs0 + valid_device(percent, dev="DUA1") + ac,
                       oem_table_id=b"BATDUAL")


def make_compat(percent=57, dev="CMP0"):
    """真实固件缺省字段：_BST 只有 3 个元素，_BIX 全 0，_BIF 可信。"""
    design = 10000
    rem = design * percent // 100
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0)]))
    bst_body = (
        store(int_obj(2), index("BST0", 0))
        + store(int_obj(0), index("BST0", 1))
        + store(int_obj(rem), index("BST0", 2))
        + ret(b"BST0")
    )
    caps = (method("_BIX", ret(package([int_obj(0), int_obj(0), int_obj(0),
                                          int_obj(0), int_obj(0)])))
            + method("_BIF", ret(package([int_obj(0), int_obj(design),
                                            int_obj(design), int_obj(1),
                                            int_obj(12000)]))))
    ac = device("AC00", name("_HID", string_obj("ACPI0003"))
                + method("_PSR", ret(int_obj(1))))
    body = (name("_HID", string_obj("PNP0C0A"))
            + method("_STA", ret(int_obj(0x0F)))
            + bst0 + method("_BST", bst_body) + caps)
    return build_table(device(dev, body) + ac, oem_table_id=b"BATCOMP")


def make_dual_fallback():
    """第一个 present 槽位缺 _BST，第二个槽位可信：验证运行中回退。"""
    broken = device("DUF0", name("_HID", string_obj("PNP0C0A"))
                    + method("_STA", ret(int_obj(0x0F))))
    ac = device("AC00", name("_HID", string_obj("ACPI0003"))
                + method("_PSR", ret(int_obj(1))))
    return build_table(broken + valid_device(57, dev="DUF1") + ac,
                       oem_table_id=b"BATFALL")


def make_dual_both():
    """两块都 present、都可信：验证容量/速率合并，而不是只取第一块。"""
    ac = device("AC00", name("_HID", string_obj("ACPI0003"))
                + method("_PSR", ret(int_obj(1))))
    return build_table(valid_device(30, dev="DUB0") + valid_device(70, dev="DUB1") + ac,
                       oem_table_id=b"BATMERGE")


def make_absent(dev="ABS0"):
    return build_table(
        device(dev, name("_HID", string_obj("PNP0C0A")) + method("_STA", ret(int_obj(0)))),
        oem_table_id=b"BATABS",
    )


def make_nobst(dev="NOB0"):
    return build_table(
        device(dev, name("_HID", string_obj("PNP0C0A"))
               + method("_STA", ret(int_obj(0x0F)))),
        oem_table_id=b"BATNOBST",
    )


def make_ec(percent=57, dev="ECB0"):
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0), int_obj(0)]))
    bst_body = (
        store(int_obj(2), index("BST0", 0))
        + store(b"B0ST", index("BST0", 2))     # 读 EC 字段 -> 解释器不支持
        + ret(b"BST0")
    )
    body = (
        name("_HID", string_obj("PNP0C0A"))
        + method("_STA", ret(int_obj(0x0F)))
        + bst0
        + operation_region("ECOR", 3, 0, 0x100)
        + field("ECOR", [("B0ST", 8)])
        + method("_BST", bst_body)
    )
    return build_table(device(dev, body), oem_table_id=b"BATEC")


def make_ec_read(percent=57, dev="ECR0", glk=0):
    """EC 型电池：Field 16 位来自 EmbeddedControl，值由主机模拟 EC 提供。"""
    design = 10000
    rem = design * percent // 100
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0), int_obj(0)]))
    real_bst = (
        store(int_obj(2), index("BST0", 0))
        + store(b"B0ST", index("BST0", 2))     # 16 位 EC 字段，应拼成 0x1644=5700
        + store(int_obj(12000), index("BST0", 3))
        + ret(b"BST0")
    )
    # 真实 DSDT 常见写法：ECON/ECRG 未置位时 _BST 返回全 0；_REG 置位后才读 EC。
    bst_body = if_then(l_equal(b"ECRG", int_obj(1)), real_bst) + else_body(ret(b"BST0"))
    caps = method("_BIF", ret(package([int_obj(0), int_obj(design), int_obj(design),
                                       int_obj(1), int_obj(12000)])))
    body = (
        name("_HID", string_obj("PNP0C0A"))
        + method("_STA", ret(int_obj(0x0F)))
        + bst0
        + method("_BST", bst_body)
        + caps
        + operation_region("ECOR", 3, 0, 0x100)
        + field("ECOR", [("B0ST", 16)])
    )
    ecdev = device("EC00", name("_HID", string_obj("PNP0C09"))
                   + method("_STA", ret(int_obj(0x0F)))
                   + method("_GLK", ret(int_obj(glk)))
                   + method("_REG", if_then(l_equal(b"\x68", int_obj(3)),
                                            store(b"\x69", b"ECRG")), args=2))
    # ECRG 放在根作用域，EC00._REG 与 ECR0._BST 都能按相对名解析到它。
    return build_table(name("ECRG", int_obj(0)) + ecdev + device(dev, body),
                       oem_table_id=b"BATECR")


def make_idxfield_io():
    """SystemIO IndexField：byte/word/dword 三种 granule，含跨 granule 与非对齐抽取。"""
    body = (
        operation_region("IXR1", 1, 0x100, 0x04)
        + field("IXR1", [("IB00", 8), ("DB00", 8)])
        + index_field("IB00", "DB00", [
            ("BY00", 8), ("BY01", 8), ("BY02", 8), ("BY03", 8),
            ("XA12", 12), (None, 4), ("UN05", 5),
        ])
        + operation_region("IXR2", 1, 0x104, 0x04)
        + field("IXR2", [("IW00", 16), ("DW00", 16)])
        + index_field("IW00", "DW00", [("WR00", 16), ("WR01", 16)])
        + operation_region("IXR3", 1, 0x108, 0x08)
        + field("IXR3", [("ID00", 32), ("DD00", 32)])
        + index_field("ID00", "DD00", [("DV00", 32)])
    )
    return build_table(body, oem_table_id=b"BATIDXIO")


def make_idxfield_mem():
    """SystemMemory IndexField 正路径（主机测试用内存回调模拟窗口）。"""
    body = (
        operation_region("IXRM", 0, 0x100000, 0x40)
        + field("IXRM", [("MI00", 8), ("MD00", 8)])
        + index_field("MI00", "MD00", [("MM00", 8), ("MM01", 16)])
    )
    return build_table(body, oem_table_id=b"BATIDXMM")


def make_bankfield():
    """BankField 多 bank 切换：同一 bank 寄存器，两条 BankField 各带不同 BankValue。"""
    body = (
        operation_region("BNIO", 1, 0x110, 0x02)
        + field("BNIO", [("BS00", 8)])
        + operation_region("BNMM", 0, 0x100100, 0x20)
        + bank_field("BNMM", "BS00", 0, [("BB00", 8)])
        + bank_field("BNMM", "BS00", 1, [("BB01", 8)])
    )
    return build_table(body, oem_table_id=b"BATBANK")


def make_idxfield_ec(dev="ECI0"):
    """EC 上的 IndexField 电池：EC 写未实现，必须 fail-closed，电池停在 UNKNOWN。"""
    design = 10000
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0), int_obj(0)]))
    real_bst = (
        store(int_obj(2), index("BST0", 0))
        + store(b"EB00", index("BST0", 2))
        + store(int_obj(12000), index("BST0", 3))
        + ret(b"BST0")
    )
    bst_body = if_then(l_equal(b"ECRG", int_obj(1)), real_bst) + else_body(ret(b"BST0"))
    caps = method("_BIF", ret(package([int_obj(0), int_obj(design), int_obj(design),
                                       int_obj(1), int_obj(12000)])))
    body = (
        name("_HID", string_obj("PNP0C0A"))
        + method("_STA", ret(int_obj(0x0F)))
        + bst0 + method("_BST", bst_body) + caps
        + operation_region("ECOR", 3, 0, 0x100)
        + field("ECOR", [("EI00", 8), ("ED00", 8)])
        + index_field("EI00", "ED00", [("EB00", 16)])
    )
    ecdev = device("EC00", name("_HID", string_obj("PNP0C09"))
                   + method("_STA", ret(int_obj(0x0F)))
                   + method("_GLK", ret(int_obj(0)))
                   + method("_REG", if_then(l_equal(b"\x68", int_obj(3)),
                                            store(b"\x69", b"ECRG")), args=2))
    return build_table(name("ECRG", int_obj(0)) + ecdev + device(dev, body),
                       oem_table_id=b"BATIDXEC")


def make_bankfield_ec():
    """BankField 的 bank 寄存器落在 EC：无 EC 写事务，必须 UNSUP。"""
    body = (
        operation_region("ECOR", 3, 0, 0x100)
        + field("ECOR", [("EB00", 8)])
        + bank_field("ECOR", "EB00", 0, [("EF00", 8)])
    )
    return build_table(body, oem_table_id=b"BATBKEC")


def make_idxfield_bad():
    """负路径夹具：缺名 / 非字段 / 位宽溢出 / region 越界 / 缺 region。"""
    body = (
        operation_region("BAD0", 1, 0x120, 0x04)
        + field("BAD0", [("BI00", 8), ("BD00", 8)])
        + name("XNAM", int_obj(5))
        + index_field("NOPE", "BD00", [("BX00", 8)])
        + index_field("BI00", "NOPE", [("BX01", 8)])
        + index_field("XNAM", "BD00", [("BX02", 8)])
        + index_field("BI00", "BD00", [("BIG0", 65)])
        + operation_region("SML0", 1, 0x130, 0x01)
        + field("SML0", [("SM00", 8), ("SM01", 8)])
        + bank_field("NOPE", "BI00", 0, [("BK00", 8)])
    )
    return build_table(body, oem_table_id=b"BATIDXBAD")


def make_tolerant(dev="BAT0"):
    """建库容错夹具：外层 DataRegion(0x5B88) 按长度跳过；If/Else 两分支声明都登记。"""
    dataregion = b"\x5b\x88" + pkg(seg("DR00") + b"\x00" + seg("DRF0") + bit_len(8))
    iff = (if_then(int_obj(1), name("INTH", int_obj(9)))
           + else_body(name("INEL", int_obj(8))))
    body = (name("_HID", string_obj("PNP0C0A"))
            + method("_STA", ret(int_obj(0x0F)))
            + dataregion + iff)
    return build_table(device(dev, body), oem_table_id=b"BATTOL")


def make_create_dword(percent=75, dev="BAT0"):
    """_BST 用 CreateDWordField 从 Buffer 拆包剩余容量，端到端进 VALID。"""
    design = 10000
    full = 10000
    rem = design * percent // 100
    rem_buf = struct.pack("<I", rem)
    # DW00 拆包 4 字节 Buffer；BY00 演示 CreateByteField + Store 回写往返。
    bst_body = (
        create_dword_field(buffer_obj(rem_buf), 0, "DW00")
        + ret(package([int_obj(2), int_obj(0), b"DW00", int_obj(12000)]))
    )
    roundtrip_body = (
        create_byte_field(buffer_obj(b"\x00"), 0, "BY00")
        + store(int_obj(0x5A), b"BY00")
        + ret(b"BY00")
    )
    body = (
        name("_HID", string_obj("PNP0C0A"))
        + method("_STA", ret(int_obj(0x0F)))
        + method("_BST", bst_body)
        + method("_BIF", ret(package([int_obj(0), int_obj(design), int_obj(full),
                                      int_obj(1), int_obj(12000)])))
        + method("_BXX", roundtrip_body)
    )
    return build_table(device(dev, body), oem_table_id=b"BATCFLD")


def make_create_bad():
    """Create*Field 负路径：越界 / 源非 Buffer / 位宽 65。"""
    small = buffer_obj(b"\x00\x00")          # 2 字节
    body = (
        name("IVAL", int_obj(7))
        + method("_B01", create_dword_field(small, 1, "OW00"))   # 字节1 +32 位 >16
        + method("_B02", create_dword_field(b"IVAL", 0, "NB00"))  # 源是整数
        + method("_B03", create_field(buffer_obj(b"\x00" * 8), 0, 65, "WF00"))
    )
    return build_table(device("BAD0", body), oem_table_id=b"BATCFBAD")


MODES = {
    "declared": lambda pct=57: make_declared(),
    "valid": make_valid,
    "valid-bix": lambda pct=57: make_valid(pct, use_bix=True),
    "valid-acoff": lambda pct=57: make_valid(pct, ac_psr=0, bst_state=0),
    "absent": lambda pct=57: make_absent(),
    "nobst": lambda pct=57: make_nobst(),
    "ec": make_ec,
    "ec-read": lambda pct=57: make_ec_read(pct, glk=0),
    "ec-glk": lambda pct=57: make_ec_read(pct, glk=1),
    "mutex": make_mutex,
    "dual": make_dual,
    "dual-both": lambda pct=57: make_dual_both(),
    "dual-fallback": lambda pct=57: make_dual_fallback(),
    "compat": lambda pct=57: make_compat(),
    "valid-discharge": lambda pct=57: make_valid(pct, dev="DIS0", ac_psr=0,
                                                 bst_state=0, bst_rate=1200),
    "idxfield-io": lambda pct=57: make_idxfield_io(),
    "idxfield-mem": lambda pct=57: make_idxfield_mem(),
    "bankfield": lambda pct=57: make_bankfield(),
    "idxfield-ec": lambda pct=57: make_idxfield_ec(),
    "bankfield-ec": lambda pct=57: make_bankfield_ec(),
    "idxfield-bad": lambda pct=57: make_idxfield_bad(),
    "create-dword": lambda pct=75: make_create_dword(75),
    "create-bad": lambda pct=57: make_create_bad(),
    "tolerant": lambda pct=57: make_tolerant(),
}


def write_array(f, name, table):
    f.write("static const unsigned char %s[] = {" % name)
    for i, b in enumerate(table):
        if i % 12 == 0:
            f.write("\n    ")
        f.write("0x%02x," % b)
    f.write("\n};\n")


def write_header(path):
    valid = make_valid(57, dev="BAT0")
    bix = make_valid(57, use_bix=True, dev="BIX0")
    ec = make_ec(57, dev="ECB0")
    ec_read = make_ec_read(57, dev="ECR0", glk=0)
    ec_glk = make_ec_read(57, dev="ECR0", glk=1)
    absent = make_absent("ABS0")
    mutex_tbl = make_mutex(57, dev="MTX0")
    dual = make_dual(57)
    discharge = make_valid(57, dev="DIS0", ac_psr=0, bst_state=0, bst_rate=1200)
    dual_both = make_dual_both()
    dual_fallback = make_dual_fallback()
    compat = make_compat()
    idx_io = make_idxfield_io()
    idx_mem = make_idxfield_mem()
    bank = make_bankfield()
    idx_ec = make_idxfield_ec()
    bank_ec = make_bankfield_ec()
    idx_bad = make_idxfield_bad()
    cf_dword = make_create_dword(75)
    cf_bad = make_create_bad()
    tolerant = make_tolerant()
    with open(path, "w", encoding="utf-8") as f:
        f.write("/* 由 tools/regress/make_battery_ssdt.py --header 生成，勿手改。 */\n")
        f.write("#ifndef BATTERY_SSDT_BLOBS_H\n#define BATTERY_SSDT_BLOBS_H\n\n")
        write_array(f, "g_battery_valid_ssdt", valid)
        write_array(f, "g_battery_bix_ssdt", bix)
        write_array(f, "g_battery_ec_ssdt", ec)
        write_array(f, "g_battery_ec_read_ssdt", ec_read)
        write_array(f, "g_battery_ec_glk_ssdt", ec_glk)
        write_array(f, "g_battery_absent_ssdt", absent)
        write_array(f, "g_battery_mutex_ssdt", mutex_tbl)
        write_array(f, "g_battery_dual_ssdt", dual)
        write_array(f, "g_battery_discharge_ssdt", discharge)
        write_array(f, "g_battery_dual_both_ssdt", dual_both)
        write_array(f, "g_battery_dual_fallback_ssdt", dual_fallback)
        write_array(f, "g_battery_compat_ssdt", compat)
        write_array(f, "g_battery_idxfield_io_ssdt", idx_io)
        write_array(f, "g_battery_idxfield_mem_ssdt", idx_mem)
        write_array(f, "g_battery_bankfield_ssdt", bank)
        write_array(f, "g_battery_idxfield_ec_ssdt", idx_ec)
        write_array(f, "g_battery_bankfield_ec_ssdt", bank_ec)
        write_array(f, "g_battery_idxfield_bad_ssdt", idx_bad)
        write_array(f, "g_battery_create_dword_ssdt", cf_dword)
        write_array(f, "g_battery_create_bad_ssdt", cf_bad)
        write_array(f, "g_battery_tolerant_ssdt", tolerant)
        f.write("\n#endif\n")
    print("wrote %s (valid=%d bix=%d ec=%d ec_read=%d ec_glk=%d absent=%d mutex=%d dual=%d discharge=%d dual_both=%d dual_fallback=%d compat=%d idx_io=%d idx_mem=%d bank=%d idx_ec=%d bank_ec=%d idx_bad=%d cf_dword=%d cf_bad=%d)" %
          (path, len(valid), len(bix), len(ec), len(ec_read), len(ec_glk),
           len(absent), len(mutex_tbl), len(dual), len(discharge), len(dual_both), len(dual_fallback), len(compat),
           len(idx_io), len(idx_mem), len(bank), len(idx_ec), len(bank_ec), len(idx_bad),
           len(cf_dword), len(cf_bad)))


def main(argv):
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    if argv[0] == "--header":
        if len(argv) < 2:
            print(__doc__, file=sys.stderr)
            return 2
        write_header(argv[1])
        return 0

    path = argv[0]
    mode = argv[1] if len(argv) > 1 else "declared"
    if mode not in MODES:
        print("未知模式: %s" % mode, file=sys.stderr)
        return 2
    pct = int(argv[2]) if len(argv) > 2 else 57
    table = MODES[mode](pct)
    with open(path, "wb") as f:
        f.write(table)
    print("wrote %s (%d bytes, mode=%s)" % (path, len(table), mode))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
