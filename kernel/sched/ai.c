/* ============================================================================
 * 赫普OS - AI 子系统
 * ML推理 + NNAPI + 语音识别/TTS + NLP + 智能建议
 * ========================================================================== */

#include <stdint.h>
#include "ai.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"


static ai_engine_t ai;

/* 简单矩阵乘法 */
static void matmul(float *a, float *b, float *c, int m, int n, int k) __attribute__((unused));
static void matmul(float *a, float *b, float *c, int m, int n, int k)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            float sum = 0;
            for (int p = 0; p < k; p++) {
                sum += a[i * k + p] * b[p * n + j];
            }
            c[i * n + j] = sum;
        }
    }
}

/* ReLU激活 */
static void relu(float *x, int n)
{
    for (int i = 0; i < n; i++) {
        if (x[i] < 0) x[i] = 0;
    }
}

/* Softmax */
static void softmax(float *x, int n)
{
    float max = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max) max = x[i];
    float sum = 0;
    for (int i = 0; i < n; i++) {
        x[i] = 1.0f;  /* 简化: exp(x[i] - max) */
        sum += x[i];
    }
    for (int i = 0; i < n; i++) x[i] /= sum;
}

void ai_init(void)
{
    memset(&ai, 0, sizeof(ai_engine_t));
    ai.initialized = 1;
    ai.npu_available = 0;
    ai.gpu_acceleration = 1;
    ai.thread_count = 1;
    ai.voice_assistant_active = 0;
    ai.always_listening = 0;
    strcpy(ai.wake_word, "Hey Hepu");

    /* 加载默认模型 */
    ai_load_model("MobileNet", AI_MODEL_CLASSIFIER, NULL);
    ai_load_model("SpeechRec", AI_MODEL_SPEECH_RECOGNITION, NULL);
    ai_load_model("NLP", AI_MODEL_NLP, NULL);

    vga_puts("[AI] AI subsystem init done \n");
    vga_printf(" model: %d, GPU accel: %s, wakeword: %s\n",
               ai.model_count, ai.gpu_acceleration ? " on " : " close ", ai.wake_word);
}

int ai_load_model(const char *name, ai_model_type_t type, const char *path)
{
    (void)path;
    if (!name || !ai.initialized) return -1;
    for (int i = 0; i < AI_MAX_MODELS; i++) {
        if (!ai.models[i].used) {
            memset(&ai.models[i], 0, sizeof(ai_model_t));
            ai.models[i].used = 1;
            ai.models[i].id = i;
            strncpy(ai.models[i].name, name, AI_MODEL_NAME_LEN - 1);
            ai.models[i].type = type;
            ai.models[i].version = 1;
            ai.models[i].loaded = 1;
            ai.models[i].accelerated = ai.gpu_acceleration;
            ai.model_count++;
            vga_printf("[AI] load model: %s(type=%d)\n", name, type);
            return i;
        }
    }
    return -1;
}

int ai_unload_model(int model_id)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used) return -1;
    ai.models[model_id].used = 0;
    ai.model_count--;
    return 0;
}

int ai_get_model(int model_id, ai_model_t *model)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used || !model) return -1;
    memcpy(model, &ai.models[model_id], sizeof(ai_model_t));
    return 0;
}

int ai_list_models(ai_model_t *models, int max)
{
    if (!models || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AI_MAX_MODELS && count < max; i++) {
        if (ai.models[i].used) memcpy(&models[count++], &ai.models[i], sizeof(ai_model_t));
    }
    return count;
}

int ai_inference(int model_id, ai_tensor_t *input, ai_result_t *results, int max_results)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used) return -1;
    if (!input || !results) return -1;

    ai_model_t *model = &ai.models[model_id];
    model->inference_count++;

    /* 原型阶段: 模拟推理，返回随机结果 */
    int count = (max_results < 5) ? max_results : 5;
    for (int i = 0; i < count; i++) {
        results[i].class_id = i;
        results[i].confidence = 1.0f / (i + 1);
        {
            char tmp[16];
            tmp[0] = 'c'; tmp[1] = 'l'; tmp[2] = 'a'; tmp[3] = 's'; tmp[4] = 's'; tmp[5] = '_';
            tmp[6] = '0' + i; tmp[7] = 0;
            strncpy(results[i].label, tmp, 63);
        }
        results[i].bounding_box[0] = 0.1f * i;
        results[i].bounding_box[1] = 0.1f * i;
        results[i].bounding_box[2] = 0.5f;
        results[i].bounding_box[3] = 0.5f;
    }

    model->avg_inference_time = 10.0f;  /* 10ms */
    return count;
}

