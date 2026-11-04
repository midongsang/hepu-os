/* ============================================================================
 * 赫普OS - 分布式设备互联
 * ========================================================================== */

#include <stdint.h>
#include "distributed_os.h"
#include "vga.h"
#include "string.h"

static distributed_os_t dos;

void distributed_os_init(void)
{
    memset(&dos, 0, sizeof(distributed_os_t));
    dos.initialized = 1;
    dos.enabled = 1;
    dos.local_device_id = 0;
    /* 本地设备 */
    memset(&dos.devices[0], 0, sizeof(dos_device_t));
    dos.devices[0].used = 1;
    dos.devices[0].id = 0;
    strncpy(dos.devices[0].name, " Hepu Phone ", 127);
    strncpy(dos.devices[0].device_id, "HEPU-LOCAL-001", 63);
    dos.devices[0].type = DOS_DEV_PHONE;
    dos.devices[0].state = DOS_DEV_CONNECTED;
    dos.devices[0].conn_type = DOS_CONN_WIFI;
    dos.devices[0].cpu_cores = 8;
    dos.devices[0].cpu_ghz = 3.2f;
    dos.devices[0].memory_total = 12UL * 1024 * 1024 * 1024;
    dos.devices[0].memory_free = 6UL * 1024 * 1024 * 1024;
    dos.devices[0].storage_total = 256UL * 1024 * 1024 * 1024;
    dos.devices[0].storage_free = 128UL * 1024 * 1024 * 1024;
    dos.devices[0].gpu_available = 1;
    dos.devices[0].npu_available = 1;
    dos.devices[0].authenticated = 1;
    dos.devices[0].trust_level = 100;
    dos.device_count = 1;
    /* 同步配置 */
    dos.sync.clipboard_sync = 1;
    dos.sync.notification_sync = 1;
    dos.sync.file_sync = 1;
    dos.sync.auto_continue = 1;

    vga_puts("[DOS] distributed device interconnect init done \n");
    vga_printf(" local device: Hepu Phone, sync: %s\n",
               dos.sync.clipboard_sync ? " on " : " close ");
}

/* 设备发现 */
int dos_discovery_start(void)
{
    dos.discovery_running = 1;
    return 0;
}

int dos_discovery_stop(void)
{
    dos.discovery_running = 0;
    return 0;
}

int dos_device_register(dos_device_t *dev)
{
    if (!dev) return -1;
    for (int i = 0; i < DOS_MAX_DEVICES; i++) {
        if (!dos.devices[i].used) {
            memcpy(&dos.devices[i], dev, sizeof(dos_device_t));
            dos.devices[i].used = 1;
            dos.devices[i].id = i;
            dos.device_count++;
            return i;
        }
    }
    return -1;
}

int dos_device_unregister(int device_id)
{
    if (device_id <= 0 || device_id >= DOS_MAX_DEVICES || !dos.devices[device_id].used) return -1;
    dos.devices[device_id].used = 0;
    dos.device_count--;
    return 0;
}

int dos_device_get(int device_id, dos_device_t *dev)
{
    if (device_id < 0 || device_id >= DOS_MAX_DEVICES || !dos.devices[device_id].used || !dev) return -1;
    memcpy(dev, &dos.devices[device_id], sizeof(dos_device_t));
    return 0;
}

int dos_device_list(dos_device_t *devs, int max)
{
    if (!devs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DOS_MAX_DEVICES && count < max; i++) {
        if (dos.devices[i].used) memcpy(&devs[count++], &dos.devices[i], sizeof(dos_device_t));
    }
    return count;
}

int dos_device_connect(int device_id)
{
    if (device_id < 0 || device_id >= DOS_MAX_DEVICES || !dos.devices[device_id].used) return -1;
    dos.devices[device_id].state = DOS_DEV_CONNECTED;
    return 0;
}

int dos_device_disconnect(int device_id)
{
    if (device_id < 0 || device_id >= DOS_MAX_DEVICES || !dos.devices[device_id].used) return -1;
    dos.devices[device_id].state = DOS_DEV_OFFLINE;
    return 0;
}

int dos_device_get_local(dos_device_t *dev)
{
    return dos_device_get(dos.local_device_id, dev);
}

int dos_device_set_trust(int device_id, int level)
{
    if (device_id < 0 || device_id >= DOS_MAX_DEVICES || !dos.devices[device_id].used) return -1;
    dos.devices[device_id].trust_level = level < 0 ? 0 : (level > 100 ? 100 : level);
    return 0;
}

