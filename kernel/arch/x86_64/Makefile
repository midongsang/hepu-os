# ============================================================================
# 赫普OS - 64位内核Makefile
# V0.10.6: 初始64位内核构建系统
# ============================================================================

# 默认目标: 显式指定, 避免 pattern 规则插在前面时把默认目标带偏
.DEFAULT_GOAL := all

# 工具链
CC = gcc
LD = ld
AS = gcc
OBJCOPY = objcopy

# 目录（从本 Makefile 位置推导，可换目录直接构建）
PROJECT_ROOT = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
KERNEL_DIR = $(PROJECT_ROOT)/kernel
ARCH_DIR = $(KERNEL_DIR)/arch/x86_64
BUILD_DIR = $(PROJECT_ROOT)/build_x86_64

# 源文件（核心文件，避免依赖32位内核）
C_SOURCES = \
    $(KERNEL_DIR)/kernel_main_64.c \
    $(ARCH_DIR)/lib64.c \
    $(ARCH_DIR)/kernel_contracts.c \
    $(ARCH_DIR)/pmm64.c \
    $(ARCH_DIR)/vmm64.c \
    $(ARCH_DIR)/gdt64.c \
    $(ARCH_DIR)/irq64_simple.c \
    $(ARCH_DIR)/syscall64_simple.c \
    $(ARCH_DIR)/syscall64_full.c \
    $(ARCH_DIR)/compat_gate.c \
    $(ARCH_DIR)/fs_dir_sys.c \
    $(ARCH_DIR)/fs_s4_selftest.c \
    $(ARCH_DIR)/fs_stat_sys.c \
    $(ARCH_DIR)/fs_open_sys.c \
    $(ARCH_DIR)/fs_mm_sys.c \
    $(ARCH_DIR)/fs_rw_sys.c \
    $(ARCH_DIR)/seccomp64.c \
    $(ARCH_DIR)/ide64.c \
    $(ARCH_DIR)/keyboard64.c \
    $(ARCH_DIR)/rtc64.c \
    $(ARCH_DIR)/ext2_64.c \
    $(ARCH_DIR)/hs_fat32.c \
    $(ARCH_DIR)/pci64.c \
    $(ARCH_DIR)/rtl8139_64.c \
    $(ARCH_DIR)/e1000_64.c \
    $(ARCH_DIR)/gfx64.c \
    $(ARCH_DIR)/display_mirror.c \
    $(ARCH_DIR)/cursor64.c \
    $(ARCH_DIR)/cursor_modern_data.c \
    $(ARCH_DIR)/png64.c \
    $(ARCH_DIR)/jpeg64.c \
    $(ARCH_DIR)/vg64.c \
    $(ARCH_DIR)/net64.c \
    $(ARCH_DIR)/net64_eth.c \
    $(ARCH_DIR)/net64_arp.c \
    $(ARCH_DIR)/net64_ip.c \
    $(ARCH_DIR)/net64_icmp.c \
    $(ARCH_DIR)/net64_udp.c \
    $(ARCH_DIR)/net64_tcp.c \
    $(ARCH_DIR)/net64_dhcp.c \
    $(ARCH_DIR)/net64_dns.c \
    $(ARCH_DIR)/net64_http.c \
    $(ARCH_DIR)/process64_full.c \
    $(ARCH_DIR)/domain64.c \
    $(ARCH_DIR)/capability64.c \
    $(ARCH_DIR)/devfs64.c \
    $(ARCH_DIR)/vfs64.c \
    $(ARCH_DIR)/vfs64_ext2.c \
    $(ARCH_DIR)/vfs64_dev.c \
    $(ARCH_DIR)/hepu_fs64.c \
    $(ARCH_DIR)/security64.c \
    $(ARCH_DIR)/auth64.c \
    $(ARCH_DIR)/oobe64.c \
    $(ARCH_DIR)/hepua.c \
    $(ARCH_DIR)/hepu_dev.c \
    $(ARCH_DIR)/shell64.c \
    $(ARCH_DIR)/shell_cmd_admin64.c \
    $(ARCH_DIR)/desktop64.c \
    $(ARCH_DIR)/bootopts64.c \
    $(ARCH_DIR)/user_process.c \
    $(ARCH_DIR)/uproc_loader64.c \
    $(ARCH_DIR)/uthread64.c \
    $(ARCH_DIR)/user_aspace64.c \
    $(ARCH_DIR)/user_lib.c \
    $(ARCH_DIR)/user_app_test.c \
    $(ARCH_DIR)/elf64.c \
    $(ARCH_DIR)/drv_register64.c \
    $(ARCH_DIR)/drv_mgr64.c \
    $(ARCH_DIR)/registry64.c \
    $(ARCH_DIR)/gps64.c \
    $(ARCH_DIR)/disk64.c \
    $(ARCH_DIR)/file_assoc64.c \
    $(ARCH_DIR)/l10n64.c \
    $(ARCH_DIR)/kmod64.c \
    $(ARCH_DIR)/storage_hal64.c \
    $(ARCH_DIR)/blk_cache64.c \
    $(ARCH_DIR)/swap64.c \
    $(ARCH_DIR)/network_hal64.c \
    $(ARCH_DIR)/socket64.c \
    $(ARCH_DIR)/ext2_fsck64.c \
    $(KERNEL_DIR)/shared/drivers/driver_framework.c \
    $(KERNEL_DIR)/shared/gfx/gfx_algo.c \
    $(KERNEL_DIR)/shared/fs/ext2_algo.c \
    $(KERNEL_DIR)/shared/fs/ext2_dir_algo.c \
    $(KERNEL_DIR)/shared/fs/fs_path.c \
    $(KERNEL_DIR)/shared/lib/lib_shared.c \
    $(KERNEL_DIR)/shared/debug/debug_shared.c \
    $(KERNEL_DIR)/shared/desktop/desk_registry.c \
    $(KERNEL_DIR)/shared/desktop/desk_surface.c \
    $(KERNEL_DIR)/shared/desktop/desk_hpdesk.c \
    $(KERNEL_DIR)/shared/desktop/desk_runtime.c \
    $(KERNEL_DIR)/shared/desktop/desk_demo.c \
    $(KERNEL_DIR)/crypto/crypto.c \
    $(KERNEL_DIR)/crypto/aes.c \
    $(KERNEL_DIR)/crypto/aes_gcm.c \
    $(KERNEL_DIR)/crypto/sha.c \
    $(KERNEL_DIR)/crypto/sha512.c \
    $(KERNEL_DIR)/crypto/hmac.c \
    $(KERNEL_DIR)/crypto/hkdf.c \
    $(KERNEL_DIR)/crypto/rsa.c \
    $(KERNEL_DIR)/crypto/ecdhe.c \
    $(KERNEL_DIR)/crypto/csprng.c \
    $(KERNEL_DIR)/crypto/x509.c \
    $(KERNEL_DIR)/net/tls.c \
    $(KERNEL_DIR)/net/tls_record.c \
    $(KERNEL_DIR)/net/tls_crypto.c \
    $(KERNEL_DIR)/net/tls_handshake.c \
    $(KERNEL_DIR)/net/tls_client.c \
    $(KERNEL_DIR)/net/tls12_client.c \
    $(KERNEL_DIR)/net/tls12_e2e_selftest.c \
    $(ARCH_DIR)/ps2mouse64.c \
    $(ARCH_DIR)/usb64.c \
    $(ARCH_DIR)/ehci64.c \
    $(ARCH_DIR)/xhci64.c \
    $(ARCH_DIR)/hsinput.c \
    $(ARCH_DIR)/hstouch.c \
    $(ARCH_DIR)/ahci64.c \
    $(ARCH_DIR)/font64.c \
    $(ARCH_DIR)/ttf64.c \
    $(ARCH_DIR)/font_data.c \
    $(ARCH_DIR)/cjk_font_data.c \
    $(ARCH_DIR)/window64.c \
    $(ARCH_DIR)/taskmgr64.c \
    $(ARCH_DIR)/filemgr64.c \
    $(ARCH_DIR)/syssettings64.c \
    $(ARCH_DIR)/notes64.c \
    $(ARCH_DIR)/term64.c \
    $(ARCH_DIR)/appstore64.c \
    $(ARCH_DIR)/kheap64.c \
    $(ARCH_DIR)/memory_pressure.c \
    $(ARCH_DIR)/partition64.c \
    $(ARCH_DIR)/user_access64.c \
    $(ARCH_DIR)/user_stack64.c \
    $(ARCH_DIR)/pit64.c \
    $(ARCH_DIR)/apic64.c \
    $(ARCH_DIR)/x86_64.c \
    $(ARCH_DIR)/spinlock.c \
    $(ARCH_DIR)/smp.c \
    $(ARCH_DIR)/acpi.c \
    $(ARCH_DIR)/firmware_info.c \
    $(ARCH_DIR)/aml64.c \
    $(ARCH_DIR)/acpi_battery.c \
    $(ARCH_DIR)/uart64.c \
    $(ARCH_DIR)/ac9764.c \
    $(ARCH_DIR)/hs_apps.c \
    $(ARCH_DIR)/hs_systools64.c \
    $(KERNEL_DIR)/shared/log/event_log.c \
    $(KERNEL_DIR)/sched/services.c \
    $(ARCH_DIR)/klog64.c \
    $(ARCH_DIR)/hs_oobe64.c \
    $(ARCH_DIR)/arch_port_x86_64.c \
    $(ARCH_DIR)/epoll64.c \
    $(ARCH_DIR)/hpdesk.c \
    $(ARCH_DIR)/icons_win_data.c \
    $(ARCH_DIR)/hsqml.c \
    $(ARCH_DIR)/hs_ui.c \
    $(ARCH_DIR)/hsipc.c \
    $(ARCH_DIR)/hsservices.c \
    $(ARCH_DIR)/hsextras.c \
    $(ARCH_DIR)/math64.c