int ai_inference_raw(int model_id, float *input, int input_size, float *output, int output_size)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used) return -1;
    if (!input || !output) return -1;
    /* 原型阶段: 简单全连接层模拟 */
    for (int i = 0; i < output_size; i++) {
        output[i] = 0;
        for (int j = 0; j < input_size; j++) {
            output[i] += input[j] * 0.01f;
        }
    }
    relu(output, output_size);
    softmax(output, output_size);
    return 0;
}

int ai_nnapi_create_model(void)
{
    return ai_load_model("NNAPI Model", AI_MODEL_CUSTOM, NULL);
}

int ai_nnapi_add_layer(int model_id, ai_layer_type_t type, int input, int output)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used) return -1;
    ai_model_t *model = &ai.models[model_id];
    if (model->layer_count >= AI_MAX_LAYERS) return -1;
    model->layers[model->layer_count].type = type;
    model->layers[model->layer_count].input_size = input;
    model->layers[model->layer_count].output_size = output;
    model->layer_count++;
    return 0;
}

int ai_nnapi_compile(int model_id)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used) return -1;
    vga_printf("[AI] NNAPI compile model #%d(%d layers)\n", model_id, ai.models[model_id].layer_count);
    return 0;
}

int ai_nnapi_execute(int model_id, float *input, float *output)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used) return -1;
    if (!input || !output) return -1;
    ai_model_t *model = &ai.models[model_id];
    /* V3.77: 修复内存泄漏——每个 DENSE 层分配的临时 buffer 在层间切换时
     * 永不释放, 多次推理会耗尽 kheap; 现按层释放, 末尾输出后释放末层 buffer。 */
    extern void kfree(void *);
    float *current = input;
    for (int i = 0; i < model->layer_count; i++) {
        ai_layer_t *layer = &model->layers[i];
        if (layer->type == AI_LAYER_DENSE) {
            /* 矩阵乘法 */
            float *next = kmalloc(layer->output_size * sizeof(float));
            if (!next) {
                if (current != input) kfree(current);   /* 分配失败也要回收 */
                vga_puts("[AI] execute OOM\n");
                return -1;
            }
            for (int j = 0; j < layer->output_size; j++) {
                next[j] = 0;
                for (int k = 0; k < layer->input_size; k++) {
                    next[j] += current[k] * 0.01f;
                }
            }
            if (current != input) kfree(current);   /* 释放上一层 DENSE buffer */
            current = next;
        } else if (layer->type == AI_LAYER_RELU) {
            relu(current, layer->output_size);
        } else if (layer->type == AI_LAYER_SOFTMAX) {
            softmax(current, layer->output_size);
        }
    }
    memcpy(output, current, model->layers[model->layer_count - 1].output_size * sizeof(float));
    if (current != input) kfree(current);   /* 释放最后一个 DENSE buffer */
    return 0;
}

int ai_speech_recognize(int16_t *audio, int samples, int sample_rate, ai_speech_result_t *result)
{
    (void)audio; (void)samples; (void)sample_rate;
    if (!result) return -1;
    /* 原型阶段: 返回模拟识别结果 */
    strcpy(result->text, " 你好 , Hepu ");
    result->confidence = 0.95f;
    result->is_final = 1;
    result->start_time = 0;
    result->end_time = (float)samples / sample_rate;
    return 0;
}

int ai_speech_start_listening(void)
{
    vga_puts("[AI] start voice listen \n");
    return 0;
}

int ai_speech_stop_listening(void)
{
    vga_puts("[AI] stop voice listen \n");
    return 0;
}

int ai_speech_set_language(const char *lang)
{
    (void)lang;
    return 0;
}

int ai_tts_speak(const char *text)
{
    if (!text) return -1;
    vga_printf("[AI] TTS: %s\n", text);
    return 0;
}

int ai_tts_stop(void) { return 0; }
int ai_tts_set_voice(int voice_id) { (void)voice_id; return 0; }
int ai_tts_set_rate(float rate) { (void)rate; return 0; }
int ai_tts_set_pitch(float pitch) { (void)pitch; return 0; }

