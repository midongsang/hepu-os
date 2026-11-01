/* ============================================================================
 * 赫普OS - 系统服务管理器 (systemd式)
 * 服务注册/启动/停止/依赖/监控/自动重启
 * ========================================================================== */

#include <stdint.h>
#include "services.h"
#include "vga.h"
#include "string.h"
#include "pit.h"

static service_t services[SERVICE_MAX];
static int service_count = 0;
static int services_initialized = 0;

void services_init(void)
{
    for (int i = 0; i < SERVICE_MAX; i++) {
        services[i].used = 0;
    }
    service_count = 0;
    services_initialized = 1;
    vga_puts("[SERVICES] service manager init done \n");
}

int services_register(service_def_t *def)
{
    if (!def || !services_initialized) return -1;

    /* 检查是否已注册 */
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && strcmp(services[i].def.name, def->name) == 0) {
            return -1;
        }
    }

    for (int i = 0; i < SERVICE_MAX; i++) {
        if (!services[i].used) {
            memset(&services[i], 0, sizeof(service_t));
            services[i].used = 1;
            memcpy(&services[i].def, def, sizeof(service_def_t));
            services[i].state = SERVICE_INACTIVE;
            service_count++;
            vga_printf("[SERVICES] registered: %s\n", def->name);
            return 0;
        }
    }
    return -1;
}

static service_t *find_service(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && strcmp(services[i].def.name, name) == 0) {
            return &services[i];
        }
    }
    return NULL;
}

static int check_dependencies(service_t *svc)
{
    for (int i = 0; i < svc->def.dep_count; i++) {
        service_t *dep = find_service(svc->def.depends[i]);
        if (!dep || dep->state != SERVICE_ACTIVE) {
            vga_printf("[SERVICES] dependency unsatisfied: %s needs %s\n",
                       svc->def.name, svc->def.depends[i]);
            return -1;
        }
    }
    return 0;
}

int services_start(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc) {
        vga_printf("[SERVICES] error: service '%s' not found \n", name);
        return -1;
    }
    if (svc->state == SERVICE_ACTIVE || svc->state == SERVICE_ACTIVATING) {
        return 0;
    }

    /* 检查依赖 */
    if (check_dependencies(svc) != 0) {
        /* 尝试启动依赖 */
        for (int i = 0; i < svc->def.dep_count; i++) {
            services_start(svc->def.depends[i]);
        }
        if (check_dependencies(svc) != 0) return -1;
    }

    svc->state = SERVICE_ACTIVATING;
    vga_printf("[SERVICES] start: %s\n", name);

    if (svc->def.start_func) {
        int ret = svc->def.start_func();
        if (ret != 0) {
            svc->state = SERVICE_FAILED;
            svc->exit_code = ret;
            vga_printf("[SERVICES] start failed: %s(code=%d)\n", name, ret);
            if (svc->def.restart_on_failure && svc->restart_count < 3) {
                svc->restart_count++;
                vga_printf("[SERVICES] auto reboot(%d/3): %s\n", svc->restart_count, name);
                return services_start(name);
            }
            return -1;
        }
    }

    svc->state = SERVICE_ACTIVE;
    svc->start_time = 0;  /* 原型阶段 */
    svc->restart_count = 0;
    vga_printf("[SERVICES] started: %s\n", name);
    return 0;
}

int services_stop(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc) return -1;
    if (svc->state != SERVICE_ACTIVE) return 0;

    svc->state = SERVICE_DEACTIVATING;
    vga_printf("[SERVICES] stop: %s\n", name);

    if (svc->def.stop_func) {
        svc->def.stop_func();
    }

    svc->state = SERVICE_INACTIVE;
    svc->stop_time = 0;
    return 0;
}

int services_restart(const char *name)
{
    int ret = services_stop(name);
    if (ret != 0) return ret;
    return services_start(name);
}

int services_reload(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc || svc->state != SERVICE_ACTIVE) return -1;
    vga_printf("[SERVICES] re- load: %s\n", name);
    return 0;
}

int services_status(const char *name, service_t *status)
{
    service_t *svc = find_service(name);
    if (!svc || !status) return -1;
    memcpy(status, svc, sizeof(service_t));
    return 0;
}

