#ifndef GFX2_H
#define GFX2_H

#include <stdint.h>

#define GFX2_MAX_TEXTURES 128
#define GFX2_MAX_BUFFERS 64
#define GFX2_MAX_SHADERS 32
#define GFX2_MAX_PIPELINES 16
#define GFX2_MAX_RENDER_PASSES 8
#define GFX2_MAX_FRAMEBUFFERS 8
#define GFX2_MAX_DESCRIPTOR_SETS 16
#define GFX2_MAX_VERTEX_ATTRIBS 16

/* 图形API级别 */
typedef enum {
    GFX_API_OPENGL_ES = 0,
    GFX_API_VULKAN,
    GFX_API_METAL,
    GFX_API_D3D12,
} gfx_api_t;

/* 像素格式 */
typedef enum {
    GFX_FORMAT_RGBA8 = 0,
    GFX_FORMAT_RGB8,
    GFX_FORMAT_RGBA4,
    GFX_FORMAT_RGB565,
    GFX_FORMAT_DEPTH16,
    GFX_FORMAT_DEPTH24,
    GFX_FORMAT_DEPTH32F,
    GFX_FORMAT_STENCIL8,
    GFX_FORMAT_DEPTH24_STENCIL8,
    GFX_FORMAT_BC1,
    GFX_FORMAT_BC2,
    GFX_FORMAT_BC3,
    GFX_FORMAT_BC4,
    GFX_FORMAT_BC5,
    GFX_FORMAT_BC6H,
    GFX_FORMAT_BC7,
    GFX_FORMAT_ETC1,
    GFX_FORMAT_ETC2,
    GFX_FORMAT_ASTC_4x4,
    GFX_FORMAT_ASTC_6x6,
    GFX_FORMAT_ASTC_8x8,
} gfx_format_t;

/* 纹理类型 */
typedef enum {
    GFX_TEXTURE_2D = 0,
    GFX_TEXTURE_3D,
    GFX_TEXTURE_CUBE,
    GFX_TEXTURE_2D_ARRAY,
} gfx_texture_type_t;

/* 纹理 */
typedef struct {
    int used;
    int id;
    char name[64];
    gfx_texture_type_t type;
    gfx_format_t format;
    int width, height, depth;
    int mip_levels;
    int array_layers;
    int samples;
    void *data;
    uint32_t size;
    int gpu_handle;
    int compressed;
    int srgb;
} gfx_texture_t;

/* 缓冲区类型 */
typedef enum {
    GFX_BUFFER_VERTEX = 0,
    GFX_BUFFER_INDEX,
    GFX_BUFFER_UNIFORM,
    GFX_BUFFER_STORAGE,
    GFX_BUFFER_TRANSFER_SRC,
    GFX_BUFFER_TRANSFER_DST,
    GFX_BUFFER_INDIRECT,
} gfx_buffer_type_t;

/* 缓冲区 */
typedef struct {
    int used;
    int id;
    gfx_buffer_type_t type;
    uint64_t size;
    void *data;
    int gpu_handle;
    int mapped;
    int coherent;
} gfx_buffer_t;

/* 着色器类型 */
typedef enum {
    GFX_SHADER_VERTEX = 0,
    GFX_SHADER_FRAGMENT,
    GFX_SHADER_GEOMETRY,
    GFX_SHADER_TESS_CONTROL,
    GFX_SHADER_TESS_EVAL,
    GFX_SHADER_COMPUTE,
} gfx_shader_type_t;

/* 着色器 */
typedef struct {
    int used;
    int id;
    char name[64];
    gfx_shader_type_t type;
    void *spirv;
    uint32_t spirv_size;
    char entry_point[64];
    int compiled;
} gfx_shader_t;

/* 顶点属性 */
typedef struct {
    int location;
    int binding;
    gfx_format_t format;
    uint32_t offset;
} gfx_vertex_attrib_t;

/* 图元拓扑 */
typedef enum {
    GFX_PRIM_POINTS = 0,
    GFX_PRIM_LINES,
    GFX_PRIM_LINE_STRIP,
    GFX_PRIM_TRIANGLES,
    GFX_PRIM_TRIANGLE_STRIP,
    GFX_PRIM_TRIANGLE_FAN,
} gfx_primitive_t;

