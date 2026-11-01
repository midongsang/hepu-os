/* ============================================================================
 * 赫普OS - 兼容性层
 * Android应用兼容 + Linux应用兼容 + 系统调用映射 + 容器沙箱
 * ========================================================================== */

#include <stdint.h>
#include "compat.h"
#include "vga.h"
#include "string.h"

static compat_t comp;

void compat_init(void)
{
    memset(&comp, 0, sizeof(compat_t));
    comp.initialized = 1;
    comp.android_compat_enabled = 0;
    comp.linux_compat_enabled = 0;

    /* 注册常用系统调用映射 */
    compat_syscall_register(0, 0, 0, "io_setup");
    compat_syscall_register(1, 1, 1, "exit");
    compat_syscall_register(2, 2, 2, "fork");
    compat_syscall_register(3, 3, 3, "read");
    compat_syscall_register(4, 4, 4, "write");
    compat_syscall_register(5, 5, 5, "open");
    compat_syscall_register(6, 6, 6, "close");
    compat_syscall_register(9, 9, 9, "mmap");
    compat_syscall_register(10, 10, 10, "mprotect");
    compat_syscall_register(11, 11, 11, "munmap");
    compat_syscall_register(12, 12, 12, "brk");
    compat_syscall_register(20, 20, 20, "writev");
    compat_syscall_register(39, 39, 39, "getpid");
    compat_syscall_register(42, 42, 42, "pipe");
    compat_syscall_register(57, 57, 57, "fork");
    compat_syscall_register(59, 59, 59, "execve");
    compat_syscall_register(60, 60, 60, "exit");
    compat_syscall_register(72, 72, 72, "fcntl");
    compat_syscall_register(79, 79, 79, "getcwd");
    compat_syscall_register(80, 80, 80, "chdir");
    compat_syscall_register(89, 89, 89, "readlink");
    compat_syscall_register(97, 97, 97, "getrlimit");
    compat_syscall_register(102, 102, 102, "getuid");
    compat_syscall_register(104, 104, 104, "getgid");
    compat_syscall_register(107, 107, 107, "geteuid");
    compat_syscall_register(108, 108, 108, "getegid");
    compat_syscall_register(228, 228, 228, "clock_gettime");
    compat_syscall_register(230, 230, 230, "clock_nanosleep");
    compat_syscall_register(231, 231, 231, "exit_group");
    compat_syscall_register(257, 257, 257, "openat");

    /* 加载默认兼容库 */
    compat_library_load("libc.so", COMPAT_LINUX);
    compat_library_load("libm.so", COMPAT_LINUX);
    compat_library_load("libpthread.so", COMPAT_LINUX);
    compat_library_load("libandroid.so", COMPAT_ANDROID);
    compat_library_load("liblog.so", COMPAT_ANDROID);

    vga_puts("[COMPAT] compat layer init done \n");
    vga_printf(" syscall map: %d, compat lib: %d\n",
               comp.syscall_count, comp.library_count);
}

