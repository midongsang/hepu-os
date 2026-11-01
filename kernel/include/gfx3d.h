/* ============================================================================
 * 赫普OS - 3D图形渲染器
 * V8.64: 初始实现
 * 软件3D渲染管线：顶点变换、裁剪、光栅化、深度测试
 * ========================================================================== */

#ifndef _GFX3D_H
#define _GFX3D_H

#include <stdint.h>
#include "gfx3d_math.h"
#include "graphics.h"

/* 3D渲染器状态 */
typedef struct {
    int width;
    int height;
    int initialized;

    /* 变换矩阵 */
    mat4_t model_matrix;
    mat4_t view_matrix;
    mat4_t projection_matrix;
    mat4_t mvp_matrix;  /* model * view * projection */

    /* 视口 */
    int viewport_x;
    int viewport_y;
    int viewport_width;
    int viewport_height;

    /* 深度缓冲 */
    float *depth_buffer;
    int depth_buffer_size;

    /* 渲染状态 */
    int depth_test_enabled;
    int depth_write_enabled;
    int cull_face_enabled;
    int wireframe_mode;
    int lighting_enabled;
    int blend_enabled;       /* V8.84: 透明混合 */
    int blend_src_factor;    /* 源混合因子 */
    int blend_dst_factor;    /* 目标混合因子 */

    /* 光照 */
    vec3_t light_direction;
    vec3_t light_color;
    vec3_t ambient_color;

    /* 统计 */
    uint32_t triangles_drawn;
    uint32_t pixels_drawn;
    uint32_t vertices_processed;

    /* V8.92: 雾效果 */
    int      fog_enabled;
    int      fog_mode;       /* 0=线性, 1=指数, 2=指数平方 */
    float    fog_start;
    float    fog_end;
    float    fog_density;
    vec3_t   fog_color;

    /* V8.92: 点/线参数 */
    float    point_size;
    float    line_width;
} gfx3d_context_t;

/* 图元类型 */
typedef enum {
    GFX3D_POINTS = 0,
    GFX3D_LINES,
    GFX3D_LINE_STRIP,
    GFX3D_TRIANGLES,
    GFX3D_TRIANGLE_STRIP,
    GFX3D_TRIANGLE_FAN
} gfx3d_primitive_t;

/* 纹理 */
typedef struct {
    int width;
    int height;
    uint32_t *pixels;  /* 0xAARRGGBB格式 */
    int initialized;
} gfx3d_texture_t;

/* 纹理过滤模式 */
typedef enum {
    GFX3D_TEX_FILTER_NEAREST = 0,
    GFX3D_TEX_FILTER_LINEAR
} gfx3d_tex_filter_t;

/* 混合因子（V8.84新增） */
typedef enum {
    GFX3D_BLEND_ZERO = 0,
    GFX3D_BLEND_ONE,
    GFX3D_BLEND_SRC_ALPHA,
    GFX3D_BLEND_ONE_MINUS_SRC_ALPHA,
    GFX3D_BLEND_DST_ALPHA,
    GFX3D_BLEND_ONE_MINUS_DST_ALPHA,
    GFX3D_BLEND_SRC_COLOR,
    GFX3D_BLEND_ONE_MINUS_SRC_COLOR
} gfx3d_blend_factor_t;

/* 函数声明 */

/* 初始化/关闭 */
int gfx3d_init(int width, int height);
void gfx3d_shutdown(void);
gfx3d_context_t *gfx3d_get_context(void);

/* 清屏 */
void gfx3d_clear(gfx_color_t color);
void gfx3d_clear_depth(float depth);

/* 矩阵操作 */
void gfx3d_set_model_matrix(mat4_t m);
void gfx3d_set_view_matrix(mat4_t m);
void gfx3d_set_projection_matrix(mat4_t m);
void gfx3d_update_mvp(void);
mat4_t gfx3d_get_mvp(void);

/* 视口 */
void gfx3d_set_viewport(int x, int y, int width, int height);

/* 渲染状态 */
void gfx3d_enable_depth_test(int enable);
void gfx3d_enable_depth_write(int enable);
void gfx3d_enable_cull_face(int enable);
void gfx3d_set_wireframe(int enable);
void gfx3d_enable_lighting(int enable);
void gfx3d_enable_blend(int enable);  /* V8.84: 透明混合开关 */
void gfx3d_set_blend_func(gfx3d_blend_factor_t src, gfx3d_blend_factor_t dst);  /* V8.84 */

/* 光照 */
void gfx3d_set_light_direction(vec3_t dir);
void gfx3d_set_light_color(vec3_t color);
void gfx3d_set_ambient_color(vec3_t color);

/* 绘制图元 */
void gfx3d_draw_arrays(gfx3d_primitive_t mode, const vertex_t *vertices, int count);
void gfx3d_draw_elements(gfx3d_primitive_t mode, const vertex_t *vertices,
                          const uint16_t *indices, int count);

/* 基础形状 */
void gfx3d_draw_cube(float size, gfx_color_t color);
void gfx3d_draw_sphere(float radius, int segments, int rings, gfx_color_t color);
void gfx3d_draw_cylinder(float radius, float height, int segments, gfx_color_t color);
void gfx3d_draw_plane(float width, float height, gfx_color_t color);
/* V8.84新增形状 */
void gfx3d_draw_cone(float radius, float height, int segments, gfx_color_t color);
void gfx3d_draw_torus(float major_radius, float minor_radius, int major_segments, int minor_segments, gfx_color_t color);
void gfx3d_draw_pyramid(float base_size, float height, gfx_color_t color);
void gfx3d_draw_disk(float inner_radius, float outer_radius, int segments, gfx_color_t color);

/* 统计 */
void gfx3d_get_stats(uint32_t *triangles, uint32_t *pixels, uint32_t *vertices);
void gfx3d_reset_stats(void);

/* 纹理管理 */
int gfx3d_texture_create(gfx3d_texture_t *tex, int width, int height);
void gfx3d_texture_destroy(gfx3d_texture_t *tex);
int gfx3d_texture_load(gfx3d_texture_t *tex, const gfx_color_t *pixels, int width, int height);
void gfx3d_texture_set_active(gfx3d_texture_t *tex);
void gfx3d_texture_set_filter(gfx3d_tex_filter_t filter);
gfx_color_t gfx3d_texture_sample(float u, float v);
int gfx3d_texture_generate_checkerboard(gfx3d_texture_t *tex, int size, int squares,
                                          gfx_color_t c1, gfx_color_t c2);

/* 带纹理的形状 */
void gfx3d_draw_textured_cube(float size, gfx3d_texture_t *tex);

/* V8.92: 雾效果 */
void gfx3d_enable_fog(int enable);
void gfx3d_set_fog_mode(int mode);  /* 0=线性, 1=指数, 2=指数平方 */
void gfx3d_set_fog_range(float start, float end);
void gfx3d_set_fog_density(float density);
void gfx3d_set_fog_color(vec3_t color);

/* V8.92: 点/线参数 */
void gfx3d_set_point_size(float size);
void gfx3d_set_line_width(float width);

/* V8.92: 更多基本形状 */
void gfx3d_draw_tetrahedron(float size, gfx_color_t color);
void gfx3d_draw_octahedron(float size, gfx_color_t color);
void gfx3d_draw_icosahedron(float size, gfx_color_t color);
void gfx3d_draw_dodecahedron(float size, gfx_color_t color);

#endif /* _GFX3D_H */
