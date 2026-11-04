#ifndef AI2_H
#define AI2_H

#include <stdint.h>

#define AI2_MAX_MODELS 16
#define AI2_MAX_TENSORS 32
#define AI2_MAX_LAYERS 64
#define AI2_MAX_VOICE_COMMANDS 32
#define AI2_MAX_OBJECTS 32
#define AI2_MAX_FACES 16
#define AI2_MAX_DATASETS 8

/* 模型类型 */
typedef enum {
    AI2_MODEL_LLM = 0,
    AI2_MODEL_IMAGE_CLASSIFY,
    AI2_MODEL_OBJECT_DETECT,
    AI2_MODEL_FACE_RECOGNIZE,
    AI2_MODEL_SPEECH_RECOGNIZE,
    AI2_MODEL_TTS,
    AI2_MODEL_NLP,
    AI2_MODEL_RECOMMEND,
    AI2_MODEL_ANOMALY,
    AI2_MODEL_CUSTOM,
} ai2_model_type_t;

/* 模型框架 */
typedef enum {
    AI2_FRAMEWORK_TFLITE = 0,
    AI2_FRAMEWORK_ONNX,
    AI2_FRAMEWORK_PYTORCH,
    AI2_FRAMEWORK_TENSORFLOW,
    AI2_FRAMEWORK_NCNN,
    AI2_FRAMEWORK_MNN,
    AI2_FRAMEWORK_COREML,
    AI2_FRAMEWORK_HEPU_NATIVE,
} ai_framework_t;

/* 张量数据类型 */
typedef enum {
    AI2_TENSOR_FLOAT32 = 0,
    AI2_TENSOR_FLOAT16,
    AI2_TENSOR_INT8,
    AI2_TENSOR_INT16,
    AI2_TENSOR_INT32,
    AI2_TENSOR_UINT8,
    AI2_TENSOR_BOOL,
} ai2_tensor_type_t;

/* 张量 */
typedef struct {
    int used;
    int id;
    char name[64];
    ai2_tensor_type_t type;
    int dims[4];
    int dim_count;
    void *data;
    uint32_t size;
    int quantized;
    float scale;
    int zero_point;
} ai2_tensor_t;

/* 神经网络层 */
typedef struct {
    int type;  /* 0=conv,1=pool,2=fc,3=relu,4=sigmoid,5=softmax,6=lstm,7=attention,8=norm,9=add */
    int input_size;
    int output_size;
    void *weights;
    void *biases;
    int params_count;
} ai2_layer_t;

/* AI模型 */
typedef struct {
    int used;
    int id;
    char name[64];
    ai2_model_type_t type;
    ai_framework_t framework;
    int version;
    int loaded;
    int running;
    /* 模型信息 */
    uint64_t model_size;  /* 字节 */
    int params_count;  /* 参数数量(百万) */
    int layers_count;
    int input_count;
    int output_count;
    /* 性能 */
    int inference_ms;  /* 平均推理时间 */
    int memory_usage_mb;
    int npu_accelerated;
    int gpu_accelerated;
    /* 精度 */
    float accuracy;
    float f1_score;
    /* 路径 */
    char model_path[256];
    /* 层 */
    ai2_layer_t layers[AI2_MAX_LAYERS];
} ai2_model_t;

/* NPU设备 */
typedef struct {
    int available;
    char name[64];
    int vendor;  /* 0=Hepu,1=Qualcomm,2=MediaTek,3=Samsung,4=Apple,5=Google */
    int cores;
    int tops;  /* TOPS算力 */
    int memory_mb;
    int supported_precision[4];  /* INT8/FP16/FP32/INT4 */
    int current_load;  /* 0-100 */
    int temperature;
} ai2_npu_t;

/* 语音识别结果 */
typedef struct {
    char text[256];
    float confidence;
    int language;
    int speaker_id;
    uint64_t duration_ms;
    int word_count;
    float word_timestamps[64];
    char words[64][32];
} ai2_speech_result_t;

/* TTS参数 */
typedef struct {
    char voice_name[64];
    int language;
    float speed;  /* 0.5-2.0 */
    float pitch;  /* 0.5-2.0 */
    float volume;  /* 0-1 */
    int emotion;  /* 0=neutral,1=happy,2=sad,3=angry,4=excited */
    int format;  /* 0=PCM,1=WAV,2=MP3,3=AAC,4=OPUS */
    int sample_rate;
    int bitrate;
} ai2_tts_params_t;

/* 图像识别结果 */
typedef struct {
    char label[64];
    float confidence;
    int x, y, width, height;
    int class_id;
} ai2_detection_t;

/* 人脸识别结果 */
typedef struct {
    int face_id;
    char name[64];
    float confidence;
    int x, y, width, height;
    float landmarks[10][2];  /* 5个关键点x2 */
    float embedding[128];
    int emotion;  /* 0=neutral,1=happy,2=sad,3=angry,4=surprise,5=fear */
    int age_estimate;
    int gender;  /* 0=unknown,1=male,2=female */
} ai2_face_result_t;

