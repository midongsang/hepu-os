# ============================================================================
# 赫普OS (HEPU Hyper Evolve OS) 构建系统
# ============================================================================

# 工具链
CC      = gcc
LD      = ld
OBJCOPY = objcopy

# 编译选项 - 独立内核，不链接标准库
# V2.25: 诊断与正式合并为单一版本——默认开启 HEPU_BOOT_DEBUG,
# 使 KLOG启动标记/IDE自测/Shell自测等诊断能力无条件编译进正式内核,
# 不再区分诊断版与正式版双镜像.
# gcc 14 默认把 implicit-function-declaration / int-conversion / incompatible-pointer-types
# 都升级为 error。32-bit legacy 内核源码风格是传统 K&R 风（缺 include、参数类型不严格）。
# 本地 gcc 13 只 warning，CI gcc 14 直接 error。TODO(V0.55): 给 32-bit 内核补头文件。
CFLAGS  = -m32 \
          -Wno-implicit-function-declaration \
          -Wno-int-conversion \
          -Wno-incompatible-pointer-types \
          -ffreestanding -fno-builtin -fno-stack-protector \
          -nostdlib -nodefaultlibs -Wall -Wextra -O2 -fomit-frame-pointer \
          -fno-strict-aliasing -fno-tree-vectorize \
          -fno-strict-aliasing -ffunction-sections -fdata-sections -MMD -MP \
          -I kernel/include -I kernel -I kernel/shared -I services -DHEPU_BOOT_DEBUG -DHEPU_AUTO_FSCK
LDFLAGS = -m elf_i386 -T kernel/linker.ld --gc-sections
# GAS 汇编文件用 gcc 预处理编译
ASFLAGS = -m32 -ffreestanding -nostdlib -DHEPU_BOOT_DEBUG -DHEPU_AUTO_FSCK

# 目录
BUILD_DIR = build_v250
KERNEL_DIR = kernel

# 源文件
ASM_SRCS = $(KERNEL_DIR)/boot/boot.S \
           $(KERNEL_DIR)/arch/x86/interrupt.S \
           $(KERNEL_DIR)/arch/x86/paging.S \
           $(KERNEL_DIR)/arch/x86/process.S \
           $(KERNEL_DIR)/arch/x86/setjmp.S \
           $(KERNEL_DIR)/arch/x86/mainline.S

# 用户态程序 (单独编译)
USER_INIT_OBJ = $(BUILD_DIR)/user_init.o
USER_PROCB_OBJ = $(BUILD_DIR)/user_procb.o
USER_SCHA_OBJ  = $(BUILD_DIR)/user_schtest_a.o
USER_SCHB_OBJ  = $(BUILD_DIR)/user_schtest_b.o

# initrd 镜像 (包含 .hug 应用)
INITRD_IMG = $(BUILD_DIR)/initrd.img
# V2.01: debug独立构建目录时, initrd固定复用主build/initrd.img
# (kernel.c以_binary_build_initrd_img_start/end引用; 必须用相对路径
#  build/initrd.img, 若用绝对路径objcopy生成符号带完整目录名→不匹配)
ifeq ($(BUILD_DIR),$(CURDIR)/build/debug)
INITRD_IMG = build/initrd.img
endif
INITRD_OBJ = $(BUILD_DIR)/initrd.o

# V2.01: scheduler.c为历史死代码(被process.c/kernel_core.c取代), 显式排除不编译
# V0.10.133: 排除x86_64目录(64位特定代码), 避免32位编译错误
C_SRCS = $(filter-out $(KERNEL_DIR)/sched/scheduler.c $(KERNEL_DIR)/arch/x86_64/%,$(shell find $(KERNEL_DIR) -name "*.c"))

# 目标文件
ASM_OBJS = $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.o,$(ASM_SRCS))
C_OBJS   = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
OBJS     = $(ASM_OBJS) $(C_OBJS) $(USER_INIT_OBJ) $(USER_PROCB_OBJ) \
           $(USER_SCHA_OBJ) $(USER_SCHB_OBJ) $(INITRD_OBJ)

# 输出
KERNEL_BIN = $(BUILD_DIR)/hepu-kernel.bin
# V2.25: FUSE沙箱对 build/hepu-kernel.elf 路径有写白障(Operation not permitted),
# 真实ELF输出为 kernel.elf, 并在 dirs 建 hepu-kernel.elf -> kernel.elf 软链兼容脚本.
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
ISO        = $(BUILD_DIR)/hepu-os.iso

.PHONY: all clean run iso dirs hug-demo debug

all: dirs $(KERNEL_BIN)

