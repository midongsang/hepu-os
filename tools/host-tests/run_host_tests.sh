#!/bin/bash
# ============================================================================
# 赫普OS 主机端测试运行器（V0.33）
#
# 用途：把 tools/host-tests/ 下的 5 个主机端回归测试一次跑完，
#      全部用 ASan+UBSan 打开，编译失败或用例失败即返回非 0。
#
# 为什么需要它：docs/security/AUDIT_64BIT.md 里写着「4 个主机端测试(ASan/UBSan)
# 全部通过」，但仓库里没有可执行的入口 —— 这条验收长期处于「没法复现」状态。
# V0.33 补上这个缺口，让主机端回归成为可重复执行的命令。
#
# 用法: bash tools/host-tests/run_host_tests.sh
# ============================================================================
set -u
cd "$(dirname "$0")/../.."

ARCH=kernel/arch/x86_64
OUT=${TMPDIR:-/tmp}/hepu_host_tests
mkdir -p "$OUT"

CC=${CC:-gcc}
CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-common -w"
INC="-I$ARCH -I$ARCH/include -Ikernel/include -Ikernel/shared -Ikernel/shared/fs"

PASS=0; FAIL=0
ok()  { echo "  [ok]   $1"; PASS=$((PASS+1)); }
bad() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }

run_one() {
    local name="$1"; shift
    local extra_def="$1"; shift
    local src="$1"; shift
    local bin="$OUT/$name"

    if ! $CC $CFLAGS $extra_def $INC -o "$bin" "$src" "$@" 2>"$OUT/$name.build.log"; then
        bad "$name (编译失败)"
        sed -n '1,12p' "$OUT/$name.build.log" | sed 's/^/         /'
        return
    fi
    if "$bin" > "$OUT/$name.run.log" 2>&1; then
        ok "$name  ($(tail -1 "$OUT/$name.run.log" | cut -c1-60))"
    else
        bad "$name (运行退出码 $?)"
        tail -20 "$OUT/$name.run.log" | sed 's/^/         /'
    fi
}

echo "=== 赫普OS 主机端测试 ==="
echo "编译器: $($CC --version | head -1)"
echo ""

# 1) 用户空间访问安全（copy_from_user / copy_to_user / copy_str_from_user）
#    注意：该测试会真实执行用户态代码路径，历史上因无条件 STAC 直接 SIGILL
#    （SMAP 是 x86-64 可选特性，未置位时 STAC 非法）——V0.33 已按硬件真实状态门控。
run_one uaccess "-DHEPU_UACCESS_HOST_TEST" tools/host-tests/uaccess_test.c $ARCH/user_access64.c

# 2) 用户地址空间隔离（私有 PML4/PDPT、ring3 不可访问内核映射）
run_one aspace "-DHEPU_HOST_TEST" tools/host-tests/aspace_test.c $ARCH/user_aspace64.c $ARCH/vmm64.c

# 3) 内核堆（分配/释放/越界/双放/碎片）
run_one heap "-DKHEAP_HOST_TEST" tools/host-tests/heap_test.c $ARCH/kheap64.c

# 4) ELF64 加载器（校验/边界/重定位）
#    elf64.c 的 ABI 判定已委托 compat_gate.c（compat_gate_detect_elf），
#    链接清单必须带上 compat_gate.c；其进程表访问器由测试桩返回 ENV_NATIVE。
run_one elf "" tools/host-tests/elf_test.c $ARCH/elf64.c $ARCH/compat_gate.c

# 5) hpdesk 窗口槽位分配/回收（回归：反复开关 32 次后槽位耗尽无法再开窗）
run_one winslot "" tools/host-tests/winslot_test.c

# 5a) 电池图标状态映射（纯函数）
#     QEMU 夹具只能造出 ABSENT/DECLARED——没有 AML 执行器进不了 VALID。
#     填充比例这条分支靠这里覆盖，避免它长期是"没人跑过"的死代码。
run_one battery_ui "" tools/host-tests/battery_ui_test.c
# M2: 极简 AML 解释器 + 电池读数（Phase 2）
run_one aml_eval "-DAML_HOST_TEST" tools/host-tests/aml_eval_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: 虚拟 EC 电池端到端（init → _REG → _BST → poll），补 QEMU 无 EC 设备
run_one ec_battery "-DAML_HOST_TEST" tools/host-tests/ec_battery_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: _GLK=1 锁死策略（不实现 FACS 锁时不碰共享 EC）
run_one ec_glk "-DAML_HOST_TEST" tools/host-tests/ec_glk_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: 双电池槽位选择（BAT0 absent 时不遮住 BAT1）
run_one multi_battery "-DAML_HOST_TEST" tools/host-tests/multi_battery_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: 健康度/剩余时间换算（1200mW 放电 -> 285 分钟）
run_one battery_energy "-DAML_HOST_TEST" tools/host-tests/battery_energy_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: 两块都 present 时的容量合并（30% + 70% -> 50%）
run_one battery_merge "-DAML_HOST_TEST" tools/host-tests/battery_merge_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: 第一个 present 槽位 _BST 失败时回退到第二个
run_one battery_fallback "-DAML_HOST_TEST" tools/host-tests/battery_fallback_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: 3 元素 _BST + 全 0 _BIX 回退 _BIF
run_one battery_compat "-DAML_HOST_TEST" tools/host-tests/battery_compat_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-full: IndexField/BankField 读取（IO/内存正路径 + EC fail-closed 负路径）
run_one idx_bank_field "-DAML_HOST_TEST" tools/host-tests/idx_bank_field_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c
# M2-cf: Create*Field 缓冲拆包 + 建库容错/诊断
run_one create_field "-DAML_HOST_TEST" tools/host-tests/create_field_test.c $ARCH/aml64.c $ARCH/acpi_battery.c $ARCH/ec64.c

