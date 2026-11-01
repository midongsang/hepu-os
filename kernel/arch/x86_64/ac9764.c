/* ============================================================================
 * 赫普OS 64位 - AC97声卡驱动（基础框架）
 * 支持Intel AC'97 Audio Controller，PCI检测，混音器控制
 * V0.10.158
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

/* AC97 PCI配置 */
#define AC97_VENDOR_ID   0x8086  /* Intel */
#define AC97_DEVICE_ID   0x2415  /* 82801AA AC'97 Audio */
#define AC97_CLASS       0x04    /* 多媒体控制器 */
#define AC97_SUBCLASS    0x01    /* 音频设备 */

/* AC97寄存器（BAR0 - 混音器寄存器） */
#define AC97_RESET           0x00  /* 复位 */
#define AC97_MASTER_VOL      0x02  /* 主音量 */
#define AC97_AUX_OUT_VOL     0x04  /* AUX输出音量 */
#define AC97_MONO_VOL        0x06  /* 单声道音量 */
#define AC97_MASTER_TONE     0x08  /* 主音调 */
#define AC97_PC_BEEP_VOL     0x0A  /* PC蜂鸣音量 */
#define AC97_PHONE_VOL       0x0C  /* 电话音量 */
#define AC97_MIC_VOL         0x0E  /* 麦克风音量 */
#define AC97_LINE_IN_VOL     0x10  /* 线路输入音量 */
#define AC97_CD_VOL          0x12  /* CD音量 */
#define AC97_VIDEO_VOL       0x14  /* 视频音量 */
#define AC97_AUX_IN_VOL      0x16  /* AUX输入音量 */
#define AC97_PCM_OUT_VOL     0x18  /* PCM输出音量 */
#define AC97_RECORD_SEL      0x1A  /* 录制选择 */
#define AC97_RECORD_GAIN     0x1C  /* 录制增益 */
#define AC97_RECORD_GAIN_MIC 0x1E  /* 麦克风录制增益 */
#define AC97_GENERAL_PURPOSE 0x20  /* 通用目的 */
#define AC97_3D_CONTROL      0x22  /* 3D控制 */
#define AC97_RESERVED        0x24  /* 保留 */
#define AC97_POWERDOWN_CTRL  0x26  /* 断电控制 */
#define AC97_EXT_AUDIO_ID    0x28  /* 扩展音频ID */
#define AC97_EXT_AUDIO_STAT  0x2A  /* 扩展音频状态 */
#define AC97_PCM_FRONT_DAC   0x2C  /* PCM前声道DAC速率 */
#define AC97_PCM_SURR_DAC    0x2E  /* PCM环绕DAC速率 */
#define AC97_PCM_LFE_DAC     0x30  /* PCM低频DAC速率 */
#define AC97_PCM_LR_ADC      0x32  /* PCM左右ADC速率 */
#define AC97_VENDOR_ID1      0x7C  /* 厂商ID1 */
#define AC97_VENDOR_ID2      0x7E  /* 厂商ID2 */

/* AC97寄存器（BAR1 - 总线主控寄存器） */
#define AC97_BM_BDBAR        0x00  /* 输入缓冲描述符列表地址 */
#define AC97_BM_CIV          0x04  /* 当前索引值 */
#define AC97_BM_LVI          0x05  /* 最后有效索引 */
#define AC97_BM_SR           0x06  /* 状态寄存器 */
#define AC97_BM_PICB         0x08  /* 当前位置缓冲区 */
#define AC97_BM_PIV          0x0A  /* 预取索引值 */
#define AC97_BM_CR           0x0B  /* 控制寄存器 */
#define AC97_BM_SMR          0x0C  /* 状态镜像寄存器 */

/* 缓冲区描述符 */
typedef struct {
    uint32_t address;   /* 缓冲区物理地址 */
    uint32_t length;    /* 缓冲区长度（位0-15，位30=BUP，位31=IOC） */
} ac97_bd_t;

/* AC97设备状态 */
typedef struct {
    uint8_t  initialized;     /* 是否已初始化 */
    uint16_t vendor_id;       /* 厂商ID */
    uint16_t device_id;       /* 设备ID */
    uint32_t mixer_base;      /* 混音器寄存器基地址（BAR0） */
    uint32_t bus_master_base; /* 总线主控寄存器基地址（BAR1） */
    uint8_t  irq;             /* 中断号 */
    uint16_t ext_audio_id;    /* 扩展音频ID */
} ac97_device_t;

static ac97_device_t ac97_dev = {0};

/* IO端口读写 */
static inline uint8_t ac97_inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t ac97_inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void ac97_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void ac97_outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* PCI配置空间读写（使用pci64.c中的实现） */
extern uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
extern void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

/* 读取混音器寄存器 */
static uint16_t ac97_read_mixer(uint16_t reg)
{
    if (!ac97_dev.mixer_base) return 0;
    return ac97_inw(ac97_dev.mixer_base + reg);
}

/* 写入混音器寄存器 */
static void ac97_write_mixer(uint16_t reg, uint16_t val)
{
    if (!ac97_dev.mixer_base) return;
    ac97_outw(ac97_dev.mixer_base + reg, val);
}

/* 读取总线主控寄存器 */
static uint8_t ac97_read_bm_byte(uint16_t reg)
{
    if (!ac97_dev.bus_master_base) return 0;
    return ac97_inb(ac97_dev.bus_master_base + reg);
}

