/* ============================================================================
 * 赫普OS - 系统服务管理器 (systemd式)
 * 服务注册/启动/停止/依赖/监控/自动重启
 *
 * 第二阶段（2026-11-26）：依赖拓扑顺序、健康检查、失败指数退避、
 * 空壳(STUB)显式标注。统一表是 32/64 唯一服务生命周期事实源。
 * ========================================================================== */

#include <stdint.h>
#include "services.h"
#include "vga.h"
#include "string.h"
#include "pit.h"
#include "crash_ctx.h"
#include "event_log.h"

#ifdef HEPU_KERNEL_64
/* 64 位当前没有 32 位 crash_ctx 的 restart snapshot 实现；
 * 服务管理只记录失败事件，不伪造恢复现场。 */
void crash_ctx_snapshot_for_restart(uint32_t pid)
{
    (void)pid;
}
#endif

/* ---------------------------------------------------------------------------
 * 事件码（本模块局部常量；event_log.h 归另一路维护，不在此处改公共头）
 * 10=registered 11=started 12=stopping 13=start failed（第一阶段沿用）
 * 14=backoff scheduled（新：已安排退避重试）
 * 15=give up（新：超过重试上限，停止重试）
 * 16=dependency error（新：缺依赖 / 循环依赖）
 * ------------------------------------------------------------------------- */
#define SVC_EV_REGISTERED    10
#define SVC_EV_STARTED       11
#define SVC_EV_STOPPING      12
#define SVC_EV_START_FAILED  13
#define SVC_EV_BACKOFF       14
#define SVC_EV_GIVEUP        15
#define SVC_EV_DEP_ERROR     16

#define SVC_MAX_RETRIES_DEFAULT 3
#define SVC_BACKOFF_BASE_TICKS  1   /* 退避基数：1/2/4/8 tick 指数递增 */
#define SVC_BACKOFF_MAX_SHIFT    3

static service_t services[SERVICE_MAX];
static int service_count = 0;
static int services_initialized = 0;

/* 测试钩子：替换单调 tick 来源；0 表示用平台默认时钟。 */
static uint64_t (*svc_clock_hook)(void) = 0;

void services_set_clock_hook(uint64_t (*fn)(void))
{
    svc_clock_hook = fn;
}

static uint64_t svc_now_ticks(void)
{
    if (svc_clock_hook) return svc_clock_hook();
#ifdef HEPU_KERNEL_64
    extern uint64_t pit64_get_ticks(void);
    return pit64_get_ticks();
#else
    return (uint64_t)pit_get_ticks();
#endif
}

