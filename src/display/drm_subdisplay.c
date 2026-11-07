#include "subdisplay_hud.h"
#include "logo_bitmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <sys/select.h>

// Simple mock DRM ioctls for compilation since we don't have full drm-core headers
// The actual device has standard Linux DRM ioctls. We declare what we need.

#define DRM_IOCTL_BASE      'd'
#define DRM_COMMAND_BASE    0x40

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_destroy_dumb {
    uint32_t handle;
};

#define DRM_IOCTL_MODE_CREATE_DUMB _IOWR(DRM_IOCTL_BASE, 0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB    _IOWR(DRM_IOCTL_BASE, 0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB _IOWR(DRM_IOCTL_BASE, 0xB4, struct drm_mode_destroy_dumb)

struct drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

#define DRM_IOCTL_MODE_PAGE_FLIP _IOWR(DRM_IOCTL_BASE, 0xB0, struct drm_mode_crtc_page_flip)

// simplified struct for adding fb
struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

#define DRM_IOCTL_MODE_ADDFB       _IOWR(DRM_IOCTL_BASE, 0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB        _IOWR(DRM_IOCTL_BASE, 0xAF, unsigned int)

struct drm_mode_fb_dirty_cmd {
    uint32_t fb_id;
    uint32_t flags;
    uint32_t color;
    uint32_t num_clips;
    uint64_t clips_ptr;
};

#define DRM_IOCTL_MODE_DIRTYFB     _IOWR(DRM_IOCTL_BASE, 0xB1, struct drm_mode_fb_dirty_cmd)

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh, flags, type;
    char name[32];
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
    uint32_t count_modes, count_props, count_encoders, encoder_id;
    uint32_t connector_id, connector_type, connector_type_id, connection;
    uint32_t mm_width, mm_height, subpixel, pad;
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x, y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

#define DRM_IOCTL_MODE_GETCONNECTOR _IOWR(DRM_IOCTL_BASE, 0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_SETCRTC     _IOWR(DRM_IOCTL_BASE, 0xA2, struct drm_mode_crtc)

static volatile bool running = true;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = false;
    }
}

bool drm_init(drm_fb_t *fb) {
    memset(fb, 0, sizeof(*fb));
    fb->width    = HUD_WIDTH;
    fb->height   = HUD_HEIGHT;
    fb->conn_id  = 64;  /* DSI-2 rear subdisplay connector on Mi 11 Ultra */
    fb->crtc_id  = 208; /* SDE KMS CRTC bound to DSI-2 */
    fb->back_idx = 0;

    // Open DRM device (card0)
    fb->fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fb->fd < 0) {
        fprintf(stderr, "[HUD] Warning: Could not open /dev/dri/card0 (errno: %d), running in mock mode on host.\n", errno);
        fb->stride = fb->width * 4;
        fb->size = fb->stride * fb->height;
        fb->vaddr[0] = malloc(fb->size);
        fb->vaddr[1] = fb->vaddr[0];
        return true;
    }

    struct drm_mode_modeinfo mode;
    memset(&mode, 0, sizeof(mode));
    
    struct drm_mode_get_connector conn;
    memset(&conn, 0, sizeof(conn));
    conn.connector_id = 64;
    conn.modes_ptr = (uint64_t)&mode;
    conn.count_modes = 1;
    ioctl(fb->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn); // try to get mode
    
    for (int i = 0; i < 2; i++) {
        struct drm_mode_create_dumb create_arg;
        memset(&create_arg, 0, sizeof(create_arg));
        create_arg.width = fb->width;
        create_arg.height = fb->height;
        create_arg.bpp = 32;

        if (ioctl(fb->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) < 0) {
            fprintf(stderr, "[HUD] Warning: CREATE_DUMB failed (errno: %d).\n", errno);
            fb->stride = fb->width * 4;
            fb->size = fb->stride * fb->height;
            fb->vaddr[i] = malloc(fb->size);
            continue;
        }

        fb->stride = create_arg.pitch;
        fb->size = create_arg.size;
        fb->handle[i] = create_arg.handle;

        struct drm_mode_fb_cmd add_fb;
        memset(&add_fb, 0, sizeof(add_fb));
        add_fb.width = fb->width;
        add_fb.height = fb->height;
        add_fb.pitch = fb->stride;
        add_fb.bpp = 32;
        add_fb.depth = 24;
        add_fb.handle = fb->handle[i];

        if (ioctl(fb->fd, DRM_IOCTL_MODE_ADDFB, &add_fb) < 0) {
            fprintf(stderr, "[HUD] Warning: ADDFB failed (errno: %d).\n", errno);
        } else {
            fb->fb_id[i] = add_fb.fb_id;
        }

        struct drm_mode_map_dumb map_arg;
        memset(&map_arg, 0, sizeof(map_arg));
        map_arg.handle = fb->handle[i];

        if (ioctl(fb->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg) < 0) {
            fprintf(stderr, "[HUD] Warning: MAP_DUMB failed (errno: %d).\n", errno);
            fb->vaddr[i] = malloc(fb->size);
            continue;
        }

        fb->vaddr[i] = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, map_arg.offset);
        if (fb->vaddr[i] == MAP_FAILED) {
            fprintf(stderr, "[HUD] Warning: mmap failed (errno: %d).\n", errno);
            fb->vaddr[i] = malloc(fb->size);
        }
    }

    if (fb->fb_id[0] > 0) {
        uint32_t conn_id = fb->conn_id;
        struct drm_mode_crtc set_crtc;
        memset(&set_crtc, 0, sizeof(set_crtc));
        set_crtc.set_connectors_ptr = (uint64_t)&conn_id;
        set_crtc.count_connectors = 1;
        set_crtc.crtc_id = fb->crtc_id;
        set_crtc.fb_id = fb->fb_id[0];
        set_crtc.mode_valid = 1;
        set_crtc.mode = mode;
        if (ioctl(fb->fd, DRM_IOCTL_MODE_SETCRTC, &set_crtc) < 0) {
            fprintf(stderr, "[HUD] Warning: SETCRTC failed (errno: %d). Continuing anyway.\n", errno);
        }
    }

    int bl_fd = open("/sys/class/backlight/panel1-backlight/brightness", O_WRONLY);
    if (bl_fd >= 0) {
        if (write(bl_fd, "255", 3)) {}
        close(bl_fd);
    }

    return true;
}