/* 多边形模式 */
typedef enum {
    GFX_POLY_FILL = 0,
    GFX_POLY_LINE,
    GFX_POLY_POINT,
} gfx_polygon_mode_t;

/* 混合因子 */
typedef enum {
    GFX_BLEND_ZERO = 0,
    GFX_BLEND_ONE,
    GFX_BLEND_SRC_COLOR,
    GFX_BLEND_ONE_MINUS_SRC_COLOR,
    GFX_BLEND_DST_COLOR,
    GFX_BLEND_ONE_MINUS_DST_COLOR,
    GFX_BLEND_SRC_ALPHA,
    GFX_BLEND_ONE_MINUS_SRC_ALPHA,
    GFX_BLEND_DST_ALPHA,
    GFX_BLEND_ONE_MINUS_DST_ALPHA,
} gfx_blend_factor_t;

/* 图形管线 */
typedef struct {
    int used;
    int id;
    char name[64];
    int vertex_shader;
    int fragment_shader;
    int geometry_shader;
    gfx_primitive_t primitive;
    gfx_polygon_mode_t polygon_mode;
    int cull_mode;  /* 0=none,1=front,2=back */
    int front_face;  /* 0=ccw,1=cw */
    int depth_test;
    int depth_write;
    int depth_compare;  /* 0=never,1=less,2=equal,3=less_equal,4=greater,5=not_equal,6=greater_equal,7=always */
    int blend_enabled;
    gfx_blend_factor_t src_blend;
    gfx_blend_factor_t dst_blend;
    gfx_vertex_attrib_t vertex_attribs[GFX2_MAX_VERTEX_ATTRIBS];
    int vertex_attrib_count;
    int compiled;
} gfx_pipeline_t;

/* 渲染通道 */
typedef struct {
    int used;
    int id;
    char name[64];
    gfx_format_t color_format;
    gfx_format_t depth_format;
    int samples;
    int clear_color[4];
    float clear_depth;
    int clear_stencil;
} gfx_render_pass_t;

/* 帧缓冲 */
typedef struct {
    int used;
    int id;
    int render_pass;
    int width, height;
    int color_attachments[4];
    int color_attachment_count;
    int depth_attachment;
} gfx_framebuffer_t;

/* 描述符集 */
typedef struct {
    int used;
    int id;
    int bindings[16];
    int binding_count;
} gfx_descriptor_set_t;

/* 视口 */
typedef struct {
    float x, y, width, height;
    float min_depth, max_depth;
} gfx_viewport_t;

/* 矩形裁剪 */
typedef struct {
    int x, y, width, height;
} gfx_scissor_t;

/* 3D顶点 */
typedef struct {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
} gfx_vertex3d_t;

/* 4x4矩阵 */
typedef struct {
    float m[16];
} gfx_matrix4_t;

/* GPU计算 */
typedef struct {
    int used;
    int id;
    char name[64];
    int compute_shader;
    int workgroup_size_x, workgroup_size_y, workgroup_size_z;
    int local_size_x, local_size_y, local_size_z;
} gfx_compute_pipeline_t;

/* 图形深化状态 */
typedef struct {
    int initialized;
    gfx_api_t api;
    int api_version;
    int device_id;
    char device_name[128];
    char vendor_name[64];
    int driver_version;
    /* 资源 */
    gfx_texture_t textures[GFX2_MAX_TEXTURES];
    int texture_count;
    gfx_buffer_t buffers[GFX2_MAX_BUFFERS];
    int buffer_count;
    gfx_shader_t shaders[GFX2_MAX_SHADERS];
    int shader_count;
    gfx_pipeline_t pipelines[GFX2_MAX_PIPELINES];
    int pipeline_count;
    gfx_render_pass_t render_passes[GFX2_MAX_RENDER_PASSES];
    int render_pass_count;
    gfx_framebuffer_t framebuffers[GFX2_MAX_FRAMEBUFFERS];
    int framebuffer_count;
    gfx_descriptor_set_t descriptor_sets[GFX2_MAX_DESCRIPTOR_SETS];
    int descriptor_set_count;
    gfx_compute_pipeline_t compute_pipelines[16];
    int compute_pipeline_count;
    /* 状态 */
    int frame_count;
    uint64_t frame_time_ns;
    int fps;
    int draw_calls;
    int triangles_rendered;
    int texture_memory_mb;
    int buffer_memory_mb;
    /* 特性支持 */
    int support_compute;
    int support_geometry;
    int support_tessellation;
    int support_compression_bc;
    int support_compression_etc;
    int support_compression_astc;
    int max_texture_size;
    int max_anisotropy;
} gfx2_t;

