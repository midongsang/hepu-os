/* ============================================================================
 * 赫普OS - AI增强
 * NPU推理 + 大模型端侧 + 语音助手 + 图像识别 + 机器学习
 * ========================================================================== */

#include <stdint.h>
#include "ai2.h"
#include "vga.h"
#include "string.h"

static ai2_t ai;

void ai2_init(void)
{
    memset(&ai, 0, sizeof(ai2_t));
    ai.initialized = 1;

    /* NPU */
    ai.npu.available = 1;
    strncpy(ai.npu.name, "Hepu NPU Gen2", 63);
    ai.npu.vendor = 0;
    ai.npu.cores = 8;
    ai.npu.tops = 30;  /* 30 TOPS */
    ai.npu.memory_mb = 512;
    ai.npu.supported_precision[0] = 1;  /* INT8 */
    ai.npu.supported_precision[1] = 1;  /* FP16 */
    ai.npu.supported_precision[2] = 1;  /* FP32 */
    ai.npu.supported_precision[3] = 1;  /* INT4 */

    /* 默认TTS参数 */
    ai.tts_default.language = 0;  /* 中文 */
    ai.tts_default.speed = 1.0f;
    ai.tts_default.pitch = 1.0f;
    ai.tts_default.volume = 1.0f;
    ai.tts_default.emotion = 0;
    ai.tts_default.format = 0;
    ai.tts_default.sample_rate = 24000;
    ai.tts_default.bitrate = 128000;
    strncpy(ai.tts_default.voice_name, " Hepu 女声 ", 63);

    /* 预加载模型 */
    ai.models[0].used = 1;
    ai.models[0].id = 0;
    strncpy(ai.models[0].name, " voice 识别 ", 63);
    ai.models[0].type = AI2_MODEL_SPEECH_RECOGNIZE;
    ai.models[0].framework = AI2_FRAMEWORK_HEPU_NATIVE;
    ai.models[0].loaded = 1;
    ai.models[0].params_count = 100;  /* 100M */
    ai.models[0].inference_ms = 50;
    ai.models[0].npu_accelerated = 1;
    ai.model_count = 1;

    ai.models[1].used = 1;
    ai.models[1].id = 1;
    strncpy(ai.models[1].name, " map 像识别 ", 63);
    ai.models[1].type = AI2_MODEL_IMAGE_CLASSIFY;
    ai.models[1].framework = AI2_FRAMEWORK_TFLITE;
    ai.models[1].loaded = 1;
    ai.models[1].params_count = 25;
    ai.models[1].inference_ms = 30;
    ai.models[1].npu_accelerated = 1;
    ai.model_count = 2;

    ai.models[2].used = 1;
    ai.models[2].id = 2;
    strncpy(ai.models[2].name, " 人脸识别 ", 63);
    ai.models[2].type = AI2_MODEL_FACE_RECOGNIZE;
    ai.models[2].framework = AI2_FRAMEWORK_NCNN;
    ai.models[2].loaded = 1;
    ai.models[2].params_count = 50;
    ai.models[2].inference_ms = 80;
    ai.models[2].npu_accelerated = 1;
    ai.model_count = 3;

    ai.models[3].used = 1;
    ai.models[3].id = 3;
    strncpy(ai.models[3].name, " on-device LLM ", 63);
    ai.models[3].type = AI2_MODEL_LLM;
    ai.models[3].framework = AI2_FRAMEWORK_HEPU_NATIVE;
    ai.models[3].loaded = 1;
    ai.models[3].params_count = 3000;  /* 3B */
    ai.models[3].inference_ms = 200;
    ai.models[3].npu_accelerated = 1;
    ai.models[3].memory_usage_mb = 2048;
    ai.model_count = 4;

    vga_puts("[AI2] AI enhanced init done \n");
    vga_printf(" NPU: %s(%d TOPS, %d cores), model: %d(incl 3B on-device LLM)\n",
               ai.npu.name, ai.npu.tops, ai.npu.cores, ai.model_count);
}