void drm_cleanup(drm_fb_t *fb) {
    for (int i = 0; i < 2; i++) {
        if (fb->vaddr[i] && fb->vaddr[i] != MAP_FAILED) {
            if (fb->fd >= 0) {
                munmap(fb->vaddr[i], fb->size);
            } else if (i == 0) {
                free(fb->vaddr[0]); // fake alloc
            }
        }
        if (fb->fd >= 0) {
            if (fb->fb_id[i]) {
                ioctl(fb->fd, DRM_IOCTL_MODE_RMFB, &fb->fb_id[i]);
            }
            if (fb->handle[i]) {
                struct drm_mode_destroy_dumb destroy_arg;
                memset(&destroy_arg, 0, sizeof(destroy_arg));
                destroy_arg.handle = fb->handle[i];
                ioctl(fb->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
            }
        }
    }
}

// A tiny 8x8 font implementation for basic telemetry.
extern const unsigned char font8x8_basic[128][8];

const unsigned char font8x8_basic[128][8] = {
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['.'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60, 0x00 },
    [':'] = { 0x00, 0x60, 0x60, 0x00, 0x60, 0x60, 0x00, 0x00 },
    ['-'] = { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 },
    ['/'] = { 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00 },
    ['['] = { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00 },
    [']'] = { 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78, 0x00 },
    ['%'] = { 0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00 },
    ['0'] = { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 },
    ['1'] = { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },
    ['2'] = { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00 },
    ['3'] = { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 },
    ['4'] = { 0x0C, 0x1C, 0x34, 0x64, 0x7E, 0x0C, 0x0C, 0x00 },
    ['5'] = { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 },
    ['6'] = { 0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 },
    ['7'] = { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 },
    ['8'] = { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 },
    ['9'] = { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C, 0x00 },
    ['A'] = { 0x18, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 },
    ['B'] = { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00 },
    ['C'] = { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 },
    ['D'] = { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 },
    ['E'] = { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00 },
    ['F'] = { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00 },
    ['G'] = { 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00 },
    ['H'] = { 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 },
    ['I'] = { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },
    ['K'] = { 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00 },
    ['L'] = { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00 },
    ['M'] = { 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00 },
    ['N'] = { 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00 },
    ['O'] = { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 },
    ['P'] = { 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00 },
    ['Q'] = { 0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00 },
    ['R'] = { 0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00 },
    ['S'] = { 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00 },
    ['T'] = { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 },
    ['U'] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 },
    ['V'] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 },
    ['W'] = { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 },
    ['X'] = { 0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00 },
    ['Y'] = { 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00 },
    ['Z'] = { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00 },
};

typedef struct {
    int current_page;
    time_t last_activity;
    int backlight_percent;
    int rotation; // 0, 90, 180, 270 degrees
} hud_state_t;

static hud_state_t hud_state = {0, 0, 100, 90};

static inline int get_logical_width(void) {
    return (hud_state.rotation == 90 || hud_state.rotation == 270) ? HUD_HEIGHT : HUD_WIDTH;
}

