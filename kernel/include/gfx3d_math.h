/* ============================================================================
 * 赫普OS - 3D图形数学库
 * V8.64: 初始实现
 * 向量、矩阵、四元数运算
 * ========================================================================== */

#ifndef _GFX3D_MATH_H
#define _GFX3D_MATH_H

#include <stdint.h>

/* 2D向量 */
typedef struct {
    float x, y;
} vec2_t;

/* 3D向量 */
typedef struct {
    float x, y, z;
} vec3_t;

/* 4D向量（齐次坐标） */
typedef struct {
    float x, y, z, w;
} vec4_t;

/* 3x3矩阵 */
typedef struct {
    float m[3][3];
} mat3_t;

/* 4x4矩阵 */
typedef struct {
    float m[4][4];
} mat4_t;

/* 四元数 */
typedef struct {
    float x, y, z, w;
} quat_t;

/* 顶点（包含位置、颜色、纹理坐标） */
typedef struct {
    vec3_t position;
    vec3_t normal;
    vec3_t color;
    vec2_t texcoord;
} vertex_t;

/* 函数声明 */

/* 向量运算 */
vec3_t vec3_create(float x, float y, float z);
vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_sub(vec3_t a, vec3_t b);
vec3_t vec3_mul(vec3_t v, float s);
vec3_t vec3_div(vec3_t v, float s);
float  vec3_dot(vec3_t a, vec3_t b);
vec3_t vec3_cross(vec3_t a, vec3_t b);
float  vec3_length(vec3_t v);
vec3_t vec3_normalize(vec3_t v);
vec3_t vec3_negate(vec3_t v);
float  vec3_distance(vec3_t a, vec3_t b);
vec3_t vec3_lerp(vec3_t a, vec3_t b, float t);

/* 4D向量运算 */
vec4_t vec4_create(float x, float y, float z, float w);
vec4_t vec4_from_vec3(vec3_t v, float w);
vec3_t vec4_to_vec3(vec4_t v);
vec4_t vec4_add(vec4_t a, vec4_t b);
vec4_t vec4_sub(vec4_t a, vec4_t b);
vec4_t vec4_mul(vec4_t v, float s);
float  vec4_dot(vec4_t a, vec4_t b);

/* 矩阵运算 */
mat4_t mat4_identity(void);
mat4_t mat4_multiply(mat4_t a, mat4_t b);
mat4_t mat4_transpose(mat4_t m);
mat4_t mat4_translate(float x, float y, float z);
mat4_t mat4_scale(float x, float y, float z);
mat4_t mat4_rotate_x(float angle);
mat4_t mat4_rotate_y(float angle);
mat4_t mat4_rotate_z(float angle);
mat4_t mat4_rotate_axis(vec3_t axis, float angle);
mat4_t mat4_perspective(float fov, float aspect, float near, float far);
mat4_t mat4_ortho(float left, float right, float bottom, float top, float near, float far);
mat4_t mat4_look_at(vec3_t eye, vec3_t center, vec3_t up);
mat4_t mat4_inverse(mat4_t m);
vec4_t mat4_mul_vec4(mat4_t m, vec4_t v);
vec3_t mat4_mul_vec3(mat4_t m, vec3_t v);

/* 四元数运算 */
quat_t quat_create(float x, float y, float z, float w);
quat_t quat_identity(void);
quat_t quat_multiply(quat_t a, quat_t b);
quat_t quat_normalize(quat_t q);
quat_t quat_from_axis_angle(vec3_t axis, float angle);
quat_t quat_from_euler(float pitch, float yaw, float roll);
mat4_t quat_to_mat4(quat_t q);
vec3_t quat_rotate_vec3(quat_t q, vec3_t v);
quat_t quat_slerp(quat_t a, quat_t b, float t);

/* 工具函数 */
float gfx3d_clamp(float value, float min, float max);
float gfx3d_lerp(float a, float b, float t);
float gfx3d_deg_to_rad(float deg);
float gfx3d_rad_to_deg(float rad);
float gfx3d_min(float a, float b);
float gfx3d_max(float a, float b);
int   gfx3d_float_to_int(float f);

#endif /* _GFX3D_MATH_H */
