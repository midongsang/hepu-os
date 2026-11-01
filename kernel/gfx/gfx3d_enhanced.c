/* ============================================================================
 * 赫普OS - 3D渲染器增强功能
 * V8.92: 初始实现
 * 雾效果、点/线参数、更多基本形状
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gfx3d.h"
#include "gfx3d_math.h"
#include "../include/graphics.h"
#include "../include/vga.h"

static float inline_sqrtf(float x)
{
    if (x <= 0) return 0;
    float guess = x * 0.5f;
    for (int i = 0; i < 10; i++)
        guess = (guess + x / guess) * 0.5f;
    return guess;
}

static float inline_expf(float x)
{
    if (x < -20) return 0;
    if (x > 20) return 1e9f;
    float result = 1.0f, term = 1.0f;
    for (int i = 1; i < 20; i++) {
        term *= x / i;
        result += term;
    }
    return result;
}

extern gfx3d_context_t g_gfx3d_ctx;

static vertex_t make_vertex(float x, float y, float z,
                            float nx, float ny, float nz,
                            gfx_color_t color)
{
    vertex_t v;
    v.position.x = x; v.position.y = y; v.position.z = z;
    v.normal.x = nx; v.normal.y = ny; v.normal.z = nz;
    v.color.x = ((color >> 16) & 0xFF) / 255.0f;
    v.color.y = ((color >> 8) & 0xFF) / 255.0f;
    v.color.z = (color & 0xFF) / 255.0f;
    v.texcoord.x = 0; v.texcoord.y = 0;
    return v;
}

void gfx3d_enable_fog(int enable) { g_gfx3d_ctx.fog_enabled = enable ? 1 : 0; }
void gfx3d_set_fog_mode(int mode) { if (mode >= 0 && mode <= 2) g_gfx3d_ctx.fog_mode = mode; }
void gfx3d_set_fog_range(float start, float end) { g_gfx3d_ctx.fog_start = start; g_gfx3d_ctx.fog_end = end; }
void gfx3d_set_fog_density(float density) { g_gfx3d_ctx.fog_density = density; }
void gfx3d_set_fog_color(vec3_t color) { g_gfx3d_ctx.fog_color = color; }
void gfx3d_set_point_size(float size) { if (size > 0) g_gfx3d_ctx.point_size = size; }
void gfx3d_set_line_width(float width) { if (width > 0) g_gfx3d_ctx.line_width = width; }

void gfx3d_draw_tetrahedron(float size, gfx_color_t color)
{
    float s = size * 0.5f;
    vertex_t verts[4];
    verts[0] = make_vertex( 0,  s,  0, 0, 1, 0, color);
    verts[1] = make_vertex(-s, -s,  s, 0, 0, 1, color);
    verts[2] = make_vertex( s, -s,  s, 1, 0, 0, color);
    verts[3] = make_vertex( 0, -s, -s, 0, 0, 1, color);
    uint16_t indices[12] = {0,1,2, 0,2,3, 0,3,1, 1,3,2};
    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, 12);
}

void gfx3d_draw_octahedron(float size, gfx_color_t color)
{
    float s = size * 0.5f;
    vertex_t verts[6];
    verts[0] = make_vertex( 0,  s,  0, 0, 1, 0, color);
    verts[1] = make_vertex( 0, -s,  0, 0,-1, 0, color);
    verts[2] = make_vertex( s,  0,  0, 1, 0, 0, color);
    verts[3] = make_vertex(-s,  0,  0,-1, 0, 0, color);
    verts[4] = make_vertex( 0,  0,  s, 0, 0, 1, color);
    verts[5] = make_vertex( 0,  0, -s, 0, 0,-1, color);
    uint16_t indices[24] = {0,4,2, 0,2,5, 0,5,3, 0,3,4, 1,2,4, 1,5,2, 1,3,5, 1,4,3};
    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, 24);
}

void gfx3d_draw_icosahedron(float size, gfx_color_t color)
{
    float phi = (1.0f + inline_sqrtf(5.0f)) / 2.0f;
    float a = size * 0.5f, b = a / phi;
    vertex_t verts[12];
    verts[0]  = make_vertex( 0,  b,  a, 0, 0, 1, color);
    verts[1]  = make_vertex( 0, -b,  a, 0, 0, 1, color);
    verts[2]  = make_vertex( 0,  b, -a, 0, 0,-1, color);
    verts[3]  = make_vertex( 0, -b, -a, 0, 0,-1, color);
    verts[4]  = make_vertex( a,  0,  b, 1, 0, 0, color);
    verts[5]  = make_vertex(-a,  0,  b,-1, 0, 0, color);
    verts[6]  = make_vertex( a,  0, -b, 1, 0, 0, color);
    verts[7]  = make_vertex(-a,  0, -b,-1, 0, 0, color);
    verts[8]  = make_vertex( b,  a,  0, 0, 1, 0, color);
    verts[9]  = make_vertex(-b,  a,  0, 0, 1, 0, color);
    verts[10] = make_vertex( b, -a,  0, 0,-1, 0, color);
    verts[11] = make_vertex(-b, -a,  0, 0,-1, 0, color);
    uint16_t indices[60] = {
        0,1,5, 0,5,9, 0,9,8, 0,8,4, 0,4,1,
        1,4,10, 1,10,11, 1,11,5, 2,3,7, 2,7,9,
        2,9,5, 2,5,11, 2,11,6, 2,6,3, 3,6,10,
        3,10,4, 3,4,8, 6,7,2, 6,11,10, 7,8,9
    };
    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, 60);
}

void gfx3d_draw_dodecahedron(float size, gfx_color_t color)
{
    float phi = (1.0f + inline_sqrtf(5.0f)) / 2.0f;
    float a = size * 0.5f, b = a / phi, c = a * phi;
    float coords[20][3] = {
        {0,b,c},{0,-b,c},{0,b,-c},{0,-b,-c},
        {c,0,b},{-c,0,b},{c,0,-b},{-c,0,-b},
        {b,c,0},{-b,c,0},{b,-c,0},{-b,-c,0},
        {a,a,a},{-a,a,a},{a,-a,a},{-a,-a,a},
        {a,a,-a},{-a,a,-a},{a,-a,-a},{-a,-a,-a}
    };
    vertex_t verts[20];
    for (int i = 0; i < 20; i++)
        verts[i] = make_vertex(coords[i][0], coords[i][1], coords[i][2],
                               coords[i][0], coords[i][1], coords[i][2], color);
    uint16_t indices[108] = {
        0,1,14, 0,14,12, 0,12,8, 0,8,9, 0,9,13, 0,13,1,
        1,13,15, 1,15,11, 1,11,14,
        2,3,18, 2,18,16, 2,16,8, 2,8,9, 2,9,17, 2,17,3,
        3,17,19, 3,19,11, 3,11,18,
        4,5,12, 4,12,14, 4,14,10, 4,10,18, 4,18,11, 4,11,5,
        5,11,19, 5,19,15, 5,15,13,
        6,7,16, 6,16,17, 6,17,9, 6,9,8, 6,8,12, 6,12,7,
        7,12,13, 7,13,15, 7,15,19
    };
    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, 108);
}

void gfx3d_enhanced_init(void)
{
    g_gfx3d_ctx.fog_enabled = 0;
    g_gfx3d_ctx.fog_mode = 0;
    g_gfx3d_ctx.fog_start = 5.0f;
    g_gfx3d_ctx.fog_end = 50.0f;
    g_gfx3d_ctx.fog_density = 0.02f;
    g_gfx3d_ctx.fog_color.x = 0.5f;
    g_gfx3d_ctx.fog_color.y = 0.5f;
    g_gfx3d_ctx.fog_color.z = 0.5f;
    g_gfx3d_ctx.point_size = 1.0f;
    g_gfx3d_ctx.line_width = 1.0f;
    vga_puts("[GFX3D] Enhanced features initialized (fog + shapes)\n");
}