# 诊断版内核 (HEPU_BOOT_DEBUG): 在_start/主函数加K0-K4 VGA+蜂鸣启动标记
# V2.01修复: 原debug目标仅追加ASFLAGS/CFLAGS但共享同一BUILD_DIR的.o,
# 增量构建时.o已存在且新于源码→make不重建→K0-K4诊断代码未编入内核bin
# (真机实测: 引导器P0/P1蜂鸣正常, 跳内核后K0的3次蜂鸣不响=诊断内核无K0).
# 修复: debug用独立构建目录build/debug(绝对路径), 与正式版.o彻底隔离;
# initrd复用主build/initrd.img以保持_binary_build_initrd_img_*符号与kernel.c一致.
debug:
	@$(MAKE) BUILD_DIR=$(CURDIR)/build/debug \
	    CFLAGS="$(CFLAGS) -DHEPU_BOOT_DEBUG -DHEPU_AUTO_FSCK" \
	    ASFLAGS="$(ASFLAGS) -DHEPU_BOOT_DEBUG -DHEPU_AUTO_FSCK" \
	    debug-all
	@cp $(BUILD_DIR)/debug/hepu-kernel.bin $(BUILD_DIR)/hepu-kernel-debug.bin
	@cp $(BUILD_DIR)/debug/kernel.elf $(BUILD_DIR)/hepu-kernel-debug.elf
	@echo "诊断版内核已生成: $(BUILD_DIR)/hepu-kernel-debug.bin"

# 内部: 在build/debug目录编译诊断内核(KERNEL_BIN受BUILD_DIR覆盖指向debug目录)
debug-all: dirs $(KERNEL_BIN)

# 创建构建目录
dirs:
	@mkdir -p $(BUILD_DIR)/boot
	@ln -sf kernel.elf $(BUILD_DIR)/hepu-kernel.elf 2>/dev/null || true
	@mkdir -p $(BUILD_DIR)/arch/x86
	@mkdir -p $(BUILD_DIR)/mm
	@mkdir -p $(BUILD_DIR)/sched
	@mkdir -p $(BUILD_DIR)/ipc
	@mkdir -p $(BUILD_DIR)/hal
	@mkdir -p $(BUILD_DIR)/lib
	@mkdir -p $(BUILD_DIR)/fs
	@mkdir -p $(BUILD_DIR)/net
	@mkdir -p $(BUILD_DIR)/drivers
	@mkdir -p $(BUILD_DIR)/apps
	@mkdir -p $(BUILD_DIR)/crypto
	@mkdir -p $(BUILD_DIR)/loader
	@mkdir -p $(BUILD_DIR)/drivers
	@mkdir -p $(BUILD_DIR)/security
	@mkdir -p $(BUILD_DIR)/kunit
	@mkdir -p $(BUILD_DIR)/fonts

# 编译汇编 (GAS 语法，用 gcc 预处理)
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S
	@echo "  AS  $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# 硬件寄存器结构体（AHCI/NVMe）必须packed，成员在硬件上实际对齐，
# 豁免取packed成员地址的对齐警告（业界通行做法，如Linux内核）
DRV_PACKED_CFLAGS = -Wno-address-of-packed-member

$(BUILD_DIR)/drivers/ahci.o: $(KERNEL_DIR)/drivers/ahci.c
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) $(DRV_PACKED_CFLAGS) -c $< -o $@

$(BUILD_DIR)/drivers/nvme.o: $(KERNEL_DIR)/drivers/nvme.c
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) $(DRV_PACKED_CFLAGS) -c $< -o $@

# 编译C
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# 编译用户态程序
$(USER_INIT_OBJ): user/init/user_init.S
	@echo "  AS  $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# 编译第二个用户态进程
$(USER_PROCB_OBJ): user/apps/procb/procb.S
	@echo "  AS  $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# V3.89-E27: 调度器动态时间片测试进程 A/B (ring3 汇编)
$(USER_SCHA_OBJ): kernel/sched/schtest_a.S
	@echo "  AS  $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

$(USER_SCHB_OBJ): kernel/sched/schtest_b.S
	@echo "  AS  $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

