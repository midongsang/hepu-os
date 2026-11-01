#ifndef PRIVACY_COMPUTE_H
#define PRIVACY_COMPUTE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 隐私计算深化
 * 联邦学习 / TEE可信执行环境 / 同态加密 / 安全多方计算
 * ========================================================================== */

#define PC_MAX_PARTICIPANTS 32
#define PC_MAX_MODELS 16
#define PC_MAX_KEYS 64
#define PC_MAX_TASKS 32
#define PC_NAME_LEN 128

/* 联邦学习角色 */
typedef enum {
    PC_FL_COORDINATOR = 0,
    PC_FL_PARTICIPANT,
    PC_FL_AGGREGATOR,
} pc_fl_role_t;

/* 联邦学习算法 */
typedef enum {
    PC_FL_FEDAVG = 0,
    PC_FL_FEDPROX,
    PC_FL_FEDADAM,
    PC_FL_FEDNOVA,
    PC_FL_SCAFFOLD,
    PC_FL_FEDBN,
} pc_fl_algo_t;

/* 同态加密方案 */
typedef enum {
    PC_HE_NONE = 0,
    PC_HE_PAILLIER,
    PC_HE_BFV,
    PC_HE_CKKS,
    PC_HE_BGV,
    PC_HE_FHEW,
} pc_he_scheme_t;

/* MPC协议 */
typedef enum {
    PC_MPC_NONE = 0,
    PC_MPC_SECRET_SHARING,
    PC_MPC_GARLED_CIRCUIT,
    PC_MPC_OT,
    PC_MPC_GMW,
    PC_MPC_SPDZ,
} pc_mpc_protocol_t;

/* TEE类型 */
typedef enum {
    PC_TEE_NONE = 0,
    PC_TEE_TRUSTZONE,
    PC_TEE_SGX,
    PC_TEE_SEV,
    PC_TEE_TDX,
    PC_TEE_HEPU_TEE,
} pc_tee_type_t;

/* 差分隐私机制 */
typedef enum {
    PC_DP_NONE = 0,
    PC_DP_LAPLACE,
    PC_DP_GAUSSIAN,
    PC_DP_EXPONENTIAL,
    PC_DP_GEO,
} pc_dp_mechanism_t;

/* 联邦学习模型 */
typedef struct {
    int used;
    int id;
    char name[PC_NAME_LEN];
    pc_fl_algo_t algorithm;
    int rounds;
    int participants;
    int target_participants;
    float learning_rate;
    float epsilon;  /* 差分隐私预算 */
    float delta;
    int dp_enabled;
    pc_dp_mechanism_t dp_mechanism;
    int he_enabled;
    pc_he_scheme_t he_scheme;
    /* 状态 */
    int current_round;
    int state;  /* 0=idle,1=training,2=aggregating,3=completed,4=error */
    float loss;
    float accuracy;
    uint64_t total_samples;
} pc_fl_model_t;

/* 参与者 */
typedef struct {
    int used;
    int id;
    char name[PC_NAME_LEN];
    char address[64];
    pc_fl_role_t role;
    int connected;
    uint64_t samples;
    float contribution;
    int reputation;
    uint64_t last_update;
} pc_participant_t;

/* TEE会话 */
typedef struct {
    int used;
    int id;
    pc_tee_type_t type;
    int initialized;
    uint64_t session_id;
    uint8_t measurement[32];  /* 度量值 */
    int verified;
    uint64_t memory_size;
    int enclave_count;
} pc_tee_session_t;

/* 密钥 */
typedef struct {
    int used;
    int id;
    char name[PC_NAME_LEN];
    pc_he_scheme_t scheme;
    int key_size;
    int is_private;
    uint64_t create_time;
    int usage_count;
} pc_key_t;

/* MPC任务 */
typedef struct {
    int used;
    int id;
    char name[PC_NAME_LEN];
    pc_mpc_protocol_t protocol;
    int parties;
    int threshold;
    int state;  /* 0=idle,1=setup,2=computing,3=completed */
    uint64_t input_size;
    uint64_t output_size;
    float communication_mb;
    uint64_t compute_time_ms;
} pc_mpc_task_t;