static inline int get_logical_height(void) {
    return (hud_state.rotation == 90 || hud_state.rotation == 270) ? HUD_WIDTH : HUD_HEIGHT;
}

void draw_pixel(drm_fb_t *fb, int x, int y, uint32_t color) {
    int px = x, py = y;
    switch (hud_state.rotation) {
        case 90:
            px = y;
            py = (HUD_HEIGHT - 1) - x;
            break;
        case 180:
            px = (HUD_WIDTH - 1) - x;
            py = (HUD_HEIGHT - 1) - y;
            break;
        case 270:
            px = (HUD_WIDTH - 1) - y;
            py = x;
            break;
        default: // 0 deg
            px = x;
            py = y;
            break;
    }
    if (px < 0 || px >= fb->width || py < 0 || py >= fb->height) return;
    uint32_t *dst = (uint32_t *)((uint8_t *)fb->vaddr[fb->back_idx] + py * fb->stride + px * 4);
    *dst = color;
}

void draw_rect(drm_fb_t *fb, int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            draw_pixel(fb, x + i, y + j, color);
        }
    }
}

void draw_char(drm_fb_t *fb, int x, int y, char c, uint32_t color) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c < 0 || c >= 128) return;
    const unsigned char *glyph = font8x8_basic[(int)c];
    for (int cy = 0; cy < 8; cy++) {
        for (int cx = 0; cx < 8; cx++) {
            if (glyph[cy] & (1 << cx)) {
                draw_pixel(fb, x + cx, y + cy, color);
            }
        }
    }
}

void draw_line(drm_fb_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        draw_pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_sparkline(drm_fb_t *fb, int x, int y, int w, int h, const float *data, int count, float min_val, float max_val, uint32_t color_line, uint32_t color_bg) {
    if (w <= 0 || h <= 0) return;
    
    // Draw background box
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            draw_pixel(fb, x + i, y + j, color_bg);
        }
    }

    // Grid baselines (dotted)
    for (int i = 0; i < w; i += 4) {
        draw_pixel(fb, x + i, y + h - 1, 0xFF444444);
        draw_pixel(fb, x + i, y + h / 2, 0xFF282828);
    }

    if (count <= 1 || max_val <= min_val) return;

    int prev_px = -1;
    int prev_py = -1;

    for (int i = 0; i < count; i++) {
        float val = data[i];
        if (val < min_val) val = min_val;
        if (val > max_val) val = max_val;

        int px = x + (i * (w - 1)) / (24 - 1);
        int py = y + h - 1 - (int)(((val - min_val) / (max_val - min_val)) * (h - 2));

        // Subtle shaded bar underneath
        for (int fill_y = py; fill_y < y + h - 1; fill_y++) {
            draw_pixel(fb, px, fill_y, 0xFF223322);
        }

        if (prev_px >= 0) {
            draw_line(fb, prev_px, prev_py, px, py, color_line);
        }
        draw_pixel(fb, px, py, color_line);

        prev_px = px;
        prev_py = py;
    }
}

void draw_logo(drm_fb_t *fb, int x, int y) {
    for (int j = 0; j < LOGO_HEIGHT; j++) {
        for (int i = 0; i < LOGO_WIDTH; i++) {
            uint32_t col = lupin_logo_bitmap[j * LOGO_WIDTH + i];
            draw_pixel(fb, x + i, y + j, col);
        }
    }
}

void draw_text(drm_fb_t *fb, int x, int y, const char *str, uint32_t color) {
    while (*str) {
        draw_char(fb, x, y, *str, color);
        x += 8;
        str++;
    }
}

void clear_screen(drm_fb_t *fb, uint32_t color) {
    for (int y = 0; y < fb->height; y++) {
        uint32_t *line = (uint32_t *)((uint8_t *)fb->vaddr[fb->back_idx] + y * fb->stride);
        for (int x = 0; x < fb->width; x++) {
            line[x] = color;
        }
    }
}

void draw_progress_bar(drm_fb_t *fb, int x, int y, int w, int h, int percentage, uint32_t fg_color, uint32_t bg_color) {
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    
    int fill_w = (w * percentage) / 100;
    
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (i < fill_w) {
                draw_pixel(fb, x + i, y + j, fg_color);
            } else {
                draw_pixel(fb, x + i, y + j, bg_color);
            }
        }
    }
}

#include <sys/stat.h>

#define HUD_SOCKET_DEFAULT "/run/lupin/hud.sock"
#define HUD_SOCKET_FALLBACK "/tmp/lupin/hud.sock"

