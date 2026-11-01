/* ============================================================================
 * 赫普OS - AC97声卡驱动实现
 * V8.30: 初始实现
 * ========================================================================== */

#include "ac97.h"
#include "../../include/vga.h"
#include "../../include/pci.h"

/* I/O端口访问 */
static inline uint8_t  ac97_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void     ac97_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t ac97_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void     ac97_outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t ac97_inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void     ac97_outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* 内存分配 */
extern void *kmalloc(uint32_t size);
extern void *kfree(void *ptr);
extern void *memset(void *dst, int val, uint32_t n);
extern void *memcpy(void *dst, const void *src, uint32_t n);

/* 全局AC97设备 */
static ac97_device_t ac97_dev;
static int ac97_found = 0;

/* 读取混音器寄存器 */
static uint16_t ac97_read_mixer(ac97_device_t *dev, uint16_t reg)
{
    return ac97_inw(dev->nam_base + reg);
}

/* 写入混音器寄存器 */
static void ac97_write_mixer(ac97_device_t *dev, uint16_t reg, uint16_t val)
{
    ac97_outw(dev->nam_base + reg, val);
}

/* 读取总线主寄存器 */
static uint8_t ac97_read_bm8(ac97_device_t *dev, uint16_t reg)
{
    return ac97_inb(dev->nabm_base + reg);
}

static uint16_t ac97_read_bm16(ac97_device_t *dev, uint16_t reg)
{
    return ac97_inw(dev->nabm_base + reg);
}

static uint32_t ac97_read_bm32(ac97_device_t *dev, uint16_t reg)
{
    return ac97_inl(dev->nabm_base + reg);
}

static void ac97_write_bm8(ac97_device_t *dev, uint16_t reg, uint8_t val)
{
    ac97_outb(dev->nabm_base + reg, val);
}

static void ac97_write_bm16(ac97_device_t *dev, uint16_t reg, uint16_t val)
{
    ac97_outw(dev->nabm_base + reg, val);
}

static void ac97_write_bm32(ac97_device_t *dev, uint16_t reg, uint32_t val)
{
    ac97_outl(dev->nabm_base + reg, val);
}

/* 设置主音量（0-63，左右声道） */
int ac97_set_master_volume(int left, int right)
{
    if (!ac97_found) return -1;

    /* AC97音量：0=最大，63=静音（反向） */
    uint16_t vol = ((63 - (right & 0x3F)) << 8) | (63 - (left & 0x3F));
    ac97_write_mixer(&ac97_dev, AC97_MASTER_VOL, vol);

    vga_printf("[AC97] Master volume: L=%d R=%d\n", left, right);
    return 0;
}

/* 设置PCM输出音量 */
int ac97_set_pcm_volume(int left, int right)
{
    if (!ac97_found) return -1;

    uint16_t vol = ((63 - (right & 0x3F)) << 8) | (63 - (left & 0x3F));
    ac97_write_mixer(&ac97_dev, AC97_PCM_OUT_VOL, vol);

    vga_printf("[AC97] PCM volume: L=%d R=%d\n", left, right);
    return 0;
}

/* 设置采样率 */
int ac97_set_sample_rate(int rate)
{
    if (!ac97_found) return -1;

    ac97_write_mixer(&ac97_dev, AC97_PCM_FRONT_DAC_RATE, (uint16_t)rate);
    ac97_dev.sample_rate = rate;

    vga_printf("[AC97] Sample rate set to %d Hz\n", rate);
    return 0;
}

