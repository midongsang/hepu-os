#ifndef AI_H
#define AI_H

#include <stdint.h>

#define AI_MAX_MODELS 8
#define AI_MAX_TENSORS 16
#define AI_MAX_LAYERS 32
#define AI_MODEL_NAME_LEN 64

/* 模型类型 */
typedef enum {
    AI_MODEL_CLASSIFIER = 0,
    AI_MODEL_DETECTOR,
    AI_MODEL_SEGMENTATION,
    AI_MODEL_SPEECH_RECOGNITION,
    AI_MODEL_NLP,
    AI_MODEL_RECOMMENDATION,
    AI_MODEL_CUSTOM,
} ai_model_type_t;

/* 张量数据类型 */
typedef enum {
    AI_TENSOR_FLOAT32 = 0,
    AI_TENSOR_INT32,
    AI_TENSOR_INT8,
    AI_TENSOR_UINT8,
    AI_TENSOR_INT64,
} ai_tensor_type_t;

/* 张量 */
typedef struct {
    int used;
    int id;
    char name[64];
    ai_tensor_type_t type;
    int dims[4];
    int ndims;
    void *data;
    uint32_t size;
} ai_tensor_t;

/* 神经网络层类型 */
typedef enum {
    AI_LAYER_CONV2D = 0,
    AI_LAYER_DENSE,
    AI_LAYER_MAXPOOL,
    AI_LAYER_AVGPOOL,
    AI_LAYER_RELU,
    AI_LAYER_SIGMOID,
    AI_LAYER_SOFTMAX,
    AI_LAYER_FLATTEN,
    AI_LAYER_DROPOUT,
    AI_LAYER_BATCHNORM,
    AI_LAYER_LSTM,
    AI_LAYER_EMBEDDING,
    AI_LAYER_CUSTOM,
} ai_layer_type_t;

/* 神经网络层 */
typedef struct {
    ai_layer_type_t type;
    int input_size;
    int output_size;
    void *weights;
    void *biases;
    int params_count;
} ai_layer_t;

/* 模型 */
typedef struct {
    int used;
    int id;
    char name[AI_MODEL_NAME_LEN];
    ai_model_type_t type;
    int version;
    int loaded;
    int accelerated;  /* 硬件加速 */
    /* 网络结构 */
    ai_layer_t layers[AI_MAX_LAYERS];
    int layer_count;
    /* 输入输出 */
    ai_tensor_t inputs[4];
    ai_tensor_t outputs[4];
    int input_count;
    int output_count;
    /* 统计 */
    uint64_t inference_count;
    uint64_t total_inference_time;
    float avg_inference_time;
} ai_model_t;

/* 推理结果 */
typedef struct {
    int class_id;
    float confidence;
    char label[64];
    float bounding_box[4];  /* x,y,w,h */
} ai_result_t;

/* 语音识别结果 */
typedef struct {
    char text[1024];
    float confidence;
    int is_final;
    float start_time;
    float end_time;
} ai_speech_result_t;

/* NLP意图 */
typedef enum {
    AI_INTENT_UNKNOWN = 0,
    AI_INTENT_CALL,
    AI_INTENT_MESSAGE,
    AI_INTENT_SEARCH,
    AI_INTENT_NAVIGATION,
    AI_INTENT_MUSIC,
    AI_INTENT_ALARM,
    AI_INTENT_REMINDER,
    AI_INTENT_WEATHER,
    AI_INTENT_SETTINGS,
    AI_INTENT_GENERAL,
} ai_intent_t;

/* NLP结果 */
typedef struct {
    ai_intent_t intent;
    char entities[8][64];
    int entity_count;
    float confidence;
    char response[1024];
} ai_nlp_result_t;

/* AI引擎状态 */
typedef struct {
    int initialized;
    ai_model_t models[AI_MAX_MODELS];
    int model_count;
    int npu_available;
    int gpu_acceleration;
    int thread_count;
    /* 语音助手 */
    int voice_assistant_active;
    int always_listening;
    char wake_word[64];
} ai_engine_t;

/* 初始化AI子系统 */
void ai_init(void);

/* 模型管理 */
int ai_load_model(const char *name, ai_model_type_t type, const char *path);
int ai_unload_model(int model_id);
int ai_get_model(int model_id, ai_model_t *model);
int ai_list_models(ai_model_t *models, int max);

/* 推理 */
int ai_inference(int model_id, ai_tensor_t *input, ai_result_t *results, int max_results);
int ai_inference_raw(int model_id, float *input, int input_size, float *output, int output_size);

/* NNAPI */
int ai_nnapi_create_model(void);
int ai_nnapi_add_layer(int model_id, ai_layer_type_t type, int input, int output);
int ai_nnapi_compile(int model_id);
int ai_nnapi_execute(int model_id, float *input, float *output);

/* 语音识别 */
int ai_speech_recognize(int16_t *audio, int samples, int sample_rate, ai_speech_result_t *result);
int ai_speech_start_listening(void);
int ai_speech_stop_listening(void);
int ai_speech_set_language(const char *lang);

/* 语音合成 (TTS) */
int ai_tts_speak(const char *text);
int ai_tts_stop(void);
int ai_tts_set_voice(int voice_id);
int ai_tts_set_rate(float rate);
int ai_tts_set_pitch(float pitch);

/* NLP */
int ai_nlp_parse(const char *text, ai_nlp_result_t *result);
int ai_nlp_get_response(const char *text, char *response, int len);

/* 图像识别 */
int ai_image_classify(uint8_t *image, int width, int height, ai_result_t *results, int max);
int ai_object_detect(uint8_t *image, int width, int height, ai_result_t *results, int max);

/* 智能建议 */
int ai_suggest_apps(const char *context, char *apps, int max);
int ai_suggest_actions(const char *context, char *actions, int max);

/* 语音助手 */
int ai_voice_assistant_start(void);
int ai_voice_assistant_stop(void);
int ai_voice_assistant_process(const char *text, char *response, int len);

/* 统计 */
int ai_model_count(void);
int ai_get_stats(int model_id, uint64_t *inferences, float *avg_time);

#endif /* AI_H */
