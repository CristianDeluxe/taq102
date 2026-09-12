#include "desk_present_drm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct buffer {
    uint32_t handle;
    uint32_t fb;
    uint32_t pitch;
    uint64_t size;
    uint8_t *map;
};

struct present {
    int fd;
    uint32_t crtc_id;
    uint32_t connector_id;
    drmModeModeInfo mode;
    struct buffer buffer[2];
    int front;              // the one on screen
    int flip_pending;
};

static int make_buffer(int fd, int w, int h, struct buffer *out) {
    struct drm_mode_create_dumb create = { .width = w, .height = h, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("create dumb");
        return -1;
    }
    out->handle = create.handle;
    out->pitch = create.pitch;
    out->size = create.size;
    if (drmModeAddFB(fd, w, h, 24, 32, create.pitch, create.handle, &out->fb)) {
        perror("addfb");
        return -1;
    }
    struct drm_mode_map_dumb map = { .handle = create.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        perror("map dumb");
        return -1;
    }
    out->map = mmap(0, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    map.offset);
    if (out->map == MAP_FAILED) {
        perror("mmap");
        out->map = NULL;
        return -1;
    }
    memset(out->map, 0, create.size);
    return 0;
}

struct present *present_open(const char *card) {
    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(card);
        return NULL;
    }
    if (drmSetMaster(fd) != 0) {
        fprintf(stderr, "drmSetMaster: %s (is glcube still running?)\n",
                strerror(errno));
        close(fd);
        return NULL;
    }
    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        perror("drmModeGetResources");
        close(fd);
        return NULL;
    }
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        if (c)
            drmModeFreeConnector(c);
    }
    if (!conn) {
        fprintf(stderr, "no connected connector\n");
        drmModeFreeResources(res);
        close(fd);
        return NULL;
    }

    struct present *p = calloc(1, sizeof *p);
    if (!p) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        close(fd);
        return NULL;
    }
    p->fd = fd;
    p->mode = conn->modes[0];
    p->connector_id = conn->connector_id;
    drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
    p->crtc_id = enc ? enc->crtc_id : res->crtcs[0];
    if (enc)
        drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    int w = p->mode.hdisplay, h = p->mode.vdisplay;
    if (make_buffer(fd, w, h, &p->buffer[0]) != 0 ||
        make_buffer(fd, w, h, &p->buffer[1]) != 0) {
        present_close(p);
        return NULL;
    }
    if (drmModeSetCrtc(fd, p->crtc_id, p->buffer[0].fb, 0, 0, &p->connector_id,
                       1, &p->mode)) {
        perror("setcrtc");
        present_close(p);
        return NULL;
    }
    p->front = 0;
    return p;
}

int present_width(const struct present *p) { return p ? p->mode.hdisplay : 0; }
int present_height(const struct present *p) { return p ? p->mode.vdisplay : 0; }

static void on_flip(int fd, unsigned int frame, unsigned int sec,
                    unsigned int usec, void *data) {
    (void)fd; (void)frame; (void)sec; (void)usec;
    int *pending = data;
    *pending = 0;
}

struct damage { int x, y, w, h; int any; };

static struct damage union_of(struct damage a, struct damage b) {
    if (!a.any) return b;
    if (!b.any) return a;
    int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    struct damage u = { x0, y0, x1 - x0, y1 - y0, 1 };
    return u;
}

// Each buffer remembers the damage it missed while the other was on screen.
static struct damage pending[2];

int present_frame_damage(struct present *p, const struct canvas *canvas,
                         int x, int y, int w, int h) {
    if (!p || !canvas || !canvas->px)
        return -1;
    int back = p->front ^ 1;
    struct buffer *b = &p->buffer[back];
    int W = p->mode.hdisplay, H = p->mode.vdisplay;
    int cols = canvas->w < W ? canvas->w : W;
    int rows = canvas->h < H ? canvas->h : H;
    struct damage now = { x, y, w, h, 1 };
    if (w < 0 || h < 0) {
        now.x = 0; now.y = 0; now.w = cols; now.h = rows;
    }
    struct damage copy = union_of(now, pending[back]);
    int x0 = copy.x < 0 ? 0 : copy.x, y0 = copy.y < 0 ? 0 : copy.y;
    int x1 = copy.x + copy.w > cols ? cols : copy.x + copy.w;
    int y1 = copy.y + copy.h > rows ? rows : copy.y + copy.h;
    for (int j = y0; j < y1; j++) {
        memcpy(b->map + (size_t)j * b->pitch + (size_t)x0 * 4,
               canvas->px + (size_t)j * canvas->w + x0, (size_t)(x1 - x0) * 4);
    }
    pending[back].any = 0;
    pending[p->front] = union_of(pending[p->front], now);

    p->flip_pending = 1;
    if (drmModePageFlip(p->fd, p->crtc_id, b->fb, DRM_MODE_PAGE_FLIP_EVENT,
                        &p->flip_pending)) {
        perror("pageflip");
        p->flip_pending = 0;
        return -1;
    }
    drmEventContext ctx = {
        .version = 2,
        .page_flip_handler = on_flip,
    };
    while (p->flip_pending) {
        if (drmHandleEvent(p->fd, &ctx) != 0) {
            perror("drmHandleEvent");
            p->flip_pending = 0;
            return -1;
        }
    }
    p->front = back;
    return 0;
}

int present_frame(struct present *p, const struct canvas *canvas) {
    return present_frame_damage(p, canvas, 0, 0, -1, -1);
}

void present_close(struct present *p) {
    if (!p)
        return;
    for (int i = 0; i < 2; i++) {
        struct buffer *b = &p->buffer[i];
        if (b->map)
            munmap(b->map, b->size);
        if (b->fb)
            drmModeRmFB(p->fd, b->fb);
        if (b->handle) {
            struct drm_mode_destroy_dumb destroy = { .handle = b->handle };
            drmIoctl(p->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
    }
    if (p->fd >= 0)
        close(p->fd);
    free(p);
}