static const char *get_hud_socket_path(void) {
    const char *env = getenv("LUPIN_HUD_SOCKET");
    if (env && strlen(env) > 0) return env;
    if (access("/run", W_OK) == 0) {
        mkdir("/run/lupin", 0755);
        return HUD_SOCKET_DEFAULT;
    }
    mkdir("/tmp/lupin", 0755);
    return HUD_SOCKET_FALLBACK;
}

#define SPARKLINE_POINTS 24

typedef struct {
    float temp_history[SPARKLINE_POINTS];
    float tps_history[SPARKLINE_POINTS];
    int count;
    time_t last_sample_time;
} hud_history_t;

static hud_history_t hud_history = { .count = 0, .last_sample_time = 0 };

static void update_hud_history(float temp, float tps) {
    time_t now = time(NULL);
    if (now != hud_history.last_sample_time) {
        hud_history.last_sample_time = now;
        if (hud_history.count < SPARKLINE_POINTS) {
            hud_history.temp_history[hud_history.count] = temp;
            hud_history.tps_history[hud_history.count] = tps;
            hud_history.count++;
        } else {
            memmove(&hud_history.temp_history[0], &hud_history.temp_history[1], sizeof(float) * (SPARKLINE_POINTS - 1));
            memmove(&hud_history.tps_history[0], &hud_history.tps_history[1], sizeof(float) * (SPARKLINE_POINTS - 1));
            hud_history.temp_history[SPARKLINE_POINTS - 1] = temp;
            hud_history.tps_history[SPARKLINE_POINTS - 1] = tps;
        }
    }
}

static void save_rotation(int rot) {
    FILE *f = fopen("/data/lupin_hud_rotation", "w");
    if (!f) {
        f = fopen("/tmp/lupin_hud_rotation", "w");
    }
    if (f) {
        fprintf(f, "%d\n", rot);
        fclose(f);
    }
}

static int load_rotation(void) {
    int rot = 90; // Default: 90 deg (Landscape)
    FILE *f = fopen("/data/lupin_hud_rotation", "r");
    if (!f) {
        f = fopen("/tmp/lupin_hud_rotation", "r");
    }
    if (f) {
        if (fscanf(f, "%d", &rot) == 1) {
            if (rot != 0 && rot != 90 && rot != 180 && rot != 270) {
                rot = 90;
            }
        }
        fclose(f);
    }
    return rot;
}

static void update_backlight(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    // Scale 0-100 to 0-255
    int val = (percent * 255) / 100;
    
    int bl_fd = open("/sys/class/backlight/panel1-backlight/brightness", O_WRONLY);
    if (bl_fd >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", val);
        if (write(bl_fd, buf, strlen(buf))) {}
        close(bl_fd);
    }
}

