#ifndef DISTRIBUTED_H
#define DISTRIBUTED_H

#include <stdint.h>

#define DIST_MAX_NODES 64
#define DIST_MAX_SERVICES 128
#define DIST_MAX_PODS 256
#define DIST_MAX_FILES 1024
#define DIST_MAX_CHUNKS 4096
#define DIST_MAX_REPLICAS 3

/* 节点状态 */
typedef enum {
    NODE_STATUS_UNKNOWN = 0,
    NODE_STATUS_READY,
    NODE_STATUS_NOT_READY,
    NODE_STATUS_MAINTENANCE,
    NODE_STATUS_OFFLINE,
    NODE_STATUS_DRAINING,
} node_status_t;

/* 节点角色 */
typedef enum {
    NODE_ROLE_WORKER = 0,
    NODE_ROLE_MASTER,
    NODE_ROLE_CONTROL_PLANE,
    NODE_ROLE_EDGE,
    NODE_ROLE_STORAGE,
    NODE_ROLE_GPU,
} node_role_t;

/* 集群节点 */
typedef struct {
    int used;
    int id;
    char name[64];
    char hostname[128];
    char ip_address[32];
    char mac_address[32];
    node_status_t status;
    node_role_t role;
    /* 资源 */
    int cpu_cores;
    int cpu_used;
    uint64_t memory_total;
    uint64_t memory_used;
    uint64_t disk_total;
    uint64_t disk_used;
    int gpu_count;
    int gpu_used;
    /* 网络 */
    uint64_t network_rx;
    uint64_t network_tx;
    int pod_count;
    /* 版本 */
    char os_version[32];
    char kernel_version[32];
    char container_runtime[32];
    char kubelet_version[32];
    /* 统计 */
    uint64_t uptime;
    uint64_t last_heartbeat;
    int heartbeat_failures;
    /* 标签 */
    char labels[16][64];
    int label_count;
    /* 污点 */
    char taints[8][128];
    int taint_count;
} cluster_node_t;

/* 服务状态 */
typedef enum {
    DIST_SERVICE_PENDING = 0,
    DIST_SERVICE_RUNNING,
    DIST_SERVICE_STOPPED,
    DIST_SERVICE_FAILED,
    DIST_SERVICE_UPDATING,
    DIST_SERVICE_SCALING,
} dist_service_status_t;

/* 服务 */
typedef struct {
    int used;
    int id;
    char name[64];
    char namespace_[32];
    dist_service_status_t status;
    int replicas;
    int ready_replicas;
    int available_replicas;
    char image[128];
    char command[256];
    /* 资源 */
    int cpu_request;
    int cpu_limit;
    uint64_t memory_request;
    uint64_t memory_limit;
    /* 端口 */
    int ports[32];
    int port_count;
    /* 环境变量 */
    char env[512];
    /* 存储 */
    char volumes[256];
    /* 网络 */
    char service_type[32];  /* ClusterIP/NodePort/LoadBalancer/ExternalName */
    char cluster_ip[32];
    char external_ip[32];
    /* 健康检查 */
    int health_check_enabled;
    int health_check_port;
    char health_check_path[128];
    /* 滚动更新 */
    int max_unavailable;
    int max_surge;
    /* 统计 */
    uint64_t created_time;
    uint64_t restarts;
    uint64_t cpu_usage;
    uint64_t memory_usage;
} distributed_service_t;

/* Pod状态 */
typedef enum {
    POD_PENDING = 0,
    POD_RUNNING,
    POD_SUCCEEDED,
    POD_FAILED,
    POD_UNKNOWN,
} pod_status_t;

/* Pod */
typedef struct {
    int used;
    int id;
    char name[64];
    char namespace_[32];
    pod_status_t status;
    int node_id;
    int service_id;
    char node_name[64];
    char pod_ip[32];
    char host_ip[32];
    /* 容器 */
    int container_count;
    char containers[8][64];
    /* 资源 */
    int cpu_usage;
    uint64_t memory_usage;
    /* 统计 */
    uint64_t start_time;
    int restart_count;
    uint64_t cpu_time;
    uint64_t memory_peak;
} distributed_pod_t;

/* 分布式文件系统文件 */
typedef struct {
    int used;
    int id;
    char path[256];
    uint64_t size;
    int chunk_count;
    int chunk_ids[64];
    int replication_factor;
    int permissions;
    int owner;
    int group;
    uint64_t created_time;
    uint64_t modified_time;
    uint64_t accessed_time;
    int directory;
    int parent_id;
} dfs_file_t;