void services_init(void)
{
    for (int i = 0; i < SERVICE_MAX; i++) {
        services[i].used = 0;
    }
    service_count = 0;
    services_initialized = 1;
    svc_clock_hook = 0;
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
            services[i].health = def->stub ? HEALTH_STUB : HEALTH_UNKNOWN;
            service_count++;
            vga_printf("[SERVICES] registered: %s%s\n", def->name,
                       def->stub ? " (stub)" : "");
            event_log_emit(EVENT_INFO, "service", SVC_EV_REGISTERED,
                           "registered %s%s", def->name,
                           def->stub ? " (stub)" : "");
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

static int svc_index_of(const char *name)
{
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && strcmp(services[i].def.name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int svc_max_retries(const service_t *svc)
{
    return svc->def.max_retries > 0 ? svc->def.max_retries : SVC_MAX_RETRIES_DEFAULT;
}

/* 检查依赖是否都已注册；返回第一个缺失依赖名（out 参数），无缺失返回 0。 */
static int svc_first_missing_dep(const service_t *svc, char *missing, int miss_len)
{
    for (int i = 0; i < svc->def.dep_count; i++) {
        if (svc_index_of(svc->def.depends[i]) < 0) {
            if (missing) strncpy(missing, svc->def.depends[i], miss_len - 1);
            missing[miss_len - 1] = 0;
            return -1;
        }
    }
    return 0;
}

/* 检测到的回边两端（环上服务），供启动路径剔除。 */
static int cycle_a = -1;
static int cycle_b = -1;

/* DFS 循环依赖检测：color 0=白 1=灰 2=黑。回边即环。 */
static int svc_dfs_cycle(int idx, int *color)
{
    color[idx] = 1;
    for (int d = 0; d < services[idx].def.dep_count; d++) {
        int j = svc_index_of(services[idx].def.depends[d]);
        if (j < 0) continue; /* 缺依赖由缺依赖路径单独报错 */
        if (color[j] == 1) {
            cycle_a = idx;
            cycle_b = j;
            vga_printf("[SERVICES] dependency cycle: %s <-> %s\n",
                       services[idx].def.name, services[j].def.name);
            event_log_emit(EVENT_ERROR, "service", SVC_EV_DEP_ERROR,
                           "cycle: %s depends on %s",
                           services[idx].def.name, services[j].def.name);
            return -1;
        }
        if (color[j] == 0 && svc_dfs_cycle(j, color) != 0) return -1;
    }
    color[idx] = 2;
    return 0;
}

static int svc_has_cycle(void)
{
    int color[SERVICE_MAX];
    cycle_a = -1;
    cycle_b = -1;
    for (int i = 0; i < SERVICE_MAX; i++) color[i] = 0;
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && color[i] == 0) {
            if (svc_dfs_cycle(i, color) != 0) return -1;
        }
    }
    return 0;
}

/* Kahn 拓扑排序；返回可排序服务个数，order 内为服务表下标。 */
static int svc_topo_order(int *order, int max_out)
{
    int indeg[SERVICE_MAX];
    int queue[SERVICE_MAX];
    int head = 0, tail = 0, count = 0;

    for (int i = 0; i < SERVICE_MAX; i++) {
        indeg[i] = 0;
        if (!services[i].used) continue;
        for (int d = 0; d < services[i].def.dep_count; d++) {
            if (svc_index_of(services[i].def.depends[d]) >= 0) indeg[i]++;
        }
    }
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && indeg[i] == 0) queue[tail++] = i;
    }
    while (head < tail) {
        int u = queue[head++];
        if (count < max_out) order[count++] = u;
        for (int v = 0; v < SERVICE_MAX; v++) {
            if (!services[v].used) continue;
            int depends_u = 0;
            for (int d = 0; d < services[v].def.dep_count; d++) {
                if (strcmp(services[v].def.depends[d], services[u].def.name) == 0) {
                    depends_u = 1; break;
                }
            }
            if (depends_u && --indeg[v] == 0) queue[tail++] = v;
        }
    }
    return count;
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

    /* 依赖必须已注册：缺依赖明确报错并记事件，不静默跳过。 */
    char missing[SERVICE_NAME_LEN];
    if (svc_first_missing_dep(svc, missing, sizeof(missing)) != 0) {
        vga_printf("[SERVICES] dep missing: %s needs %s\n", name, missing);
        event_log_emit(EVENT_ERROR, "service", SVC_EV_DEP_ERROR,
                       "dep missing: %s needs %s", name, missing);
        svc->state = SERVICE_FAILED;
        svc->health = HEALTH_FAILED;
        return -1;
    }

    /* 依赖未激活则先递归启动；仍不满足则报错。 */
    for (int i = 0; i < svc->def.dep_count; i++) {
        services_start(svc->def.depends[i]);
    }
    for (int i = 0; i < svc->def.dep_count; i++) {
        service_t *dep = find_service(svc->def.depends[i]);
        if (!dep || dep->state != SERVICE_ACTIVE) {
            vga_printf("[SERVICES] dependency unsatisfied: %s needs %s\n",
                       name, svc->def.depends[i]);
            event_log_emit(EVENT_ERROR, "service", SVC_EV_DEP_ERROR,
                           "dep unsatisfied: %s needs %s",
                           name, svc->def.depends[i]);
            svc->state = SERVICE_FAILED;
            svc->health = HEALTH_FAILED;
            return -1;
        }
    }

    svc->state = SERVICE_ACTIVATING;
    vga_printf("[SERVICES] start: %s\n", name);

    if (svc->def.start_func) {
        int ret = svc->def.start_func();
        if (ret != 0) {
            svc->state = SERVICE_FAILED;
            svc->health = HEALTH_FAILED;
            svc->exit_code = ret;
            svc->fail_count++;
            vga_printf("[SERVICES] start failed: %s(code=%d)\n", name, ret);
            event_log_emit(EVENT_ERROR, "service", SVC_EV_START_FAILED,
                           "start failed %s rc=%d", name, ret);

            int maxr = svc_max_retries(svc);
            if (svc->def.restart_on_failure && svc->retry_attempt < maxr) {
                crash_ctx_snapshot_for_restart((uint32_t)svc->def.pid);
                int shift = svc->retry_attempt < SVC_BACKOFF_MAX_SHIFT
                            ? svc->retry_attempt : SVC_BACKOFF_MAX_SHIFT;
                uint32_t delay = SVC_BACKOFF_BASE_TICKS << shift;
                svc->backoff_until = (uint32_t)svc_now_ticks() + delay;
                vga_printf("[SERVICES] backoff: retry %d/%d %s in %d ticks\n",
                           svc->retry_attempt + 1, maxr, name, delay);
                event_log_emit(EVENT_WARN, "service", SVC_EV_BACKOFF,
                               "backoff retry %d/%d %s delay=%d ticks",
                               svc->retry_attempt + 1, maxr, name, delay);
            } else {
                svc->backoff_until = 0;
                vga_printf("[SERVICES] give up: %s after %d failures\n",
                           name, svc->fail_count);
                event_log_emit(EVENT_ERROR, "service", SVC_EV_GIVEUP,
                               "give up %s after %d failures",
                               name, svc->fail_count);
            }
            return -1;
        }
    }

    svc->state = SERVICE_ACTIVE;
    svc->start_time = svc_now_ticks();
    svc->fail_count = 0;
    svc->retry_attempt = 0;
    svc->restart_count = 0;
    svc->backoff_until = 0;
    /* 空壳服务保持 STUB：start_func 返回成功只代表状态机接受请求，
     * 不代表真实 daemon/进程已拉起，不得报 HEALTHY。 */
    if (svc->def.stub) {
        svc->health = HEALTH_STUB;
    } else if (svc->def.start_func) {
        svc->health = HEALTH_HEALTHY;
    } else {
        svc->health = HEALTH_UNKNOWN;
    }
    vga_printf("[SERVICES] started: %s (%s)\n", name,
               svc->health == HEALTH_STUB ? "stub" :
               svc->health == HEALTH_HEALTHY ? "healthy" : "unknown");
    event_log_emit(EVENT_INFO, "service", SVC_EV_STARTED, "started %s%s",
                   name, svc->def.stub ? " (stub)" : "");
    return 0;
}

