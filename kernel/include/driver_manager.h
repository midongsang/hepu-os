#ifndef DRIVER_MANAGER_H
#define DRIVER_MANAGER_H

#include <stdint.h>

/* 驱动状态 */
typedef enum {
    DM_STATE_UNLOADED = 0,
    DM_STATE_LOADED,
    DM_STATE_PROBING,
    DM_STATE_READY,
    DM_STATE_SUSPENDED,
    DM_STATE_ERROR
} dm_state_t;

/* 驱动操作接口 */
typedef struct {
    int (*probe)(void *dev);       /* 探测硬件 */
    int (*init)(void *dev);        /* 初始化 */
    int (*suspend)(void *dev);     /* 挂起 */
    int (*resume)(void *dev);      /* 恢复 */
    int (*shutdown)(void *dev);    /* 关闭 */
    int (*ioctl)(void *dev, uint32_t cmd, void *arg);
} dm_ops_t;

/* 驱动描述符 */
typedef struct {
    char name[32];
    char author[32];
    char version[16];
    char description[128];
    uint32_t category;      /* 硬件类别 */
    uint32_t priority;      /* 加载优先级 */
    dm_ops_t *ops;
    void *private_data;
} dm_desc_t;

/* 驱动管理API */
void driver_manager_init(void);
int driver_register(dm_desc_t *desc);
int driver_unregister(int driver_id);
int driver_probe(int driver_id, void *dev);
int driver_init(int driver_id, void *dev);
int driver_suspend(int driver_id);
int driver_resume(int driver_id);
int driver_shutdown(int driver_id);
int driver_ioctl(int driver_id, uint32_t cmd, void *arg);
int driver_get_state(int driver_id);
int driver_list(dm_desc_t *descs, int max);
int driver_find_by_name(const char *name);
int driver_find_by_category(uint32_t category, dm_desc_t *descs, int max);
int driver_suspend_all(void);
int driver_resume_all(void);
int driver_shutdown_all(void);
int driver_get_count(void);
int driver_get_ready_count(void);

#endif /* DRIVER_MANAGER_H */