# 5b) PNG 解码器（inflate/反滤波/颜色类型/位深，V0.54.13 P0）
#     夹具：tools/host-tests/data/png_case{1..5}.png + .expect，逐像素比对
if $CC $CFLAGS -DPNG64_HOST $INC -o "$OUT/png64" \
     tools/host-tests/png_test.c $ARCH/png64.c 2>"$OUT/png64.build.log"; then
    PNGFAIL=0
    for n in 1 2 3 4 5; do
        if ! "$OUT/png64" tools/host-tests/data/png_case$n.png \
             tools/host-tests/data/png_case$n.expect > "$OUT/png64.$n.log" 2>&1; then
            PNGFAIL=1
            bad "png64 case$n ($(tail -1 "$OUT/png64.$n.log" | cut -c1-60))"
        fi
    done
    [ "$PNGFAIL" -eq 0 ] && ok "png64 (5 个用例全通过: RGBA/RGB/调色板/灰度8/灰度1bit)"
else
    bad "png64 (编译失败)"
    sed -n '1,12p' "$OUT/png64.build.log" | sed 's/^/         /'
fi

# 6) ext2 共享算法（需要镜像夹具：现场生成，测试内含相对路径）
if command -v mkfs.ext2 >/dev/null 2>&1 && command -v debugfs >/dev/null 2>&1; then
    FIX="$OUT/ext2_fix"
    if bash tools/host-tests/gen_ext2_fixtures.sh "$FIX" >/dev/null 2>&1; then
        if $CC $CFLAGS $INC -o "$OUT/ext2" tools/host-tests/ext2_test.c kernel/shared/fs/ext2_algo.c 2>"$OUT/ext2.build.log"; then
            if (cd "$FIX" && "$OUT/ext2" > "$OUT/ext2.run.log" 2>&1); then
                ok "ext2  ($(tail -1 "$OUT/ext2.run.log" | cut -c1-60))"
            else
                bad "ext2 (运行退出码 $?)"
                tail -20 "$OUT/ext2.run.log" | sed 's/^/         /'
            fi
        else
            bad "ext2 (编译失败)"
            sed -n '1,12p' "$OUT/ext2.build.log" | sed 's/^/         /'
        fi
    else
        bad "ext2 (夹具生成失败)"
    fi
else
    bad "ext2 (缺少 mkfs.ext2 / debugfs，跳过)"
fi

echo ""
# 6b) ext2 fsck inode 检查（V0.55.2: check_inode 接入 check_inode_tables 遍历）
if $CC $CFLAGS $INC -o "$OUT/fsck" tools/host-tests/fsck_test.c 2>"$OUT/fsck.build.log"; then
    if "$OUT/fsck" > "$OUT/fsck.run.log" 2>&1; then
        ok "fsck  ($(tail -1 "$OUT/fsck.run.log" | cut -c1-60))"
    else
        bad "fsck (运行退出码 $?)"
        tail -20 "$OUT/fsck.run.log" | sed 's/^/         /'
    fi
else
    bad "fsck (编译失败)"
    sed -n '1,12p' "$OUT/fsck.build.log" | sed 's/^/         /'
fi

# 7) 桌面 UTF-8 文本工具（M0：码点前进/后退、列宽、有界追加）
run_one utf8 "" tools/host-tests/utf8_text_test.c $ARCH/hs_text_utf8.c

# 8) IME 核心（M1：无存储依赖的拼音组词/候选/上屏）
run_one ime_core "" tools/host-tests/ime_core_test.c \
    kernel/shared/ime/ime.c kernel/shared/ime/ime_data.c kernel/shared/ime/ime_data_table.c

# 9) IME 常用样本首屏命中率（M3：500 字 / 100 词组）
run_one ime_firstpage "" tools/host-tests/ime_firstpage_test.c \
    kernel/shared/ime/ime.c kernel/shared/ime/ime_data.c kernel/shared/ime/ime_data_table.c

echo "=== 结果: 通过 $PASS / 失败 $FAIL ==="
[ "$FAIL" -eq 0 ] || exit 1
exit 0