/* 大模型对话 */
typedef struct {
    int used;
    int id;
    char system_prompt[512];
    int max_tokens;
    float temperature;
    float top_p;
    int context_length;
    char model_name[64];
    int streaming;
} ai2_llm_session_t;

/* 机器学习数据集 */
typedef struct {
    int used;
    int id;
    char name[64];
    int samples_count;
    int features_count;
    int classes_count;
    int train_ratio;  /* 百分比 */
    int test_ratio;
    int val_ratio;
    char data_path[256];
} ai2_dataset_t;

/* AI增强状态 */
typedef struct {
    int initialized;
    /* 模型 */
    ai2_model_t models[AI2_MAX_MODELS];
    int model_count;
    /* 张量 */
    ai2_tensor_t tensors[AI2_MAX_TENSORS];
    int tensor_count;
    /* NPU */
    ai2_npu_t npu;
    /* 语音 */
    int speech_listening;
    ai2_speech_result_t last_speech;
    /* TTS */
    ai2_tts_params_t tts_default;
    /* 视觉 */
    ai2_detection_t detections[AI2_MAX_OBJECTS];
    int detection_count;
    ai2_face_result_t faces[AI2_MAX_FACES];
    int face_count;
    /* LLM */
    ai2_llm_session_t llm_sessions[8];
    int llm_session_count;
    /* 数据集 */
    ai2_dataset_t datasets[AI2_MAX_DATASETS];
    int dataset_count;
    /* 统计 */
    uint64_t total_inferences;
    uint64_t total_inference_time_ms;
    int npu_inferences;
    int gpu_inferences;
    int cpu_inferences;
} ai2_t;

/* 初始化AI增强 */
void ai2_init(void);

/* 模型管理 */
int ai2_load_model(const char *name, ai2_model_type_t type, ai_framework_t framework, const char *path);
int ai2_unload_model(int model_id);
int ai2_get_model(int model_id, ai2_model_t *model);
int ai2_list_models(ai2_model_t *models, int max);
int ai2_run_inference(int model_id, ai2_tensor_t *inputs, int input_count, ai2_tensor_t *outputs, int output_count);

/* NPU */
int ai2_npu_get_info(ai2_npu_t *npu);
int ai2_npu_available(void);
int ai2_npu_get_load(void);

/* 张量管理 */
int ai2_create_tensor(const char *name, ai2_tensor_type_t type, int *dims, int dim_count, void *data);
int ai2_destroy_tensor(int tensor_id);
int ai2_get_tensor(int tensor_id, ai2_tensor_t *tensor);
int ai2_quantize_tensor(int tensor_id, ai2_tensor_type_t target_type, float scale, int zero_point);

/* 语音识别 */
int ai2_speech_start_listen(void);
int ai2_speech_stop_listen(void);
int ai2_speech_recognize(const int16_t *audio, int samples, int sample_rate, ai2_speech_result_t *result);
int ai2_speech_get_last(ai2_speech_result_t *result);

/* TTS */
int ai2_tts_synthesize(const char *text, ai2_tts_params_t *params, void *output, uint32_t *output_size);
int ai2_tts_set_default(ai2_tts_params_t *params);
int ai2_tts_get_voices(char (*voices)[64], int max);

/* 图像识别 */
int ai2_image_classify(const uint8_t *image, int width, int height, int format, ai2_detection_t *results, int max);
int ai2_object_detect(const uint8_t *image, int width, int height, ai2_detection_t *results, int max);
int ai2_face_detect(const uint8_t *image, int width, int height, ai2_face_result_t *results, int max);
int ai2_face_recognize(const uint8_t *image, int width, int height, ai2_face_result_t *result);

/* 大模型 */
int ai2_llm_create_session(const char *system_prompt, const char *model_name, int max_tokens);
int ai2_llm_destroy_session(int session_id);
int ai2_llm_chat(int session_id, const char *user_message, char *response, int response_size);
int ai2_llm_stream(int session_id, const char *user_message, void (*callback)(const char *token, void *arg), void *arg);
int ai2_llm_set_params(int session_id, float temperature, float top_p, int max_tokens);

/* 机器学习 */
int ai2_ml_train(int dataset_id, const char *model_type, int epochs, float learning_rate);
int ai2_ml_predict(int model_id, float *features, int feature_count, float *result);
int ai2_ml_evaluate(int model_id, int dataset_id, float *accuracy, float *loss);
int ai2_dataset_create(const char *name, int features, int classes);
int ai2_dataset_add_sample(int dataset_id, float *features, int label);

/* 统计 */
int ai2_get_stats(uint64_t *total_inferences, uint64_t *total_time, int *npu_count, int *gpu_count, int *cpu_count);

#endif /* AI2_H */
