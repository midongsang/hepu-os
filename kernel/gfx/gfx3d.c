/* ============================================================================
 * 赫普OS - 3D图形渲染器实现
 * V8.64: 初始实现（含纹理映射）
 * 软件3D渲染管线
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/gfx3d.h"
#include "../include/vga.h"
#include "../include/kheap.h"

/* 外部2D绘图函数 */
extern void gfx_put_pixel(uint32_t x, uint32_t y, gfx_color_t color);
extern void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, gfx_color_t color);

/* 颜色工具 */
#define COLOR_R(c) ((c >> 16) & 0xFF)
#define COLOR_G(c) ((c >> 8) & 0xFF)
#define COLOR_B(c) (c & 0xFF)
#define MAKE_COLOR(r,g,b) (((r)<<16)|((g)<<8)|(b))

/* 全局3D上下文 */
static gfx3d_context_t ctx;
static int initialized = 0;

/* 当前活动纹理 */
static gfx3d_texture_t *active_texture = NULL;
static gfx3d_tex_filter_t tex_filter = GFX3D_TEX_FILTER_NEAREST;

/* 数学函数 */
static float sqrt_f(float x) {
    if (x <= 0) return 0;
    float r = x;
    for (int i = 0; i < 15; i++) r = (r + x/r) / 2;
    return r;
}
static float sin_f(float x) {
    while (x > 3.14159f) x -= 6.28318f;
    while (x < -3.14159f) x += 6.28318f;
    float x2=x*x, x3=x2*x, x5=x3*x2, x7=x5*x2;
    return x - x3/6 + x5/120 - x7/5040;
}
static float cos_f(float x) { return sin_f(x + 1.5708f); }

/* 初始化3D渲染器 */
int gfx3d_init(int width, int height)
{
    if (initialized) return 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.width = width; ctx.height = height; ctx.initialized = 1;
    ctx.model_matrix = mat4_identity();
    ctx.view_matrix = mat4_identity();
    ctx.projection_matrix = mat4_identity();
    ctx.mvp_matrix = mat4_identity();
    ctx.viewport_x = 0; ctx.viewport_y = 0;
    ctx.viewport_width = width; ctx.viewport_height = height;
    ctx.depth_buffer_size = width * height;
    ctx.depth_buffer = (float *)kmalloc(ctx.depth_buffer_size * sizeof(float));
    if (!ctx.depth_buffer) return -1;
    memset(ctx.depth_buffer, 0, ctx.depth_buffer_size * sizeof(float));
    ctx.depth_test_enabled = 1; ctx.depth_write_enabled = 1;
    ctx.cull_face_enabled = 1; ctx.wireframe_mode = 0; ctx.lighting_enabled = 1;
    ctx.light_direction = vec3_normalize(vec3_create(0.5f, 1.0f, 0.5f));
    ctx.light_color = vec3_create(1,1,1);
    ctx.ambient_color = vec3_create(0.3f,0.3f,0.3f);
    initialized = 1;
    vga_printf("[GFX3D] Initialized %dx%d\n", width, height);
    return 0;
}

void gfx3d_shutdown(void) {
    if (!initialized) return;
    if (ctx.depth_buffer) { kfree(ctx.depth_buffer); ctx.depth_buffer = NULL; }
    initialized = 0;
}

gfx3d_context_t *gfx3d_get_context(void) { return &ctx; }
void gfx3d_clear(gfx_color_t color) { gfx_fill_rect(0,0,ctx.width,ctx.height,color); }
void gfx3d_clear_depth(float depth) {
    if (!ctx.depth_buffer) return;
    for (int i=0;i<ctx.depth_buffer_size;i++) ctx.depth_buffer[i]=depth;
}

