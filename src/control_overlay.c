#include "control_overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>
#include "canvas.h"
#include "control_center_layout.h"
#include "control_center_paint.h"
#include "font.h"
#include "statusbar.h"

struct control_overlay {
    int w, h;
    GLuint program, bar_tex, panel_tex, scrim_tex;
    struct canvas bar, panel;
    unsigned char *rgba;
    struct cc_fonts fonts;
    char font_path[512], bar_shown[128];
    unsigned uploads;
    int64_t painted_at_ms;
    int prepared;
    struct cc_model painted;
};

static GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

static GLuint texture(int w, int h, const void *pixels) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return t;
}

static struct font *open_font(const char *dir, const char *face, int size) {
    char path[512];
    int n = snprintf(path, sizeof path, "%s/Inter-%s.ttf", dir, face);
    struct font *f = n >= 0 && n < (int)sizeof path ? font_open(path, size) : NULL;
    printf("glcube text: %s %d: %s\n", face, size, f ? "Inter" : "bitmap fallback");
    if (f) {
        /* Warm every supported character so a slider never rasterises digits. */
        char text[512]; int used = 0;
        for (int cp = 32; cp < 256; cp++) {
            if (cp < 128) text[used++] = (char)cp;
            else { text[used++] = (char)(0xc0 | (cp >> 6)); text[used++] = (char)(0x80 | (cp & 63)); }
        }
        memcpy(text + used, "\xe2\x80\xa6\xef\xbf\xbd", 7);
        uint32_t pixel = 0;
        struct canvas scratch = { .px = &pixel, .w = 1, .h = 1 };
        font_draw(f, &scratch, 0, 0, text, 0xffffffffu);
    }
    return f;
}

struct control_overlay *control_overlay_new(int w, int h, const char *fonts) {
    struct control_overlay *o = calloc(1, sizeof *o);
    if (!o) return NULL;
    o->w = w; o->h = h;
    o->bar = (struct canvas){ .px = calloc((size_t)w * statusbar_height(w), 4), .w = w, .h = statusbar_height(w) };
    o->panel = (struct canvas){ .px = calloc((size_t)CC_PANEL.w * CC_PANEL.h, 4), .w = CC_PANEL.w, .h = CC_PANEL.h };
    size_t count = (size_t)o->panel.w * o->panel.h;
    if ((size_t)o->bar.w * o->bar.h > count) count = (size_t)o->bar.w * o->bar.h;
    o->rgba = malloc(count * 4);
    if (!o->bar.px || !o->panel.px || !o->rgba) { control_overlay_free(o); return NULL; }
    GLuint vs = shader(GL_VERTEX_SHADER,
        "attribute vec2 pos; attribute vec2 uv; varying vec2 v_uv;"
        "void main(){v_uv=uv; gl_Position=vec4(pos,0.0,1.0);}");
    GLuint fs = shader(GL_FRAGMENT_SHADER,
        "precision mediump float; varying vec2 v_uv; uniform sampler2D tex;"
        "void main(){gl_FragColor=texture2D(tex,v_uv);}");
    o->program = glCreateProgram();
    glAttachShader(o->program, vs); glAttachShader(o->program, fs);
    glBindAttribLocation(o->program, 0, "pos"); glBindAttribLocation(o->program, 1, "uv");
    glLinkProgram(o->program);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint linked;
    glGetProgramiv(o->program, GL_LINK_STATUS, &linked);
    if (!linked) { control_overlay_free(o); return NULL; }
    GLint program, binding;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glUseProgram(o->program);
    glUniform1i(glGetUniformLocation(o->program, "tex"), 0);
    o->bar_tex = texture(o->bar.w, o->bar.h, o->bar.px);
    o->panel_tex = texture(o->panel.w, o->panel.h, NULL);
    const unsigned char scrim[] = {0, 0, 0, 0x59};
    o->scrim_tex = texture(1, 1, scrim);
    glBindTexture(GL_TEXTURE_2D, (GLuint)binding);
    glUseProgram((GLuint)program);
    snprintf(o->font_path, sizeof o->font_path, "%s/Inter-SemiBold.ttf", fonts);
    o->fonts.title = open_font(fonts, "SemiBold", 22);
    o->fonts.value = open_font(fonts, "SemiBold", 28);
    o->fonts.big = open_font(fonts, "SemiBold", 56);
    o->fonts.label = open_font(fonts, "Regular", 20);
    o->fonts.footer = open_font(fonts, "Regular", 16);
    return o;
}

static void upload_region(struct control_overlay *o, GLuint tex, const struct canvas *c,
                           int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        uint32_t p = c->px[(y + j) * c->w + x + i];
        size_t at = ((size_t)j * w + i) * 4;
        o->rgba[at] = p >> 16; o->rgba[at+1] = p >> 8;
        o->rgba[at+2] = p; o->rgba[at+3] = p >> 24;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, o->rgba);
}
static void upload(struct control_overlay *o, GLuint tex, const struct canvas *c) {
    upload_region(o, tex, c, 0, 0, c->w, c->h);
}

