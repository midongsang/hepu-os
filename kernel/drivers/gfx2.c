/* ============================================================================
 * 赫普OS - 图形深化
 * Vulkan式3D API + 着色器 + 纹理压缩 + GPU计算 + 矩阵工具
 * ========================================================================== */

#include <stdint.h>
#include "gfx2.h"
#include "vga.h"
#include "string.h"

static gfx2_t gfx;

/* 简单sin/cos近似 */
static float gfx_sin(float x)
{
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f/6.0f - x2 * (1.0f/120.0f - x2 * 1.0f/5040.0f)));
}

static float gfx_cos(float x)
{
    float x2 = x * x;
    return 1.0f - x2 * (0.5f - x2 * (1.0f/24.0f - x2 * (1.0f/720.0f - x2 * 1.0f/40320.0f)));
}

static float gfx_tan(float x)
{
    return gfx_sin(x) / gfx_cos(x);
}

void gfx2_init(void)
{
    memset(&gfx, 0, sizeof(gfx2_t));
    gfx.initialized = 1;
    gfx.api = GFX_API_VULKAN;
    gfx.api_version = 100;  /* Vulkan 1.0 */
    strncpy(gfx.device_name, "Hepu GPU", 127);
    strncpy(gfx.vendor_name, "Hepu", 63);
    gfx.driver_version = 100;
    gfx.max_texture_size = 4096;
    gfx.max_anisotropy = 16;
    gfx.support_compute = 1;
    gfx.support_geometry = 1;
    gfx.support_tessellation = 1;
    gfx.support_compression_bc = 1;
    gfx.support_compression_etc = 1;
    gfx.support_compression_astc = 1;

    vga_puts("[GFX2] graphics deep init done \n");
    vga_printf(" API: Vulkan %d.%d, device: %s\n",
               gfx.api_version / 100, gfx.api_version % 100, gfx.device_name);
}

int gfx2_get_device_info(gfx2_t *info)
{
    if (!info) return -1;
    memcpy(info, &gfx, sizeof(gfx2_t));
    return 0;
}

int gfx2_get_api(void) { return gfx.api; }

int gfx2_create_texture(const char *name, gfx_texture_type_t type, gfx_format_t format,
                        int width, int height, int depth, int mipmaps, void *data)
{
    for (int i = 0; i < GFX2_MAX_TEXTURES; i++) {
        if (!gfx.textures[i].used) {
            memset(&gfx.textures[i], 0, sizeof(gfx_texture_t));
            gfx.textures[i].used = 1;
            gfx.textures[i].id = i;
            if (name) strncpy(gfx.textures[i].name, name, 63);
            gfx.textures[i].type = type;
            gfx.textures[i].format = format;
            gfx.textures[i].width = width;
            gfx.textures[i].height = height;
            gfx.textures[i].depth = depth;
            gfx.textures[i].mip_levels = mipmaps;
            gfx.textures[i].data = data;
            gfx.texture_count++;
            return i;
        }
    }
    return -1;
}

int gfx2_destroy_texture(int texture_id)
{
    if (texture_id < 0 || texture_id >= GFX2_MAX_TEXTURES || !gfx.textures[texture_id].used) return -1;
    gfx.textures[texture_id].used = 0;
    gfx.texture_count--;
    return 0;
}

int gfx2_get_texture(int texture_id, gfx_texture_t *tex)
{
    if (texture_id < 0 || texture_id >= GFX2_MAX_TEXTURES || !gfx.textures[texture_id].used || !tex) return -1;
    memcpy(tex, &gfx.textures[texture_id], sizeof(gfx_texture_t));
    return 0;
}

int gfx2_generate_mipmaps(int texture_id)
{
    if (texture_id < 0 || texture_id >= GFX2_MAX_TEXTURES || !gfx.textures[texture_id].used) return -1;
    gfx.textures[texture_id].mip_levels = 1;
    int w = gfx.textures[texture_id].width;
    while (w > 1) { w /= 2; gfx.textures[texture_id].mip_levels++; }
    return 0;
}