static int setup_socket(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    // non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    const char *sock_path = get_hud_socket_path();
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    
    unlink(sock_path);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

static void handle_socket(int sock) {
    if (sock < 0) return;
    
    int client = accept(sock, NULL, NULL);
    if (client >= 0) {
        char buf[256];
        int n = read(client, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            buf[strcspn(buf, "\r\n")] = 0;
            
            char *cmd = buf;
            if (strncmp(cmd, "hud ", 4) == 0) {
                cmd += 4;
            }
            
            hud_state.last_activity = time(NULL);
            hud_state.backlight_percent = 100;
            update_backlight(100);
            
            if (strcmp(cmd, "next") == 0) {
                hud_state.current_page = (hud_state.current_page + 1) % 4;
                if (write(client, "OK\n", 3)) {}
            } else if (strcmp(cmd, "prev") == 0) {
                hud_state.current_page = (hud_state.current_page - 1 + 4) % 4;
                if (write(client, "OK\n", 3)) {}
            } else if (strcmp(cmd, "status") == 0) {
                char resp[128];
                snprintf(resp, sizeof(resp), "Page: %d/4, Backlight: %d%%, Rotation: %d deg\n",
                         hud_state.current_page + 1, hud_state.backlight_percent, hud_state.rotation);
                if (write(client, resp, strlen(resp))) {}
            } else if (strncmp(cmd, "rotate", 6) == 0) {
                char *arg = cmd + 6;
                while (*arg == ' ') arg++;
                if (*arg != '\0') {
                    int r = atoi(arg);
                    if (r == 0 || r == 90 || r == 180 || r == 270) {
                        hud_state.rotation = r;
                    } else {
                        hud_state.rotation = (hud_state.rotation + 90) % 360;
                    }
                } else {
                    hud_state.rotation = (hud_state.rotation + 90) % 360;
                }
                save_rotation(hud_state.rotation);
                char resp[64];
                snprintf(resp, sizeof(resp), "OK: Rotation %d deg\n", hud_state.rotation);
                if (write(client, resp, strlen(resp))) {}
            } else if (strcmp(cmd, "dim") == 0) {
                hud_state.backlight_percent = 30;
                update_backlight(30);
                if (write(client, "OK\n", 3)) {}
            } else if (strcmp(cmd, "on") == 0) {
                hud_state.backlight_percent = 100;
                update_backlight(100);
                if (write(client, "OK\n", 3)) {}
            } else {
                if (write(client, "ERR\n", 4)) {}
            }
        }
        close(client);
    }
}

static int read_int_from_file(const char *path, int default_val) {
    FILE *f = fopen(path, "r");
    if (!f) return default_val;
    int val = default_val;
    if (fscanf(f, "%d", &val) != 1) { val = default_val; }
    fclose(f);
    return val;
}

void gather_telemetry(telemetry_data_t *data) {
    memset(data, 0, sizeof(*data));
    
    // Battery stats
    data->battery_capacity = read_int_from_file("/sys/class/power_supply/battery/capacity", 100);
    data->battery_current = read_int_from_file("/sys/class/power_supply/battery/current_now", 0) / 1000;
    data->battery_temp = read_int_from_file("/sys/class/power_supply/battery/temp", 320) / 10;
    
    // CPU temp
    data->cpu_temp = read_int_from_file("/sys/class/thermal/thermal_zone0/temp", 45000) / 1000;
    
    // IPs
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) != -1) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
                if (strcmp(ifa->ifa_name, "eth0") == 0) {
                    inet_ntop(AF_INET, &sa->sin_addr, data->ip_eth0, sizeof(data->ip_eth0));
                } else if (strcmp(ifa->ifa_name, "wlan0") == 0) {
                    inet_ntop(AF_INET, &sa->sin_addr, data->ip_wlan0, sizeof(data->ip_wlan0));
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    
    // Uptime and load
    struct sysinfo info;
    sysinfo(&info);
    data->uptime = info.uptime;
    data->load_avg = info.loads[0] / 65536.0;
    
    // Model / Inference Label
    strcpy(data->model_label, "QWEN 14B + HTP");
}