ASM_SOURCES = \
    $(ARCH_DIR)/multiboot_entry.S \
    $(ARCH_DIR)/entry_64.S \
    $(ARCH_DIR)/user_switch.S \
    $(ARCH_DIR)/kjmp64.S

# 对象文件（放在build目录下）
# ---- 图形栈边界守卫 (GRAPHICS_BOUNDARY.md) ----
# 64 位的图形实现只有三处: gfx64.c / desktop64.c / shared/gfx/gfx_algo.c(纯算法)。
# kernel/gfx/* (18 个 vga/vbe/gop/virtio 后端)、arch/x86/desktop_render.c、
# vga_gfx.c、vga_cjk.c 全部属于 32 位内核。若 64 位构建意外引入它们,
# 不要静默编进去 —— 直接让构建失败。
GFX32_IN_64 := $(filter $(KERNEL_DIR)/gfx/% %/desktop_render.c %/vga_gfx.c \
                       %/vga_cjk.c %/elastic_desktop.c %/boot_gfx.c \
                       %/gfx_selftest.c %/vga.c,$(C_SOURCES))
ifneq ($(strip $(GFX32_IN_64)),)
$(error 图形栈边界被打破: 64 位构建引入了 32 位图形实现 -> $(GFX32_IN_64))
endif