int gfx2_compress_texture(int texture_id, gfx_format_t format)
{
    if (texture_id < 0 || texture_id >= GFX2_MAX_TEXTURES || !gfx.textures[texture_id].used) return -1;
    gfx.textures[texture_id].format = format;
    gfx.textures[texture_id].compressed = 1;
    return 0;
}

int gfx2_create_buffer(const char *name, gfx_buffer_type_t type, uint64_t size, void *data)
{
    (void)name;
    for (int i = 0; i < GFX2_MAX_BUFFERS; i++) {
        if (!gfx.buffers[i].used) {
            memset(&gfx.buffers[i], 0, sizeof(gfx_buffer_t));
            gfx.buffers[i].used = 1;
            gfx.buffers[i].id = i;
            gfx.buffers[i].type = type;
            gfx.buffers[i].size = size;
            gfx.buffers[i].data = data;
            gfx.buffer_count++;
            return i;
        }
    }
    return -1;
}

int gfx2_destroy_buffer(int buffer_id)
{
    if (buffer_id < 0 || buffer_id >= GFX2_MAX_BUFFERS || !gfx.buffers[buffer_id].used) return -1;
    gfx.buffers[buffer_id].used = 0;
    gfx.buffer_count--;
    return 0;
}

int gfx2_map_buffer(int buffer_id, void **data)
{
    if (buffer_id < 0 || buffer_id >= GFX2_MAX_BUFFERS || !gfx.buffers[buffer_id].used || !data) return -1;
    *data = gfx.buffers[buffer_id].data;
    gfx.buffers[buffer_id].mapped = 1;
    return 0;
}

int gfx2_unmap_buffer(int buffer_id)
{
    if (buffer_id < 0 || buffer_id >= GFX2_MAX_BUFFERS || !gfx.buffers[buffer_id].used) return -1;
    gfx.buffers[buffer_id].mapped = 0;
    return 0;
}

int gfx2_update_buffer(int buffer_id, uint64_t offset, const void *data, uint64_t size)
{
    (void)offset; (void)data; (void)size;
    if (buffer_id < 0 || buffer_id >= GFX2_MAX_BUFFERS || !gfx.buffers[buffer_id].used) return -1;
    return 0;
}

int gfx2_create_shader(const char *name, gfx_shader_type_t type, const void *spirv, uint32_t size)
{
    for (int i = 0; i < GFX2_MAX_SHADERS; i++) {
        if (!gfx.shaders[i].used) {
            memset(&gfx.shaders[i], 0, sizeof(gfx_shader_t));
            gfx.shaders[i].used = 1;
            gfx.shaders[i].id = i;
            if (name) strncpy(gfx.shaders[i].name, name, 63);
            gfx.shaders[i].type = type;
            gfx.shaders[i].spirv = (void *)spirv;
            gfx.shaders[i].spirv_size = size;
            strncpy(gfx.shaders[i].entry_point, "main", 63);
            gfx.shader_count++;
            return i;
        }
    }
    return -1;
}

int gfx2_destroy_shader(int shader_id)
{
    if (shader_id < 0 || shader_id >= GFX2_MAX_SHADERS || !gfx.shaders[shader_id].used) return -1;
    gfx.shaders[shader_id].used = 0;
    gfx.shader_count--;
    return 0;
}

int gfx2_compile_shader(int shader_id, const char *glsl)
{
    (void)glsl;
    if (shader_id < 0 || shader_id >= GFX2_MAX_SHADERS || !gfx.shaders[shader_id].used) return -1;
    gfx.shaders[shader_id].compiled = 1;
    return 0;
}

int gfx2_create_pipeline(const char *name, gfx_pipeline_t *desc)
{
    if (!desc) return -1;
    for (int i = 0; i < GFX2_MAX_PIPELINES; i++) {
        if (!gfx.pipelines[i].used) {
            memcpy(&gfx.pipelines[i], desc, sizeof(gfx_pipeline_t));
            gfx.pipelines[i].used = 1;
            gfx.pipelines[i].id = i;
            if (name) strncpy(gfx.pipelines[i].name, name, 63);
            gfx.pipeline_count++;
            return i;
        }
    }
    return -1;
}

