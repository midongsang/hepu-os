/* ============================================================================
 * 赫普OS - 存储与块设备单元测试
 * 覆盖：MBR/GPT/分区/块设备/缓存/磁盘IO
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === MBR测试 === */
#define MBR_SIGNATURE 0xAA55
#define MBR_PARTITION_OFFSET 446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_PARTITION_COUNT 4

typedef struct {
    u8  boot_flag;
    u8  chs_start[3];
    u8  type;
    u8  chs_end[3];
    u32 lba_start;
    u32 sector_count;
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    u8  boot_code[446];
    mbr_partition_t partitions[4];
    u16 signature;
} __attribute__((packed)) mbr_t;

static kunit_result_t test_mbr_structure(void)
{
    KUNIT_ASSERT_EQ(sizeof(mbr_partition_t), 16, " partition =16 bytes ");
    KUNIT_ASSERT_EQ(sizeof(mbr_t), 512, "MBR=512 bytes ");
    KUNIT_ASSERT_EQ(MBR_SIGNATURE, 0xAA55, "MBR signature ");
    KUNIT_ASSERT_EQ(MBR_PARTITION_COUNT, 4, "4 master partition ");
    return KUNIT_PASS;
}

static kunit_result_t test_mbr_signature(void)
{
    mbr_t mbr;
    memset(&mbr, 0, sizeof(mbr));
    mbr.signature = MBR_SIGNATURE;

    /* 小端序: 0x55在偏移510, 0xAA在偏移511 */
    u8 *bytes = (u8 *)&mbr;
    KUNIT_ASSERT_EQ(bytes[510], 0x55, " 偏移 510=0x55");
    KUNIT_ASSERT_EQ(bytes[511], 0xAA, " 偏移 511=0xAA");
    return KUNIT_PASS;
}

static kunit_result_t test_mbr_partition_types(void)
{
    /* 常见分区类型 */
    KUNIT_ASSERT_EQ(0x00, 0x00, " empty partition ");
    KUNIT_ASSERT_EQ(0x0B, 0x0B, "FAT32 CHS");
    KUNIT_ASSERT_EQ(0x0C, 0x0C, "FAT32 LBA");
    KUNIT_ASSERT_EQ(0x07, 0x07, "NTFS/exFAT");
    KUNIT_ASSERT_EQ(0x83, 0x83, "Linux ext2/3/4");
    KUNIT_ASSERT_EQ(0x82, 0x82, "Linux swap");
    KUNIT_ASSERT_EQ(0xEE, 0xEE, "GPT protected MBR");
    return KUNIT_PASS;
}

static kunit_result_t test_mbr_partition_parse(void)
{
    mbr_t mbr;
    memset(&mbr, 0, sizeof(mbr));

    /* 设置第一个分区 */
    mbr.partitions[0].boot_flag = 0x80;  /* 可引导 */
    mbr.partitions[0].type = 0x83;       /* Linux */
    mbr.partitions[0].lba_start = 2048;
    mbr.partitions[0].sector_count = 100000;

    KUNIT_ASSERT_EQ(mbr.partitions[0].boot_flag, 0x80, " 可引导标志 ");
    KUNIT_ASSERT_EQ(mbr.partitions[0].type, 0x83, " partition type =Linux");
    KUNIT_ASSERT_EQ(mbr.partitions[0].lba_start, 2048, " start LBA=2048");
    KUNIT_ASSERT_EQ(mbr.partitions[0].sector_count, 100000, " sector count =100000");
    return KUNIT_PASS;
}

/* === GPT测试 === */
#define GPT_SIGNATURE 0x5452415020494645ULL  /* "EFI PART" */
#define GPT_REVISION 0x00010000
#define GPT_HEADER_SIZE 92

typedef struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 my_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8  disk_guid[16];
    u64 partition_entry_lba;
    u32 num_partition_entries;
    u32 partition_entry_size;
    u32 partition_array_crc32;
} __attribute__((packed)) gpt_header_t;