/* 初始化输出DMA缓冲区 */
static int ac97_init_output_dma(ac97_device_t *dev)
{
    /* 分配缓冲区描述符列表（32个描述符，对齐到4字节） */
    dev->out_bd_count = 32;
    dev->out_bd = (ac97_bd_t *)kmalloc(sizeof(ac97_bd_t) * dev->out_bd_count);
    if (!dev->out_bd) return -1;
    memset(dev->out_bd, 0, sizeof(ac97_bd_t) * dev->out_bd_count);

    /* 分配数据缓冲区（每个描述符4KB，共128KB） */
    int buf_size = 4096;
    dev->out_buffer = (uint8_t *)kmalloc(buf_size * dev->out_bd_count);
    if (!dev->out_buffer) {
        kfree(dev->out_bd);
        return -1;
    }
    memset(dev->out_buffer, 0, buf_size * dev->out_bd_count);

    /* 初始化描述符 */
    for (int i = 0; i < dev->out_bd_count; i++) {
        dev->out_bd[i].address = (uint32_t)(dev->out_buffer + i * buf_size);
        dev->out_bd[i].length = buf_size / 4;  /* 采样数（16位立体声=4字节/采样） */
        dev->out_bd[i].flags = AC97_BD_IOC;    /* 完成中断 */
    }

    /* 设置缓冲区描述符列表地址 */
    ac97_write_bm32(dev, AC97_BM_PO_BDBAR, (uint32_t)dev->out_bd);

    /* 设置最后有效索引 */
    ac97_write_bm8(dev, AC97_BM_PO_LVI, dev->out_bd_count - 1);

    dev->out_current = 0;

    vga_printf("[AC97] Output DMA initialized: %d buffers, %d bytes each\n",
               dev->out_bd_count, buf_size);
    return 0;
}

/* 开始播放 */
int ac97_start_playback(void)
{
    if (!ac97_found) return -1;

    /* 清除状态 */
    ac97_write_bm16(&ac97_dev, AC97_BM_PO_SR, 0x1C);  /* 清除LVBCI/BCIS/FIFO */

    /* 使能中断 */
    ac97_write_bm8(&ac97_dev, AC97_BM_PO_CR,
                   AC97_CR_IOCE | AC97_CR_FEIE | AC97_CR_LVBIE);

    /* 启动DMA */
    uint8_t cr = ac97_read_bm8(&ac97_dev, AC97_BM_PO_CR);
    cr |= AC97_CR_RPBM;
    ac97_write_bm8(&ac97_dev, AC97_BM_PO_CR, cr);

    vga_puts("[AC97] Playback started\n");
    return 0;
}

/* 停止播放 */
int ac97_stop_playback(void)
{
    if (!ac97_found) return -1;

    uint8_t cr = ac97_read_bm8(&ac97_dev, AC97_BM_PO_CR);
    cr &= ~AC97_CR_RPBM;
    ac97_write_bm8(&ac97_dev, AC97_BM_PO_CR, cr);

    vga_puts("[AC97] Playback stopped\n");
    return 0;
}

/* 播放PCM数据（简化：直接填充缓冲区） */
int ac97_play_pcm(const void *data, int length)
{
    if (!ac97_found || !data || length <= 0) return -1;

    /* 简化实现：将数据复制到输出缓冲区 */
    int buf_size = 4096;
    int copy_len = length;
    if (copy_len > buf_size) copy_len = buf_size;

    memcpy(ac97_dev.out_buffer, data, copy_len);

    /* 如果DMA未运行，启动播放 */
    uint8_t cr = ac97_read_bm8(&ac97_dev, AC97_BM_PO_CR);
    if (!(cr & AC97_CR_RPBM)) {
        ac97_start_playback();
    }

    return copy_len;
}

/* 中断处理 */
void ac97_irq_handler(void)
{
    if (!ac97_found) return;

    /* 检查输出状态 */
    uint16_t sr = ac97_read_bm16(&ac97_dev, AC97_BM_PO_SR);
    if (sr & (AC97_SR_LVBCI | AC97_SR_BCIS)) {
        /* 清除中断状态 */
        ac97_write_bm16(&ac97_dev, AC97_BM_PO_SR, sr & 0x1C);
        ac97_dev.out_current = (ac97_dev.out_current + 1) % ac97_dev.out_bd_count;
    }

    /* 检查输入状态 */
    sr = ac97_read_bm16(&ac97_dev, AC97_BM_PI_SR);
    if (sr & (AC97_SR_LVBCI | AC97_SR_BCIS)) {
        ac97_write_bm16(&ac97_dev, AC97_BM_PI_SR, sr & 0x1C);
    }
}

