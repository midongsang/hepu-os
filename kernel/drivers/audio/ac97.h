/* ============================================================================
 * 赫普OS - AC97声卡驱动头文件
 * V8.30: 初始实现
 * ========================================================================== */

#ifndef _AC97_H
#define _AC97_H

#include <stdint.h>

/* AC97 PCI IDs */
#define AC97_VENDOR_INTEL    0x8086
#define AC97_DEVICE_82801AA  0x2415  /* ICH */
#define AC97_DEVICE_82801AB  0x2425  /* ICH0 */
#define AC97_DEVICE_82801BA  0x2445  /* ICH2 */
#define AC97_DEVICE_82801CA  0x2485  /* ICH3 */
#define AC97_DEVICE_82801DB  0x24C5  /* ICH4 */
#define AC97_DEVICE_82801EB  0x24D5  /* ICH5 */

/* AC97音频类码 */
#define AC97_PCI_CLASS       0x01    /* 存储类？不，AC97是多媒体类 */
#define AC97_PCI_SUBCLASS    0x01    /* 音频子类 */
#define AC97_PCI_PROG_IF     0x00    /* AC97编程接口 */

/* AC97寄存器（BAR0，I/O空间） */
#define AC97_NAM_BAR         0x10    /* Native Audio Mixer BAR */
#define AC97_NABMBAR         0x14    /* Native Audio Bus Master BAR */

/* 混音器寄存器偏移（NAM） */
#define AC97_RESET           0x00    /* 重置 */
#define AC97_MASTER_VOL      0x02    /* 主音量 */
#define AC97_AUX_OUT_VOL     0x04    /* AUX输出音量 */
#define AC97_MONO_VOL        0x06    /* 单声道音量 */
#define AC97_MASTER_TONE     0x08    /* 主音调 */
#define AC97_PC_BEEP_VOL     0x0A    /* PC蜂鸣器音量 */
#define AC97_PHONE_VOL       0x0C    /* 电话音量 */
#define AC97_MIC_VOL         0x0E    /* 麦克风音量 */
#define AC97_LINE_IN_VOL     0x10    /* 线路输入音量 */
#define AC97_CD_VOL          0x12    /* CD音量 */
#define AC97_VIDEO_VOL       0x14    /* 视频音量 */
#define AC97_AUX_IN_VOL      0x16    /* AUX输入音量 */
#define AC97_PCM_OUT_VOL     0x18    /* PCM输出音量 */
#define AC97_REC_SEL         0x1A    /* 录制选择 */
#define AC97_REC_GAIN        0x1C    /* 录制增益 */
#define AC97_REC_GAIN_MIC    0x1E    /* 麦克风录制增益 */
#define AC97_GENERAL         0x20    /* 通用 */
#define AC97_3D_CONTROL      0x22    /* 3D控制 */
#define AC97_RESERVED        0x24    /* 保留 */
#define AC97_POWERDOWN_CTRL  0x26    /* 断电控制 */
#define AC97_EXT_AUDIO_ID    0x28    /* 扩展音频ID */
#define AC97_EXT_AUDIO_STAT  0x2A    /* 扩展音频状态 */
#define AC97_PCM_FRONT_DAC_RATE 0x2C /* PCM前DAC采样率 */
#define AC97_PCM_SURR_DAC_RATE  0x2E /* PCM环绕DAC采样率 */
#define AC97_PCM_LFE_DAC_RATE   0x30 /* PCM LFE DAC采样率 */
#define AC97_PCM_LR_ADC_RATE    0x32 /* PCM左右ADC采样率 */
#define AC97_PCM_MIC_ADC_RATE   0x34 /* PCM麦克风ADC采样率 */
#define AC97_CENTER_LFE_MASTER  0x36 /* 中置/LFE主音量 */
#define AC97_SURROUND_MASTER    0x38 /* 环绕主音量 */

/* 总线主寄存器偏移（NABMBAR） */
#define AC97_BM_PI_BDBAR     0x00    /* PCM输入缓冲区描述符列表地址 */
#define AC97_BM_PI_CIV       0x04    /* PCM输入当前索引值 */
#define AC97_BM_PI_LVI       0x05    /* PCM输入最后有效索引 */
#define AC97_BM_PI_SR        0x06    /* PCM输入状态寄存器 */
#define AC97_BM_PI_PICB      0x08    /* PCM输入当前位置缓冲区 */
#define AC97_BM_PI_PIV       0x0A    /* PCM输入预取索引值 */
#define AC97_BM_PI_CR        0x0B    /* PCM输入控制寄存器 */