C_OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(C_SOURCES:.c=.o)))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(ASM_SOURCES:.S=.o)))

# VPATH用于查找源文件
vpath %.c $(KERNEL_DIR) $(ARCH_DIR) $(KERNEL_DIR)/lib $(KERNEL_DIR)/shared/gfx $(KERNEL_DIR)/shared/drivers $(KERNEL_DIR)/shared/fs $(KERNEL_DIR)/shared/lib $(KERNEL_DIR)/shared/debug $(KERNEL_DIR)/shared/desktop $(KERNEL_DIR)/shared/log $(KERNEL_DIR)/sched $(KERNEL_DIR)/crypto $(KERNEL_DIR)/net
vpath %.S $(ARCH_DIR)


# ============================================================================
# 编译选项
# -Os: stage2 低端内存只留 640KB 内核区(0x10000-0x9FFFF), 必须控制内核体积;
#      -Os 相比 -O2 约省 78KB (V0.55 P3-GUI 新增向导后仍留有安全余量)
CFLAGS = \
    -m64 \
    -mcmodel=small \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -ffreestanding \
    -fstack-protector-all \
    -mstack-protector-guard=global \
    -fno-pic \
    -fno-pie \
    -fcf-protection=none \
    -fno-strict-aliasing \
    -fwrapv \
    -Os \
    -ffunction-sections     -fdata-sections \
    -Wall \
    -Wextra \
    -I$(KERNEL_DIR)/include \
    -I$(KERNEL_DIR) \
    -I$(ARCH_DIR) \
    -I$(ARCH_DIR)/include \
    -I$(KERNEL_DIR)/shared \
    -I$(KERNEL_DIR)/shared/fs \
    -I$(KERNEL_DIR)/shared/lib \
    -I$(KERNEL_DIR)/shared/debug \
    -D__x86_64__ \
    -DHEPU_KERNEL_64