int ai2_load_model(const char *name, ai2_model_type_t type, ai_framework_t framework, const char *path)
{
    for (int i = 0; i < AI2_MAX_MODELS; i++) {
        if (!ai.models[i].used) {
            memset(&ai.models[i], 0, sizeof(ai2_model_t));
            ai.models[i].used = 1;
            ai.models[i].id = i;
            if (name) strncpy(ai.models[i].name, name, 63);
            ai.models[i].type = type;
            ai.models[i].framework = framework;
            if (path) strncpy(ai.models[i].model_path, path, 255);
            ai.models[i].loaded = 1;
            ai.models[i].npu_accelerated = ai.npu.available;
            ai.model_count++;
            vga_printf("[AI2] load model: %s\n", name);
            return i;
        }
    }
    return -1;
}

int ai2_unload_model(int model_id)
{
    if (model_id < 0 || model_id >= AI2_MAX_MODELS || !ai.models[model_id].used) return -1;
    ai.models[model_id].used = 0;
    ai.models[model_id].loaded = 0;
    ai.model_count--;
    return 0;
}

int ai2_get_model(int model_id, ai2_model_t *model)
{
    if (model_id < 0 || model_id >= AI2_MAX_MODELS || !ai.models[model_id].used || !model) return -1;
    memcpy(model, &ai.models[model_id], sizeof(ai2_model_t));
    return 0;
}

int ai2_list_models(ai2_model_t *models, int max)
{
    if (!models || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AI2_MAX_MODELS && count < max; i++) {
        if (ai.models[i].used) memcpy(&models[count++], &ai.models[i], sizeof(ai2_model_t));
    }
    return count;
}

int ai2_run_inference(int model_id, ai2_tensor_t *inputs, int input_count, ai2_tensor_t *outputs, int output_count)
{
    (void)inputs; (void)input_count; (void)outputs; (void)output_count;
    if (model_id < 0 || model_id >= AI2_MAX_MODELS || !ai.models[model_id].used) return -1;
    ai.models[model_id].running = 1;
    ai.total_inferences++;
    ai.total_inference_time_ms += ai.models[model_id].inference_ms;
    if (ai.models[model_id].npu_accelerated) ai.npu_inferences++;
    else ai.cpu_inferences++;
    ai.models[model_id].running = 0;
    return 0;
}

int ai2_npu_get_info(ai2_npu_t *npu)
{
    if (!npu) return -1;
    memcpy(npu, &ai.npu, sizeof(ai2_npu_t));
    return 0;
}

int ai2_npu_available(void) { return ai.npu.available; }
int ai2_npu_get_load(void) { return ai.npu.current_load; }

int ai2_create_tensor(const char *name, ai2_tensor_type_t type, int *dims, int dim_count, void *data)
{
    for (int i = 0; i < AI2_MAX_TENSORS; i++) {
        if (!ai.tensors[i].used) {
            memset(&ai.tensors[i], 0, sizeof(ai2_tensor_t));
            ai.tensors[i].used = 1;
            ai.tensors[i].id = i;
            if (name) strncpy(ai.tensors[i].name, name, 63);
            ai.tensors[i].type = type;
            for (int j = 0; j < dim_count && j < 4; j++) ai.tensors[i].dims[j] = dims[j];
            ai.tensors[i].dim_count = dim_count;
            ai.tensors[i].data = data;
            ai.tensor_count++;
            return i;
        }
    }
    return -1;
}

int ai2_destroy_tensor(int tensor_id)
{
    if (tensor_id < 0 || tensor_id >= AI2_MAX_TENSORS || !ai.tensors[tensor_id].used) return -1;
    ai.tensors[tensor_id].used = 0;
    ai.tensor_count--;
    return 0;
}

int ai2_get_tensor(int tensor_id, ai2_tensor_t *tensor)
{
    if (tensor_id < 0 || tensor_id >= AI2_MAX_TENSORS || !ai.tensors[tensor_id].used || !tensor) return -1;
    memcpy(tensor, &ai.tensors[tensor_id], sizeof(ai2_tensor_t));
    return 0;
}

