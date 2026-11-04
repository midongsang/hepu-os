/* ============================================================================
 * 赫普OS - binfmt 格式注册框架 (多格式软件兼容层 L0)
 * 通过文件魔数识别可执行格式并分发给对应加载器.
 * V3.87: 支持 Linux ELF32; 预留 Windows PE / Apple Mach-O 槽位.
 * ========================================================================== */

#ifndef HEPU_BINFMT_H
#define HEPU_BINFMT_H

#include <stdint.h>

/* 加载结果 */
#define BINFMT_OK           0
#define BINFMT_ERR_FORMAT   (-1)   /* 魔数/结构不匹配 */
#define BINFMT_ERR_UNSUP    (-2)   /* 格式支持但特性不支持(如动态链接) */
#define BINFMT_ERR_MEM      (-3)   /* 内存不足 */
#define BINFMT_ERR_IO       (-4)   /* 读取失败 */

/* 格式标识 */
#define BINFMT_ID_UNKNOWN   0
#define BINFMT_ID_ELF32     1       /* Linux/ELF32 */
#define BINFMT_ID_PE32      2       /* Windows PE (V3.89) */
#define BINFMT_ID_MACHO     3       /* Apple Mach-O (远期) */
#define BINFMT_ID_HEPU_HUG  4       /* 自研 .hug */

/* 格式注册项 */
typedef struct {
    uint32_t    id;
    const char *name;
    uint32_t    magic_len;
    const uint8_t *magic;
    /* 识别函数: 返回 1 匹配 */
    int (*probe)(const uint8_t *buf, uint32_t len);
    /* 加载函数: 从 initrd/VFS 加载并执行 */
    int (*load)(const char *path, const uint8_t *data, uint32_t size);
} binfmt_t;

/* 注册格式 (内部调用) */
int binfmt_register(const binfmt_t *fmt);

/* 识别格式: 返回格式ID */
uint32_t binfmt_probe(const uint8_t *buf, uint32_t len);

/* 按路径加载执行 (Shell `run` / execve 分发入口) */
int binfmt_exec(const char *path, const uint8_t *data, uint32_t size);

/* V3.87 B-3: 设置/读取当前进程命令行 (shell `run` 真实传参).
 * cmdline 为完整命令串 "prog arg1 arg2", 加载器分词注入 argv 区. */
void binfmt_set_cmdline(const char *cmdline);
const char *binfmt_get_cmdline(void);

/* 初始化: 注册内置格式 */
void binfmt_init(void);

/* 兼容层自检 */
void binfmt_selftest(void);

#endif /* HEPU_BINFMT_H */

/* PE32 stub 偏移常量 (从 binfmt_pe.c 移来，供 binfmt_pe_util.c 使用) */
#define PE_STUB_PAGES   1

/* 用户态 stub 页虚拟地址 = ImageBase + SizeOfImage (页对齐) */
/* stub 槽内偏移 */
#define PE_STUB_WRITE   0x000u
#define PE_STUB_READ    0x020u
#define PE_STUB_EXIT    0x040u
#define PE_STUB_STDHANDLE 0x050u
#define PE_STUB_UNSUP   0x060u
#define PE_STUB_CMDLINE 0x080u   /* GetCommandLineA: mov eax,imm32 由加载器 patch */
#define PE_STUB_TICK    0x0A0u   /* GetTickCount: SYS_TIME 秒→毫秒 (粗粒度) */
#define PE_STUB_SLEEP   0x0C0u   /* Sleep: SYS_SLEEP 毫秒 */
#define PE_STUB_MODNAME 0x0E0u   /* GetModuleFileNameA: 纯用户态 rep movsb 拷贝路径 */
/* ---- PE/COFF 公开结构 (Microsoft PE Format 规范) ---- */
typedef struct {
    uint16_t e_magic;                 /* 'MZ' */
    uint8_t  _pad[0x3C - 2];
    uint32_t e_lfanew;                /* 偏移 0x3C: PE 头位置 */
} pe_dos_header_t;

typedef struct {
    uint16_t Machine;                 /* 0x14C = i386 */
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} pe_coff_header_t;                   /* IMAGE_FILE_HEADER = 20B (签名在其前) */

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} pe_datadir_t;

typedef struct {
    uint16_t Magic;                   /* 0x10B = PE32 */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;     /* RVA */
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;               /* 2=GUI 3=CONSOLE */
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    pe_datadir_t DataDirectory[16];   /* [1] = Import Table */
} pe_optional_header_t;

typedef struct {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} pe_section_header_t;

/* ---- 常量 ---- */
#define PE_MZ_MAGIC    0x5A4Du
#define PE_SIG         0x00004550u    /* 'PE\0\0' */
#define PE_MACHINE_I386 0x14Cu
#define PE_MAGIC_PE32  0x10Bu
#define PE_SUBSYS_CONSOLE 3u
#define PE_MAX_SECTIONS 16
#define PE_MAX_IMPORTS  64


/* stub 页数据区布局常量 */
#define PE_CMDLINE_STR  0x100u
#define PE_ARGV_STR0    0x160u
#define PE_ARGV_STR1    0x180u
#define PE_ARGV_STR2    0x1A0u
#define PE_ARGV_STR3    0x1C0u
#define PE_ARGV_TBL     0x1E0u
#define PE_MODULE_STR   0x200u
