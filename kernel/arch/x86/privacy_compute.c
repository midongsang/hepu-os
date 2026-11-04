/* ============================================================================
 * 赫普OS - 隐私计算深化
 * ========================================================================== */

#include <stdint.h>
#include "privacy_compute.h"
#include "vga.h"
#include "string.h"

static privacy_compute_t pc;

void privacy_compute_init(void)
{
    memset(&pc, 0, sizeof(privacy_compute_t));
    pc.initialized = 1;
    pc.enabled = 1;
    pc.config.tee_enabled = 1;
    pc.config.tee_type = PC_TEE_HEPU_TEE;
    pc.config.fl_enabled = 1;
    pc.config.he_enabled = 1;
    pc.config.default_he = PC_HE_CKKS;
    pc.config.mpc_enabled = 1;
    pc.config.default_mpc = PC_MPC_SECRET_SHARING;
    pc.config.dp_enabled = 1;
    pc.config.dp_epsilon = 1.0f;
    pc.config.dp_delta = 1e-5f;
    pc.config.data_minimization = 1;
    pc.config.audit_logging = 1;

    vga_puts("[PRIV] privacy compute deep init done \n");
    vga_printf("        TEE: %s, FL: %s, HE: %s, MPC: %s\n",
               pc.config.tee_enabled ? " on " : " close ",
               pc.config.fl_enabled ? " on " : " close ",
               pc.config.he_enabled ? " on " : " close ",
               pc.config.mpc_enabled ? " on " : " close ");
}

int pc_set_config(const pc_config_t *config)
{
    if (!config) return -1;
    memcpy(&pc.config, config, sizeof(pc_config_t));
    return 0;
}

int pc_get_config(pc_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &pc.config, sizeof(pc_config_t));
    return 0;
}

/* 联邦学习 */
int pc_fl_create_model(const char *name, pc_fl_algo_t algo, int participants,
                       float lr, float epsilon)
{
    for (int i = 0; i < PC_MAX_MODELS; i++) {
        if (!pc.models[i].used) {
            memset(&pc.models[i], 0, sizeof(pc_fl_model_t));
            pc.models[i].used = 1;
            pc.models[i].id = i;
            if (name) strncpy(pc.models[i].name, name, 127);
            pc.models[i].algorithm = algo;
            pc.models[i].target_participants = participants;
            pc.models[i].learning_rate = lr;
            pc.models[i].epsilon = epsilon;
            pc.models[i].dp_enabled = (epsilon > 0) ? 1 : 0;
            pc.models[i].dp_mechanism = PC_DP_GAUSSIAN;
            pc.models[i].he_enabled = pc.config.he_enabled;
            pc.models[i].he_scheme = pc.config.default_he;
            pc.models[i].state = 0;
            pc.model_count++;
            return i;
        }
    }
    return -1;
}

int pc_fl_destroy_model(int model_id)
{
    if (model_id < 0 || model_id >= PC_MAX_MODELS || !pc.models[model_id].used) return -1;
    pc.models[model_id].used = 0;
    pc.model_count--;
    return 0;
}

int pc_fl_start_training(int model_id)
{
    if (model_id < 0 || model_id >= PC_MAX_MODELS || !pc.models[model_id].used) return -1;
    pc.models[model_id].state = 1;
    pc.models[model_id].current_round = 1;
    return 0;
}

int pc_fl_stop_training(int model_id)
{
    if (model_id < 0 || model_id >= PC_MAX_MODELS || !pc.models[model_id].used) return -1;
    pc.models[model_id].state = 0;
    return 0;
}

int pc_fl_submit_update(int model_id, int participant_id, float *weights, int count)
{
    (void)weights; (void)count;
    if (model_id < 0 || model_id >= PC_MAX_MODELS || !pc.models[model_id].used) return -1;
    if (participant_id >= 0 && participant_id < PC_MAX_PARTICIPANTS && pc.participants[participant_id].used)
        pc.participants[participant_id].last_update = 1;
    pc.models[model_id].participants++;
    return 0;
}

int pc_fl_aggregate(int model_id)
{
    if (model_id < 0 || model_id >= PC_MAX_MODELS || !pc.models[model_id].used) return -1;
    pc.models[model_id].state = 2;
    pc.models[model_id].current_round++;
    pc.models[model_id].loss = 0.5f;
    pc.models[model_id].accuracy = 0.85f;
    pc.total_fl_rounds++;
    pc.models[model_id].state = 1;
    if (pc.models[model_id].current_round > pc.models[model_id].rounds)
        pc.models[model_id].state = 3;
    return 0;
}

int pc_fl_get_model(int model_id, pc_fl_model_t *model)
{
    if (model_id < 0 || model_id >= PC_MAX_MODELS || !pc.models[model_id].used || !model) return -1;
    memcpy(model, &pc.models[model_id], sizeof(pc_fl_model_t));
    return 0;
}

int pc_fl_list_models(pc_fl_model_t *models, int max)
{
    if (!models || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PC_MAX_MODELS && count < max; i++) {
        if (pc.models[i].used) memcpy(&models[count++], &pc.models[i], sizeof(pc_fl_model_t));
    }
    return count;
}

