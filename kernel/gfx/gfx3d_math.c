/* ============================================================================
 * 赫普OS - 3D图形数学库实现
 * V8.64: 初始实现
 * 向量、矩阵、四元数运算
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "../include/gfx3d_math.h"

/* 数学常量 */
#define PI 3.14159265358979323846f
#define EPSILON 0.00001f

/* 平方根（牛顿迭代） */
static float sqrt_f(float x)
{
    if (x <= 0) return 0;
    float result = x;
    for (int i = 0; i < 20; i++) {
        result = (result + x / result) / 2.0f;
    }
    return result;
}

/* 正弦（泰勒级数，角度用弧度） */
static float sin_f(float x)
{
    /* 归一化到 [-PI, PI] */
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;

    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    float x9 = x7 * x2;

    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f + x9 / 362880.0f;
}

/* 余弦 */
static float cos_f(float x)
{
    return sin_f(x + PI / 2.0f);
}

/* 正切 */
static float tan_f(float x)
{
    float c = cos_f(x);
    if (c < EPSILON && c > -EPSILON) return 0;
    return sin_f(x) / c;
}

/* 反正切（简化版） */
static float atan_f(float x)
{
    /* 简化的反正切近似 */
    float x2 = x * x;
    return x * (0.7853981634f - (x - 1) * (0.2447f + 0.0663f * x2));
}

/* 反正切2 */
static float atan2_f(float y, float x)
{
    if (x > 0) return atan_f(y / x);
    if (x < 0) {
        if (y >= 0) return atan_f(y / x) + PI;
        else return atan_f(y / x) - PI;
    }
    if (y > 0) return PI / 2;
    if (y < 0) return -PI / 2;
    return 0;
}

/* 向量运算 */
vec3_t vec3_create(float x, float y, float z)
{
    vec3_t v = {x, y, z};
    return v;
}

vec3_t vec3_add(vec3_t a, vec3_t b)
{
    return vec3_create(a.x + b.x, a.y + b.y, a.z + b.z);
}

vec3_t vec3_sub(vec3_t a, vec3_t b)
{
    return vec3_create(a.x - b.x, a.y - b.y, a.z - b.z);
}

vec3_t vec3_mul(vec3_t v, float s)
{
    return vec3_create(v.x * s, v.y * s, v.z * s);
}

vec3_t vec3_div(vec3_t v, float s)
{
    if (s < EPSILON && s > -EPSILON) return v;
    return vec3_create(v.x / s, v.y / s, v.z / s);
}