/* 数据块 */
typedef struct {
    int used;
    int id;
    int file_id;
    int chunk_index;
    uint64_t size;
    uint64_t offset;
    char checksum[64];  /* SHA256 */
    int replica_nodes[DIST_MAX_REPLICAS];
    int replica_count;
    int healthy;
    uint64_t last_verified;
} dfs_chunk_t;

/* 分布式文件系统状态 */
typedef struct {
    int initialized;
    int enabled;
    dfs_file_t files[DIST_MAX_FILES];
    int file_count;
    dfs_chunk_t chunks[DIST_MAX_CHUNKS];
    int chunk_count;
    uint64_t total_space;
    uint64_t used_space;
    uint64_t available_space;
    int replication_default;
    int chunk_size;  /* 默认64MB */
    char namenode[64];
    int datanode_count;
} distributed_fs_t;

/* 集群状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 集群信息 */
    char cluster_name[64];
    char cluster_id[64];
    char api_server[128];
    int node_count;
    int ready_nodes;
    /* 节点 */
    cluster_node_t nodes[DIST_MAX_NODES];
    /* 服务 */
    distributed_service_t services[DIST_MAX_SERVICES];
    int service_count;
    /* Pod */
    distributed_pod_t pods[DIST_MAX_PODS];
    int pod_count;
    /* 分布式文件系统 */
    distributed_fs_t dfs;
    /* 调度器 */
    char scheduler[32];  /* default/自定义 */
    int scheduling_algorithm;  /* 0=spread,1=binpack,2=random */
    /* 统计 */
    uint64_t total_deployments;
    uint64_t total_scheduling;
    uint64_t total_failures;
    uint64_t cluster_cpu_capacity;
    uint64_t cluster_memory_capacity;
    uint64_t cluster_cpu_used;
    uint64_t cluster_memory_used;
} distributed_t;

/* 初始化分布式系统 */
void distributed_init(void);

/* 集群管理 */
int dist_cluster_join(const char *name, const char *ip, node_role_t role);
int dist_cluster_leave(int node_id);
int dist_cluster_get_node(int node_id, cluster_node_t *node);
int dist_cluster_list_nodes(cluster_node_t *nodes, int max);
int dist_cluster_get_ready_nodes(cluster_node_t *nodes, int max);
int dist_cluster_heartbeat(int node_id);
int dist_cluster_cordon(int node_id);
int dist_cluster_uncordon(int node_id);
int dist_cluster_drain(int node_id);

/* 服务管理 */
int dist_service_create(const char *name, const char *image, int replicas);
int dist_service_delete(int service_id);
int dist_service_scale(int service_id, int replicas);
int dist_service_update(int service_id, const char *image);
int dist_service_rollback(int service_id);
int dist_service_get(int service_id, distributed_service_t *service);
int dist_service_list(distributed_service_t *services, int max);
int dist_service_get_by_name(const char *name, distributed_service_t *service);
int dist_service_logs(int service_id, int tail);

/* Pod管理 */
int dist_pod_schedule(int service_id);
int dist_pod_delete(int pod_id);
int dist_pod_get(int pod_id, distributed_pod_t *pod);
int dist_pod_list(distributed_pod_t *pods, int max);
int dist_pod_list_by_node(int node_id, distributed_pod_t *pods, int max);
int dist_pod_list_by_service(int service_id, distributed_pod_t *pods, int max);
int dist_pod_logs(int pod_id, int tail);
int dist_pod_exec(int pod_id, const char *command);

/* 分布式文件系统 */
int dist_dfs_mkdir(const char *path);
int dist_dfs_create(const char *path, const void *data, uint64_t size);
int dist_dfs_read(const char *path, void *buffer, uint64_t max_size);
int dist_dfs_write(const char *path, const void *data, uint64_t size);
int dist_dfs_delete(const char *path);
int dist_dfs_list(const char *path, dfs_file_t *files, int max);
int dist_dfs_stat(const char *path, dfs_file_t *file);
int dist_dfs_replicate(int file_id, int factor);
int dist_dfs_balance(void);
int dist_dfs_get_stats(distributed_fs_t *stats);

/* 调度器 */
int dist_schedule_pod(distributed_pod_t *pod);
int dist_scheduler_set_algorithm(int algorithm);
int dist_scheduler_get_algorithm(int *algorithm);

/* 统计 */
int dist_get_stats(distributed_t *stats);
int dist_get_cluster_stats(uint64_t *cpu_capacity, uint64_t *cpu_used, uint64_t *mem_capacity, uint64_t *mem_used);

#endif /* DISTRIBUTED_H */