void gfx3d_set_model_matrix(mat4_t m) { ctx.model_matrix=m; gfx3d_update_mvp(); }
void gfx3d_set_view_matrix(mat4_t m) { ctx.view_matrix=m; gfx3d_update_mvp(); }
void gfx3d_set_projection_matrix(mat4_t m) { ctx.projection_matrix=m; gfx3d_update_mvp(); }
void gfx3d_update_mvp(void) {
    mat4_t mv = mat4_multiply(ctx.view_matrix, ctx.model_matrix);
    ctx.mvp_matrix = mat4_multiply(ctx.projection_matrix, mv);
}
mat4_t gfx3d_get_mvp(void) { return ctx.mvp_matrix; }
void gfx3d_set_viewport(int x,int y,int w,int h) { ctx.viewport_x=x;ctx.viewport_y=y;ctx.viewport_width=w;ctx.viewport_height=h; }
void gfx3d_enable_depth_test(int e) { ctx.depth_test_enabled=e; }
void gfx3d_enable_depth_write(int e) { ctx.depth_write_enabled=e; }
void gfx3d_enable_cull_face(int e) { ctx.cull_face_enabled=e; }
void gfx3d_set_wireframe(int e) { ctx.wireframe_mode=e; }
void gfx3d_enable_lighting(int e) { ctx.lighting_enabled=e; }
void gfx3d_set_light_direction(vec3_t d) { ctx.light_direction=vec3_normalize(d); }
void gfx3d_set_light_color(vec3_t c) { ctx.light_color=c; }
void gfx3d_set_ambient_color(vec3_t c) { ctx.ambient_color=c; }

/* 顶点变换 */
static void transform_vertex(const vertex_t *in, vertex_t *out) {
    vec4_t pos = vec4_from_vec3(in->position,1.0f);
    pos = mat4_mul_vec4(ctx.mvp_matrix, pos);
    out->position = vec4_to_vec3(pos);
    out->normal = vec3_normalize(mat4_mul_vec3(ctx.model_matrix, in->normal));
    out->color = in->color;
    out->texcoord = in->texcoord;
}

static void project_vertex(const vertex_t *in, vertex_t *out, int *sx, int *sy) {
    vec4_t pos = vec4_from_vec3(in->position,1.0f);
    if (pos.w > 0.001f) {
        out->position.x = pos.x/pos.w; out->position.y = pos.y/pos.w; out->position.z = pos.z/pos.w;
    } else out->position = in->position;
    *sx = ctx.viewport_x + (int)((out->position.x*0.5f+0.5f)*ctx.viewport_width);
    *sy = ctx.viewport_y + (int)((1.0f-(out->position.y*0.5f+0.5f))*ctx.viewport_height);
}

static gfx_color_t calculate_lighting(const vertex_t *v) {
    vec3_t result;
    if (!ctx.lighting_enabled) {
        result = v->color;
    } else {
        float diffuse = vec3_dot(v->normal, ctx.light_direction);
        if (diffuse<0) diffuse=0;
        result.x = ctx.ambient_color.x + ctx.light_color.x*diffuse;
        result.y = ctx.ambient_color.y + ctx.light_color.y*diffuse;
        result.z = ctx.ambient_color.z + ctx.light_color.z*diffuse;
        result.x *= v->color.x; result.y *= v->color.y; result.z *= v->color.z;
        result.x = gfx3d_clamp(result.x,0,1); result.y=gfx3d_clamp(result.y,0,1); result.z=gfx3d_clamp(result.z,0,1);
    }
    return MAKE_COLOR((uint8_t)(result.x*255),(uint8_t)(result.y*255),(uint8_t)(result.z*255));
}

/* 纹理采样 */
gfx_color_t gfx3d_texture_sample(float u, float v) {
    if (!active_texture || !active_texture->pixels || !active_texture->initialized) return 0xFF00FF;
    u = u - (int)u; if (u<0) u+=1;
    v = v - (int)v; if (v<0) v+=1;
    int x = (int)(u * (active_texture->width-1));
    int y = (int)(v * (active_texture->height-1));
    if (x<0)x=0; if(x>=active_texture->width)x=active_texture->width-1;
    if (y<0)y=0; if(y>=active_texture->height)y=active_texture->height-1;
    if (tex_filter == GFX3D_TEX_FILTER_LINEAR) {
        int x2=x+1, y2=y+1;
        if(x2>=active_texture->width)x2=x;
        if(y2>=active_texture->height)y2=y;
        float fx=u*(active_texture->width-1)-x, fy=v*(active_texture->height-1)-y;
        uint32_t c00=active_texture->pixels[y*active_texture->width+x];
        uint32_t c10=active_texture->pixels[y*active_texture->width+x2];
        uint32_t c01=active_texture->pixels[y2*active_texture->width+x];
        uint32_t c11=active_texture->pixels[y2*active_texture->width+x2];
        uint8_t r=(uint8_t)(COLOR_R(c00)*(1-fx)*(1-fy)+COLOR_R(c10)*fx*(1-fy)+COLOR_R(c01)*(1-fx)*fy+COLOR_R(c11)*fx*fy);
        uint8_t g=(uint8_t)(COLOR_G(c00)*(1-fx)*(1-fy)+COLOR_G(c10)*fx*(1-fy)+COLOR_G(c01)*(1-fx)*fy+COLOR_G(c11)*fx*fy);
        uint8_t b=(uint8_t)(COLOR_B(c00)*(1-fx)*(1-fy)+COLOR_B(c10)*fx*(1-fy)+COLOR_B(c01)*(1-fx)*fy+COLOR_B(c11)*fx*fy);
        return MAKE_COLOR(r,g,b);
    }
    return active_texture->pixels[y*active_texture->width+x];
}