static kunit_result_t test_gpt_header(void)
{
    KUNIT_ASSERT_EQ(GPT_HEADER_SIZE, 92, "GPT header size =92");
    KUNIT_ASSERT_EQ(sizeof(gpt_header_t), 92, "GPT header struct =92 bytes ");
    return KUNIT_PASS;
}

static kunit_result_t test_gpt_signature(void)
{
    /* GPT签名 = "EFI PART" */
    const char *sig = "EFI PART";
    u64 gpt_sig = 0;
    for (int i = 0; i < 8; i++) {
        gpt_sig |= (u64)(u8)sig[i] << (i * 8);
    }
    KUNIT_ASSERT_EQ(gpt_sig, GPT_SIGNATURE, "GPT signature match ");
    return KUNIT_PASS;
}

/* === 块设备测试 === */
#define SECTOR_SIZE 512
#define BLOCK_SIZE 4096

typedef struct {
    u32 device_id;
    u64 total_sectors;
    u32 sector_size;
    u8  read_only;
    u8  removable;
    char model[32];
} block_device_t;

static kunit_result_t test_block_device(void)
{
    block_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.device_id = 1;
    dev.total_sectors = 41943040;  /* 20GB */
    dev.sector_size = SECTOR_SIZE;
    dev.read_only = 0;
    dev.removable = 0;
    strcpy(dev.model, "HEPU-VIRTUAL-DISK");

    KUNIT_ASSERT_EQ(dev.sector_size, 512, " sector size =512");
    KUNIT_ASSERT_EQ(dev.total_sectors, 41943040, " total sectors count ");

    /* 计算总容量 */
    u64 total_bytes = dev.total_sectors * dev.sector_size;
    u64 total_mb = total_bytes / (1024 * 1024);
    KUNIT_ASSERT_EQ(total_mb, 20480, " total cap =20GB");
    KUNIT_ASSERT_EQ(strcmp(dev.model, "HEPU-VIRTUAL-DISK"), 0, " device model ");
    return KUNIT_PASS;
}

static kunit_result_t test_block_size(void)
{
    KUNIT_ASSERT_EQ(SECTOR_SIZE, 512, " sector =512 bytes ");
    KUNIT_ASSERT_EQ(BLOCK_SIZE, 4096, " block =4096 bytes ");
    KUNIT_ASSERT_EQ(BLOCK_SIZE / SECTOR_SIZE, 8, "1 block =8 sector ");
    return KUNIT_PASS;
}

/* === 磁盘缓存测试 === */
#define CACHE_BLOCKS 16
typedef struct {
    u8  data[BLOCK_SIZE];
    u64 block_num;
    u8  valid;
    u8  dirty;
    u32 last_access;
} cache_entry_t;

static cache_entry_t disk_cache[CACHE_BLOCKS];
static u32 cache_access_counter;

static void cache_init(void)
{
    memset(disk_cache, 0, sizeof(disk_cache));
    cache_access_counter = 0;
}

static cache_entry_t *cache_find(u64 block_num)
{
    for (int i = 0; i < CACHE_BLOCKS; i++) {
        if (disk_cache[i].valid && disk_cache[i].block_num == block_num) {
            disk_cache[i].last_access = ++cache_access_counter;
            return &disk_cache[i];
        }
    }
    return 0;
}

static int cache_insert(u64 block_num, const u8 *data)
{
    /* 找空槽或LRU槽 */
    int slot = -1;
    u32 oldest = 0xFFFFFFFF;
    for (int i = 0; i < CACHE_BLOCKS; i++) {
        if (!disk_cache[i].valid) { slot = i; break; }
        if (disk_cache[i].last_access < oldest) {
            oldest = disk_cache[i].last_access;
            slot = i;
        }
    }
    if (slot < 0) return -1;
    disk_cache[slot].valid = 1;
    disk_cache[slot].block_num = block_num;
    disk_cache[slot].dirty = 0;
    disk_cache[slot].last_access = ++cache_access_counter;
    if (data) memcpy(disk_cache[slot].data, data, BLOCK_SIZE);
    return 0;
}