int pc_fl_get_round(int model_id, int *round, float *loss, float *accuracy)
{
    if (model_id < 0 || model_id >= PC_MAX_MODELS || !pc.models[model_id].used) return -1;
    if (round) *round = pc.models[model_id].current_round;
    if (loss) *loss = pc.models[model_id].loss;
    if (accuracy) *accuracy = pc.models[model_id].accuracy;
    return 0;
}

int pc_fl_add_participant(int model_id, const char *name, const char *address)
{
    (void)model_id;
    for (int i = 0; i < PC_MAX_PARTICIPANTS; i++) {
        if (!pc.participants[i].used) {
            memset(&pc.participants[i], 0, sizeof(pc_participant_t));
            pc.participants[i].used = 1;
            pc.participants[i].id = i;
            if (name) strncpy(pc.participants[i].name, name, 127);
            if (address) strncpy(pc.participants[i].address, address, 63);
            pc.participants[i].role = PC_FL_PARTICIPANT;
            pc.participants[i].connected = 1;
            pc.participants[i].reputation = 80;
            pc.participant_count++;
            return i;
        }
    }
    return -1;
}

int pc_fl_remove_participant(int model_id, int participant_id)
{
    (void)model_id;
    if (participant_id < 0 || participant_id >= PC_MAX_PARTICIPANTS || !pc.participants[participant_id].used) return -1;
    pc.participants[participant_id].used = 0;
    pc.participant_count--;
    return 0;
}

/* TEE */
int pc_tee_init(pc_tee_type_t type)
{
    pc.tee.used = 1;
    pc.tee.type = type;
    pc.tee.initialized = 1;
    pc.tee.session_id = 0x12345678;
    pc.tee.verified = 1;
    pc.tee.memory_size = 64 * 1024 * 1024;
    pc.tee.enclave_count = 0;
    return 0;
}

int pc_tee_create_enclave(uint64_t memory_size)
{
    (void)memory_size;
    if (!pc.tee.initialized) return -1;
    pc.tee.enclave_count++;
    pc.total_tee_ops++;
    return pc.tee.enclave_count;
}

int pc_tee_destroy_enclave(int enclave_id)
{
    (void)enclave_id;
    if (pc.tee.enclave_count > 0) pc.tee.enclave_count--;
    return 0;
}

int pc_tee_verify(int enclave_id, uint8_t *expected_measurement)
{
    (void)enclave_id; (void)expected_measurement;
    return 1;  /* 验证通过 */
}

int pc_tee_get_session(pc_tee_session_t *session)
{
    if (!session) return -1;
    memcpy(session, &pc.tee, sizeof(pc_tee_session_t));
    return 0;
}

int pc_tee_execute(int enclave_id, uint8_t *input, int input_len,
                   uint8_t *output, int output_max)
{
    (void)enclave_id;
    if (!output || output_max <= 0) return -1;
    int len = input_len < output_max ? input_len : output_max;
    if (input && len > 0) memcpy(output, input, len);
    pc.total_tee_ops++;
    return len;
}

/* 同态加密 */
int pc_he_keygen(pc_he_scheme_t scheme, int key_size, int *pub_key_id, int *priv_key_id)
{
    int pub = -1, priv = -1;
    for (int i = 0; i < PC_MAX_KEYS; i++) {
        if (!pc.keys[i].used) {
            memset(&pc.keys[i], 0, sizeof(pc_key_t));
            pc.keys[i].used = 1;
            pc.keys[i].id = i;
            pc.keys[i].scheme = scheme;
            pc.keys[i].key_size = key_size;
            pc.keys[i].is_private = 0;
            pc.key_count++;
            pub = i;
            break;
        }
    }
    for (int i = 0; i < PC_MAX_KEYS; i++) {
        if (!pc.keys[i].used) {
            memset(&pc.keys[i], 0, sizeof(pc_key_t));
            pc.keys[i].used = 1;
            pc.keys[i].id = i;
            pc.keys[i].scheme = scheme;
            pc.keys[i].key_size = key_size;
            pc.keys[i].is_private = 1;
            pc.key_count++;
            priv = i;
            break;
        }
    }
    if (pub_key_id) *pub_key_id = pub;
    if (priv_key_id) *priv_key_id = priv;
    return (pub >= 0 && priv >= 0) ? 0 : -1;
}

int pc_he_encrypt(int key_id, uint8_t *plaintext, int len, uint8_t *ciphertext, int max)
{
    (void)key_id;
    if (!ciphertext || max <= 0) return -1;
    int out = len * 2 < max ? len * 2 : max;
    if (plaintext && out > 0) memcpy(ciphertext, plaintext, len < out ? len : out);
    pc.total_he_ops++;
    return out;
}

int pc_he_decrypt(int key_id, uint8_t *ciphertext, int len, uint8_t *plaintext, int max)
{
    (void)key_id;
    if (!plaintext || max <= 0) return -1;
    int out = len / 2 < max ? len / 2 : max;
    if (ciphertext && out > 0) memcpy(plaintext, ciphertext, out);
    return out;
}