static void draw_pixel(int x,int y,float depth,gfx_color_t color) {
    if(x<0||x>=ctx.width||y<0||y>=ctx.height) return;
    int idx=y*ctx.width+x;
    if(ctx.depth_test_enabled&&ctx.depth_buffer) if(depth>ctx.depth_buffer[idx]) return;
    if(ctx.depth_write_enabled&&ctx.depth_buffer) ctx.depth_buffer[idx]=depth;
    gfx_put_pixel(x,y,color); ctx.pixels_drawn++;
}

static void draw_line(int x0,int y0,int x1,int y1,gfx_color_t c) {
    int dx=(x1>x0)?(x1-x0):(x0-x1), dy=(y1>y0)?(y1-y0):(y0-y1);
    int sx=(x0<x1)?1:-1, sy=(y0<y1)?1:-1, err=dx-dy;
    while(1){ draw_pixel(x0,y0,0.5f,c); if(x0==x1&&y0==y1)break; int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;} }
}

/* 绘制三角形（带纹理） */
static void draw_triangle_tex(const vertex_t *v0,const vertex_t *v1,const vertex_t *v2) {
    int x0,y0,x1,y1,x2,y2;
    vertex_t tv0,tv1,tv2;
    transform_vertex(v0,&tv0); transform_vertex(v1,&tv1); transform_vertex(v2,&tv2);
    project_vertex(&tv0,&tv0,&x0,&y0); project_vertex(&tv1,&tv1,&x1,&y1); project_vertex(&tv2,&tv2,&x2,&y2);
    if(ctx.cull_face_enabled) {
        float cross=(float)(x1-x0)*(y2-y0)-(float)(y1-y0)*(x2-x0);
        if(cross<0) return;
    }
    if(ctx.wireframe_mode) {
        gfx_color_t c=calculate_lighting(&tv0);
        draw_line(x0,y0,x1,y1,c); draw_line(x1,y1,x2,y2,c); draw_line(x2,y2,x0,y0,c);
        ctx.triangles_drawn++; return;
    }
    /* 按y排序 */
    if(y0>y1){int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;vertex_t vt=tv0;tv0=tv1;tv1=vt;}
    if(y1>y2){int t=x1;x1=x2;x2=t;t=y1;y1=y2;y2=t;vertex_t vt=tv1;tv1=tv2;tv2=vt;}
    if(y0>y1){int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;vertex_t vt=tv0;tv0=tv1;tv1=vt;}
    int total_h=y2-y0;
    for(int y=y0;y<=y2;y++) {
        int second=(y>y1)||(y1==y0);
        int seg_h=second?(y2-y1):(y1-y0);
        if(seg_h==0) continue;
        float alpha=(float)(y-y0)/total_h;
        float beta=(float)(y-(second?y1:y0))/seg_h;
        int ax=x0+(int)((x2-x0)*alpha);
        int bx=second?(x1+(int)((x2-x1)*beta)):(x0+(int)((x1-x0)*beta));
        if(ax>bx){int t=ax;ax=bx;bx=t;}
        /* 纹理坐标插值 */
        float u0=tv0.texcoord.x+(tv2.texcoord.x-tv0.texcoord.x)*alpha;
        float v0t=tv0.texcoord.y+(tv2.texcoord.y-tv0.texcoord.y)*alpha;
        for(int x=ax;x<=bx;x++) {
            gfx_color_t color;
            if(active_texture&&active_texture->initialized) {
                color=gfx3d_texture_sample(u0,v0t);
            } else {
                color=calculate_lighting(&tv0);
            }
            float depth=tv0.position.z+(tv2.position.z-tv0.position.z)*alpha;
            draw_pixel(x,y,depth,color);
        }
    }
    ctx.triangles_drawn++;
}