int ai_nlp_parse(const char *text, ai_nlp_result_t *result)
{
    if (!text || !result) return -1;
    memset(result, 0, sizeof(ai_nlp_result_t));

    /* 简单关键词匹配 */
    if (strstr(text, " make call ") || strstr(text, "call")) {
        result->intent = AI_INTENT_CALL;
        strcpy(result->response, " 好 , 您 dial ");
    } else if (strstr(text, " 发 SMS ") || strstr(text, "message")) {
        result->intent = AI_INTENT_MESSAGE;
        strcpy(result->response, " 好 , 请说 SMS 内容 ");
    } else if (strstr(text, " search ") || strstr(text, "search")) {
        result->intent = AI_INTENT_SEARCH;
        strcpy(result->response, " 您 search ");
    } else if (strstr(text, " 导航 ") || strstr(text, "navigation")) {
        result->intent = AI_INTENT_NAVIGATION;
        strcpy(result->response, " 您导航 ");
    } else if (strstr(text, " 音乐 ") || strstr(text, "music")) {
        result->intent = AI_INTENT_MUSIC;
        strcpy(result->response, " 您 play music ");
    } else if (strstr(text, " 闹钟 ") || strstr(text, "alarm")) {
        result->intent = AI_INTENT_ALARM;
        strcpy(result->response, " 好 , set alarm ");
    } else if (strstr(text, " weather ") || strstr(text, "weather")) {
        result->intent = AI_INTENT_WEATHER;
        strcpy(result->response, " today's weather 晴朗 ,25 度 ");
    } else {
        result->intent = AI_INTENT_GENERAL;
        strcpy(result->response, " 我 is Hepu assistant , has 什么可以帮您 ?");
    }
    result->confidence = 0.85f;
    return 0;
}

int ai_nlp_get_response(const char *text, char *response, int len)
{
    ai_nlp_result_t result;
    if (ai_nlp_parse(text, &result) != 0) return -1;
    strncpy(response, result.response, len - 1);
    response[len - 1] = '\0';
    return 0;
}

int ai_image_classify(uint8_t *image, int width, int height, ai_result_t *results, int max)
{
    (void)image; (void)width; (void)height;
    if (!results) return -1;
    /* 原型阶段: 返回模拟分类结果 */
    int count = (max < 3) ? max : 3;
    const char *labels[] = {"cat", "dog", "bird"};
    for (int i = 0; i < count; i++) {
        results[i].class_id = i;
        results[i].confidence = 0.9f - i * 0.1f;
        strncpy(results[i].label, labels[i], 63);
    }
    return count;
}

int ai_object_detect(uint8_t *image, int width, int height, ai_result_t *results, int max)
{
    (void)image; (void)width; (void)height;
    if (!results) return -1;
    int count = (max < 2) ? max : 2;
    for (int i = 0; i < count; i++) {
        results[i].class_id = i;
        results[i].confidence = 0.8f;
        strcpy(results[i].label, i == 0 ? "person" : "car");
        results[i].bounding_box[0] = 0.1f + i * 0.3f;
        results[i].bounding_box[1] = 0.2f;
        results[i].bounding_box[2] = 0.3f;
        results[i].bounding_box[3] = 0.5f;
    }
    return count;
}

int ai_suggest_apps(const char *context, char *apps, int max)
{
    (void)context; (void)max;
    if (!apps) return -1;
    strcpy(apps, "com.hepu.music,com.hepu.camera,com.hepu.browser");
    return 3;
}

int ai_suggest_actions(const char *context, char *actions, int max)
{
    (void)context; (void)max;
    if (!actions) return -1;
    strcpy(actions, "play_music,take_photo,search_web");
    return 3;
}

int ai_voice_assistant_start(void)
{
    ai.voice_assistant_active = 1;
    vga_puts("[AI] voice assistant started \n");
    return 0;
}

int ai_voice_assistant_stop(void)
{
    ai.voice_assistant_active = 0;
    return 0;
}

int ai_voice_assistant_process(const char *text, char *response, int len)
{
    return ai_nlp_get_response(text, response, len);
}

int ai_model_count(void) { return ai.model_count; }

int ai_get_stats(int model_id, uint64_t *inferences, float *avg_time)
{
    if (model_id < 0 || model_id >= AI_MAX_MODELS || !ai.models[model_id].used) return -1;
    if (inferences) *inferences = ai.models[model_id].inference_count;
    if (avg_time) *avg_time = ai.models[model_id].avg_inference_time;
    return 0;
}
