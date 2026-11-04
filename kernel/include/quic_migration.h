#ifndef QUIC_MIGRATION_H
#include "types.h"
#define QUIC_MIGRATION_H

#include <stdint.h>
#include "quic.h"

#define QUIC_MIGRATION_MAX_PATHS 4
#define QUIC_MIGRATION_PROBE_TIMEOUT 3000  /* 3秒 */
#define QUIC_MIGRATION_MAX_IDS 8

/* QUIC路径状态 */
typedef enum {
    QUIC_PATH_UNKNOWN = 0,
    QUIC_PATH_PROBING = 1,
    QUIC_PATH_ACTIVE = 2,
    QUIC_PATH_FAILED = 3,
    QUIC_PATH_INACTIVE = 4
} quic_path_state_t;

/* QUIC路径 */
typedef struct {
    u8  local_addr[16];   /* 本地地址（IPv4/IPv6） */
    u8  remote_addr[16];  /* 远程地址 */
    u16 local_port;
    u16 remote_port;
    u32 rtt;
    u32 rtt_var;
    u64 bytes_sent;
    u64 bytes_received;
    u64 packets_sent;
    u64 packets_received;
    u32 last_probe_time;
    quic_path_state_t state;
    int      in_use;
} quic_path_t;

/* QUIC连接迁移状态 */
typedef struct {
    quic_path_t paths[QUIC_MIGRATION_MAX_PATHS];
    int      active_path;
    int      migrating;
    u64 migration_start_time;
    u8  connection_ids[QUIC_MIGRATION_MAX_IDS][20];
    u8  connection_id_lens[QUIC_MIGRATION_MAX_IDS];
    u64 next_connection_id_seq;
    int      connection_id_count;
    int      enabled;
} quic_migration_t;

void quic_migration_init(void);
int quic_migration_enable(quic_migration_t *migration, int enable);
int quic_migration_add_path(quic_migration_t *migration,
                            const u8 *local_addr, u16 local_port,
                            const u8 *remote_addr, u16 remote_port);
int quic_migration_remove_path(quic_migration_t *migration, int path_index);
int quic_migration_start(quic_migration_t *migration, int new_path_index);
int quic_migration_complete(quic_migration_t *migration);
int quic_migration_fail(quic_migration_t *migration);
int quic_migration_send_path_challenge(quic_migration_t *migration, int path_index);
int quic_migration_handle_path_response(quic_migration_t *migration,
                                         const u8 *data, u32 len);
int quic_migration_add_connection_id(quic_migration_t *migration,
                                     const u8 *cid, u8 cid_len);
int quic_migration_retire_connection_id(quic_migration_t *migration,
                                         u64 seq_num);
int quic_migration_get_active_path(const quic_migration_t *migration,
                                    quic_path_t *path);
int quic_migration_get_best_path(const quic_migration_t *migration);
int quic_migration_is_migrating(const quic_migration_t *migration);
int quic_migration_get_stats(const quic_migration_t *migration,
                              u32 *paths, u32 *active_path,
                              u64 *total_bytes);
const char *quic_path_state_name(quic_path_state_t state);

#endif /* QUIC_MIGRATION_H */