void gfx3d_draw_arrays(gfx3d_primitive_t mode,const vertex_t *vertices,int count) {
    if(!initialized||!vertices||count<1) return;
    ctx.vertices_processed+=count;
    switch(mode) {
        case GFX3D_TRIANGLES:
            for(int i=0;i+2<count;i+=3) draw_triangle_tex(&vertices[i],&vertices[i+1],&vertices[i+2]);
            break;
        case GFX3D_TRIANGLE_STRIP:
            for(int i=0;i+2<count;i++) {
                if(i%2==0) draw_triangle_tex(&vertices[i],&vertices[i+1],&vertices[i+2]);
                else draw_triangle_tex(&vertices[i+1],&vertices[i],&vertices[i+2]);
            }
            break;
        case GFX3D_TRIANGLE_FAN:
            for(int i=1;i+1<count;i++) draw_triangle_tex(&vertices[0],&vertices[i],&vertices[i+1]);
            break;
        default: break;
    }
}

void gfx3d_draw_elements(gfx3d_primitive_t mode,const vertex_t *vertices,const uint16_t *indices,int count) {
    if(!initialized||!vertices||!indices||count<1) return;
    vertex_t *exp=(vertex_t*)kmalloc(count*sizeof(vertex_t));
    if(!exp) return;
    for(int i=0;i<count;i++) exp[i]=vertices[indices[i]];
    gfx3d_draw_arrays(mode,exp,count);
    kfree(exp);
}

/* 纹理管理 */
int gfx3d_texture_create(gfx3d_texture_t *tex,int width,int height) {
    if(!tex||width<=0||height<=0) return -1;
    tex->width=width; tex->height=height;
    tex->pixels=(uint32_t*)kmalloc(width*height*sizeof(uint32_t));
    if(!tex->pixels) return -1;
    memset(tex->pixels,0,width*height*sizeof(uint32_t));
    tex->initialized=1;
    return 0;
}

void gfx3d_texture_destroy(gfx3d_texture_t *tex) {
    if(!tex) return;
    if(tex->pixels) { kfree(tex->pixels); tex->pixels=NULL; }
    tex->initialized=0;
}

int gfx3d_texture_load(gfx3d_texture_t *tex,const gfx_color_t *pixels,int width,int height) {
    if(!tex||!pixels||!tex->initialized) return -1;
    if(width!=tex->width||height!=tex->height) return -1;
    memcpy(tex->pixels,pixels,width*height*sizeof(uint32_t));
    return 0;
}

void gfx3d_texture_set_active(gfx3d_texture_t *tex) { active_texture=tex; }
void gfx3d_texture_set_filter(gfx3d_tex_filter_t filter) { tex_filter=filter; }

int gfx3d_texture_generate_checkerboard(gfx3d_texture_t *tex,int size,int squares,gfx_color_t c1,gfx_color_t c2) {
    if(!tex||size<=0||squares<=0) return -1;
    if(gfx3d_texture_create(tex,size,size)!=0) return -1;
    int sq=size/squares;
    for(int y=0;y<size;y++) {
        for(int x=0;x<size;x++) {
            int idx=y*size+x;
            if(((x/sq)+(y/sq))%2==0) tex->pixels[idx]=c1;
            else tex->pixels[idx]=c2;
        }
    }
    return 0;
}