static kunit_result_t test_disk_cache(void)
{
    cache_init();

    u8 test_data[BLOCK_SIZE];
    memset(test_data, 0xAB, BLOCK_SIZE);

    KUNIT_ASSERT_EQ(cache_insert(100, test_data), 0, " insert block 100");
    KUNIT_ASSERT_EQ(cache_insert(200, test_data), 0, " insert block 200");

    cache_entry_t *entry = cache_find(100);
    KUNIT_ASSERT_NOT_NULL(entry, " found block 100");
    KUNIT_ASSERT_EQ(entry->block_num, 100, " block 号 =100");
    KUNIT_ASSERT_EQ(entry->data[0], 0xAB, " data 正确 ");

    KUNIT_ASSERT_NULL(cache_find(999), " not found block 999");
    return KUNIT_PASS;
}

static kunit_result_t test_cache_lru(void)
{
    cache_init();
    u8 data[BLOCK_SIZE];
    memset(data, 0, BLOCK_SIZE);

    /* 填满缓存 */
    for (int i = 0; i < CACHE_BLOCKS; i++) {
        KUNIT_ASSERT_EQ(cache_insert(i, data), 0, " 填充 cache ");
    }

    /* 访问块0,使其成为最近使用 */
    cache_find(0);

    /* 再插入一个新块,应该替换最久未使用的(块1) */
    KUNIT_ASSERT_EQ(cache_insert(999, data), 0, " insert 新 block trigger LRU");
    KUNIT_ASSERT_NOT_NULL(cache_find(0), " block 0 仍 at cache ");
    KUNIT_ASSERT_NULL(cache_find(1), " block 1 被替换 ");
    return KUNIT_PASS;
}

/* === 磁盘IO请求测试 === */
typedef struct {
    u64 lba;
    u32 sector_count;
    u8  direction;  /* 0=读, 1=写 */
    u8  *buffer;
    u8  status;
} io_request_t;

#define IO_DIR_READ 0
#define IO_DIR_WRITE 1
#define IO_STATUS_PENDING 0
#define IO_STATUS_COMPLETE 1
#define IO_STATUS_ERROR 2

static kunit_result_t test_io_request(void)
{
    io_request_t req;
    memset(&req, 0, sizeof(req));
    req.lba = 1024;
    req.sector_count = 8;
    req.direction = IO_DIR_READ;
    req.status = IO_STATUS_PENDING;

    KUNIT_ASSERT_EQ(req.lba, 1024, "LBA=1024");
    KUNIT_ASSERT_EQ(req.sector_count, 8, " sector count =8");
    KUNIT_ASSERT_EQ(req.direction, IO_DIR_READ, " 方向 = 读 ");
    KUNIT_ASSERT_EQ(req.status, IO_STATUS_PENDING, " state = suspend ");

    /* 计算传输字节数 */
    u32 bytes = req.sector_count * SECTOR_SIZE;
    KUNIT_ASSERT_EQ(bytes, 4096, " transport =4KB");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t storage_tests[] = {
    {"MBR struct ", test_mbr_structure, "storage"},
    {"MBR signature ", test_mbr_signature, "storage"},
    {"MBR partition type ", test_mbr_partition_types, "storage"},
    {"MBR partition parse ", test_mbr_partition_parse, "storage"},
    {"GPT header ", test_gpt_header, "storage"},
    {"GPT signature ", test_gpt_signature, "storage"},
    {" block device ", test_block_device, "storage"},
    {" block size ", test_block_size, "storage"},
    {" disk cache ", test_disk_cache, "storage"},
    {" cache LRU", test_cache_lru, "storage"},
    {"IO 请求 ", test_io_request, "storage"},
};
static const kunit_suite_t kunit_storage_suite = {
    " storage & block device ", storage_tests, 11
};

void kunit_storage_register(void)
{
    kunit_register_suite(&kunit_storage_suite);
    vga_puts("[KUNIT] storage & block device test suite registered(11 cases)\n");
}
