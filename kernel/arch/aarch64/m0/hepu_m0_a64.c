/* Hepu OS — ARM64 M0 里程碑（EFI 应用）
 *
 * 目标（docs/planning/MULTIARCH_PORT_EVALUATION.md §6.2 的第一段）：
 *   UEFI 引导 → 拿到 GOP 帧缓冲 → 串口输出 → 整屏填色（证明能出图）→ 关机
 * 这一版跑在固件环境（还没进 EL1 内核，页表/异常级别在 M1 做）。
 *
 * 只依赖 gnu-efi + PL011 直写，不用任何 libc。
 */
#include <efi.h>
#include <efilib.h>

/* ---------- PL011 UART（QEMU virt: 0x0900_0000） ---------- */
#define PL011_BASE 0x09000000UL
#define PL011_DR   (*(volatile uint32_t *)(PL011_BASE + 0x00))
#define PL011_FR   (*(volatile uint32_t *)(PL011_BASE + 0x18))
#define FR_TXFF    (1u << 5)

static void uart_putc(char c)
{
    if (c == '\n') uart_putc('\r');
    while (PL011_FR & FR_TXFF) { }
    PL011_DR = (uint32_t)c;
}

static void uart_puts(const char *s)
{
    while (s && *s) uart_putc(*s++);
}

static void uart_dec(unsigned long v)
{
    char buf[24];
    int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v && n < 23) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n--) uart_putc(buf[n]);
}

static void psci_off(void)
{
    __asm__ volatile(
        "mov w0, #0x8\n"
        "movk w0, #0x8400, lsl #16\n"
        "hvc #0" : : : "x0");
    for (;;) __asm__ volatile("wfi");
}

/* 判断像素格式是否 32bpp，能否直接按 32 位填 */
static EFI_STATUS gop_fill_demo(EFI_SYSTEM_TABLE *st)
{
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS s = uefi_call_wrapper(st->BootServices->LocateProtocol, 3,
                                     &gop_guid, NULL, (void **)&gop);
    if (EFI_ERROR(s) || !gop || !gop->Mode || !gop->Mode->Info) {
        uart_puts("[M0] no GOP\n");
        return EFI_NOT_FOUND;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = gop->Mode->Info;
    uart_puts("[M0] GOP res = ");
    uart_dec(mi->HorizontalResolution); uart_putc('x');
    uart_dec(mi->VerticalResolution); uart_putc('\n');

    /* 只有 32bpp 才做原始填色；否则只报告不改 */
    if (mi->PixelFormat != PixelBlueGreenRedReserved8BitPerColor &&
        mi->PixelFormat != PixelRedGreenBlueReserved8BitPerColor) {
        uart_puts("[M0] pixel format not 32bpp, skip fill\n");
        return EFI_SUCCESS;
    }

    UINT32 *fb = (UINT32 *)(UINTN)gop->Mode->FrameBufferBase;
    UINT32 stride = gop->Mode->Info->PixelsPerScanLine;
    /* 上半屏深蓝、下半屏墨绿：不用中文字库，纯色块证明帧缓冲可写 */
    for (UINT32 y = 0; y < mi->VerticalResolution; y++) {
        UINT32 c = (y < mi->VerticalResolution / 2) ? 0x00223344u : 0x00335544u;
        for (UINT32 x = 0; x < mi->HorizontalResolution; x++)
            fb[y * stride + x] = c;
    }
    uart_puts("[M0] framebuffer filled (2 bands)\n");
    return EFI_SUCCESS;
}

EFI_STATUS
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    InitializeLib(image_handle, st);
    uart_puts("\n[HEPU-A64] M0: reached UEFI entry on aarch64\n");

    gop_fill_demo(st);

    /* 用固件的控制台也打一行，便于串口/图形控制台两边都能看到 */
    Print(L"[HEPU-A64] M0 complete (GOP + PL011)\n");

    uart_puts("[HEPU-A64] powering off via PSCI\n");
    /* 等 1 秒让串口刷完，再关 */
    uefi_call_wrapper(st->BootServices->Stall, 1, 8000000);
    psci_off();
    return EFI_SUCCESS;
}