# 可选：启动时自动运行 sec_test 自检电池（含 /bin/sec_test.elf 等用户程序）。
# 默认关闭：本 ifeq 不成立时不定义 HEPU_BOOT_SELFTEST，源码侧
# kernel_main_64.c 的 `#if defined(HEPU_BOOT_SELFTEST) && HEPU_BOOT_SELFTEST`
# 守卫因此整段不编入发布内核。需要跑电池： make BOOT_SELFTEST=1。
# 注意：这是编译期开关，不是运行时常驻探针；默认构建串口不应出现
# PIEPROBE/FBSYN/FBDESC/STATP 等电池输出。
ifeq ($(BOOT_SELFTEST),1)
CFLAGS += -DHEPU_BOOT_SELFTEST
endif

# P4/S4：只运行用户态 getdents 非根目录探针（不拉起整套启动自检电池）。
# 64 位镜像级回归 tools/regress64.sh 用 S4_SELFTEST=1 构建。
ifeq ($(S4_SELFTEST),1)
CFLAGS += -DHEPU_S4_SELFTEST
endif

# TLS 1.2 端到端开机自检（默认关闭；仅联调测试构建用 TLS_E2E_SELFTEST=1 打开）。
# 打开后在 DHCP 就绪后自动跑一次真实握手 + 加密 GET，结果打串口。
# 默认构建（gate.sh / regress64.sh）不进这段代码，不影响回归。
ifeq ($(TLS_E2E_SELFTEST),1)
CFLAGS += -DHEPU_TLS_E2E_SELFTEST
endif

# ATA 高 LBA 写读回环自检（desktop64.c 内 #ifdef HEPU_ATA_HILBA_TEST）。
# 发布构建默认关闭；挂独立 slave 测试盘复验时： make ATA_HILBA_TEST=1
ifeq ($(ATA_HILBA_TEST),1)
CFLAGS += -DHEPU_ATA_HILBA_TEST
endif

# OPEN-02 边界①：usb-storage 分段遍历读校验 + 单扇区写读回环（usb64.c 内
# #ifdef HEPU_MSC_SWEEP_TEST）。发布构建默认关闭；挂独立 usb-storage 测试盘
# 复验时： make MSC_SWEEP_TEST=1。
ifeq ($(MSC_SWEEP_TEST),1)
CFLAGS += -DHEPU_MSC_SWEEP_TEST
endif

# OPEN-02 边界④：usb-storage 全盘逐扇区读校验（多扇区 BOT 命令，16 扇区/条）。
# 发布构建默认关闭；取证时 make MSC_FULLREAD_TEST=1。
ifeq ($(MSC_FULLREAD_TEST),1)
CFLAGS += -DHEPU_MSC_FULLREAD_TEST
endif

# OPEN-06 验收探针：默认关闭；仅取证构建用 make SVC_PROBE=1 打开，
# 注册 svc-probe 夹具（前 3 次启动失败、第 4 次成功）。发布构建不编入。
ifeq ($(SVC_PROBE),1)
CFLAGS += -DHEPU_SVC_PROBE
endif

# OPEN-05 取证夹具：默认关闭；仅取证构建用 make EMERG_PROBE=1 打开，
# 启动时向 emergency 异常环写一条探针条目，供 `journal` 验证异常环并入输出。
# 发布构建不编入。
ifeq ($(EMERG_PROBE),1)
CFLAGS += -DHEPU_EMERG_PROBE
endif

# Linux 兼容进程的 syscall 跟踪（调试用，默认关）。
# 打开： make HEPU_TRACE_LINUX_SYSCALL=1
ifeq ($(HEPU_TRACE_LINUX_SYSCALL),1)
CFLAGS += -DHEPU_TRACE_LINUX_SYSCALL
endif

# 用户地址空间隔离（私有页表 + 内核映射 ring3 不可访问，见 user_aspace64.c）。
# QEMU 回归已通过（正常启动/桌面/sec_test/连续10次exec无泄漏、槽位复用），默认开启；
# 如需临时关闭： make USER_ISOLATION=0
USER_ISOLATION ?= 1
ifneq ($(USER_ISOLATION),0)
CFLAGS += -DHEPU_USER_ISOLATION=1
endif