float vec3_dot(vec3_t a, vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t vec3_cross(vec3_t a, vec3_t b)
{
    return vec3_create(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

float vec3_length(vec3_t v)
{
    return sqrt_f(v.x * v.x + v.y * v.y + v.z * v.z);
}

vec3_t vec3_normalize(vec3_t v)
{
    float len = vec3_length(v);
    if (len < EPSILON) return vec3_create(0, 0, 0);
    return vec3_div(v, len);
}

vec3_t vec3_negate(vec3_t v)
{
    return vec3_create(-v.x, -v.y, -v.z);
}

float vec3_distance(vec3_t a, vec3_t b)
{
    return vec3_length(vec3_sub(a, b));
}

vec3_t vec3_lerp(vec3_t a, vec3_t b, float t)
{
    return vec3_create(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

/* 4D向量运算 */
vec4_t vec4_create(float x, float y, float z, float w)
{
    vec4_t v = {x, y, z, w};
    return v;
}

vec4_t vec4_from_vec3(vec3_t v, float w)
{
    return vec4_create(v.x, v.y, v.z, w);
}

vec3_t vec4_to_vec3(vec4_t v)
{
    if (v.w < EPSILON && v.w > -EPSILON) return vec3_create(v.x, v.y, v.z);
    return vec3_create(v.x / v.w, v.y / v.w, v.z / v.w);
}

vec4_t vec4_add(vec4_t a, vec4_t b)
{
    return vec4_create(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

vec4_t vec4_sub(vec4_t a, vec4_t b)
{
    return vec4_create(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}

vec4_t vec4_mul(vec4_t v, float s)
{
    return vec4_create(v.x * s, v.y * s, v.z * s, v.w * s);
}

float vec4_dot(vec4_t a, vec4_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/* 矩阵运算 */
mat4_t mat4_identity(void)
{
    mat4_t m;
    memset(&m, 0, sizeof(mat4_t));
    m.m[0][0] = 1;
    m.m[1][1] = 1;
    m.m[2][2] = 1;
    m.m[3][3] = 1;
    return m;
}

mat4_t mat4_multiply(mat4_t a, mat4_t b)
{
    mat4_t result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = 0;
            for (int k = 0; k < 4; k++) {
                result.m[i][j] += a.m[i][k] * b.m[k][j];
            }
        }
    }
    return result;
}

mat4_t mat4_transpose(mat4_t m)
{
    mat4_t result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = m.m[j][i];
        }
    }
    return result;
}

mat4_t mat4_translate(float x, float y, float z)
{
    mat4_t m = mat4_identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

mat4_t mat4_scale(float x, float y, float z)
{
    mat4_t m = mat4_identity();
    m.m[0][0] = x;
    m.m[1][1] = y;
    m.m[2][2] = z;
    return m;
}

mat4_t mat4_rotate_x(float angle)
{
    mat4_t m = mat4_identity();
    float c = cos_f(angle);
    float s = sin_f(angle);
    m.m[1][1] = c;
    m.m[1][2] = -s;
    m.m[2][1] = s;
    m.m[2][2] = c;
    return m;
}

mat4_t mat4_rotate_y(float angle)
{
    mat4_t m = mat4_identity();
    float c = cos_f(angle);
    float s = sin_f(angle);
    m.m[0][0] = c;
    m.m[0][2] = s;
    m.m[2][0] = -s;
    m.m[2][2] = c;
    return m;
}

mat4_t mat4_rotate_z(float angle)
{
    mat4_t m = mat4_identity();
    float c = cos_f(angle);
    float s = sin_f(angle);
    m.m[0][0] = c;
    m.m[0][1] = -s;
    m.m[1][0] = s;
    m.m[1][1] = c;
    return m;
}

mat4_t mat4_rotate_axis(vec3_t axis, float angle)
{
    axis = vec3_normalize(axis);
    float c = cos_f(angle);
    float s = sin_f(angle);
    float t = 1 - c;

    mat4_t m;
    m.m[0][0] = t * axis.x * axis.x + c;
    m.m[0][1] = t * axis.x * axis.y - s * axis.z;
    m.m[0][2] = t * axis.x * axis.z + s * axis.y;
    m.m[0][3] = 0;

    m.m[1][0] = t * axis.x * axis.y + s * axis.z;
    m.m[1][1] = t * axis.y * axis.y + c;
    m.m[1][2] = t * axis.y * axis.z - s * axis.x;
    m.m[1][3] = 0;

    m.m[2][0] = t * axis.x * axis.z - s * axis.y;
    m.m[2][1] = t * axis.y * axis.z + s * axis.x;
    m.m[2][2] = t * axis.z * axis.z + c;
    m.m[2][3] = 0;

    m.m[3][0] = 0;
    m.m[3][1] = 0;
    m.m[3][2] = 0;
    m.m[3][3] = 1;

    return m;
}

mat4_t mat4_perspective(float fov, float aspect, float near, float far)
{
    mat4_t m;
    memset(&m, 0, sizeof(mat4_t));

    float f = 1.0f / tan_f(fov / 2.0f);
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = (far + near) / (near - far);
    m.m[2][3] = (2 * far * near) / (near - far);
    m.m[3][2] = -1;

    return m;
}

mat4_t mat4_ortho(float left, float right, float bottom, float top, float near, float far)
{
    mat4_t m = mat4_identity();
    m.m[0][0] = 2 / (right - left);
    m.m[1][1] = 2 / (top - bottom);
    m.m[2][2] = -2 / (far - near);
    m.m[0][3] = -(right + left) / (right - left);
    m.m[1][3] = -(top + bottom) / (top - bottom);
    m.m[2][3] = -(far + near) / (far - near);
    return m;
}

mat4_t mat4_look_at(vec3_t eye, vec3_t center, vec3_t up)
{
    vec3_t f = vec3_normalize(vec3_sub(center, eye));
    vec3_t s = vec3_normalize(vec3_cross(f, up));
    vec3_t u = vec3_cross(s, f);

    mat4_t m;
    m.m[0][0] = s.x;
    m.m[0][1] = s.y;
    m.m[0][2] = s.z;
    m.m[0][3] = -vec3_dot(s, eye);

    m.m[1][0] = u.x;
    m.m[1][1] = u.y;
    m.m[1][2] = u.z;
    m.m[1][3] = -vec3_dot(u, eye);

    m.m[2][0] = -f.x;
    m.m[2][1] = -f.y;
    m.m[2][2] = -f.z;
    m.m[2][3] = vec3_dot(f, eye);

    m.m[3][0] = 0;
    m.m[3][1] = 0;
    m.m[3][2] = 0;
    m.m[3][3] = 1;

    return m;
}

mat4_t mat4_inverse(mat4_t m)
{
    /* 简化的4x4矩阵求逆（使用伴随矩阵法） */
    float inv[16], det;
    int i;

    float *a = &m.m[0][0];

    inv[0] =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] +
              a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] -
              a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8] =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] +
              a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] -
              a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] -
              a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5] =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] +
              a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] -
              a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] +
              a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2] =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] +
              a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] -
              a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
    inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] +
              a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] -
              a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] -
              a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
    inv[7] =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] +
              a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]  + a[4]*a[1]*a[11] -
              a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
    inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]  - a[4]*a[1]*a[10] +
              a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];

    det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if (det < EPSILON && det > -EPSILON) return mat4_identity();

    det = 1.0f / det;

    mat4_t result;
    for (i = 0; i < 16; i++) {
        (&result.m[0][0])[i] = inv[i] * det;
    }

    return result;
}