int services_stop(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc) return -1;
    if (svc->state != SERVICE_ACTIVE) return 0;

    svc->state = SERVICE_DEACTIVATING;
    vga_printf("[SERVICES] stop: %s\n", name);
    event_log_emit(EVENT_INFO, "service", SVC_EV_STOPPING, "stopping %s", name);

    if (svc->def.stop_func) {
        svc->def.stop_func();
    }

    svc->state = SERVICE_INACTIVE;
    svc->stop_time = svc_now_ticks();
    svc->health = svc->def.stub ? HEALTH_STUB : HEALTH_UNKNOWN;
    svc->backoff_until = 0;
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

int services_is_healthy(const char *name)
{
    service_t *svc = find_service(name);
    if (!svc) return 0;
    return (svc->health == HEALTH_HEALTHY) ? 1 : 0;
}

int services_wait(const char *name, int timeout_ms)
{
    (void)timeout_ms;
    service_t *svc = find_service(name);
    if (!svc) return -1;
    /* 原型阶段: 直接返回当前状态 */
    return (svc->state == SERVICE_ACTIVE) ? 0 : -1;
}

/* 内置服务启动函数（当前均为空壳：只打印状态机消息，未拉起真实 daemon/进程） */
static int svc_network_start(void)
{
    vga_puts("[SVC] net service start \n");
    return 0;
}
static int svc_network_stop(void) { return 0; }

static int svc_audio_start(void)
{
#ifdef HEPU_KERNEL_64
    /* 64 位音频后端桥接在 hsservices.c；统一表是唯一生命周期入口。 */
    extern int hs_audio_init(void);
    vga_puts("[SVC] audio service start (backend: hs_audio_init)\n");
    return hs_audio_init();
#else
    vga_puts("[SVC] audio service start \n");
    return 0;
#endif
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

/* 全图校验：缺依赖 / 循环依赖均记事件、置 FAILED 并返回 -1；
 * 供启动流程与 KUnit 复用。 */
int services_validate_graph(void)
{
    int err = 0;
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (!services[i].used) continue;
        char missing[SERVICE_NAME_LEN];
        if (svc_first_missing_dep(&services[i], missing, sizeof(missing)) != 0) {
            vga_printf("[SERVICES] dep missing: %s needs %s\n",
                       services[i].def.name, missing);
            event_log_emit(EVENT_ERROR, "service", SVC_EV_DEP_ERROR,
                           "dep missing: %s needs %s",
                           services[i].def.name, missing);
            services[i].state = SERVICE_FAILED;
            services[i].health = HEALTH_FAILED;
            err = -1;
        }
    }
    if (svc_has_cycle() != 0) {
        for (int k = 0; k < 2; k++) {
            int i = (k == 0) ? cycle_a : cycle_b;
            if (i >= 0 && services[i].used) {
                services[i].state = SERVICE_FAILED;
                services[i].health = HEALTH_FAILED;
            }
        }
        err = -1;
    }
    return err;
}