# V0.54.8: kernel_main_64.c 的 implicit declaration 已补全 (tss64_init, ide64_read_sector),
# 但其他模块 (syscall64_full.c, ext2_64.c, gfx64.c 等) 仍有隐式声明 — CI gcc 14 默认当 error.
# 先加回 -Wno-implicit-function-declaration, TODO(V0.55) 逐个模块彻底补头文件.
CFLAGS += -Wno-implicit-function-declaration

ASFLAGS = \
    -m64 \
    -mno-red-zone \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pic \
    -fno-pie \
    -I$(KERNEL_DIR)/include \
    -I$(ARCH_DIR)

LDFLAGS = \
    -nostdlib \
    --gc-sections \
    -T $(ARCH_DIR)/x86_64.ld \
    -z max-page-size=0x1000

# 目标
TARGET = $(BUILD_DIR)/hepu-kernel64.bin
ELF_TARGET = $(BUILD_DIR)/hepu-kernel64.elf

# 默认目标

# B组: Lua 5.4 接入 (third_party/lua)
#
# 这些文件和内核其余部分**不能**一起用 -mno-sse 编: x86_64 SysV 的浮点 ABI
# 规定 double 走 xmm0, 禁 SSE 后编译器直接报 "SSE register return with SSE
# disabled"。所以 Lua 与 klibm64 单独用 -msse2, 二进制里它们才互相对得上。
# 它们也不引内核头(内核 include 里的宏会和标准头打架), 只用 third_party/lua/include
# 那套极简兼容头。
# ============================================================================
LUA_SRC = $(PROJECT_ROOT)/third_party/lua/src
LUA_INC = $(PROJECT_ROOT)/third_party/lua/include
LUA_CORE = lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject \
           lopcodes lparser lstate lstring ltable ltm lundump lvm lzio \
           lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib

LUA_CFLAGS = -m64 -mcmodel=small -mno-red-zone -msse2 \
             -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
             -fcf-protection=none -fno-strict-aliasing -fwrapv -Os \
             -ffunction-sections -fdata-sections \
             -I$(LUA_SRC) -I$(LUA_INC) -D__x86_64__ \
             -DLUA_USER_H='"lua64_user.h"'

LUA_OBJECTS = $(addprefix $(BUILD_DIR)/lua_, $(addsuffix .o, $(LUA_CORE))) \
              $(BUILD_DIR)/lua64.o $(BUILD_DIR)/klibm64.o

$(BUILD_DIR)/lua_%.o: $(LUA_SRC)/%.c | $(BUILD_DIR)
	@$(CC) $(LUA_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lua64.o: $(ARCH_DIR)/lua64.c | $(BUILD_DIR)
	@$(CC) $(LUA_CFLAGS) -I$(ARCH_DIR) -c $< -o $@

$(BUILD_DIR)/klibm64.o: $(ARCH_DIR)/klibm64.c | $(BUILD_DIR)
	@$(CC) $(LUA_CFLAGS) -c $< -o $@

all: $(TARGET)

# 创建构建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译C文件
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  [CC] $<"

# 编译shared目录的C文件
$(BUILD_DIR)/driver_framework.o: $(KERNEL_DIR)/shared/drivers/driver_framework.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  [CC] $< (shared)"

# 编译汇编文件
$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@
	@echo "  [AS] $<"

# 链接为ELF
$(ELF_TARGET): $(C_OBJECTS) $(ASM_OBJECTS) $(LUA_OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(C_OBJECTS) $(ASM_OBJECTS) $(LUA_OBJECTS)
	@echo "  [LD] $@"

# 转换为二进制
$(TARGET): $(ELF_TARGET)
	$(OBJCOPY) -O binary $< $@
	@echo "  [OBJCOPY] $@"
	@ls -lh $@

# 清理
clean:
	rm -rf $(BUILD_DIR)

# 显示信息
info:
	@echo "=== HepuOS 64-bit Kernel Build Info ==="
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "C_SOURCES: $(C_SOURCES)"
	@echo "C_OBJECTS: $(C_OBJECTS)"
	@echo "ASM_OBJECTS: $(ASM_OBJECTS)"
	@echo "Target: $(TARGET)"
	@echo "======================================="

.PHONY: all clean info