int services_list(service_t *list, int max)
{
    if (!list) return 0;
    int count = 0;
    for (int i = 0; i < SERVICE_MAX && count < max; i++) {
        if (services[i].used) {
            memcpy(&list[count], &services[i], sizeof(service_t));
            count++;
        }
    }
    return count;
}

int services_enable(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc) return -1;
    svc->def.enabled = 1;
    svc->def.start_policy = SERVICE_START_BOOT;
    return 0;
}

int services_disable(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc) return -1;
    svc->def.enabled = 0;
    svc->def.start_policy = SERVICE_START_MANUAL;
    return 0;
}

int services_is_active(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc) return 0;
    return (svc->state == SERVICE_ACTIVE) ? 1 : 0;
}

int services_wait(const char *name, int timeout_ms)
{
    (void)timeout_ms;
    service_t *svc = find_service(name);
    if (!svc) return -1;
    /* 原型阶段: 直接返回当前状态 */
    return (svc->state == SERVICE_ACTIVE) ? 0 : -1;
}

/* 内置服务启动函数 */
static int svc_network_start(void)
{
    vga_puts("[SVC] net service start \n");
    return 0;
}
static int svc_network_stop(void) { return 0; }

static int svc_audio_start(void)
{
    vga_puts("[SVC] audio service start \n");
    return 0;
}
static int svc_audio_stop(void) { return 0; }

static int svc_bluetooth_start(void)
{
    vga_puts("[SVC] BT service start \n");
    return 0;
}
static int svc_bluetooth_stop(void) { return 0; }

static int svc_wifi_start(void)
{
    vga_puts("[SVC] WiFi service start \n");
    return 0;
}
static int svc_wifi_stop(void) { return 0; }

static int svc_gui_start(void)
{
    vga_puts("[SVC] GUI service start \n");
    return 0;
}
static int svc_gui_stop(void) { return 0; }

int services_start_boot(void)
{
    vga_puts("[SERVICES] start boot service ...\n");

    /* 注册内置服务 */
    service_def_t def;

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "network");
    strcpy(def.description, " net mgr service ");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_network_start;
    def.stop_func = svc_network_stop;
    def.enabled = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "audio");
    strcpy(def.description, " audio mgr service ");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_audio_start;
    def.stop_func = svc_audio_stop;
    def.enabled = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "bluetooth");
    strcpy(def.description, " BT mgr service ");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_bluetooth_start;
    def.stop_func = svc_bluetooth_stop;
    def.enabled = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "wifi");
    strcpy(def.description, "WiFi mgr service ");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_wifi_start;
    def.stop_func = svc_wifi_stop;
    def.enabled = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "gui");
    strcpy(def.description, " graphics 界面 service ");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_gui_start;
    def.stop_func = svc_gui_stop;
    def.dep_count = 1;
    strcpy(def.depends[0], "network");
    def.enabled = 1;
    services_register(&def);

    /* 启动所有enabled的服务 */
    int started = 0;
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && services[i].def.enabled &&
            services[i].def.start_policy == SERVICE_START_BOOT) {
            if (services_start(services[i].def.name) == 0) started++;
        }
    }

    vga_printf("[SERVICES] boot services started: %d/%d\n", started, service_count);
    return started;
}

int services_stop_all(void)
{
    int stopped = 0;
    /* 逆序停止 (先停依赖者) */
    for (int i = SERVICE_MAX - 1; i >= 0; i--) {
        if (services[i].used && services[i].state == SERVICE_ACTIVE) {
            if (services_stop(services[i].def.name) == 0) stopped++;
        }
    }
    return stopped;
}

void services_monitor_tick(void)
{
    /* 原型阶段: 监控服务状态, 自动重启失败的服务 */
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && services[i].state == SERVICE_FAILED &&
            services[i].def.restart_on_failure && services[i].restart_count < 3) {
            services[i].restart_count++;
            services_start(services[i].def.name);
        }
    }
}

int services_count(void)
{
    return service_count;
}

int services_active_count(void)
{
    int count = 0;
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && services[i].state == SERVICE_ACTIVE) count++;
    }
    return count;
}