/* 初始化AC97 */
int ac97_init(void)
{
    if (!ac97_found) return -1;

    ac97_device_t *dev = &ac97_dev;

    vga_printf("[AC97] Initializing at NAM=0x%x NABM=0x%x IRQ=%d\n",
               dev->nam_base, dev->nabm_base, dev->irq);

    /* 1. 冷重置 */
    uint32_t glob_cnt = ac97_read_bm32(dev, AC97_BM_GLOB_CNT);
    glob_cnt |= AC97_GLOB_CNT_COLD;
    ac97_write_bm32(dev, AC97_BM_GLOB_CNT, glob_cnt);

    /* 等待重置完成 */
    for (volatile int i = 0; i < 100000; i++) {
        if (!(ac97_read_bm32(dev, AC97_BM_GLOB_CNT) & AC97_GLOB_CNT_COLD)) break;
    }

    /* 2. 热重置 */
    glob_cnt = ac97_read_bm32(dev, AC97_BM_GLOB_CNT);
    glob_cnt |= AC97_GLOB_CNT_WARM;
    ac97_write_bm32(dev, AC97_BM_GLOB_CNT, glob_cnt);
    for (volatile int i = 0; i < 100000; i++);
    glob_cnt &= ~AC97_GLOB_CNT_WARM;
    ac97_write_bm32(dev, AC97_BM_GLOB_CNT, glob_cnt);

    /* 3. 读取编解码器ID */
    uint16_t ext_id = ac97_read_mixer(dev, AC97_EXT_AUDIO_ID);
    vga_printf("[AC97] Extended audio ID: 0x%04x\n", ext_id);

    /* 4. 设置默认音量 */
    ac97_set_master_volume(50, 50);  /* 约80%音量 */
    ac97_set_pcm_volume(63, 63);     /* 最大PCM音量 */

    /* 5. 设置默认采样率 */
    ac97_set_sample_rate(44100);
    dev->channels = 2;
    dev->bits_per_sample = 16;

    /* 6. 初始化输出DMA */
    if (ac97_init_output_dma(dev) != 0) {
        vga_puts("[AC97] Failed to init output DMA\n");
        return -1;
    }

    /* 7. 使能全局中断 */
    glob_cnt = ac97_read_bm32(dev, AC97_BM_GLOB_CNT);
    glob_cnt |= AC97_GLOB_CNT_GIE;
    ac97_write_bm32(dev, AC97_BM_GLOB_CNT, glob_cnt);

    dev->initialized = 1;
    vga_puts("[AC97] Initialized successfully\n");
    return 0;
}

/* 检测AC97设备 */
int ac97_detect(void)
{
    /* AC97: class=0x04 (Multimedia), subclass=0x01 (Audio), prog_if=0x00 */
    int idx = pci_find_by_class(0x04, 0x01);
    if (idx < 0) {
        vga_puts("[AC97] No audio controller found\n");
        return -1;
    }

    pci_device_t *p = &pci_devices[idx];
    vga_printf("[AC97] Found audio controller: %04x:%04x at %02x:%02x.%x\n",
               p->vendor_id, p->device_id, p->bus, p->dev, p->func);

    /* 读取BAR0（NAM）和BAR1（NABMBAR） */
    uint32_t bar0 = pci_config_read(p->bus, p->dev, p->func, 0x10);
    uint32_t bar1 = pci_config_read(p->bus, p->dev, p->func, 0x14);

    ac97_dev.nam_base = bar0 & 0xFFFE;   /* I/O空间，清除最低位 */
    ac97_dev.nabm_base = bar1 & 0xFFFE;
    ac97_dev.irq = p->irq_line;

    vga_printf("[AC97] NAM=0x%x NABM=0x%x IRQ=%d\n",
               ac97_dev.nam_base, ac97_dev.nabm_base, ac97_dev.irq);

    /* 启用总线主控和I/O空间 */
    uint16_t cmd = pci_config_read(p->bus, p->dev, p->func, 0x04);
    cmd |= 0x01 | 0x04;  /* I/O空间 + 总线主控 */
    pci_config_write(p->bus, p->dev, p->func, 0x04, cmd);

    ac97_found = 1;
    return 0;
}