static uint16_t ac97_read_bm_word(uint16_t reg)
{
    if (!ac97_dev.bus_master_base) return 0;
    return ac97_inw(ac97_dev.bus_master_base + reg);
}

static void ac97_write_bm_byte(uint16_t reg, uint8_t val)
{
    if (!ac97_dev.bus_master_base) return;
    ac97_outb(ac97_dev.bus_master_base + reg, val);
}

/* 检测AC97设备 */
static int ac97_detect(void)
{
    /* 扫描PCI总线0，查找音频设备 */
    for (int dev = 0; dev < 32; dev++) {
        uint32_t vendor_dev = pci_config_read32(0, dev, 0, 0x00);
        uint16_t vendor = vendor_dev & 0xFFFF;
        uint16_t device = (vendor_dev >> 16) & 0xFFFF;

        if (vendor == 0xFFFF) continue;  /* 无设备 */

        uint32_t class_rev = pci_config_read32(0, dev, 0, 0x08);
        uint8_t class_code = (class_rev >> 24) & 0xFF;
        uint8_t subclass = (class_rev >> 16) & 0xFF;

        /* 检查是否为音频设备 */
        if (class_code == AC97_CLASS && subclass == AC97_SUBCLASS) {
            ac97_dev.vendor_id = vendor;
            ac97_dev.device_id = device;

            /* 读取BAR0（混音器寄存器） */
            uint32_t bar0 = pci_config_read32(0, dev, 0, 0x10);
            ac97_dev.mixer_base = bar0 & 0xFFFC;

            /* 读取BAR1（总线主控寄存器） */
            uint32_t bar1 = pci_config_read32(0, dev, 0, 0x14);
            ac97_dev.bus_master_base = bar1 & 0xFFFC;

            /* 读取中断号 */
            uint32_t irq_pin = pci_config_read32(0, dev, 0, 0x3C);
            ac97_dev.irq = irq_pin & 0xFF;

            vga_puts("[AC97] Found audio device: dev=");
            char buf[8];
            int n = dev, i = 0;
            if (n == 0) buf[i++] = '0';
            while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
            buf[i] = 0;
            for (int j = 0; j < i / 2; j++) {
                char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
            }
            vga_puts(buf);
            vga_puts(" vendor=0x");
            vga_put_hex(vendor);
            vga_puts(" device=0x");
            vga_put_hex(device);
            vga_puts("\n");

            return 0;
        }
    }

    return -1;  /* 未找到 */
}

/* 初始化AC97 */
int ac97_init(void)
{
    vga_puts("[AC97] Initializing AC'97 audio controller...\n");

    /* 检测设备 */
    if (ac97_detect() != 0) {
        vga_puts("[AC97] No AC'97 audio device found\n");
        return -1;
    }

    vga_puts("[AC97] Mixer base: 0x");
    vga_put_hex(ac97_dev.mixer_base);
    vga_puts(" Bus master base: 0x");
    vga_put_hex(ac97_dev.bus_master_base);
    vga_puts(" IRQ: ");
    char buf[8];
    int n = ac97_dev.irq, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts("\n");

    /* 复位AC97 */
    ac97_write_mixer(AC97_RESET, 0xFFFF);

    /* 读取扩展音频ID */
    ac97_dev.ext_audio_id = ac97_read_mixer(AC97_EXT_AUDIO_ID);
    vga_puts("[AC97] Extended audio ID: 0x");
    vga_put_hex(ac97_dev.ext_audio_id);
    vga_puts("\n");

    /* 设置主音量（最大） */
    ac97_write_mixer(AC97_MASTER_VOL, 0x0000);  /* 0x0000 = 最大音量 */

    /* 设置PCM输出音量（最大） */
    ac97_write_mixer(AC97_PCM_OUT_VOL, 0x0000);

    /* 设置麦克风音量 */
    ac97_write_mixer(AC97_MIC_VOL, 0x0008);  /* 20dB增益 */

    /* 启用总线主控 */
    ac97_write_bm_byte(AC97_BM_CR, 0x00);  /* 先停止 */

    ac97_dev.initialized = 1;

    vga_puts("[AC97] Initialized successfully\n");
    return 0;
}

/* 设置主音量（0-63，0=最大） */
void ac97_set_master_volume(uint8_t left, uint8_t right)
{
    if (!ac97_dev.initialized) return;
    uint16_t vol = ((left & 0x3F) << 8) | (right & 0x3F);
    ac97_write_mixer(AC97_MASTER_VOL, vol);
}

/* 设置PCM输出音量 */
void ac97_set_pcm_volume(uint8_t left, uint8_t right)
{
    if (!ac97_dev.initialized) return;
    uint16_t vol = ((left & 0x3F) << 8) | (right & 0x3F);
    ac97_write_mixer(AC97_PCM_OUT_VOL, vol);
}

/* 获取设备状态 */
int ac97_get_info(uint16_t *vendor, uint16_t *device,
                  uint32_t *mixer_base, uint32_t *bm_base, uint8_t *irq)
{
    if (!ac97_dev.initialized) return -1;
    if (vendor) *vendor = ac97_dev.vendor_id;
    if (device) *device = ac97_dev.device_id;
    if (mixer_base) *mixer_base = ac97_dev.mixer_base;
    if (bm_base) *bm_base = ac97_dev.bus_master_base;
    if (irq) *irq = ac97_dev.irq;
    return 0;
}

/* 是否已初始化 */
int ac97_is_initialized(void)
{
    return ac97_dev.initialized;
}