# 生成 initrd 镜像 (包含所有 .hug 应用)
$(INITRD_IMG): $(BUILD_DIR)/user/hello.hug $(BUILD_DIR)/user/hello_c.hug \
    $(BUILD_DIR)/user/vfs_test.hug $(BUILD_DIR)/user/ipc_test.hug \
    $(BUILD_DIR)/user/sysinfo.hug $(BUILD_DIR)/user/calculator.hug \
    $(BUILD_DIR)/user/calendar.hug $(BUILD_DIR)/user/notepad.hug \
    $(BUILD_DIR)/user/clock.hug $(BUILD_DIR)/user/filemgr.hug \
    $(BUILD_DIR)/user/diag.hug \
    $(BUILD_DIR)/user/top.hug $(BUILD_DIR)/user/spin.hug \
    $(BUILD_DIR)/linuxhello.elf $(BUILD_DIR)/linuxc.elf $(BUILD_DIR)/linuxbrk.elf $(BUILD_DIR)/linuxcrt.elf \
    $(BUILD_DIR)/winhello.exe $(BUILD_DIR)/winargs.exe $(BUILD_DIR)/macho_demo \
    $(BUILD_DIR)/loop.elf \
    $(BUILD_DIR)/crashdemo.elf $(BUILD_DIR)/crashud.elf \
    $(BUILD_DIR)/hello-rust.elf $(BUILD_DIR)/cat.elf $(BUILD_DIR)/echo.elf $(BUILD_DIR)/sysinfo.elf $(BUILD_DIR)/file-server.elf $(BUILD_DIR)/fs-client.elf $(BUILD_DIR)/ipc-e2e-test.elf $(BUILD_DIR)/fork-test.elf $(BUILD_DIR)/file-server-e2e-test.elf $(BUILD_DIR)/socket-test.elf $(BUILD_DIR)/app-store.elf $(BUILD_DIR)/log-service.elf \
    $(BUILD_DIR)/gfxtest.elf \
    $(BUILD_DIR)/test_hepu.txt
	@echo "  MKINITRD $@"
	@python3 tools/mkinitrd.py $@ $^ > /dev/null

# V4.02: Rust用户态应用——C核心+汇编最底层+Rust用户态技术路线验证
# 使用nightly Rust工具链，自定义hepu-i686目标，Linux i386 syscall ABI
RUST_TARGET := user/rust/hepu-i686.json
RUST_BUILD_DIR := user/rust/target/hepu-i686/debug
RUST_CARGO_FLAGS := -Z build-std=core,alloc -Z build-std-features=compiler-builtins-mem -Z json-target-spec --target ./hepu-i686.json

$(BUILD_DIR)/hello-rust.elf: user/rust/hello-rust/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO hello-rust"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/hello-rust $@

$(BUILD_DIR)/cat.elf: user/rust/cat/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO cat"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/cat $@

$(BUILD_DIR)/echo.elf: user/rust/echo/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO echo"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/echo $@

$(BUILD_DIR)/sysinfo.elf: user/rust/sysinfo/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO sysinfo"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/sysinfo $@

$(BUILD_DIR)/file-server.elf: user/rust/file-server/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO file-server"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/file-server $@

$(BUILD_DIR)/fs-client.elf: user/rust/fs-client/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO fs-client"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/fs-client $@

$(BUILD_DIR)/ipc-e2e-test.elf: user/rust/ipc-e2e-test/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO ipc-e2e-test"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/ipc-e2e-test $@

$(BUILD_DIR)/fork-test.elf: user/rust/fork-test/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO fork-test"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/fork-test $@

$(BUILD_DIR)/file-server-e2e-test.elf: user/rust/file-server-e2e-test/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO file-server-e2e-test"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/file-server-e2e-test $@

# V6.69: BSD Socket API测试
$(BUILD_DIR)/socket-test.elf: user/rust/socket-test/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO socket-test"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/socket-test $@

# V6.71: 应用商店
$(BUILD_DIR)/app-store.elf: user/rust/app-store/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO app-store"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/app-store $@

# V7.94: Rust日志服务
$(BUILD_DIR)/log-service.elf: user/rust/log-service/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO log-service"
	@cd user/rust && cargo build $(RUST_CARGO_FLAGS) 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/log-service $@

# V4.02: 图形模式测试应用 (VGA 320x200 256色, 通过/dev/fb0操作帧缓冲)
$(BUILD_DIR)/gfxtest.elf: user/apps/gfxtest/gfxtest.c
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -Wl,-e,_start -o $@ $<

# V3.87-E10: CPU 限额回收测试——用户态死循环(纯占 CPU, 不调 syscall)
$(BUILD_DIR)/loop.elf: user/apps/loop_demo/loop.c
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -Wl,-e,_start -o $@ $<

# V3.88-E18: 用户态异常隔离测试——除零(#DE) / 非法指令(#UD)
$(BUILD_DIR)/crashdemo.elf: user/apps/crash_demo/crash.c
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -Wl,-e,_start -o $@ $<
$(BUILD_DIR)/crashud.elf: user/apps/crash_demo/crash_ud.S
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -o $@ $<

