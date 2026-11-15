#!/bin/bash
# 图形栈边界自检 —— 32 位图形实现与 64 位图形实现必须互不混入。
# 判据见 docs/architecture/GRAPHICS_BOUNDARY.md。
#
# 用法: ./tools/check_gfx_boundary.sh
# 退出码: 0 = 边界完好, 1 = 边界被打破

cd "$(dirname "$0")/.." || exit 1
FAIL=0
say() { printf '%s\n' "$1"; }

# ---- 判据 1: 64 位构建不得编入 32 位图形实现 ----
MK64=kernel/arch/x86_64/Makefile
# 排除守卫宏自身的定义行（那是"禁止清单"，不是"编入清单"）
leak=$(grep -E '\$\(KERNEL_DIR\)/gfx/' "$MK64" | grep -v 'gfx_algo' | grep -v 'GFX32_IN_64' || true)
if [ -n "$leak" ]; then
    say "[BOUND] FAIL(kernel/gfx): 64 位 Makefile 引用了 32 位图形目录"
    say "        $leak"
    FAIL=1
else
    say "[BOUND] ok: 64 位 Makefile 不含 kernel/gfx/"
fi

# 守卫宏是否存在（构建期拦截）
if grep -q 'GFX32_IN_64' "$MK64"; then
    say "[BOUND] ok: 构建期守卫 GFX32_IN_64 在位"
else
    say "[BOUND] FAIL(guard): 64 位 Makefile 缺少图形边界守卫"
    FAIL=1
fi

# ---- 判据 2: 共享图形层必须纯算法，不含任何硬件细节 ----
SHARED=kernel/shared/gfx
# 只认真正的硬件访问形态：端口 IO / 固定物理地址。
# 不检后端枚举名（GFX_BACKEND_VGA/VBE 属于契约词汇，不是硬件细节）。
hw=$(grep -rn -E 'outb\(|outl\(|inb\(|inl\(|0x3C0|0x3C4|0x3CE|0x3D4|0xA0000|0xB8000' "$SHARED" --include=*.c --include=*.h || true)
if [ -n "$hw" ]; then
    say "[BOUND] FAIL(shared): shared/gfx 含硬件细节"
    say "$hw"
    FAIL=1
else
    say "[BOUND] ok: shared/gfx 纯算法（无 outb/端口/物理地址）"
fi

# ---- 判据 3: 32 位图形文件必须留在 32 位目录 ----
missing=0
for f in kernel/gfx/gfx_core.c kernel/gfx/gfx_vga.c kernel/gfx/gfx_vbe.c \
         kernel/arch/x86/vga_gfx.c kernel/drivers/vga_cjk.c; do
    [ -f "$f" ] || { say "[BOUND] FAIL(missing): $f 不见了"; missing=1; FAIL=1; }
done
# desktop_render.c(旧 32 位桌面) 已按 D-7 迁出内核 -> archive/legacy-desktop/。
# 判据的本意是"32 位图形实现不得混进 64 位目录", 迁出内核同样合格;
# 但若它出现在 x86_64 目录, 那就是混入, 直接判 FAIL。
if [ -f kernel/arch/x86_64/desktop_render.c ]; then
    say "[BOUND] FAIL(leak): 旧桌面 desktop_render.c 出现在 64 位目录"
    FAIL=1
elif [ -f kernel/arch/x86/desktop_render.c ]; then
    say "[BOUND] ok: desktop_render.c 仍在 32 位目录"
else
    [ -f archive/legacy-desktop/desktop_render.c ] || {
        say "[BOUND] FAIL(lost): desktop_render.c 既不在 32 位目录也不在 archive/"; FAIL=1; }
    [ -f archive/legacy-desktop/desktop_render.c ] && \
        say "[BOUND] ok: desktop_render.c 已迁出内核(archive/legacy-desktop)"
fi
[ "$missing" = "0" ] && say "[BOUND] ok: 其余 32 位图形实现全部在 32 位目录里"

# ---- 判据 4: 64 位图形实现只有三处 ----
n64=$(grep -c -E 'gfx64\.c|desktop64\.c|gfx_algo\.c' "$MK64")
if [ "$n64" -ge 3 ]; then
    say "[BOUND] ok: 64 位图形 = gfx64.c + desktop64.c + shared/gfx/gfx_algo.c"
else
    say "[BOUND] FAIL(count): 64 位图形实现文件数异常 ($n64)"
    FAIL=1
fi

if [ "$FAIL" = "0" ]; then
    say "[BOUND] === 图形栈边界完好 ==="
else
    say "[BOUND] === 图形栈边界被打破 ==="
fi
exit "$FAIL"