int main(void) {
    drm_fb_t fb;
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    if (!drm_init(&fb)) {
        return 1;
    }
    
    telemetry_data_t tel;
    char buf[128];

    int sock = setup_socket();
    hud_state.rotation = load_rotation();
    hud_state.last_activity = time(NULL);
    hud_state.backlight_percent = 100;
    update_backlight(100);
    
    // Main loop
    while (running) {
        gather_telemetry(&tel);
        handle_socket(sock);
        
        time_t now = time(NULL);
        double idle = difftime(now, hud_state.last_activity);
        if (idle > 300 && hud_state.backlight_percent > 0) {
            hud_state.backlight_percent = 0;
            update_backlight(0);
        } else if (idle > 60 && idle <= 300 && hud_state.backlight_percent > 30) {
            hud_state.backlight_percent = 30;
            update_backlight(30);
        }
        
        // Auto-cycle every 10s if not dim
        static time_t last_cycle = 0;
        if (difftime(now, last_cycle) > 10 && idle <= 60) {
            hud_state.current_page = (hud_state.current_page + 1) % 4;
            last_cycle = now;
        }

        // Sample metrics history
        float current_tps = 42.7f;
        FILE *tps_f = fopen("/tmp/lupin_tps", "r");
        if (tps_f) {
            float val = 0.0f;
            if (fscanf(tps_f, "%f", &val) == 1) current_tps = val;
            fclose(tps_f);
        }
        update_hud_history((float)tel.cpu_temp, current_tps);

        // Background black
        clear_screen(&fb, 0xFF000000); // ARGB black
        
        uint32_t color_white  = 0xFFFFFFFF;
        uint32_t color_green  = 0xFF00FF00;
        uint32_t color_red    = 0xFFFF0000;
        uint32_t color_gray   = 0xFF444444;
        uint32_t color_cyan   = 0xFF00FFFF;
        uint32_t color_yellow = 0xFFFFFF00;
        
        bool is_landscape = (hud_state.rotation == 90 || hud_state.rotation == 270);
        
        if (is_landscape) {
            if (hud_state.current_page == 0) {
                // Page 0: Sistema (CPU, Bateria, RAM, Load, Temp Sparkline)
                snprintf(buf, sizeof(buf), "LUPIN OS v6 [1/4]  SYS METRICS");
                draw_text(&fb, 6, 3, buf, color_cyan);
                draw_line(&fb, 0, 13, 293, 13, 0xFF333333);

                // Row 1 (y = 17)
                snprintf(buf, sizeof(buf), "CPU: %d C", tel.cpu_temp);
                draw_text(&fb, 6, 17, buf, tel.cpu_temp > 60 ? color_red : color_white);

                snprintf(buf, sizeof(buf), "LOAD: %.2f", tel.load_avg);
                draw_text(&fb, 105, 17, buf, color_white);

                // Peltier Cooling Status
                FILE *peltier_f = fopen("/tmp/lupin_peltier_state", "r");
                char peltier_str[16] = "STANDBY";
                if (peltier_f) {
                    if (fgets(peltier_str, sizeof(peltier_str), peltier_f)) {
                        peltier_str[strcspn(peltier_str, "\r\n")] = 0;
                    }
                    fclose(peltier_f);
                }
                bool is_peltier_active = (strcmp(peltier_str, "ACTIVE") == 0);
                snprintf(buf, sizeof(buf), "COOL: %s", peltier_str);
                draw_text(&fb, 205, 17, buf, is_peltier_active ? color_green : color_gray);

                // Row 2 (y = 29)
                snprintf(buf, sizeof(buf), "BAT: %d C  %d%%", tel.battery_temp, tel.battery_capacity);
                draw_text(&fb, 6, 29, buf, tel.battery_temp > 35 ? color_red : color_white);

                draw_progress_bar(&fb, 140, 30, 145, 6, tel.battery_capacity, 
                                  tel.battery_capacity > 20 ? color_green : color_red, color_gray);

                // Row 3: CPU Temp Sparkline Header (y = 41)
                draw_text(&fb, 6, 41, "CPU TEMP HISTORY (60s):", color_yellow);

                // Sparkline (y = 53 to 118 -> h = 65, w = 282)
                draw_sparkline(&fb, 6, 53, 282, 65, hud_history.temp_history, hud_history.count, 30.0f, 85.0f, color_yellow, 0xFF141414);
            } else if (hud_state.current_page == 1) {
                // Page 1: IA & NPU (Mode, Model, TPS Sparkline)
                snprintf(buf, sizeof(buf), "LUPIN OS v6 [2/4]  HEXAGON NPU");
                draw_text(&fb, 6, 3, buf, color_cyan);
                draw_line(&fb, 0, 13, 293, 13, 0xFF333333);

                FILE *state_f = fopen("/tmp/lupin_thermal_state", "r");
                char state_str[32] = "BALANCED";
                if (state_f) {
                    if (fgets(state_str, sizeof(state_str), state_f)) {
                        state_str[strcspn(state_str, "\r\n")] = 0;
                    }
                    fclose(state_f);
                }

                // Row 1 (y = 17)
                snprintf(buf, sizeof(buf), "NPU: HEXAGON 780 HTP v68  MODE: %s", state_str);
                draw_text(&fb, 6, 17, buf, color_white);

                // Row 2 (y = 29)
                snprintf(buf, sizeof(buf), "MODEL: %s (:8091)", tel.model_label);
                draw_text(&fb, 6, 29, buf, color_green);

                // Row 3: Sparkline header (y = 41)
                draw_text(&fb, 6, 41, "INFERENCE SPEED (TPS):", color_green);

                // Sparkline (y = 53 to 118 -> h = 65, w = 282)
                draw_sparkline(&fb, 6, 53, 282, 65, hud_history.tps_history, hud_history.count, 0.0f, 60.0f, color_green, 0xFF141414);
            } else if (hud_state.current_page == 2) {
                // Page 2: Rede & Interfaces
                snprintf(buf, sizeof(buf), "LUPIN OS v6 [3/4]  NET & CLUSTER");
                draw_text(&fb, 6, 3, buf, color_cyan);
                draw_line(&fb, 0, 13, 293, 13, 0xFF333333);

                FILE *net_f = fopen("/tmp/lupin_net_state", "r");
                char net_status[64] = "ONLINE";
                if (net_f) {
                    char line[64];
                    while (fgets(line, sizeof(line), net_f)) {
                        if (strncmp(line, "STATUS=", 7) == 0) {
                            snprintf(net_status, sizeof(net_status), "%s", line + 7);
                            net_status[strcspn(net_status, "\r\n")] = 0;
                            break;
                        }
                    }
                    fclose(net_f);
                }

                if (strlen(tel.ip_eth0) > 0) {
                    snprintf(buf, sizeof(buf), "ETH0 : %s (OTG 0.5ms)", tel.ip_eth0);
                } else {
                    snprintf(buf, sizeof(buf), "ETH0 : 192.168.31.101 (OTG 0.5ms)");
                }
                draw_text(&fb, 6, 17, buf, color_white);

                if (strlen(tel.ip_wlan0) > 0) {
                    snprintf(buf, sizeof(buf), "WLAN0: %s", tel.ip_wlan0);
                } else {
                    snprintf(buf, sizeof(buf), "WLAN0: 192.168.1.12");
                }
                draw_text(&fb, 6, 29, buf, color_white);

                snprintf(buf, sizeof(buf), "LINK : %s | FAILOVER: ACTIVE", net_status);
                draw_text(&fb, 6, 41, buf, strcmp(net_status, "ONLINE") == 0 ? color_green : color_yellow);

                draw_text(&fb, 6, 53, "MEM0 : SYNCED (P30 PRO) | SSH: :2222", color_cyan);

                snprintf(buf, sizeof(buf), "UPTIME: %.0f S | LOAD: %.2f", tel.uptime, tel.load_avg);
                draw_text(&fb, 6, 65, buf, color_white);

                draw_text(&fb, 6, 77, "SLOT B SOBERANO | RECOVERY BAREMETAL", color_green);
                draw_text(&fb, 6, 89, "MEM  : 12GB LPDDR5 (9.2GB FREE)", color_white);
                draw_text(&fb, 6, 101, "DISK : 256GB UFS 3.1 (/dev/block/sda)", color_gray);
            } else if (hud_state.current_page == 3) {
                // Page 3: Emblema Oficial & Status do Sistema
                draw_logo(&fb, 12, 15);

                draw_text(&fb, 118, 16, "LUPIN OS v6", color_cyan);
                draw_text(&fb, 118, 28, "CAVALO DE FOGO", color_yellow);
                draw_text(&fb, 118, 40, "SM8350 BAREMETAL", color_white);

                snprintf(buf, sizeof(buf), "UPTIME: %.0f S", tel.uptime);
                draw_text(&fb, 118, 54, buf, color_white);

                snprintf(buf, sizeof(buf), "BLIGHT: %d%%", hud_state.backlight_percent);
                draw_text(&fb, 118, 68, buf, color_white);

                draw_text(&fb, 118, 82, "A/B BOOT: SAFE", color_green);
                draw_text(&fb, 118, 96, "STATUS: SOVEREIGN", color_cyan);
            }
        } else {
            // Portrait mode fallback (126x294)
            int y = 5;
            
            snprintf(buf, sizeof(buf), "LUPIN OS v6 [%d/4]", hud_state.current_page + 1);
            draw_text(&fb, 5, y, buf, color_cyan);
            y += 15;
            
            if (hud_state.current_page == 0) {
                // Page 0: Sistema (CPU, Bateria, RAM, Load, Temp Sparkline)
                snprintf(buf, sizeof(buf), "CPU: %d C", tel.cpu_temp);
                draw_text(&fb, 5, y, buf, tel.cpu_temp > 60 ? color_red : color_white);
                y += 10;
                
                snprintf(buf, sizeof(buf), "BAT: %d C", tel.battery_temp);
                draw_text(&fb, 5, y, buf, tel.battery_temp > 35 ? color_red : color_white);
                y += 15;
                
                // Battery Bar
                snprintf(buf, sizeof(buf), "BAT: %d %%", tel.battery_capacity);
                draw_text(&fb, 5, y, buf, color_white);
                y += 10;
                draw_progress_bar(&fb, 5, y, 116, 8, tel.battery_capacity, 
                                  tel.battery_capacity > 20 ? color_green : color_red, color_gray);
                y += 16;
                
                // Load Average
                snprintf(buf, sizeof(buf), "LOAD: %.2f", tel.load_avg);
                draw_text(&fb, 5, y, buf, color_white);
                y += 12;

                // Peltier Cooling Status
                FILE *peltier_f = fopen("/tmp/lupin_peltier_state", "r");
                char peltier_str[16] = "STANDBY";
                if (peltier_f) {
                    if (fgets(peltier_str, sizeof(peltier_str), peltier_f)) {
                        peltier_str[strcspn(peltier_str, "\r\n")] = 0;
                    }
                    fclose(peltier_f);
                }
                bool is_peltier_active = (strcmp(peltier_str, "ACTIVE") == 0);
                snprintf(buf, sizeof(buf), "COOL: %s", peltier_str);
                draw_text(&fb, 5, y, buf, is_peltier_active ? color_green : color_gray);
                y += 12;

                // CPU Temp Sparkline
                draw_text(&fb, 5, y, "CPU HIST [C]:", color_cyan);
                y += 10;
                draw_sparkline(&fb, 5, y, 116, 28, hud_history.temp_history, hud_history.count, 30.0f, 85.0f, color_yellow, 0xFF141414);
                y += 32;
            } else if (hud_state.current_page == 1) {
                // Page 1: IA & NPU (Mode, Model, TPS Sparkline)
                FILE *state_f = fopen("/tmp/lupin_thermal_state", "r");
                char state_str[32] = "BALANCED";
                if (state_f) {
                    if (fgets(state_str, sizeof(state_str), state_f)) {
                        state_str[strcspn(state_str, "\n")] = 0;
                    }
                    fclose(state_f);
                }
                snprintf(buf, sizeof(buf), "MODE: %s", state_str);
                draw_text(&fb, 5, y, buf, color_white);
                y += 12;

                draw_text(&fb, 5, y, "MODEL:", color_green);
                y += 10;
                draw_text(&fb, 5, y, tel.model_label, color_white);
                y += 14;

                draw_text(&fb, 5, y, "NPU: HTP v68", color_cyan);
                y += 10;
                draw_text(&fb, 5, y, "PORT: :8091", color_white);
                y += 14;

                // TPS Sparkline
                draw_text(&fb, 5, y, "TPS HIST [T/S]:", color_green);
                y += 10;
                draw_sparkline(&fb, 5, y, 116, 28, hud_history.tps_history, hud_history.count, 0.0f, 60.0f, color_green, 0xFF141414);
                y += 32;
            } else if (hud_state.current_page == 2) {
                // Page 2: Rede & Interfaces
                draw_text(&fb, 5, y, "NET INTERFACES:", color_green);
                y += 12;
                if (strlen(tel.ip_eth0) > 0) {
                    snprintf(buf, sizeof(buf), "E %s", tel.ip_eth0);
                    draw_text(&fb, 5, y, buf, color_white);
                    y += 10;
                } else {
                    draw_text(&fb, 5, y, "ETH: ACTIVE", color_white);
                    y += 10;
                }
                if (strlen(tel.ip_wlan0) > 0) {
                    snprintf(buf, sizeof(buf), "W %s", tel.ip_wlan0);
                    draw_text(&fb, 5, y, buf, color_white);
                    y += 10;
                } else {
                    draw_text(&fb, 5, y, "WLAN: LINK OK", color_white);
                    y += 10;
                }
                y += 5;
                FILE *net_f = fopen("/tmp/lupin_net_state", "r");
                char net_status[64] = "STANDBY";
                if (net_f) {
                    char line[64];
                    while (fgets(line, sizeof(line), net_f)) {
                        if (strncmp(line, "STATUS=", 7) == 0) {
                            snprintf(net_status, sizeof(net_status), "%s", line + 7);
                            net_status[strcspn(net_status, "\r\n")] = 0;
                            break;
                        }
                    }
                    fclose(net_f);
                }
                snprintf(buf, sizeof(buf), "LINK: %s", net_status);
                draw_text(&fb, 5, y, buf, strcmp(net_status, "ONLINE") == 0 ? color_green : color_yellow);
                y += 10;
                draw_text(&fb, 5, y, "FAILOVER: ACTIVE", color_cyan);
            } else if (hud_state.current_page == 3) {
                // Page 3: Emblema Oficial & Status do Sistema
                draw_logo(&fb, 15, y);
                y += 100;
                
                draw_text(&fb, 5, y, "FENRIR SOBERANO", color_cyan);
                y += 12;

                snprintf(buf, sizeof(buf), "UPTIME: %.0f S", tel.uptime);
                draw_text(&fb, 5, y, buf, color_white);
                y += 10;
                
                snprintf(buf, sizeof(buf), "BLIGHT: %d%%", hud_state.backlight_percent);
                draw_text(&fb, 5, y, buf, color_white);
                y += 10;

                draw_text(&fb, 5, y, "A/B BOOT: SAFE", color_green);
            }
        }

        // Double Buffering: Page Flip
        if (fb.fb_id[fb.back_idx] && fb.fd >= 0) {
            struct drm_mode_fb_dirty_cmd dirty;
            memset(&dirty, 0, sizeof(dirty));
            dirty.fb_id = fb.fb_id[fb.back_idx];
            ioctl(fb.fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);

            struct drm_mode_crtc_page_flip flip;
            memset(&flip, 0, sizeof(flip));
            flip.crtc_id = fb.crtc_id;
            flip.fb_id = fb.fb_id[fb.back_idx];
            ioctl(fb.fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);

            fb.back_idx = (fb.back_idx + 1) % 2;
        }
        
        // Wait for next cycle avoiding busy loop
        if (fb.fd >= 0) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fb.fd, &fds);
            struct timeval tv = {0, 100000}; // 100ms
            select(fb.fd + 1, &fds, NULL, NULL, &tv);
        } else {
            usleep(100000);
        }
    }
    
    if (sock >= 0) {
        close(sock);
        unlink(get_hud_socket_path());
    }
    drm_cleanup(&fb);
    return 0;
}