# V3.87: Linux ELF32 兼容层测试程序 (裸 syscall: write + exit)
$(BUILD_DIR)/linuxhello.elf: user/apps/linux_demo/linuxhello.S
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -o $@ $<

# V3.87: Linux ELF32 兼容层 C 语言测试 (getpid/open/close + 沙箱验证)
$(BUILD_DIR)/linuxcrt.elf: user/apps/linux_crt_demo/linuxcrt.S user/apps/linux_crt_demo/linuxrt.c
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -Wl,-e,_start -o $@ $^

$(BUILD_DIR)/linuxbrk.elf: user/apps/linux_c_demo/linuxbrk.c
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -Wl,-e,_start -o $@ $<
$(BUILD_DIR)/linuxc.elf: user/apps/linux_c_demo/linuxc.c
	@echo "  LD  $<"
	@gcc -m32 -nostdlib -static -Wl,-e,_start -o $@ $<

# V3.87 阶段B: Win32 PE32 控制台测试程序 (gen_winhello.py 生成, 规范 PE 结构)
$(BUILD_DIR)/winhello.exe: user/apps/win32_demo/gen_winhello.py
	@echo "  GEN $@"
	@python3 user/apps/win32_demo/gen_winhello.py $@

# V3.87 阶段B-2: Win32 API 扩展演示程序 (GetStdHandle/GetCommandLineA/
# GetTickCount/Sleep, gen_winargs.py 生成, 两遍回填跳转)
$(BUILD_DIR)/winargs.exe: user/apps/win32_demo/gen_winargs.py
	@echo "  GEN $@"
	@python3 user/apps/win32_demo/gen_winargs.py $@

# V3.87 阶段C: 最小 32 位 x86 Mach-O 演示程序 (FEEDFACE, gen_macho.py 生成)
$(BUILD_DIR)/macho_demo: user/apps/macho_demo/gen_macho.py
	@echo "  GEN $@"
	@python3 user/apps/macho_demo/gen_macho.py $@

# 编译 libhepu C库
$(BUILD_DIR)/user/string.o: user/libhepu/string.c
	@mkdir -p $(BUILD_DIR)/user
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/stdio.o: user/libhepu/stdio.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/stdlib.o: user/libhepu/stdlib.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/unistd.o: user/libhepu/unistd.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/time.o: user/libhepu/time.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/math.o: user/libhepu/math.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/zlib.o: user/third_party/zlib/zlib.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/third_party/zlib -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/deflate.o: user/third_party/zlib/deflate.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/third_party/zlib -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/png.o: user/third_party/libpng/png.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/third_party/libpng -I user/third_party/zlib -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/jpeg.o: user/third_party/libjpeg/jpeg.c
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/third_party/libjpeg -I user/libhepu/include -c $< -o $@

$(BUILD_DIR)/user/crt0.o: user/libhepu/crt0.S
	@$(CC) -m32 -ffreestanding -nostdlib -c $< -o $@

# 编译C语言用户态应用并打包成.hug
$(BUILD_DIR)/user/hello_c.hug: user/apps/hello_c/hello.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/hello_c.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/hello_c.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/hello_c.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/hello_c.elf $(BUILD_DIR)/user/hello_c.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/hello_c.bin $@ > /dev/null

# 编译VFS测试应用
$(BUILD_DIR)/user/vfs_test.hug: user/apps/vfs_test/vfs_test.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/vfs_test.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/vfs_test.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/vfs_test.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/vfs_test.elf $(BUILD_DIR)/user/vfs_test.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/vfs_test.bin $@ > /dev/null

# 编译IPC测试应用
$(BUILD_DIR)/user/ipc_test.hug: user/apps/ipc_test/ipc_test.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/ipc_test.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/ipc_test.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/ipc_test.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/ipc_test.elf $(BUILD_DIR)/user/ipc_test.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/ipc_test.bin $@ > /dev/null

# V0.37 新增系统应用
$(BUILD_DIR)/user/sysinfo.hug: user/apps/sysinfo/sysinfo.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/sysinfo.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/sysinfo.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/sysinfo.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/sysinfo.elf $(BUILD_DIR)/user/sysinfo.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/sysinfo.bin $@ > /dev/null

$(BUILD_DIR)/user/calculator.hug: user/apps/calculator/calculator.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/calculator.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/calculator.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/calculator.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/calculator.elf $(BUILD_DIR)/user/calculator.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/calculator.bin $@ > /dev/null

$(BUILD_DIR)/user/calendar.hug: user/apps/calendar/calendar.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/calendar.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/calendar.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/calendar.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/calendar.elf $(BUILD_DIR)/user/calendar.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/calendar.bin $@ > /dev/null