/* 带纹理的立方体 */
void gfx3d_draw_textured_cube(float size,gfx3d_texture_t *tex) {
    float s=size/2.0f;
    vec3_t col={1,1,1};
    vertex_t verts[24];
    int idx=0;
    /* 前面 */
    verts[idx++]=(vertex_t){{-s,-s,s},{0,0,1},col,{0,0}};
    verts[idx++]=(vertex_t){{s,-s,s},{0,0,1},col,{1,0}};
    verts[idx++]=(vertex_t){{s,s,s},{0,0,1},col,{1,1}};
    verts[idx++]=(vertex_t){{-s,s,s},{0,0,1},col,{0,1}};
    /* 后面 */
    verts[idx++]=(vertex_t){{s,-s,-s},{0,0,-1},col,{0,0}};
    verts[idx++]=(vertex_t){{-s,-s,-s},{0,0,-1},col,{1,0}};
    verts[idx++]=(vertex_t){{-s,s,-s},{0,0,-1},col,{1,1}};
    verts[idx++]=(vertex_t){{s,s,-s},{0,0,-1},col,{0,1}};
    /* 左面 */
    verts[idx++]=(vertex_t){{-s,-s,-s},{-1,0,0},col,{0,0}};
    verts[idx++]=(vertex_t){{-s,-s,s},{-1,0,0},col,{1,0}};
    verts[idx++]=(vertex_t){{-s,s,s},{-1,0,0},col,{1,1}};
    verts[idx++]=(vertex_t){{-s,s,-s},{-1,0,0},col,{0,1}};
    /* 右面 */
    verts[idx++]=(vertex_t){{s,-s,s},{1,0,0},col,{0,0}};
    verts[idx++]=(vertex_t){{s,-s,-s},{1,0,0},col,{1,0}};
    verts[idx++]=(vertex_t){{s,s,-s},{1,0,0},col,{1,1}};
    verts[idx++]=(vertex_t){{s,s,s},{1,0,0},col,{0,1}};
    /* 上面 */
    verts[idx++]=(vertex_t){{-s,s,s},{0,1,0},col,{0,0}};
    verts[idx++]=(vertex_t){{s,s,s},{0,1,0},col,{1,0}};
    verts[idx++]=(vertex_t){{s,s,-s},{0,1,0},col,{1,1}};
    verts[idx++]=(vertex_t){{-s,s,-s},{0,1,0},col,{0,1}};
    /* 下面 */
    verts[idx++]=(vertex_t){{-s,-s,-s},{0,-1,0},col,{0,0}};
    verts[idx++]=(vertex_t){{s,-s,-s},{0,-1,0},col,{1,0}};
    verts[idx++]=(vertex_t){{s,-s,s},{0,-1,0},col,{1,1}};
    verts[idx++]=(vertex_t){{-s,-s,s},{0,-1,0},col,{0,1}};

    uint16_t indices[36];
    idx=0;
    for(int f=0;f<6;f++) {
        int base=f*4;
        indices[idx++]=base; indices[idx++]=base+1; indices[idx++]=base+2;
        indices[idx++]=base; indices[idx++]=base+2; indices[idx++]=base+3;
    }

    gfx3d_texture_set_active(tex);
    gfx3d_draw_elements(GFX3D_TRIANGLES,verts,indices,36);
    gfx3d_texture_set_active(NULL);
}

void gfx3d_draw_cube(float size,gfx_color_t color) {
    float s=size/2.0f;
    vec3_t col={COLOR_R(color)/255.0f,COLOR_G(color)/255.0f,COLOR_B(color)/255.0f};
    vec3_t pos[8]={{-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},{-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s}};
    uint16_t indices[36]={0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,2,6,7,2,7,3,3,7,4,3,4,0,1,5,6,1,6,2};
    vertex_t verts[8];
    for(int i=0;i<8;i++){verts[i].position=pos[i];verts[i].normal=vec3_normalize(pos[i]);verts[i].color=col;verts[i].texcoord.x=0;verts[i].texcoord.y=0;}
    gfx3d_draw_elements(GFX3D_TRIANGLES,verts,indices,36);
}

void gfx3d_draw_sphere(float radius,int segments,int rings,gfx_color_t color) {
    if(segments<3)segments=3; if(rings<2)rings=2;
    vec3_t col={COLOR_R(color)/255.0f,COLOR_G(color)/255.0f,COLOR_B(color)/255.0f};
    int vc=(rings+1)*(segments+1);
    vertex_t *verts=(vertex_t*)kmalloc(vc*sizeof(vertex_t));
    if(!verts) return;
    int idx=0;
    for(int r=0;r<=rings;r++) {
        float phi=3.14159f*r/rings;
        for(int s=0;s<=segments;s++) {
            float theta=2*3.14159f*s/segments;
            float x=radius*sin_f(phi)*cos_f(theta), y=radius*cos_f(phi), z=radius*sin_f(phi)*sin_f(theta);
            verts[idx].position=(vec3_t){x,y,z};
            verts[idx].normal=vec3_normalize((vec3_t){x,y,z});
            verts[idx].color=col; verts[idx].texcoord.x=(float)s/segments; verts[idx].texcoord.y=(float)r/rings;
            idx++;
        }
    }
    int ic=rings*segments*6;
    uint16_t *indices=(uint16_t*)kmalloc(ic*sizeof(uint16_t));
    if(!indices){kfree(verts);return;}
    idx=0;
    for(int r=0;r<rings;r++) for(int s=0;s<segments;s++) {
        int a=r*(segments+1)+s,b=a+segments+1;
        indices[idx++]=a;indices[idx++]=b;indices[idx++]=a+1;
        indices[idx++]=b;indices[idx++]=b+1;indices[idx++]=a+1;
    }
    gfx3d_draw_elements(GFX3D_TRIANGLES,verts,indices,ic);
    kfree(verts);kfree(indices);
}