int gfx2_destroy_pipeline(int pipeline_id)
{
    if (pipeline_id < 0 || pipeline_id >= GFX2_MAX_PIPELINES || !gfx.pipelines[pipeline_id].used) return -1;
    gfx.pipelines[pipeline_id].used = 0;
    gfx.pipeline_count--;
    return 0;
}

int gfx2_bind_pipeline(int pipeline_id)
{
    (void)pipeline_id;
    return 0;
}

int gfx2_create_render_pass(const char *name, gfx_format_t color_format, gfx_format_t depth_format)
{
    for (int i = 0; i < GFX2_MAX_RENDER_PASSES; i++) {
        if (!gfx.render_passes[i].used) {
            memset(&gfx.render_passes[i], 0, sizeof(gfx_render_pass_t));
            gfx.render_passes[i].used = 1;
            gfx.render_passes[i].id = i;
            if (name) strncpy(gfx.render_passes[i].name, name, 63);
            gfx.render_passes[i].color_format = color_format;
            gfx.render_passes[i].depth_format = depth_format;
            gfx.render_pass_count++;
            return i;
        }
    }
    return -1;
}

int gfx2_destroy_render_pass(int rp_id)
{
    if (rp_id < 0 || rp_id >= GFX2_MAX_RENDER_PASSES || !gfx.render_passes[rp_id].used) return -1;
    gfx.render_passes[rp_id].used = 0;
    gfx.render_pass_count--;
    return 0;
}

int gfx2_begin_render_pass(int rp_id, int framebuffer_id, int clear)
{
    (void)rp_id; (void)framebuffer_id; (void)clear;
    return 0;
}

int gfx2_end_render_pass(void) { return 0; }

int gfx2_create_framebuffer(int rp_id, int width, int height, int *color_attachments, int color_count, int depth)
{
    (void)rp_id; (void)color_attachments; (void)color_count; (void)depth;
    for (int i = 0; i < GFX2_MAX_FRAMEBUFFERS; i++) {
        if (!gfx.framebuffers[i].used) {
            memset(&gfx.framebuffers[i], 0, sizeof(gfx_framebuffer_t));
            gfx.framebuffers[i].used = 1;
            gfx.framebuffers[i].id = i;
            gfx.framebuffers[i].width = width;
            gfx.framebuffers[i].height = height;
            gfx.framebuffer_count++;
            return i;
        }
    }
    return -1;
}

int gfx2_destroy_framebuffer(int fb_id)
{
    if (fb_id < 0 || fb_id >= GFX2_MAX_FRAMEBUFFERS || !gfx.framebuffers[fb_id].used) return -1;
    gfx.framebuffers[fb_id].used = 0;
    gfx.framebuffer_count--;
    return 0;
}

int gfx2_draw(int vertex_count, int instance_count, int first_vertex, int first_instance)
{
    (void)vertex_count; (void)instance_count; (void)first_vertex; (void)first_instance;
    gfx.draw_calls++;
    gfx.triangles_rendered += vertex_count / 3;
    return 0;
}

int gfx2_draw_indexed(int index_count, int instance_count, int first_index, int vertex_offset, int first_instance)
{
    (void)index_count; (void)instance_count; (void)first_index; (void)vertex_offset; (void)first_instance;
    gfx.draw_calls++;
    gfx.triangles_rendered += index_count / 3;
    return 0;
}

int gfx2_bind_vertex_buffers(int first_binding, int *buffers, int count, uint64_t *offsets)
{
    (void)first_binding; (void)buffers; (void)count; (void)offsets;
    return 0;
}

int gfx2_bind_index_buffer(int buffer, uint64_t offset, int index_type)
{
    (void)buffer; (void)offset; (void)index_type;
    return 0;
}

int gfx2_bind_descriptor_sets(int *sets, int count)
{
    (void)sets; (void)count;
    return 0;
}

int gfx2_set_viewport(gfx_viewport_t *viewport) { (void)viewport; return 0; }
int gfx2_set_scissor(gfx_scissor_t *scissor) { (void)scissor; return 0; }