static void quad(struct control_overlay *o, GLuint tex, float x, float y, int w, int h, int flipped) {
    float l = 2.f*x/o->w-1.f, r = 2.f*(x+w)/o->w-1.f;
    float t = 1.f-2.f*y/o->h, b = 1.f-2.f*(y+h)/o->h;
    GLfloat v[] = {l,t,0,0, r,t,1,0, l,b,0,1, r,t,1,0, r,b,1,1, l,b,0,1};
    if (flipped) for (int i = 0; i < 24; i++) if (i%4 < 2) v[i] = -v[i];
    glBindTexture(GL_TEXTURE_2D, tex);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), v);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), v+2);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void control_overlay_draw(struct control_overlay *o, struct cc_model *m,
                          const struct status *st, int flipped, int64_t now, int defer_updates) {
    GLint program, buffer, active, binding, src_rgb, dst_rgb, src_alpha, dst_alpha;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active); glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb); glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
    GLint enabled[3], sizes[3], types[3], strides[3], normalized[3], buffers[3];
    void *pointers[3];
    for (int i = 0; i < 3; i++) {
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled[i]);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &sizes[i]);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &types[i]);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &strides[i]);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &normalized[i]);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buffers[i]);
        glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointers[i]);
    }
    glUseProgram(o->program); glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* Prepare the first texture before the initial KMS frame. Later openings
       only move the quad when its contents have not changed. */
    if (!o->prepared) {
        cc_paint(&o->panel, m, &o->fonts); upload(o, o->panel_tex, &o->panel);
        o->painted = *m; o->prepared = 1; o->uploads++; m->dirty = 0;
    }
    if (cc_visible(m)) {
        if (!defer_updates && m->dirty && (!m->dragging_brightness || now - o->painted_at_ms >= 33)) {
            struct cc_rect dirty;
            if (cc_paint_update(&o->panel, m, &o->painted, &o->fonts, &dirty)) {
                upload_region(o, o->panel_tex, &o->panel, dirty.x - CC_PANEL.x,
                              dirty.y - CC_PANEL.y, dirty.w, dirty.h);
                o->uploads++;
            }
            o->painted = *m; m->dirty = 0; o->painted_at_ms = now;
        }
        quad(o, o->scrim_tex, 0, 0, o->w, o->h, flipped);
        quad(o, o->panel_tex, CC_PANEL.x, CC_PANEL.y-cc_slide_offset(m), CC_PANEL.w, CC_PANEL.h, flipped);
    }
    char key[128];
    snprintf(key, sizeof key, "%d|%d|%d|%d", st->have_wifi, status_wifi_bars(st), st->cap, st->plugged);
    if (strcmp(key, o->bar_shown) && (!defer_updates || !o->bar_shown[0])) {
        const struct statusbar_style style = {0, 0, 0xffffffffu, 0x66ffffffu, 0xffffffffu, 0xFF34C759u, 0xFFFF3B30u, o->font_path};
        memset(o->bar.px, 0, (size_t)o->bar.w*o->bar.h*4);
        statusbar_paint(&o->bar, st, &style); upload(o, o->bar_tex, &o->bar);
        snprintf(o->bar_shown, sizeof o->bar_shown, "%s", key);
    }
    quad(o, o->bar_tex, 0, 0, o->bar.w, o->bar.h, flipped);
    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buffers[i]);
        glVertexAttribPointer(i, sizes[i], (GLenum)types[i], (GLboolean)normalized[i], strides[i], pointers[i]);
        if (enabled[i]) glEnableVertexAttribArray(i); else glDisableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buffer); glUseProgram((GLuint)program);
    glBindTexture(GL_TEXTURE_2D, (GLuint)binding); glActiveTexture((GLenum)active);
    glBlendFuncSeparate((GLenum)src_rgb, (GLenum)dst_rgb, (GLenum)src_alpha, (GLenum)dst_alpha);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (!blend) glDisable(GL_BLEND);
}
unsigned control_overlay_take_uploads(struct control_overlay *o) {
    unsigned n = o->uploads; o->uploads = 0; return n;
}
void control_overlay_free(struct control_overlay *o) {
    if (!o) return;
    font_close(o->fonts.title); font_close(o->fonts.value); font_close(o->fonts.big);
    font_close(o->fonts.label); font_close(o->fonts.footer);
    glDeleteTextures(1, &o->bar_tex); glDeleteTextures(1, &o->panel_tex); glDeleteTextures(1, &o->scrim_tex);
    glDeleteProgram(o->program);
    free(o->bar.px); free(o->panel.px); free(o->rgba); free(o);
}