void gfx3d_draw_cylinder(float radius,float height,int segments,gfx_color_t color) {
    if(segments<3)segments=3;
    vec3_t col={COLOR_R(color)/255.0f,COLOR_G(color)/255.0f,COLOR_B(color)/255.0f};
    int vc=(segments+1)*2;
    vertex_t *verts=(vertex_t*)kmalloc(vc*sizeof(vertex_t));
    if(!verts) return;
    int idx=0;
    for(int i=0;i<=segments;i++) {
        float a=2*3.14159f*i/segments;
        float x=radius*cos_f(a),z=radius*sin_f(a);
        verts[idx].position=(vec3_t){x,-height/2,z};verts[idx].normal=vec3_normalize((vec3_t){x,0,z});verts[idx].color=col;idx++;
        verts[idx].position=(vec3_t){x,height/2,z};verts[idx].normal=vec3_normalize((vec3_t){x,0,z});verts[idx].color=col;idx++;
    }
    int ic=segments*6;
    uint16_t *indices=(uint16_t*)kmalloc(ic*sizeof(uint16_t));
    if(!indices){kfree(verts);return;}
    idx=0;
    for(int i=0;i<segments;i++){int a=i*2;indices[idx++]=a;indices[idx++]=a+1;indices[idx++]=a+2;indices[idx++]=a+1;indices[idx++]=a+3;indices[idx++]=a+2;}
    gfx3d_draw_elements(GFX3D_TRIANGLES,verts,indices,ic);
    kfree(verts);kfree(indices);
}

void gfx3d_draw_plane(float width,float height,gfx_color_t color) {
    vec3_t col={COLOR_R(color)/255.0f,COLOR_G(color)/255.0f,COLOR_B(color)/255.0f};
    float w=width/2,h=height/2;
    vertex_t verts[4]={
        {{-w,0,-h},{0,1,0},col,{0,0}},{{w,0,-h},{0,1,0},col,{1,0}},
        {{w,0,h},{0,1,0},col,{1,1}},{{-w,0,h},{0,1,0},col,{0,1}}
    };
    uint16_t indices[6]={0,1,2,0,2,3};
    gfx3d_draw_elements(GFX3D_TRIANGLES,verts,indices,6);
}

void gfx3d_get_stats(uint32_t *t,uint32_t *p,uint32_t *v) {
    if(t)*t=ctx.triangles_drawn; if(p)*p=ctx.pixels_drawn; if(v)*v=ctx.vertices_processed;
}
void gfx3d_reset_stats(void) { ctx.triangles_drawn=0;ctx.pixels_drawn=0;ctx.vertices_processed=0; }

/* ============================================================================
 * V8.84: 透明混合 (Alpha Blending)
 * ========================================================================== */

void gfx3d_enable_blend(int enable) { ctx.blend_enabled = enable; }
void gfx3d_set_blend_func(gfx3d_blend_factor_t src, gfx3d_blend_factor_t dst) {
    ctx.blend_src_factor = src;
    ctx.blend_dst_factor = dst;
}

/* 计算混合因子 */
static float blend_factor(gfx3d_blend_factor_t factor, float src_a, float dst_a,
                          float src_r, float dst_r) {
    switch (factor) {
        case GFX3D_BLEND_ZERO: return 0.0f;
        case GFX3D_BLEND_ONE: return 1.0f;
        case GFX3D_BLEND_SRC_ALPHA: return src_a;
        case GFX3D_BLEND_ONE_MINUS_SRC_ALPHA: return 1.0f - src_a;
        case GFX3D_BLEND_DST_ALPHA: return dst_a;
        case GFX3D_BLEND_ONE_MINUS_DST_ALPHA: return 1.0f - dst_a;
        case GFX3D_BLEND_SRC_COLOR: return src_r;
        case GFX3D_BLEND_ONE_MINUS_SRC_COLOR: return 1.0f - src_r;
        default: return 1.0f;
    }
}