$(BUILD_DIR)/user/notepad.hug: user/apps/notepad/notepad.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/notepad.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/notepad.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/notepad.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/notepad.elf $(BUILD_DIR)/user/notepad.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/notepad.bin $@ > /dev/null

# V0.38 新增系统应用
$(BUILD_DIR)/user/clock.hug: user/apps/clock/clock.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/clock.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/clock.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/clock.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/clock.elf $(BUILD_DIR)/user/clock.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/clock.bin $@ > /dev/null

$(BUILD_DIR)/user/filemgr.hug: user/apps/filemgr/filemgr.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/filemgr.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/filemgr.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/filemgr.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/filemgr.elf $(BUILD_DIR)/user/filemgr.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/filemgr.bin $@ > /dev/null

# V0.39 系统诊断应用
$(BUILD_DIR)/user/diag.hug: user/apps/diag/diag.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/diag.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/diag.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/diag.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/diag.elf $(BUILD_DIR)/user/diag.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/diag.bin $@ > /dev/null

# V0.40 性能监控应用
$(BUILD_DIR)/user/top.hug: user/apps/top/top.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/top.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/top.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/top.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/top.elf $(BUILD_DIR)/user/top.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/top.bin $@ > /dev/null

# V3.86: 死循环测试应用 (验证调度器超时强制回收)
$(BUILD_DIR)/user/spin.hug: user/apps/spin/spin.c \
    $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	  -I user/libhepu/include -c $< -o $(BUILD_DIR)/user/spin.o
	@$(LD) -m elf_i386 -Ttext=0x20000000 -o $(BUILD_DIR)/user/spin.elf \
	  $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/spin.o \
	  $(BUILD_DIR)/user/string.o $(BUILD_DIR)/user/stdio.o
	@$(OBJCOPY) -O binary $(BUILD_DIR)/user/spin.elf $(BUILD_DIR)/user/spin.bin
	@python3 tools/mkhug.py $(BUILD_DIR)/user/spin.bin $@ > /dev/null

# 编译 .hug 测试应用
$(BUILD_DIR)/user/hello.hug: user/apps/hello/hello.S
	@mkdir -p $(BUILD_DIR)/user
	@echo "  CC  $<"
	@$(CC) -m32 -ffreestanding -nostdlib -Wl,--oformat=binary -Wl,-Ttext=0x20000000 -o $(BUILD_DIR)/user/hello.bin $<
	@python3 tools/mkhug.py $(BUILD_DIR)/user/hello.bin $@ > /dev/null

# initrd 镜像转 ELF 目标文件
$(INITRD_OBJ): $(INITRD_IMG)
	@echo "  OBJCOPY $<"
	@$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.initrd,alloc,load,readonly,data,contents $< $@

# 链接内核 ELF
$(KERNEL_ELF): $(OBJS)
	@echo "  LD  $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

# 生成纯二进制内核
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "  BIN $@"
	@$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "=== 赫普OS 内核编译完成 ==="
	@ls -lh $(KERNEL_BIN)
	@echo ""

# 在 QEMU 中运行
run: $(KERNEL_BIN)
	@echo "启动 QEMU..."
	qemu-system-i386 -kernel $(KERNEL_BIN) -m 256 -serial stdio

# 生成 ISO 镜像（需要 grub-mkrescue）
iso: $(KERNEL_BIN)
	@mkdir -p $(BUILD_DIR)/iso/boot/grub
	@cp $(KERNEL_BIN) $(BUILD_DIR)/iso/boot/hepu-kernel.bin
	@cp grub.cfg $(BUILD_DIR)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(BUILD_DIR)/iso
	@echo "ISO 生成: $(ISO)"

# 构建 UEFI 版本四件套（HepuBoot UEFI 引导器）
uefi: $(KERNEL_BIN)
	@echo "构建 HepuBoot UEFI 版本..."
	@cp $(KERNEL_BIN) build/hepu-kernel.bin
	@bash tools/hepboot/uefi/build_uefi_image.sh

# 演示 .hug 打包
hug-demo:
	@echo "打包示例 .hug 应用..."
	python3 hug-sdk/tools/hugtool.py pack \
		hug-sdk/examples/hello-hepu \
		$(BUILD_DIR)/hello-hepu.hug
	@echo ""
	@python3 hug-sdk/tools/hugtool.py info $(BUILD_DIR)/hello-hepu.hug
	@echo ""
	@python3 hug-sdk/tools/hugtool.py verify $(BUILD_DIR)/hello-hepu.hug

clean:
	@rm -rf $(BUILD_DIR)
	@echo "已清理构建目录"

# 包含头文件依赖
-include $(C_OBJS:.o=.d)