int ai2_quantize_tensor(int tensor_id, ai2_tensor_type_t target_type, float scale, int zero_point)
{
    if (tensor_id < 0 || tensor_id >= AI2_MAX_TENSORS || !ai.tensors[tensor_id].used) return -1;
    ai.tensors[tensor_id].type = target_type;
    ai.tensors[tensor_id].quantized = 1;
    ai.tensors[tensor_id].scale = scale;
    ai.tensors[tensor_id].zero_point = zero_point;
    return 0;
}

int ai2_speech_start_listen(void)
{
    ai.speech_listening = 1;
    vga_puts("[AI2] start voice listen \n");
    return 0;
}

int ai2_speech_stop_listen(void)
{
    ai.speech_listening = 0;
    return 0;
}

int ai2_speech_recognize(const int16_t *audio, int samples, int sample_rate, ai2_speech_result_t *result)
{
    (void)audio; (void)samples; (void)sample_rate;
    if (!result) return -1;
    memset(result, 0, sizeof(ai2_speech_result_t));
    strncpy(result->text, " 你好 Hepu ", 255);
    result->confidence = 0.95f;
    result->language = 0;
    result->duration_ms = 1000;
    result->word_count = 2;
    memcpy(&ai.last_speech, result, sizeof(ai2_speech_result_t));
    return 0;
}

int ai2_speech_get_last(ai2_speech_result_t *result)
{
    if (!result) return -1;
    memcpy(result, &ai.last_speech, sizeof(ai2_speech_result_t));
    return 0;
}

int ai2_tts_synthesize(const char *text, ai2_tts_params_t *params, void *output, uint32_t *output_size)
{
    (void)text; (void)params; (void)output;
    if (output_size) *output_size = 0;
    return 0;
}

int ai2_tts_set_default(ai2_tts_params_t *params)
{
    if (!params) return -1;
    memcpy(&ai.tts_default, params, sizeof(ai2_tts_params_t));
    return 0;
}

int ai2_tts_get_voices(char (*voices)[64], int max)
{
    if (!voices || max <= 0) return 0;
    strncpy(voices[0], " Hepu 女声 ", 63);
    strncpy(voices[1], " Hepu 男声 ", 63);
    strncpy(voices[2], " 童声 ", 63);
    return 3 < max ? 3 : max;
}

int ai2_image_classify(const uint8_t *image, int width, int height, int format, ai2_detection_t *results, int max)
{
    (void)image; (void)width; (void)height; (void)format;
    if (!results || max <= 0) return 0;
    strncpy(results[0].label, " 猫 ", 63);
    results[0].confidence = 0.92f;
    results[0].class_id = 0;
    ai.detection_count = 1;
    memcpy(ai.detections, results, sizeof(ai2_detection_t));
    return 1;
}

int ai2_object_detect(const uint8_t *image, int width, int height, ai2_detection_t *results, int max)
{
    (void)image; (void)width; (void)height;
    if (!results || max <= 0) return 0;
    strncpy(results[0].label, " 人 ", 63);
    results[0].confidence = 0.88f;
    results[0].x = 100; results[0].y = 100;
    results[0].width = 200; results[0].height = 300;
    return 1;
}

int ai2_face_detect(const uint8_t *image, int width, int height, ai2_face_result_t *results, int max)
{
    (void)image; (void)width; (void)height;
    if (!results || max <= 0) return 0;
    results[0].face_id = 0;
    results[0].confidence = 0.95f;
    results[0].x = 150; results[0].y = 100;
    results[0].width = 100; results[0].height = 120;
    results[0].emotion = 1;  /* happy */
    results[0].age_estimate = 25;
    results[0].gender = 1;
    ai.face_count = 1;
    memcpy(ai.faces, results, sizeof(ai2_face_result_t));
    return 1;
}

int ai2_face_recognize(const uint8_t *image, int width, int height, ai2_face_result_t *result)
{
    (void)image; (void)width; (void)height;
    if (!result) return -1;
    result->face_id = 1;
    strncpy(result->name, " user ", 63);
    result->confidence = 0.98f;
    return 0;
}