int dos_device_authenticate(int device_id)
{
    if (device_id < 0 || device_id >= DOS_MAX_DEVICES || !dos.devices[device_id].used) return -1;
    dos.devices[device_id].authenticated = 1;
    return 0;
}

/* 分布式任务 */
int dos_task_submit(const char *name, int device_id, dos_priority_t priority,
                    uint32_t code_size, uint32_t data_size)
{
    for (int i = 0; i < DOS_MAX_TASKS; i++) {
        if (!dos.tasks[i].used) {
            memset(&dos.tasks[i], 0, sizeof(dos_task_t));
            dos.tasks[i].used = 1;
            dos.tasks[i].id = i;
            if (name) strncpy(dos.tasks[i].name, name, 127);
            dos.tasks[i].state = DOS_TASK_PENDING;
            dos.tasks[i].priority = priority;
            dos.tasks[i].device_id = device_id;
            dos.tasks[i].origin_device = dos.local_device_id;
            dos.tasks[i].code_size = code_size;
            dos.tasks[i].data_size = data_size;
            dos.task_count++;
            dos.total_tasks++;
            return i;
        }
    }
    return -1;
}

int dos_task_cancel(int task_id)
{
    if (task_id < 0 || task_id >= DOS_MAX_TASKS || !dos.tasks[task_id].used) return -1;
    dos.tasks[task_id].state = DOS_TASK_CANCELLED;
    return 0;
}

int dos_task_get(int task_id, dos_task_t *task)
{
    if (task_id < 0 || task_id >= DOS_MAX_TASKS || !dos.tasks[task_id].used || !task) return -1;
    memcpy(task, &dos.tasks[task_id], sizeof(dos_task_t));
    return 0;
}

int dos_task_list(dos_task_t *tasks, int max)
{
    if (!tasks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DOS_MAX_TASKS && count < max; i++) {
        if (dos.tasks[i].used) memcpy(&tasks[count++], &dos.tasks[i], sizeof(dos_task_t));
    }
    return count;
}

int dos_task_migrate(int task_id, int new_device)
{
    if (task_id < 0 || task_id >= DOS_MAX_TASKS || !dos.tasks[task_id].used) return -1;
    dos.tasks[task_id].state = DOS_TASK_MIGRATING;
    dos.tasks[task_id].device_id = new_device;
    dos.tasks[task_id].state = DOS_TASK_RUNNING;
    return 0;
}

int dos_task_complete(int task_id, uint32_t result_size)
{
    if (task_id < 0 || task_id >= DOS_MAX_TASKS || !dos.tasks[task_id].used) return -1;
    dos.tasks[task_id].state = DOS_TASK_COMPLETED;
    dos.tasks[task_id].result_size = result_size;
    dos.tasks[task_id].progress = 100;
    dos.completed_tasks++;
    return 0;
}

int dos_task_get_progress(int task_id, int *progress)
{
    if (task_id < 0 || task_id >= DOS_MAX_TASKS || !dos.tasks[task_id].used || !progress) return -1;
    *progress = dos.tasks[task_id].progress;
    return 0;
}

/* 服务管理 */
int dos_service_register(const char *name, dos_service_type_t type,
                         int device_id, uint32_t capacity)
{
    for (int i = 0; i < DOS_MAX_SERVICES; i++) {
        if (!dos.services[i].used) {
            memset(&dos.services[i], 0, sizeof(dos_service_t));
            dos.services[i].used = 1;
            dos.services[i].id = i;
            if (name) strncpy(dos.services[i].name, name, 127);
            dos.services[i].type = type;
            dos.services[i].device_id = device_id;
            dos.services[i].capacity = capacity;
            dos.services[i].available = 1;
            dos.service_count++;
            return i;
        }
    }
    return -1;
}

int dos_service_unregister(int service_id)
{
    if (service_id < 0 || service_id >= DOS_MAX_SERVICES || !dos.services[service_id].used) return -1;
    dos.services[service_id].used = 0;
    dos.service_count--;
    return 0;
}

int dos_service_get(int service_id, dos_service_t *svc)
{
    if (service_id < 0 || service_id >= DOS_MAX_SERVICES || !dos.services[service_id].used || !svc) return -1;
    memcpy(svc, &dos.services[service_id], sizeof(dos_service_t));
    return 0;
}