vec4_t mat4_mul_vec4(mat4_t m, vec4_t v)
{
    vec4_t result;
    result.x = m.m[0][0]*v.x + m.m[0][1]*v.y + m.m[0][2]*v.z + m.m[0][3]*v.w;
    result.y = m.m[1][0]*v.x + m.m[1][1]*v.y + m.m[1][2]*v.z + m.m[1][3]*v.w;
    result.z = m.m[2][0]*v.x + m.m[2][1]*v.y + m.m[2][2]*v.z + m.m[2][3]*v.w;
    result.w = m.m[3][0]*v.x + m.m[3][1]*v.y + m.m[3][2]*v.z + m.m[3][3]*v.w;
    return result;
}

vec3_t mat4_mul_vec3(mat4_t m, vec3_t v)
{
    vec4_t v4 = vec4_from_vec3(v, 1);
    v4 = mat4_mul_vec4(m, v4);
    return vec4_to_vec3(v4);
}

/* 四元数运算 */
quat_t quat_create(float x, float y, float z, float w)
{
    quat_t q = {x, y, z, w};
    return q;
}

quat_t quat_identity(void)
{
    return quat_create(0, 0, 0, 1);
}

quat_t quat_multiply(quat_t a, quat_t b)
{
    return quat_create(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    );
}

quat_t quat_normalize(quat_t q)
{
    float len = sqrt_f(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len < EPSILON) return quat_identity();
    return quat_create(q.x/len, q.y/len, q.z/len, q.w/len);
}

quat_t quat_from_axis_angle(vec3_t axis, float angle)
{
    axis = vec3_normalize(axis);
    float half = angle / 2;
    float s = sin_f(half);
    return quat_create(axis.x * s, axis.y * s, axis.z * s, cos_f(half));
}

quat_t quat_from_euler(float pitch, float yaw, float roll)
{
    float cy = cos_f(yaw * 0.5f);
    float sy = sin_f(yaw * 0.5f);
    float cp = cos_f(pitch * 0.5f);
    float sp = sin_f(pitch * 0.5f);
    float cr = cos_f(roll * 0.5f);
    float sr = sin_f(roll * 0.5f);

    return quat_create(
        cy * sp * cr + sy * cp * sr,
        sy * cp * cr - cy * sp * sr,
        cy * cp * sr - sy * sp * cr,
        cy * cp * cr + sy * sp * sr
    );
}

mat4_t quat_to_mat4(quat_t q)
{
    mat4_t m = mat4_identity();

    float x = q.x, y = q.y, z = q.z, w = q.w;

    m.m[0][0] = 1 - 2*(y*y + z*z);
    m.m[0][1] = 2*(x*y - w*z);
    m.m[0][2] = 2*(x*z + w*y);

    m.m[1][0] = 2*(x*y + w*z);
    m.m[1][1] = 1 - 2*(x*x + z*z);
    m.m[1][2] = 2*(y*z - w*x);

    m.m[2][0] = 2*(x*z - w*y);
    m.m[2][1] = 2*(y*z + w*x);
    m.m[2][2] = 1 - 2*(x*x + y*y);

    return m;
}

vec3_t quat_rotate_vec3(quat_t q, vec3_t v)
{
    vec3_t uv = vec3_cross(vec3_create(q.x, q.y, q.z), v);
    vec3_t uuv = vec3_cross(vec3_create(q.x, q.y, q.z), uv);
    uv = vec3_mul(uv, 2 * q.w);
    uuv = vec3_mul(uuv, 2);
    return vec3_add(vec3_add(v, uv), uuv);
}

quat_t quat_slerp(quat_t a, quat_t b, float t)
{
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;

    if (dot < 0) {
        b = quat_create(-b.x, -b.y, -b.z, -b.w);
        dot = -dot;
    }

    if (dot > 0.9995f) {
        return quat_normalize(quat_create(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        ));
    }

    float theta_0 = atan2_f(sqrt_f(1 - dot*dot), dot);
    float theta = theta_0 * t;
    float sin_theta = sin_f(theta);
    float sin_theta_0 = sin_f(theta_0);

    float s0 = cos_f(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;

    return quat_create(
        s0 * a.x + s1 * b.x,
        s0 * a.y + s1 * b.y,
        s0 * a.z + s1 * b.z,
        s0 * a.w + s1 * b.w
    );
}

/* 工具函数 */
float gfx3d_clamp(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float gfx3d_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float gfx3d_deg_to_rad(float deg)
{
    return deg * PI / 180.0f;
}

float gfx3d_rad_to_deg(float rad)
{
    return rad * 180.0f / PI;
}

float gfx3d_min(float a, float b)
{
    return a < b ? a : b;
}

float gfx3d_max(float a, float b)
{
    return a > b ? a : b;
}

int gfx3d_float_to_int(float f)
{
    return (int)(f + (f >= 0 ? 0.5f : -0.5f));
}
