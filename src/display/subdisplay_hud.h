#ifndef SUBDISPLAY_HUD_H
#define SUBDISPLAY_HUD_H

#include <stdint.h>
#include <stdbool.h>

#define HUD_WIDTH  126
#define HUD_HEIGHT 294

// Structure for telemetry data
typedef struct {
    int battery_capacity;
    int battery_current;
    int battery_temp;
    int cpu_temp;
    char ip_eth0[64];
    char ip_wlan0[64];
    double uptime;
    double load_avg;
    char model_label[64];
} telemetry_data_t;

// Structure for the DRM framebuffer
typedef struct {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t size;
    uint32_t handle[2];
    uint8_t *vaddr[2];
    uint32_t fb_id[2];
    uint32_t crtc_id;
    uint32_t conn_id;
    int back_idx;
} drm_fb_t;

// Initialize DRM and the framebuffer
bool drm_init(drm_fb_t *fb);

// Cleanup DRM resources
void drm_cleanup(drm_fb_t *fb);

// Draw a single character
void draw_char(drm_fb_t *fb, int x, int y, char c, uint32_t color);

// Draw a string of text
void draw_text(drm_fb_t *fb, int x, int y, const char *str, uint32_t color);

// Clear the screen with a specific color
void clear_screen(drm_fb_t *fb, uint32_t color);

// Draw a filled rectangle
void draw_rect(drm_fb_t *fb, int x, int y, int w, int h, uint32_t color);

// Draw a progress bar
void draw_progress_bar(drm_fb_t *fb, int x, int y, int w, int h, int percentage, uint32_t color, uint32_t bg_color);

// Draw a line segment
void draw_line(drm_fb_t *fb, int x0, int y0, int x1, int y1, uint32_t color);

// Draw a sparkline graph for historical metrics
void draw_sparkline(drm_fb_t *fb, int x, int y, int w, int h, const float *data, int count, float min_val, float max_val, uint32_t color_line, uint32_t color_bg);

// Draw the official Lupin OS cyber wolf emblem
void draw_logo(drm_fb_t *fb, int x, int y);

// Gather telemetry data from sysfs/proc
void gather_telemetry(telemetry_data_t *data);

#endif // SUBDISPLAY_HUD_H