int dos_service_list(dos_service_t *svcs, int max)
{
    if (!svcs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DOS_MAX_SERVICES && count < max; i++) {
        if (dos.services[i].used) memcpy(&svcs[count++], &dos.services[i], sizeof(dos_service_t));
    }
    return count;
}

int dos_service_find(dos_service_type_t type, dos_service_t *svcs, int max)
{
    if (!svcs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DOS_MAX_SERVICES && count < max; i++) {
        if (dos.services[i].used && dos.services[i].type == type && dos.services[i].available)
            memcpy(&svcs[count++], &dos.services[i], sizeof(dos_service_t));
    }
    return count;
}

int dos_service_allocate(int service_id, uint32_t amount)
{
    if (service_id < 0 || service_id >= DOS_MAX_SERVICES || !dos.services[service_id].used) return -1;
    if (dos.services[service_id].used_capacity + amount > dos.services[service_id].capacity) return -1;
    dos.services[service_id].used_capacity += amount;
    return 0;
}

int dos_service_release(int service_id, uint32_t amount)
{
    if (service_id < 0 || service_id >= DOS_MAX_SERVICES || !dos.services[service_id].used) return -1;
    if (dos.services[service_id].used_capacity >= amount)
        dos.services[service_id].used_capacity -= amount;
    else
        dos.services[service_id].used_capacity = 0;
    return 0;
}

/* 资源调度 */
int dos_schedule_task(dos_task_t *task, int *best_device)
{
    (void)task;
    if (!best_device) return -1;
    *best_device = dos.local_device_id;
    return 0;
}

int dos_get_best_device(dos_service_type_t service, int *device_id)
{
    (void)service;
    if (!device_id) return -1;
    *device_id = dos.local_device_id;
    return 0;
}

int dos_load_balance(void)
{
    return 0;
}

/* 文件共享 */
int dos_file_share(const char *path, int device_id, int permissions)
{
    for (int i = 0; i < DOS_MAX_FILES; i++) {
        if (!dos.files[i].used) {
            memset(&dos.files[i], 0, sizeof(dos_file_t));
            dos.files[i].used = 1;
            dos.files[i].id = i;
            if (path) {
                strncpy(dos.files[i].path, path, 255);
                const char *name = path;
                for (int j = 0; path[j]; j++) if (path[j] == '/') name = path + j + 1;
                strncpy(dos.files[i].name, name, 127);
            }
            dos.files[i].device_id = device_id;
            dos.files[i].shared = 1;
            dos.files[i].permissions = permissions;
            dos.file_count++;
            return i;
        }
    }
    return -1;
}

int dos_file_unshare(int file_id)
{
    if (file_id < 0 || file_id >= DOS_MAX_FILES || !dos.files[file_id].used) return -1;
    dos.files[file_id].used = 0;
    dos.file_count--;
    return 0;
}

int dos_file_list(dos_file_t *files, int max)
{
    if (!files || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DOS_MAX_FILES && count < max; i++) {
        if (dos.files[i].used) memcpy(&files[count++], &dos.files[i], sizeof(dos_file_t));
    }
    return count;
}

int dos_file_get(int file_id, dos_file_t *file)
{
    if (file_id < 0 || file_id >= DOS_MAX_FILES || !dos.files[file_id].used || !file) return -1;
    memcpy(file, &dos.files[file_id], sizeof(dos_file_t));
    return 0;
}

/* 数据同步 */
int dos_sync_set_config(const dos_sync_config_t *config)
{
    if (!config) return -1;
    memcpy(&dos.sync, config, sizeof(dos_sync_config_t));
    return 0;
}

int dos_sync_get_config(dos_sync_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &dos.sync, sizeof(dos_sync_config_t));
    return 0;
}

int dos_sync_clipboard(const char *text)
{
    (void)text;
    dos.sync_count++;
    return 0;
}

int dos_sync_notification(const char *title, const char *text, int device_id)
{
    (void)title; (void)text; (void)device_id;
    dos.sync_count++;
    return 0;
}

int dos_sync_file(const char *path, int device_id)
{
    (void)path; (void)device_id;
    dos.sync_count++;
    return 0;
}

int dos_continue_app(const char *app_id, int from_device, int to_device)
{
    (void)app_id; (void)from_device; (void)to_device;
    return 0;
}

/* 统计 */
int dos_get_stats(distributed_os_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &dos, sizeof(distributed_os_t));
    return 0;
}