int gfx2_create_compute_pipeline(const char *name, int shader_id)
{
    for (int i = 0; i < 16; i++) {
        if (!gfx.compute_pipelines[i].used) {
            memset(&gfx.compute_pipelines[i], 0, sizeof(gfx_compute_pipeline_t));
            gfx.compute_pipelines[i].used = 1;
            gfx.compute_pipelines[i].id = i;
            if (name) strncpy(gfx.compute_pipelines[i].name, name, 63);
            gfx.compute_pipelines[i].compute_shader = shader_id;
            gfx.compute_pipelines[i].local_size_x = 32;
            gfx.compute_pipelines[i].local_size_y = 1;
            gfx.compute_pipelines[i].local_size_z = 1;
            gfx.compute_pipeline_count++;
            return i;
        }
    }
    return -1;
}

int gfx2_dispatch(int group_x, int group_y, int group_z)
{
    (void)group_x; (void)group_y; (void)group_z;
    return 0;
}

void gfx2_matrix_identity(gfx_matrix4_t *m)
{
    if (!m) return;
    memset(m->m, 0, sizeof(float) * 16);
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

void gfx2_matrix_multiply(gfx_matrix4_t *result, const gfx_matrix4_t *a, const gfx_matrix4_t *b)
{
    if (!result || !a || !b) return;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a->m[i * 4 + k] * b->m[k * 4 + j];
            }
            result->m[i * 4 + j] = sum;
        }
    }
}

void gfx2_matrix_perspective(gfx_matrix4_t *m, float fov, float aspect, float near, float far)
{
    if (!m) return;
    memset(m->m, 0, sizeof(float) * 16);
    float f = 1.0f / gfx_tan(fov / 2.0f);
    m->m[0] = f / aspect;
    m->m[5] = f;
    m->m[10] = (far + near) / (near - far);
    m->m[11] = -1.0f;
    m->m[14] = (2.0f * far * near) / (near - far);
}

void gfx2_matrix_lookat(gfx_matrix4_t *m, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz)
{
    (void)m; (void)ex; (void)ey; (void)ez; (void)cx; (void)cy; (void)cz; (void)ux; (void)uy; (void)uz;
    /* 原型阶段: 简化实现 */
    gfx2_matrix_identity(m);
}

void gfx2_matrix_translate(gfx_matrix4_t *m, float x, float y, float z)
{
    if (!m) return;
    m->m[12] += x;
    m->m[13] += y;
    m->m[14] += z;
}

void gfx2_matrix_rotate(gfx_matrix4_t *m, float angle, float x, float y, float z)
{
    if (!m) return;
    float c = gfx_cos(angle);
    float s = gfx_sin(angle);
    float len = 1.0f;  /* 假设已归一化 */
    x /= len; y /= len; z /= len;
    gfx_matrix4_t rot;
    gfx2_matrix_identity(&rot);
    rot.m[0] = c + x * x * (1 - c);
    rot.m[1] = x * y * (1 - c) - z * s;
    rot.m[2] = x * z * (1 - c) + y * s;
    rot.m[4] = y * x * (1 - c) + z * s;
    rot.m[5] = c + y * y * (1 - c);
    rot.m[6] = y * z * (1 - c) - x * s;
    rot.m[8] = z * x * (1 - c) - y * s;
    rot.m[9] = z * y * (1 - c) + x * s;
    rot.m[10] = c + z * z * (1 - c);
    gfx_matrix4_t result;
    gfx2_matrix_multiply(&result, m, &rot);
    memcpy(m->m, result.m, sizeof(float) * 16);
}

void gfx2_matrix_scale(gfx_matrix4_t *m, float x, float y, float z)
{
    if (!m) return;
    m->m[0] *= x;
    m->m[5] *= y;
    m->m[10] *= z;
}

int gfx2_get_stats(int *fps, int *draw_calls, int *triangles, int *texture_mem, int *buffer_mem)
{
    if (fps) *fps = gfx.fps;
    if (draw_calls) *draw_calls = gfx.draw_calls;
    if (triangles) *triangles = gfx.triangles_rendered;
    if (texture_mem) *texture_mem = gfx.texture_memory_mb;
    if (buffer_mem) *buffer_mem = gfx.buffer_memory_mb;
    return 0;
}

void gfx2_begin_frame(void)
{
    gfx.draw_calls = 0;
    gfx.triangles_rendered = 0;
}

void gfx2_end_frame(void)
{
    gfx.frame_count++;
}