/* 初始化图形深化 */
void gfx2_init(void);

/* 设备查询 */
int gfx2_get_device_info(gfx2_t *info);
int gfx2_get_api(void);

/* 纹理管理 */
int gfx2_create_texture(const char *name, gfx_texture_type_t type, gfx_format_t format,
                        int width, int height, int depth, int mipmaps, void *data);
int gfx2_destroy_texture(int texture_id);
int gfx2_get_texture(int texture_id, gfx_texture_t *tex);
int gfx2_generate_mipmaps(int texture_id);
int gfx2_compress_texture(int texture_id, gfx_format_t format);

/* 缓冲区管理 */
int gfx2_create_buffer(const char *name, gfx_buffer_type_t type, uint64_t size, void *data);
int gfx2_destroy_buffer(int buffer_id);
int gfx2_map_buffer(int buffer_id, void **data);
int gfx2_unmap_buffer(int buffer_id);
int gfx2_update_buffer(int buffer_id, uint64_t offset, const void *data, uint64_t size);

/* 着色器管理 */
int gfx2_create_shader(const char *name, gfx_shader_type_t type, const void *spirv, uint32_t size);
int gfx2_destroy_shader(int shader_id);
int gfx2_compile_shader(int shader_id, const char *glsl);

/* 管线管理 */
int gfx2_create_pipeline(const char *name, gfx_pipeline_t *desc);
int gfx2_destroy_pipeline(int pipeline_id);
int gfx2_bind_pipeline(int pipeline_id);

/* 渲染通道 */
int gfx2_create_render_pass(const char *name, gfx_format_t color_format, gfx_format_t depth_format);
int gfx2_destroy_render_pass(int rp_id);
int gfx2_begin_render_pass(int rp_id, int framebuffer_id, int clear);
int gfx2_end_render_pass(void);

/* 帧缓冲 */
int gfx2_create_framebuffer(int rp_id, int width, int height, int *color_attachments, int color_count, int depth);
int gfx2_destroy_framebuffer(int fb_id);

/* 绘制命令 */
int gfx2_draw(int vertex_count, int instance_count, int first_vertex, int first_instance);
int gfx2_draw_indexed(int index_count, int instance_count, int first_index, int vertex_offset, int first_instance);
int gfx2_bind_vertex_buffers(int first_binding, int *buffers, int count, uint64_t *offsets);
int gfx2_bind_index_buffer(int buffer, uint64_t offset, int index_type);
int gfx2_bind_descriptor_sets(int *sets, int count);
int gfx2_set_viewport(gfx_viewport_t *viewport);
int gfx2_set_scissor(gfx_scissor_t *scissor);

/* GPU计算 */
int gfx2_create_compute_pipeline(const char *name, int shader_id);
int gfx2_dispatch(int group_x, int group_y, int group_z);

/* 矩阵工具 */
void gfx2_matrix_identity(gfx_matrix4_t *m);
void gfx2_matrix_multiply(gfx_matrix4_t *result, const gfx_matrix4_t *a, const gfx_matrix4_t *b);
void gfx2_matrix_perspective(gfx_matrix4_t *m, float fov, float aspect, float near, float far);
void gfx2_matrix_lookat(gfx_matrix4_t *m, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz);
void gfx2_matrix_translate(gfx_matrix4_t *m, float x, float y, float z);
void gfx2_matrix_rotate(gfx_matrix4_t *m, float angle, float x, float y, float z);
void gfx2_matrix_scale(gfx_matrix4_t *m, float x, float y, float z);

/* 统计 */
int gfx2_get_stats(int *fps, int *draw_calls, int *triangles, int *texture_mem, int *buffer_mem);
void gfx2_begin_frame(void);
void gfx2_end_frame(void);

#endif /* GFX2_H */