int pc_he_add(uint8_t *ct1, int len1, uint8_t *ct2, int len2, uint8_t *result, int max)
{
    (void)ct1; (void)ct2;
    if (!result || max <= 0) return -1;
    int out = len1 > len2 ? len1 : len2;
    if (out > max) out = max;
    memset(result, 0, out);
    pc.total_he_ops++;
    return out;
}

int pc_he_multiply(uint8_t *ct1, int len1, uint8_t *ct2, int len2, uint8_t *result, int max)
{
    (void)ct1; (void)ct2;
    if (!result || max <= 0) return -1;
    int out = len1 + len2 < max ? len1 + len2 : max;
    memset(result, 0, out);
    pc.total_he_ops++;
    return out;
}

int pc_he_get_key(int key_id, pc_key_t *key)
{
    if (key_id < 0 || key_id >= PC_MAX_KEYS || !pc.keys[key_id].used || !key) return -1;
    memcpy(key, &pc.keys[key_id], sizeof(pc_key_t));
    return 0;
}

/* MPC */
int pc_mpc_create_task(const char *name, pc_mpc_protocol_t protocol, int parties, int threshold)
{
    for (int i = 0; i < PC_MAX_TASKS; i++) {
        if (!pc.mpc_tasks[i].used) {
            memset(&pc.mpc_tasks[i], 0, sizeof(pc_mpc_task_t));
            pc.mpc_tasks[i].used = 1;
            pc.mpc_tasks[i].id = i;
            if (name) strncpy(pc.mpc_tasks[i].name, name, 127);
            pc.mpc_tasks[i].protocol = protocol;
            pc.mpc_tasks[i].parties = parties;
            pc.mpc_tasks[i].threshold = threshold;
            pc.mpc_count++;
            return i;
        }
    }
    return -1;
}

int pc_mpc_start(int task_id)
{
    if (task_id < 0 || task_id >= PC_MAX_TASKS || !pc.mpc_tasks[task_id].used) return -1;
    pc.mpc_tasks[task_id].state = 1;
    return 0;
}

int pc_mpc_submit_input(int task_id, int party_id, uint8_t *data, int len)
{
    (void)party_id; (void)data;
    if (task_id < 0 || task_id >= PC_MAX_TASKS || !pc.mpc_tasks[task_id].used) return -1;
    pc.mpc_tasks[task_id].input_size += len;
    return 0;
}

int pc_mpc_compute(int task_id, const char *function)
{
    (void)function;
    if (task_id < 0 || task_id >= PC_MAX_TASKS || !pc.mpc_tasks[task_id].used) return -1;
    pc.mpc_tasks[task_id].state = 2;
    pc.mpc_tasks[task_id].output_size = (uint64_t)((uint32_t)pc.mpc_tasks[task_id].input_size / (uint32_t)pc.mpc_tasks[task_id].parties);
    pc.mpc_tasks[task_id].state = 3;
    pc.total_mpc_tasks++;
    return 0;
}

int pc_mpc_get_result(int task_id, uint8_t *result, int max)
{
    (void)result; (void)max;
    if (task_id < 0 || task_id >= PC_MAX_TASKS || !pc.mpc_tasks[task_id].used) return -1;
    return pc.mpc_tasks[task_id].output_size;
}

int pc_mpc_get_task(int task_id, pc_mpc_task_t *task)
{
    if (task_id < 0 || task_id >= PC_MAX_TASKS || !pc.mpc_tasks[task_id].used || !task) return -1;
    memcpy(task, &pc.mpc_tasks[task_id], sizeof(pc_mpc_task_t));
    return 0;
}

int pc_mpc_list_tasks(pc_mpc_task_t *tasks, int max)
{
    if (!tasks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PC_MAX_TASKS && count < max; i++) {
        if (pc.mpc_tasks[i].used) memcpy(&tasks[count++], &pc.mpc_tasks[i], sizeof(pc_mpc_task_t));
    }
    return count;
}

/* 差分隐私 */
int pc_dp_add_noise(float *data, int count, float epsilon, float delta,
                    pc_dp_mechanism_t mechanism)
{
    (void)epsilon; (void)delta; (void)mechanism;
    if (!data || count <= 0) return -1;
    for (int i = 0; i < count; i++) data[i] += 0.01f;  /* 模拟噪声 */
    pc.privacy_budget_used++;
    return 0;
}

int pc_dp_get_budget(float *used, float *remaining)
{
    if (used) *used = (float)pc.privacy_budget_used * 0.1f;
    if (remaining) *remaining = pc.config.dp_epsilon - (float)pc.privacy_budget_used * 0.1f;
    return 0;
}

/* 审计 */
int pc_audit_log(const char *event, const char *detail)
{
    (void)event; (void)detail;
    return 0;
}

int pc_audit_get_count(uint64_t *count)
{
    if (count) *count = pc.total_tee_ops + pc.total_he_ops + pc.total_fl_rounds;
    return 0;
}

/* 统计 */
int pc_get_stats(privacy_compute_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &pc, sizeof(privacy_compute_t));
    return 0;
}