/* 隐私计算配置 */
typedef struct {
    int enabled;
    int tee_enabled;
    pc_tee_type_t tee_type;
    int fl_enabled;
    int he_enabled;
    pc_he_scheme_t default_he;
    int mpc_enabled;
    pc_mpc_protocol_t default_mpc;
    int dp_enabled;
    float dp_epsilon;
    float dp_delta;
    int data_minimization;
    int audit_logging;
} pc_config_t;

/* 隐私计算状态 */
typedef struct {
    int initialized;
    int enabled;
    pc_config_t config;
    /* 联邦学习 */
    pc_fl_model_t models[PC_MAX_MODELS];
    int model_count;
    /* 参与者 */
    pc_participant_t participants[PC_MAX_PARTICIPANTS];
    int participant_count;
    /* TEE */
    pc_tee_session_t tee;
    /* 密钥 */
    pc_key_t keys[PC_MAX_KEYS];
    int key_count;
    /* MPC */
    pc_mpc_task_t mpc_tasks[PC_MAX_TASKS];
    int mpc_count;
    /* 统计 */
    uint64_t total_fl_rounds;
    uint64_t total_he_ops;
    uint64_t total_mpc_tasks;
    uint64_t total_tee_ops;
    uint64_t privacy_budget_used;
} privacy_compute_t;

/* 初始化 */
void privacy_compute_init(void);

/* 配置 */
int pc_set_config(const pc_config_t *config);
int pc_get_config(pc_config_t *config);

/* 联邦学习 */
int pc_fl_create_model(const char *name, pc_fl_algo_t algo, int participants,
                       float lr, float epsilon);
int pc_fl_destroy_model(int model_id);
int pc_fl_start_training(int model_id);
int pc_fl_stop_training(int model_id);
int pc_fl_submit_update(int model_id, int participant_id, float *weights, int count);
int pc_fl_aggregate(int model_id);
int pc_fl_get_model(int model_id, pc_fl_model_t *model);
int pc_fl_list_models(pc_fl_model_t *models, int max);
int pc_fl_get_round(int model_id, int *round, float *loss, float *accuracy);
int pc_fl_add_participant(int model_id, const char *name, const char *address);
int pc_fl_remove_participant(int model_id, int participant_id);

/* TEE */
int pc_tee_init(pc_tee_type_t type);
int pc_tee_create_enclave(uint64_t memory_size);
int pc_tee_destroy_enclave(int enclave_id);
int pc_tee_verify(int enclave_id, uint8_t *expected_measurement);
int pc_tee_get_session(pc_tee_session_t *session);
int pc_tee_execute(int enclave_id, uint8_t *input, int input_len,
                   uint8_t *output, int output_max);

/* 同态加密 */
int pc_he_keygen(pc_he_scheme_t scheme, int key_size, int *pub_key_id, int *priv_key_id);
int pc_he_encrypt(int key_id, uint8_t *plaintext, int len, uint8_t *ciphertext, int max);
int pc_he_decrypt(int key_id, uint8_t *ciphertext, int len, uint8_t *plaintext, int max);
int pc_he_add(uint8_t *ct1, int len1, uint8_t *ct2, int len2, uint8_t *result, int max);
int pc_he_multiply(uint8_t *ct1, int len1, uint8_t *ct2, int len2, uint8_t *result, int max);
int pc_he_get_key(int key_id, pc_key_t *key);

/* MPC */
int pc_mpc_create_task(const char *name, pc_mpc_protocol_t protocol, int parties, int threshold);
int pc_mpc_start(int task_id);
int pc_mpc_submit_input(int task_id, int party_id, uint8_t *data, int len);
int pc_mpc_compute(int task_id, const char *function);
int pc_mpc_get_result(int task_id, uint8_t *result, int max);
int pc_mpc_get_task(int task_id, pc_mpc_task_t *task);
int pc_mpc_list_tasks(pc_mpc_task_t *tasks, int max);

/* 差分隐私 */
int pc_dp_add_noise(float *data, int count, float epsilon, float delta,
                    pc_dp_mechanism_t mechanism);
int pc_dp_get_budget(float *used, float *remaining);

/* 审计 */
int pc_audit_log(const char *event, const char *detail);
int pc_audit_get_count(uint64_t *count);

/* 统计 */
int pc_get_stats(privacy_compute_t *stats);

#endif /* PRIVACY_COMPUTE_H */