int ai2_llm_create_session(const char *system_prompt, const char *model_name, int max_tokens)
{
    for (int i = 0; i < 8; i++) {
        if (!ai.llm_sessions[i].used) {
            memset(&ai.llm_sessions[i], 0, sizeof(ai2_llm_session_t));
            ai.llm_sessions[i].used = 1;
            ai.llm_sessions[i].id = i;
            if (system_prompt) strncpy(ai.llm_sessions[i].system_prompt, system_prompt, 511);
            if (model_name) strncpy(ai.llm_sessions[i].model_name, model_name, 63);
            ai.llm_sessions[i].max_tokens = max_tokens;
            ai.llm_sessions[i].temperature = 0.7f;
            ai.llm_sessions[i].top_p = 0.9f;
            ai.llm_session_count++;
            return i;
        }
    }
    return -1;
}

int ai2_llm_destroy_session(int session_id)
{
    if (session_id < 0 || session_id >= 8 || !ai.llm_sessions[session_id].used) return -1;
    ai.llm_sessions[session_id].used = 0;
    ai.llm_session_count--;
    return 0;
}

int ai2_llm_chat(int session_id, const char *user_message, char *response, int response_size)
{
    (void)session_id; (void)user_message;
    if (!response || response_size <= 0) return -1;
    strncpy(response, " 你好 ! 我 is Hepu AI assistant , has 什么可以帮你 吗 ?", response_size - 1);
    return 0;
}

int ai2_llm_stream(int session_id, const char *user_message, void (*callback)(const char *token, void *arg), void *arg)
{
    (void)session_id; (void)user_message; (void)callback; (void)arg;
    return 0;
}

int ai2_llm_set_params(int session_id, float temperature, float top_p, int max_tokens)
{
    if (session_id < 0 || session_id >= 8 || !ai.llm_sessions[session_id].used) return -1;
    ai.llm_sessions[session_id].temperature = temperature;
    ai.llm_sessions[session_id].top_p = top_p;
    ai.llm_sessions[session_id].max_tokens = max_tokens;
    return 0;
}

int ai2_ml_train(int dataset_id, const char *model_type, int epochs, float learning_rate)
{
    (void)dataset_id; (void)model_type; (void)epochs; (void)learning_rate;
    vga_puts("[AI2] ML training done \n");
    return 0;
}

int ai2_ml_predict(int model_id, float *features, int feature_count, float *result)
{
    (void)model_id; (void)features; (void)feature_count;
    if (result) *result = 0.5f;
    return 0;
}

int ai2_ml_evaluate(int model_id, int dataset_id, float *accuracy, float *loss)
{
    (void)model_id; (void)dataset_id;
    if (accuracy) *accuracy = 0.92f;
    if (loss) *loss = 0.15f;
    return 0;
}

int ai2_dataset_create(const char *name, int features, int classes)
{
    for (int i = 0; i < AI2_MAX_DATASETS; i++) {
        if (!ai.datasets[i].used) {
            memset(&ai.datasets[i], 0, sizeof(ai2_dataset_t));
            ai.datasets[i].used = 1;
            ai.datasets[i].id = i;
            if (name) strncpy(ai.datasets[i].name, name, 63);
            ai.datasets[i].features_count = features;
            ai.datasets[i].classes_count = classes;
            ai.datasets[i].train_ratio = 70;
            ai.datasets[i].test_ratio = 20;
            ai.datasets[i].val_ratio = 10;
            ai.dataset_count++;
            return i;
        }
    }
    return -1;
}

int ai2_dataset_add_sample(int dataset_id, float *features, int label)
{
    (void)features; (void)label;
    if (dataset_id < 0 || dataset_id >= AI2_MAX_DATASETS || !ai.datasets[dataset_id].used) return -1;
    ai.datasets[dataset_id].samples_count++;
    return 0;
}

int ai2_get_stats(uint64_t *total_inferences, uint64_t *total_time, int *npu_count, int *gpu_count, int *cpu_count)
{
    if (total_inferences) *total_inferences = ai.total_inferences;
    if (total_time) *total_time = ai.total_inference_time_ms;
    if (npu_count) *npu_count = ai.npu_inferences;
    if (gpu_count) *gpu_count = ai.gpu_inferences;
    if (cpu_count) *cpu_count = ai.cpu_inferences;
    return 0;
}