/* 混合颜色（带alpha） */
static gfx_color_t blend_color(gfx_color_t src, gfx_color_t dst, float alpha) {
    if (!ctx.blend_enabled) return src;

    float sr = COLOR_R(src) / 255.0f;
    float sg = COLOR_G(src) / 255.0f;
    float sb = COLOR_B(src) / 255.0f;
    float sa = alpha;

    float dr = COLOR_R(dst) / 255.0f;
    float dg = COLOR_G(dst) / 255.0f;
    float db = COLOR_B(dst) / 255.0f;
    float da = 1.0f;  /* 目标alpha假设为1 */

    float sf = blend_factor(ctx.blend_src_factor, sa, da, sr, dr);
    float df = blend_factor(ctx.blend_dst_factor, sa, da, sr, dr);

    int r = (int)((sr * sf + dr * df) * 255);
    int g = (int)((sg * sf + dg * df) * 255);
    int b = (int)((sb * sf + db * df) * 255);

    if (r > 255) r = 255; if (r < 0) r = 0;
    if (g > 255) g = 255; if (g < 0) g = 0;
    if (b > 255) b = 255; if (b < 0) b = 0;

    return MAKE_COLOR(r, g, b);
}

/* ============================================================================
 * V8.84: 新3D形状
 * ========================================================================== */

/* 圆锥 */
void gfx3d_draw_cone(float radius, float height, int segments, gfx_color_t color) {
    if (segments < 3) segments = 3;
    vec3_t col = {COLOR_R(color)/255.0f, COLOR_G(color)/255.0f, COLOR_B(color)/255.0f};

    /* 顶点：底面中心 + 底面圆周 + 顶点 */
    int vc = segments + 2;
    vertex_t *verts = (vertex_t *)kmalloc(vc * sizeof(vertex_t));
    if (!verts) return;

    int idx = 0;
    /* 顶点（顶部） */
    verts[idx].position = (vec3_t){0, height/2, 0};
    verts[idx].normal = (vec3_t){0, 1, 0};
    verts[idx].color = col;
    idx++;

    /* 底面中心 */
    verts[idx].position = (vec3_t){0, -height/2, 0};
    verts[idx].normal = (vec3_t){0, -1, 0};
    verts[idx].color = col;
    idx++;

    /* 底面圆周 */
    for (int i = 0; i < segments; i++) {
        float a = 2 * 3.14159f * i / segments;
        float x = radius * cos_f(a);
        float z = radius * sin_f(a);
        verts[idx].position = (vec3_t){x, -height/2, z};
        verts[idx].normal = vec3_normalize((vec3_t){x, height/2, z});
        verts[idx].color = col;
        idx++;
    }

    /* 索引：侧面三角形 + 底面扇形 */
    int ic = segments * 3 * 2;
    uint16_t *indices = (uint16_t *)kmalloc(ic * sizeof(uint16_t));
    if (!indices) { kfree(verts); return; }

    idx = 0;
    for (int i = 0; i < segments; i++) {
        int next = (i + 1) % segments;
        /* 侧面：顶点 -> 当前 -> 下一个 */
        indices[idx++] = 0;
        indices[idx++] = 2 + i;
        indices[idx++] = 2 + next;
        /* 底面：中心 -> 下一个 -> 当前 */
        indices[idx++] = 1;
        indices[idx++] = 2 + next;
        indices[idx++] = 2 + i;
    }

    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, idx);
    kfree(verts);
    kfree(indices);
}