int services_start_boot(void)
{
    vga_puts("[SERVICES] start boot service ...\n");

    /* 注册内置服务（全部 stub：无真实 daemon/进程，仅状态机） */
    service_def_t def;

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "network");
    strcpy(def.description, " net mgr service (stub: 无真实 daemon/进程)");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_network_start;
    def.stop_func = svc_network_stop;
    def.enabled = 1;
    def.stub = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "audio");
    strcpy(def.description, " audio mgr service (stub: 无真实 daemon/进程)");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_audio_start;
    def.stop_func = svc_audio_stop;
    def.enabled = 1;
    def.stub = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "wifi");
    strcpy(def.description, "WiFi mgr service (stub: 无真实 daemon/进程)");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_wifi_start;
    def.stop_func = svc_wifi_stop;
    def.dep_count = 1;
    strcpy(def.depends[0], "network");
    def.enabled = 1;
    def.stub = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "bluetooth");
    strcpy(def.description, " BT mgr service (stub: 无真实 daemon/进程)");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_bluetooth_start;
    def.stop_func = svc_bluetooth_stop;
    def.dep_count = 1;
    strcpy(def.depends[0], "wifi");
    def.enabled = 1;
    def.stub = 1;
    services_register(&def);

    memset(&def, 0, sizeof(def));
    strcpy(def.name, "gui");
    strcpy(def.description, " graphics 界面 service (stub: 无真实 daemon/进程)");
    def.type = SERVICE_TYPE_DAEMON;
    def.start_policy = SERVICE_START_BOOT;
    def.start_func = svc_gui_start;
    def.stop_func = svc_gui_stop;
    def.dep_count = 2;
    strcpy(def.depends[0], "network");
    strcpy(def.depends[1], "audio");
    def.enabled = 1;
    def.stub = 1;
    services_register(&def);

    /* 1) 全图校验：缺依赖 / 循环依赖明确报出并记事件，不静默跳过。 */
    int bad[SERVICE_MAX];
    for (int i = 0; i < SERVICE_MAX; i++) bad[i] = 0;
    services_validate_graph();

    /* 缺依赖服务置 FAILED 并剔除出启动序列。 */
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (!services[i].used) continue;
        char missing[SERVICE_NAME_LEN];
        if (svc_first_missing_dep(&services[i], missing, sizeof(missing)) != 0) {
            services[i].state = SERVICE_FAILED;
            services[i].health = HEALTH_FAILED;
            bad[i] = 1;
        }
    }

    /* 2) 循环依赖：仅剔除回边两端的环上服务，其余照常排序启动。 */
    for (int k = 0; k < 2; k++) {
        int i = (k == 0) ? cycle_a : cycle_b;
        if (i >= 0 && services[i].used) {
            services[i].state = SERVICE_FAILED;
            services[i].health = HEALTH_FAILED;
            bad[i] = 1;
        }
    }

    /* 3) 拓扑顺序启动：依赖先于被依赖者。 */
    int order[SERVICE_MAX];
    int order_n = svc_topo_order(order, SERVICE_MAX);
    int started = 0;
    for (int k = 0; k < order_n; k++) {
        int i = order[k];
        if (bad[i]) continue;
        if (!services[i].def.enabled ||
            services[i].def.start_policy != SERVICE_START_BOOT) continue;
        if (services_start(services[i].def.name) == 0) started++;
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
    uint64_t now = svc_now_ticks();
    for (int i = 0; i < SERVICE_MAX; i++) {
        service_t *svc = &services[i];
        if (!svc->used || svc->state != SERVICE_FAILED) continue;
        if (!svc->def.restart_on_failure) continue;
        if (svc->backoff_until == 0) continue;                    /* 无待执行重试 / 已放弃 */
        if ((uint64_t)svc->backoff_until > now) continue;         /* 退避未到点 */
        svc->backoff_until = 0;
        svc->retry_attempt++;
        svc->restart_count = svc->retry_attempt;
        vga_printf("[SERVICES] retry after backoff: %s\n", svc->def.name);
        services_start(svc->def.name);
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

int services_healthy_count(void)
{
    int count = 0;
    for (int i = 0; i < SERVICE_MAX; i++) {
        if (services[i].used && services[i].health == HEALTH_HEALTHY) count++;
    }
    return count;
}