int android_compat_enable(int enable)
{
    comp.android_compat_enabled = enable ? 1 : 0;
    vga_printf("[COMPAT] Android compat %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int android_app_install(const char *apk_path)
{
    if (!apk_path || !comp.android_compat_enabled) return -1;
    for (int i = 0; i < COMPAT_MAX_ANDROID_APPS; i++) {
        if (!comp.android_apps[i].used) {
            memset(&comp.android_apps[i], 0, sizeof(android_app_t));
            comp.android_apps[i].used = 1;
            comp.android_apps[i].id = i;
            strncpy(comp.android_apps[i].apk_path, apk_path, 255);
            /* 从路径提取包名 */
            const char *name = apk_path;
            const char *slash = strrchr(apk_path, '/');
            if (slash) name = slash + 1;
            strncpy(comp.android_apps[i].package_name, name, 127);
            strncpy(comp.android_apps[i].app_name, name, 127);
            strncpy(comp.android_apps[i].version_name, "1.0", 31);
            comp.android_apps[i].version_code = 1;
            comp.android_apps[i].target_sdk = 33;
            comp.android_apps[i].min_sdk = 21;
            comp.android_apps[i].state = ANDROID_APP_INSTALLED;
            comp.android_apps[i].uid = 10000 + i;
            {
            char tmp[256] = "/data/android/";
            int j = 14;
            while (*name && j < 254) tmp[j++] = *name++;
            tmp[j++] = '/'; tmp[j] = 0;
            strncpy(comp.android_apps[i].data_dir, tmp, 255);
        }
            comp.android_app_count++;
            vga_printf("[COMPAT] install Android app: %s\n", name);
            return i;
        }
    }
    return -1;
}

int android_app_uninstall(const char *package)
{
    if (!package) return -1;
    for (int i = 0; i < COMPAT_MAX_ANDROID_APPS; i++) {
        if (comp.android_apps[i].used && strcmp(comp.android_apps[i].package_name, package) == 0) {
            comp.android_apps[i].used = 0;
            comp.android_app_count--;
            vga_printf("[COMPAT] unmount Android app: %s\n", package);
            return 0;
        }
    }
    return -1;
}

int android_app_launch(const char *package)
{
    if (!package || !comp.android_compat_enabled) return -1;
    for (int i = 0; i < COMPAT_MAX_ANDROID_APPS; i++) {
        if (comp.android_apps[i].used && strcmp(comp.android_apps[i].package_name, package) == 0) {
            comp.android_apps[i].state = ANDROID_APP_RUNNING;
            comp.android_apps[i].pid = 1000 + i;
            comp.android_apps[i].last_launch = 0;
            comp.android_apps[i].launch_count++;
            comp.total_android_launches++;
            vga_printf("[COMPAT] start Android app: %s(pid=%d)\n", package, comp.android_apps[i].pid);
            return comp.android_apps[i].pid;
        }
    }
    return -1;
}

int android_app_stop(const char *package)
{
    if (!package) return -1;
    for (int i = 0; i < COMPAT_MAX_ANDROID_APPS; i++) {
        if (comp.android_apps[i].used && strcmp(comp.android_apps[i].package_name, package) == 0) {
            comp.android_apps[i].state = ANDROID_APP_STOPPED;
            comp.android_apps[i].pid = 0;
            return 0;
        }
    }
    return -1;
}

int android_app_get(const char *package, android_app_t *app)
{
    if (!package || !app) return -1;
    for (int i = 0; i < COMPAT_MAX_ANDROID_APPS; i++) {
        if (comp.android_apps[i].used && strcmp(comp.android_apps[i].package_name, package) == 0) {
            memcpy(app, &comp.android_apps[i], sizeof(android_app_t));
            return i;
        }
    }
    return -1;
}

int android_app_list(android_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < COMPAT_MAX_ANDROID_APPS && count < max; i++) {
        if (comp.android_apps[i].used) memcpy(&apps[count++], &comp.android_apps[i], sizeof(android_app_t));
    }
    return count;
}

int android_app_get_state(const char *package)
{
    if (!package) return -1;
    for (int i = 0; i < COMPAT_MAX_ANDROID_APPS; i++) {
        if (comp.android_apps[i].used && strcmp(comp.android_apps[i].package_name, package) == 0) {
            return comp.android_apps[i].state;
        }
    }
    return -1;
}

int android_compat_is_enabled(void) { return comp.android_compat_enabled; }

int linux_compat_enable(int enable)
{
    comp.linux_compat_enabled = enable ? 1 : 0;
    vga_printf("[COMPAT] Linux compat %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int linux_app_run(const char *path, const char *args, const char *env)
{
    if (!path || !comp.linux_compat_enabled) return -1;
    for (int i = 0; i < COMPAT_MAX_LINUX_APPS; i++) {
        if (!comp.linux_apps[i].used) {
            memset(&comp.linux_apps[i], 0, sizeof(linux_app_t));
            comp.linux_apps[i].used = 1;
            comp.linux_apps[i].id = i;
            const char *name = strrchr(path, '/');
            strncpy(comp.linux_apps[i].name, name ? name + 1 : path, 127);
            strncpy(comp.linux_apps[i].path, path, 255);
            if (args) strncpy(comp.linux_apps[i].args, args, 255);
            if (env) strncpy(comp.linux_apps[i].env, env, 511);
            comp.linux_apps[i].pid = 2000 + i;
            comp.linux_apps[i].state = 1;  /* 运行 */
            comp.linux_app_count++;
            comp.total_linux_launches++;
            vga_printf("[COMPAT] run Linux app: %s(pid=%d)\n", path, comp.linux_apps[i].pid);
            return comp.linux_apps[i].pid;
        }
    }
    return -1;
}

int linux_app_stop(int pid)
{
    for (int i = 0; i < COMPAT_MAX_LINUX_APPS; i++) {
        if (comp.linux_apps[i].used && comp.linux_apps[i].pid == pid) {
            comp.linux_apps[i].state = 0;
            comp.linux_apps[i].exit_code = 0;
            return 0;
        }
    }
    return -1;
}

int linux_app_get(int pid, linux_app_t *app)
{
    if (!app) return -1;
    for (int i = 0; i < COMPAT_MAX_LINUX_APPS; i++) {
        if (comp.linux_apps[i].used && comp.linux_apps[i].pid == pid) {
            memcpy(app, &comp.linux_apps[i], sizeof(linux_app_t));
            return i;
        }
    }
    return -1;
}

int linux_app_list(linux_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < COMPAT_MAX_LINUX_APPS && count < max; i++) {
        if (comp.linux_apps[i].used) memcpy(&apps[count++], &comp.linux_apps[i], sizeof(linux_app_t));
    }
    return count;
}

int linux_compat_is_enabled(void) { return comp.linux_compat_enabled; }

int compat_syscall_register(int host_call, int android_call, int linux_call, const char *name)
{
    if (comp.syscall_count >= COMPAT_MAX_SYS_CALLS) return -1;
    syscall_map_t *m = &comp.syscall_map[comp.syscall_count];
    m->host_call = host_call;
    m->android_call = android_call;
    m->linux_call = linux_call;
    m->name = name;
    m->implemented = 1;
    m->arg_count = 0;
    comp.syscall_count++;
    return 0;
}

int compat_syscall_translate(compat_type_t type, int foreign_call)
{
    for (int i = 0; i < comp.syscall_count; i++) {
        if (type == COMPAT_ANDROID && comp.syscall_map[i].android_call == foreign_call) {
            return comp.syscall_map[i].host_call;
        }
        if (type == COMPAT_LINUX && comp.syscall_map[i].linux_call == foreign_call) {
            return comp.syscall_map[i].host_call;
        }
    }
    return -1;
}

int compat_syscall_get_map(syscall_map_t *map, int max)
{
    if (!map || max <= 0) return 0;
    int count = (comp.syscall_count < max) ? comp.syscall_count : max;
    memcpy(map, comp.syscall_map, sizeof(syscall_map_t) * count);
    return count;
}

int compat_library_load(const char *name, compat_type_t type)
{
    if (!name) return -1;
    for (int i = 0; i < COMPAT_MAX_LIBRARIES; i++) {
        if (!comp.libraries[i].used) {
            memset(&comp.libraries[i], 0, sizeof(compat_library_t));
            comp.libraries[i].used = 1;
            comp.libraries[i].id = i;
            strncpy(comp.libraries[i].name, name, 63);
            strncpy(comp.libraries[i].version, "1.0", 31);
            comp.libraries[i].type = type;
            comp.libraries[i].loaded = 1;
            comp.libraries[i].base_address = 0x40000000 + i * 0x100000;
            comp.libraries[i].size = 0x100000;
            comp.library_count++;
            return i;
        }
    }
    return -1;
}

int compat_library_unload(int lib_id)
{
    if (lib_id < 0 || lib_id >= COMPAT_MAX_LIBRARIES || !comp.libraries[lib_id].used) return -1;
    comp.libraries[lib_id].used = 0;
    comp.library_count--;
    return 0;
}

int compat_library_get(int lib_id, compat_library_t *lib)
{
    if (lib_id < 0 || lib_id >= COMPAT_MAX_LIBRARIES || !comp.libraries[lib_id].used || !lib) return -1;
    memcpy(lib, &comp.libraries[lib_id], sizeof(compat_library_t));
    return 0;
}

int compat_library_list(compat_library_t *libs, int max)
{
    if (!libs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < COMPAT_MAX_LIBRARIES && count < max; i++) {
        if (comp.libraries[i].used) memcpy(&libs[count++], &comp.libraries[i], sizeof(compat_library_t));
    }
    return count;
}

int compat_container_create(compat_type_t type, const char *name)
{
    for (int i = 0; i < 16; i++) {
        if (!comp.containers[i].id && comp.containers[i].type == 0) {
            memset(&comp.containers[i], 0, sizeof(compat_container_t));
            comp.containers[i].id = i + 1;
            comp.containers[i].type = type;
            if (name) strncpy(comp.containers[i].name, name, 63);
            comp.containers[i].isolation_level = 2;
            comp.containers[i].network_access = 1;
            comp.containers[i].file_access = 1;
            comp.containers[i].device_access = 0;
            comp.containers[i].memory_limit = 512 * 1024;  /* 512MB */
            comp.containers[i].cpu_limit = 100;
            {
            char tmp[256] = "/container/";
            int j = 11;
            tmp[j++] = '0' + (i + 1);
            tmp[j++] = '/'; tmp[j] = 0;
            strncpy(comp.containers[i].rootfs, tmp, 255);
        }
            comp.container_count++;
            vga_printf("[COMPAT] create container: %s(type=%d)\n", name ? name : "?", type);
            return comp.containers[i].id;
        }
    }
    return -1;
}

int compat_container_destroy(int container_id)
{
    for (int i = 0; i < 16; i++) {
        if (comp.containers[i].id == container_id) {
            memset(&comp.containers[i], 0, sizeof(compat_container_t));
            comp.container_count--;
            return 0;
        }
    }
    return -1;
}

int compat_container_start(int container_id)
{
    for (int i = 0; i < 16; i++) {
        if (comp.containers[i].id == container_id) {
            comp.containers[i].pid = 3000 + i;
            vga_printf("[COMPAT] start container #%d(pid=%d)\n", container_id, comp.containers[i].pid);
            return 0;
        }
    }
    return -1;
}

int compat_container_stop(int container_id)
{
    for (int i = 0; i < 16; i++) {
        if (comp.containers[i].id == container_id) {
            comp.containers[i].pid = 0;
            return 0;
        }
    }
    return -1;
}

int compat_container_get(int container_id, compat_container_t *container)
{
    if (!container) return -1;
    for (int i = 0; i < 16; i++) {
        if (comp.containers[i].id == container_id) {
            memcpy(container, &comp.containers[i], sizeof(compat_container_t));
            return 0;
        }
    }
    return -1;
}

int compat_container_list(compat_container_t *containers, int max)
{
    if (!containers || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 16 && count < max; i++) {
        if (comp.containers[i].id) memcpy(&containers[count++], &comp.containers[i], sizeof(compat_container_t));
    }
    return count;
}

int compat_bridge_call(compat_type_t type, const char *function, uint32_t *args, int arg_count, uint32_t *result)
{
    (void)type; (void)function; (void)args; (void)arg_count;
    if (result) *result = 0;
    return 0;
}

int compat_convert_path(compat_type_t type, const char *foreign_path, char *host_path, int len)
{
    if (!foreign_path || !host_path || len <= 0) return -1;
    if (type == COMPAT_ANDROID) {
        {
            strncpy(host_path, "/android_root", len - 1);
            int j = 13;
            while (*foreign_path && j < len - 1) host_path[j++] = *foreign_path++;
            host_path[j] = 0;
        }
    } else if (type == COMPAT_LINUX) {
        {
            strncpy(host_path, "/linux_root", len - 1);
            int j = 11;
            while (*foreign_path && j < len - 1) host_path[j++] = *foreign_path++;
            host_path[j] = 0;
        }
    } else {
        strncpy(host_path, foreign_path, len - 1);
    }
    return 0;
}

int compat_convert_error(int foreign_error)
{
    /* 原型阶段: 直接返回 */
    return foreign_error;
}

int compat_android_app_count(void) { return comp.android_app_count; }
int compat_linux_app_count(void) { return comp.linux_app_count; }
int compat_container_count(void) { return comp.container_count; }