#define AC97_BM_PO_BDBAR     0x10    /* PCM输出缓冲区描述符列表地址 */
#define AC97_BM_PO_CIV       0x14    /* PCM输出当前索引值 */
#define AC97_BM_PO_LVI       0x15    /* PCM输出最后有效索引 */
#define AC97_BM_PO_SR        0x16    /* PCM输出状态寄存器 */
#define AC97_BM_PO_PICB      0x18    /* PCM输出当前位置缓冲区 */
#define AC97_BM_PO_PIV       0x1A    /* PCM输出预取索引值 */
#define AC97_BM_PO_CR        0x1B    /* PCM输出控制寄存器 */

#define AC97_BM_MC_BDBAR     0x20    /* 麦克风输入缓冲区描述符列表地址 */
#define AC97_BM_MC_CIV       0x24    /* 麦克风输入当前索引值 */
#define AC97_BM_MC_LVI       0x25    /* 麦克风输入最后有效索引 */
#define AC97_BM_MC_SR        0x26    /* 麦克风输入状态寄存器 */
#define AC97_BM_MC_PICB      0x28    /* 麦克风输入当前位置缓冲区 */
#define AC97_BM_MC_PIV       0x2A    /* 麦克风输入预取索引值 */
#define AC97_BM_MC_CR        0x2B    /* 麦克风输入控制寄存器 */

#define AC97_BM_GLOB_CNT     0x2C    /* 全局控制 */
#define AC97_BM_GLOB_STA     0x30    /* 全局状态 */
#define AC97_BM_CAS          0x34    /* 编解码器选择 */
#define AC97_BM_SIT          0x35    /* 串行中断定时器 */
#define AC97_BM_CHANNELS     0x36    /* 通道采样 */

/* 缓冲区描述符 */
typedef struct {
    uint32_t address;       /* 缓冲区物理地址 */
    uint16_t length;        /* 缓冲区长度（采样数） */
    uint16_t flags;         /* 标志（IOC=中断完成，BUP=缓冲区未完成） */
} __attribute__((packed)) ac97_bd_t;

#define AC97_BD_IOC          0x8000  /* 完成中断 */
#define AC97_BD_BUP          0x4000  /* 缓冲区未完成 */

/* 状态寄存器位 */
#define AC97_SR_DMA_STOPPED  0x01    /* DMA停止 */
#define AC97_SR_LVBCI        0x04    /* 最后有效缓冲区完成中断 */
#define AC97_SR_BCIS         0x08    /* 缓冲区完成中断状态 */
#define AC97_SR_FIFO_ERR     0x10    /* FIFO错误 */

/* 控制寄存器位 */
#define AC97_CR_RPBM         0x01    /* 运行/暂停总线主 */
#define AC97_CR_RR           0x02    /* 重置寄存器 */
#define AC97_CR_LVBIE        0x04    /* 最后有效缓冲区中断使能 */
#define AC97_CR_FEIE         0x08    /* FIFO错误中断使能 */
#define AC97_CR_IOCE         0x10    /* 中断完成使能 */

/* 全局控制位 */
#define AC97_GLOB_CNT_GIE    0x01    /* 全局中断使能 */
#define AC97_GLOB_CNT_COLD   0x02    /* 冷重置 */
#define AC97_GLOB_CNT_WARM   0x04    /* 热重置 */
#define AC97_GLOB_CNT_SHUT   0x08    /* 关闭 */

/* AC97设备结构 */
typedef struct {
    uint16_t nam_base;      /* 混音器I/O基地址 */
    uint16_t nabm_base;     /* 总线主I/O基地址 */
    uint8_t  irq;           /* 中断号 */
    int      initialized;   /* 初始化标志 */

    /* 输出缓冲区 */
    ac97_bd_t *out_bd;      /* 输出缓冲区描述符列表 */
    uint8_t  *out_buffer;   /* 输出数据缓冲区 */
    int      out_bd_count;  /* 输出描述符数量 */
    int      out_current;   /* 当前输出描述符索引 */

    /* 输入缓冲区 */
    ac97_bd_t *in_bd;       /* 输入缓冲区描述符列表 */
    uint8_t  *in_buffer;    /* 输入数据缓冲区 */
    int      in_bd_count;   /* 输入描述符数量 */
    int      in_current;    /* 当前输入描述符索引 */

    /* 音频参数 */
    int      sample_rate;   /* 采样率 */
    int      channels;      /* 声道数 */
    int      bits_per_sample; /* 位深 */
} ac97_device_t;

/* 函数声明 */
int ac97_init(void);
int ac97_detect(void);
int ac97_set_master_volume(int left, int right);
int ac97_set_pcm_volume(int left, int right);
int ac97_set_sample_rate(int rate);
int ac97_play_pcm(const void *data, int length);
int ac97_start_playback(void);
int ac97_stop_playback(void);
int ac97_start_recording(void);
int ac97_stop_recording(void);
void ac97_irq_handler(void);

#endif /* _AC97_H */