/* 圆环（Torus） */
void gfx3d_draw_torus(float major_radius, float minor_radius,
                      int major_segments, int minor_segments, gfx_color_t color) {
    if (major_segments < 3) major_segments = 3;
    if (minor_segments < 3) minor_segments = 3;
    vec3_t col = {COLOR_R(color)/255.0f, COLOR_G(color)/255.0f, COLOR_B(color)/255.0f};

    int vc = (major_segments + 1) * (minor_segments + 1);
    vertex_t *verts = (vertex_t *)kmalloc(vc * sizeof(vertex_t));
    if (!verts) return;

    int idx = 0;
    for (int i = 0; i <= major_segments; i++) {
        float u = 2 * 3.14159f * i / major_segments;
        float cu = cos_f(u), su = sin_f(u);
        for (int j = 0; j <= minor_segments; j++) {
            float v = 2 * 3.14159f * j / minor_segments;
            float cv = cos_f(v), sv = sin_f(v);

            float x = (major_radius + minor_radius * cv) * cu;
            float y = minor_radius * sv;
            float z = (major_radius + minor_radius * cv) * su;

            verts[idx].position = (vec3_t){x, y, z};
            verts[idx].normal = vec3_normalize((vec3_t){cv*cu, sv, cv*su});
            verts[idx].color = col;
            idx++;
        }
    }

    int ic = major_segments * minor_segments * 6;
    uint16_t *indices = (uint16_t *)kmalloc(ic * sizeof(uint16_t));
    if (!indices) { kfree(verts); return; }

    idx = 0;
    for (int i = 0; i < major_segments; i++) {
        for (int j = 0; j < minor_segments; j++) {
            int a = i * (minor_segments + 1) + j;
            int b = a + 1;
            int c = a + (minor_segments + 1);
            int d = c + 1;
            indices[idx++] = a; indices[idx++] = b; indices[idx++] = c;
            indices[idx++] = b; indices[idx++] = d; indices[idx++] = c;
        }
    }

    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, idx);
    kfree(verts);
    kfree(indices);
}

/* 金字塔 */
void gfx3d_draw_pyramid(float base_size, float height, gfx_color_t color) {
    vec3_t col = {COLOR_R(color)/255.0f, COLOR_G(color)/255.0f, COLOR_B(color)/255.0f};
    float s = base_size / 2;

    vertex_t verts[5] = {
        /* 顶点 */
        {{0, height/2, 0}, {0, 1, 0}, col, {0, 0}},
        /* 底面四个角 */
        {{-s, -height/2, -s}, {0, -1, 0}, col, {0, 0}},
        {{s, -height/2, -s}, {0, -1, 0}, col, {1, 0}},
        {{s, -height/2, s}, {0, -1, 0}, col, {1, 1}},
        {{-s, -height/2, s}, {0, -1, 0}, col, {0, 1}}
    };

    uint16_t indices[18] = {
        /* 四个侧面 */
        0, 1, 2,
        0, 2, 3,
        0, 3, 4,
        0, 4, 1,
        /* 底面 */
        1, 4, 3,
        1, 3, 2
    };

    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, 18);
}

/* 圆盘（环形） */
void gfx3d_draw_disk(float inner_radius, float outer_radius, int segments, gfx_color_t color) {
    if (segments < 3) segments = 3;
    vec3_t col = {COLOR_R(color)/255.0f, COLOR_G(color)/255.0f, COLOR_B(color)/255.0f};

    int vc = (segments + 1) * 2;
    vertex_t *verts = (vertex_t *)kmalloc(vc * sizeof(vertex_t));
    if (!verts) return;

    int idx = 0;
    for (int i = 0; i <= segments; i++) {
        float a = 2 * 3.14159f * i / segments;
        float x = cos_f(a), z = sin_f(a);
        /* 内圈 */
        verts[idx].position = (vec3_t){inner_radius * x, 0, inner_radius * z};
        verts[idx].normal = (vec3_t){0, 1, 0};
        verts[idx].color = col;
        idx++;
        /* 外圈 */
        verts[idx].position = (vec3_t){outer_radius * x, 0, outer_radius * z};
        verts[idx].normal = (vec3_t){0, 1, 0};
        verts[idx].color = col;
        idx++;
    }

    int ic = segments * 6;
    uint16_t *indices = (uint16_t *)kmalloc(ic * sizeof(uint16_t));
    if (!indices) { kfree(verts); return; }

    idx = 0;
    for (int i = 0; i < segments; i++) {
        int a = i * 2;
        indices[idx++] = a;
        indices[idx++] = a + 1;
        indices[idx++] = a + 2;
        indices[idx++] = a + 1;
        indices[idx++] = a + 3;
        indices[idx++] = a + 2;
    }

    gfx3d_draw_elements(GFX3D_TRIANGLES, verts, indices, idx);
    kfree(verts);
    kfree(indices);
}
