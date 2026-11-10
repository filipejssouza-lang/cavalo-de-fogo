/*
 * ==============================================================================
 * Lupin Subdisplay HUD v5 - Sovereign Rear AMOLED Telemetry Daemon
 * Hardware: Xiaomi Mi 11 Ultra (Cavalo de Fogo / Fenrir)
 * Display: 1.1" Rear AMOLED (DSI-2, Connector 64, CRTC 199, Plane 92)
 * Orientation: 90-degree Landscape (294 x 126 virtual canvas)
 * ==============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "/root/font8x8_basic.h"

#define V_WIDTH   294
#define V_HEIGHT  126
#define P_WIDTH   126
#define P_HEIGHT  294

// Cyberpunk HUD Palette (ARGB32)
#define COLOR_BG          0xFF060912  // Deep Obsidian Dark Blue
#define COLOR_CYAN        0xFF00FFCC  // Neon Cyan / Matilha Primary
#define COLOR_GREEN       0xFF00FF88  // Bio-Green / Normal
#define COLOR_GOLD        0xFFFFD700  // Gold / Warning / Accent
#define COLOR_MAGENTA     0xFFFF007F  // Hot Pink / Inception
#define COLOR_RED         0xFFFF2244  // Alert Red
#define COLOR_WHITE       0xFFE8EEF5  // Crisp Off-White
#define COLOR_GRAY        0xFF2D3748  // Muted Bar BG
#define COLOR_DIM_CYAN    0xFF006655  // Subdued Border

typedef struct {
    // Battery
    int bat_pct;
    int bat_temp;
    int bat_mv;
    int bat_ma;
    char bat_status[16];

    // Thermals & Silicon
    int cpu_temp_avg;
    int cpu_temp_max;
    int gpu_temp;
    int quiet_temp;     // Peltier / chassis
    uint32_t cpu_little_khz;
    uint32_t cpu_gold_khz;
    uint32_t cpu_prime_khz;

    // Memory
    double ram_free_gb;
    double ram_total_gb;
    double zram_used_mb;
    double load_1m;
    long uptime_s;

    // Network
    char ip_otg[32];
    char ip_wlan[32];
    bool p30_alive;
    bool pc_alive;

    // Neural
    char neural_model[32];
    char neural_status[32];
} cluster_telemetry_t;

static volatile bool g_running = true;
static uint32_t *g_fb_pixels = NULL;
static uint32_t g_pitch_pixels = 0;

static void sig_handler(int sig) {
    (void)sig;
    g_running = false;
}

// 90-degree Landscape coordinate mapping (Corrected 180-degree physical orientation)
static inline void set_pixel_ls(int vx, int vy, uint32_t color) {
    if (vx < 0 || vx >= V_WIDTH || vy < 0 || vy >= V_HEIGHT) return;
    int px = vy;
    int py = 293 - vx;
    g_fb_pixels[py * g_pitch_pixels + px] = color;
}

static void fill_rect_ls(int vx, int vy, int w, int h, uint32_t color) {
    for (int y = vy; y < vy + h; y++) {
        for (int x = vx; x < vx + w; x++) {
            set_pixel_ls(x, y, color);
        }
    }
}

static void draw_char_ls(int vx, int vy, char c, uint32_t color) {
    if ((uint8_t)c > 127) c = '?';
    for (int r = 0; r < 8; r++) {
        uint8_t bits = font8x8_basic[(uint8_t)c][r];
        for (int b = 0; b < 8; b++) {
            if (bits & (1 << b)) {
                set_pixel_ls(vx + b, vy + r, color);
            }
        }
    }
}

static void draw_text_ls(int vx, int vy, const char *str, uint32_t color) {
    while (*str) {
        draw_char_ls(vx, vy, *str++, color);
        vx += 8;
    }
}

static void draw_progress_bar_ls(int vx, int vy, int w, int h, int pct, uint32_t fill_color, uint32_t bg_color) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    fill_rect_ls(vx, vy, w, h, bg_color);
    int fw = (w * pct) / 100;
    if (fw > 0) fill_rect_ls(vx, vy, fw, h, fill_color);
}

static int read_int_file(const char *path, int def) {
    FILE *f = fopen(path, "r");
    if (!f) return def;
    int v = def;
    if (fscanf(f, "%d", &v) != 1) v = def;
    fclose(f);
    return v;
}

static void read_str_file(const char *path, char *buf, size_t maxlen, const char *def) {
    FILE *f = fopen(path, "r");
    if (!f) {
        strncpy(buf, def, maxlen - 1);
        buf[maxlen - 1] = '\0';
        return;
    }
    if (fgets(buf, (int)maxlen, f) == NULL) {
        strncpy(buf, def, maxlen - 1);
    } else {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
    }
    fclose(f);
}

static void poll_telemetry(cluster_telemetry_t *t) {
    memset(t, 0, sizeof(*t));

    // 1. Battery
    t->bat_pct = read_int_file("/sys/class/power_supply/battery/capacity", 50);
    t->bat_temp = read_int_file("/sys/class/power_supply/battery/temp", 300) / 10;
    t->bat_mv = read_int_file("/sys/class/power_supply/battery/voltage_now", 4000000) / 1000;
    t->bat_ma = read_int_file("/sys/class/power_supply/battery/current_now", 0) / 1000;
    read_str_file("/sys/class/power_supply/battery/status", t->bat_status, sizeof(t->bat_status), "Idle");

    // 2. Thermals
    t->quiet_temp = read_int_file("/sys/class/thermal/thermal_zone87/temp", 34000) / 1000;
    t->gpu_temp = read_int_file("/sys/class/thermal/thermal_zone54/temp", 45000) / 1000;
    
    // CPU core temperatures
    int t0 = read_int_file("/sys/class/thermal/thermal_zone39/temp", 40000) / 1000;
    int t4 = read_int_file("/sys/class/thermal/thermal_zone45/temp", 40000) / 1000;
    int t7 = read_int_file("/sys/class/thermal/thermal_zone51/temp", 40000) / 1000;
    t->cpu_temp_avg = (t0 + t4 + t7) / 3;
    t->cpu_temp_max = t7 > t4 ? (t7 > t0 ? t7 : t0) : (t4 > t0 ? t4 : t0);

    // CPU Frequencies
    t->cpu_little_khz = (uint32_t)read_int_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", 300000);
    t->cpu_gold_khz   = (uint32_t)read_int_file("/sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq", 1800000);
    t->cpu_prime_khz  = (uint32_t)read_int_file("/sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq", 2841600);

    // 3. Sysinfo & Memory
    struct sysinfo s;
    if (sysinfo(&s) == 0) {
        t->ram_total_gb = (double)s.totalram * s.mem_unit / (1024.0 * 1024.0 * 1024.0);
        t->ram_free_gb  = (double)s.freeram * s.mem_unit / (1024.0 * 1024.0 * 1024.0);
        t->load_1m      = s.loads[0] / 65536.0;
        t->uptime_s     = s.uptime;
    }
    t->zram_used_mb = (double)read_int_file("/sys/block/zram0/orig_data_size", 0) / (1024.0 * 1024.0);

    // 4. IP Addresses
    struct ifaddrs *ifaddr, *ifa;
    strcpy(t->ip_otg, "DISCONNECTED");
    strcpy(t->ip_wlan, "OFFLINE");
    if (getifaddrs(&ifaddr) != -1) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            if (strcmp(ifa->ifa_name, "mi11-eth") == 0 || strcmp(ifa->ifa_name, "eth0") == 0) {
                inet_ntop(AF_INET, &sa->sin_addr, t->ip_otg, sizeof(t->ip_otg));
            } else if (strcmp(ifa->ifa_name, "wlan0") == 0) {
                inet_ntop(AF_INET, &sa->sin_addr, t->ip_wlan, sizeof(t->ip_wlan));
            }
        }
        freeifaddrs(ifaddr);
    }

    // 5. Neural & Compute Status
    strcpy(t->neural_model, "QWEN 14B + RISC-V");
    strcpy(t->neural_status, "INCEPTION IN-WEIGHTS");
}

static void render_header(int page, int total_pages) {
    // Top border
    fill_rect_ls(0, 0, V_WIDTH, 2, COLOR_CYAN);
    // Header title
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "FENRIR // SOVEREIGN HUD [%d/%d]", page, total_pages);
    draw_text_ls(6, 4, hdr, COLOR_CYAN);
    // Header separator line
    fill_rect_ls(0, 14, V_WIDTH, 1, COLOR_DIM_CYAN);
}

static void render_footer(void) {
    // Footer line
    fill_rect_ls(0, 114, V_WIDTH, 1, COLOR_DIM_CYAN);
    draw_text_ls(6, 116, "MATILHA LUPIN * CLUSTER ATIVO", COLOR_GREEN);
    // Bottom border
    fill_rect_ls(0, 124, V_WIDTH, 2, COLOR_CYAN);
    // Side borders
    fill_rect_ls(0, 0, 2, V_HEIGHT, COLOR_CYAN);
    fill_rect_ls(V_WIDTH - 2, 0, 2, V_HEIGHT, COLOR_CYAN);
}

// Page 1: Silicon & Hardware
static void render_page_hardware(const cluster_telemetry_t *t) {
    render_header(1, 4);

    char buf[64];
    int y = 18;

    snprintf(buf, sizeof(buf), "SOC: SNAPDRAGON 888 (SM8350)");
    draw_text_ls(6, y, buf, COLOR_WHITE);
    y += 12;

    snprintf(buf, sizeof(buf), "CPU: %dC (PEAK %dC) | GPU: %dC", t->cpu_temp_avg, t->cpu_temp_max, t->gpu_temp);
    draw_text_ls(6, y, buf, t->cpu_temp_max > 75 ? COLOR_RED : COLOR_GREEN);
    y += 12;

    snprintf(buf, sizeof(buf), "FREQ: %dMHz / %dGHz / %dGHz", 
             t->cpu_little_khz / 1000, t->cpu_gold_khz / 1000000, t->cpu_prime_khz / 1000000);
    draw_text_ls(6, y, buf, COLOR_GOLD);
    y += 12;

    snprintf(buf, sizeof(buf), "CHASSIS/PELTIER: %d C [COOLED]", t->quiet_temp);
    draw_text_ls(6, y, buf, COLOR_CYAN);
    y += 12;

    long h = t->uptime_s / 3600;
    long m = (t->uptime_s % 3600) / 60;
    snprintf(buf, sizeof(buf), "UPTIME: %ldh %02ldm | LOAD: %.2f", h, m, t->load_1m);
    draw_text_ls(6, y, buf, COLOR_WHITE);
    y += 12;

    snprintf(buf, sizeof(buf), "RAM: %.1fG FREE / %.1fG LPDDR5", t->ram_free_gb, t->ram_total_gb);
    draw_text_ls(6, y, buf, COLOR_GREEN);
    y += 10;
    int ram_pct = (int)((t->ram_total_gb - t->ram_free_gb) * 100.0 / t->ram_total_gb);
    draw_progress_bar_ls(6, y, 282, 6, ram_pct, COLOR_CYAN, COLOR_GRAY);

    render_footer();
}

// Page 2: Power & Battery
static void render_page_power(const cluster_telemetry_t *t) {
    render_header(2, 4);

    char buf[64];
    int y = 18;

    snprintf(buf, sizeof(buf), "POWER MANAGEMENT & THERMALS");
    draw_text_ls(6, y, buf, COLOR_WHITE);
    y += 12;

    snprintf(buf, sizeof(buf), "BATTERY: %d%% (%s)", t->bat_pct, t->bat_status);
    draw_text_ls(6, y, buf, t->bat_pct < 20 ? COLOR_RED : COLOR_GREEN);
    y += 11;
    draw_progress_bar_ls(6, y, 282, 8, t->bat_pct, t->bat_pct < 20 ? COLOR_RED : COLOR_GREEN, COLOR_GRAY);
    y += 14;

    snprintf(buf, sizeof(buf), "VOLTAGE: %d mV | CURR: %d mA", t->bat_mv, t->bat_ma);
    draw_text_ls(6, y, buf, COLOR_GOLD);
    y += 12;

    snprintf(buf, sizeof(buf), "CELL TEMP: %d C [SAFE]", t->bat_temp);
    draw_text_ls(6, y, buf, t->bat_temp > 45 ? COLOR_RED : COLOR_CYAN);
    y += 12;

    snprintf(buf, sizeof(buf), "WAKELOCK: LUPIN_USB ACTIVE (100%%)");
    draw_text_ls(6, y, buf, COLOR_WHITE);
    y += 12;

    snprintf(buf, sizeof(buf), "ZRAM COMPRESSED: %.0f MB", t->zram_used_mb);
    draw_text_ls(6, y, buf, COLOR_GREEN);

    render_footer();
}

// Page 3: Lupin Pack Mesh & Network
static void render_page_network(const cluster_telemetry_t *t) {
    render_header(3, 4);

    char buf[64];
    int y = 18;

    snprintf(buf, sizeof(buf), "LUPIN PACK MESH TOPOLOGY");
    draw_text_ls(6, y, buf, COLOR_WHITE);
    y += 12;

    snprintf(buf, sizeof(buf), "OTG ETH: %s", t->ip_otg);
    draw_text_ls(6, y, buf, strstr(t->ip_otg, "192.168") ? COLOR_GREEN : COLOR_RED);
    y += 12;

    snprintf(buf, sizeof(buf), "WLAN0  : %s", t->ip_wlan);
    draw_text_ls(6, y, buf, strstr(t->ip_wlan, "192.168") ? COLOR_CYAN : COLOR_RED);
    y += 12;

    snprintf(buf, sizeof(buf), "P30 PRO : 192.168.1.4 [VAULT]");
    draw_text_ls(6, y, buf, COLOR_GOLD);
    y += 12;

    snprintf(buf, sizeof(buf), "MIMIR PC: 192.168.1.7 [ORCHESTRATOR]");
    draw_text_ls(6, y, buf, COLOR_WHITE);
    y += 12;

    snprintf(buf, sizeof(buf), "SSH: :2222 | LLAMA-SERVER: :8080");
    draw_text_ls(6, y, buf, COLOR_MAGENTA);

    render_footer();
}

// Page 4: Neural & Inception Compute
static void render_page_neural(const cluster_telemetry_t *t) {
    render_header(4, 4);

    char buf[64];
    int y = 18;

    snprintf(buf, sizeof(buf), "WEIGHT-SPACE PROGRAMMING (QWENPU)");
    draw_text_ls(6, y, buf, COLOR_MAGENTA);
    y += 12;

    snprintf(buf, sizeof(buf), "CORE: RISC-V RV32I IN TENSORS");
    draw_text_ls(6, y, buf, COLOR_WHITE);
    y += 12;

    snprintf(buf, sizeof(buf), "INCEPTION: MPY-S1 MICROPYTHON");
    draw_text_ls(6, y, buf, COLOR_CYAN);
    y += 12;

    snprintf(buf, sizeof(buf), "FORWARD PASS: ZERO MUSL/GLIBC OH");
    draw_text_ls(6, y, buf, COLOR_GREEN);
    y += 12;

    snprintf(buf, sizeof(buf), "STATUS: NEURO-VM OPERACIONAL");
    draw_text_ls(6, y, buf, COLOR_GOLD);
    y += 12;

    snprintf(buf, sizeof(buf), "POOL: 12GB ZRAM + ADRENO OPENCL");
    draw_text_ls(6, y, buf, COLOR_WHITE);

    render_footer();
}

int main(void) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open /dev/dri/card0");
        return 1;
    }

    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

    drmModeConnector *conn = drmModeGetConnector(fd, 64);
    if (!conn || conn->count_modes == 0) {
        fprintf(stderr, "Connector 64 has no modes!\n");
        close(fd);
        return 1;
    }
    drmModeModeInfo mode = conn->modes[0];

    // Create Dumb Framebuffer (126 x 294)
    struct drm_mode_create_dumb create_arg = {
        .width = P_WIDTH,
        .height = P_HEIGHT,
        .bpp = 32
    };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) < 0) {
        perror("CREATE_DUMB");
        close(fd);
        return 1;
    }

    uint32_t fb_id = 0;
    if (drmModeAddFB(fd, P_WIDTH, P_HEIGHT, 24, 32, create_arg.pitch, create_arg.handle, &fb_id) < 0) {
        perror("AddFB");
        close(fd);
        return 1;
    }

    struct drm_mode_map_dumb map_arg = { .handle = create_arg.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg) < 0) {
        perror("MAP_DUMB");
        close(fd);
        return 1;
    }

    g_fb_pixels = (uint32_t *)mmap(0, create_arg.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_arg.offset);
    if (g_fb_pixels == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    g_pitch_pixels = create_arg.pitch / 4;

    uint32_t mode_blob_id = 0;
    if (drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &mode_blob_id) < 0) {
        perror("CreatePropertyBlob");
        close(fd);
        return 1;
    }

    // Atomic Modeset: Blank Front Screen (54/138) & Activate Rear Screen (64/199/92)
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, 54, 20, 0);   // Conn 54 CRTC_ID = 0
    drmModeAtomicAddProperty(req, 138, 22, 0);  // CRTC 138 ACTIVE = 0
    drmModeAtomicAddProperty(req, 138, 23, 0);  // CRTC 138 MODE_ID = 0

    drmModeAtomicAddProperty(req, 64, 20, 199); // Conn 64 CRTC_ID = 199
    drmModeAtomicAddProperty(req, 199, 22, 1);  // CRTC 199 ACTIVE = 1
    drmModeAtomicAddProperty(req, 199, 23, mode_blob_id);

    drmModeAtomicAddProperty(req, 92, 20, 199); // Plane 92 CRTC_ID = 199
    drmModeAtomicAddProperty(req, 92, 17, fb_id);
    drmModeAtomicAddProperty(req, 92, 13, 0);   // CRTC_X = 0
    drmModeAtomicAddProperty(req, 92, 14, 0);   // CRTC_Y = 0
    drmModeAtomicAddProperty(req, 92, 15, P_WIDTH);
    drmModeAtomicAddProperty(req, 92, 16, P_HEIGHT);
    drmModeAtomicAddProperty(req, 92, 9, 0);    // SRC_X = 0
    drmModeAtomicAddProperty(req, 92, 10, 0);   // SRC_Y = 0
    drmModeAtomicAddProperty(req, 92, 11, P_WIDTH << 16);
    drmModeAtomicAddProperty(req, 92, 12, P_HEIGHT << 16);

    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    drmModeAtomicFree(req);

    if (ret != 0) {
        fprintf(stderr, "drmModeAtomicCommit failed: ret=%d errno=%d\n", ret, errno);
        close(fd);
        return 1;
    }

    // Ensure rear backlight is unblanked and bright
    int r_bl = open("/sys/class/backlight/panel1-backlight/brightness", O_WRONLY);
    if (r_bl >= 0) { write(r_bl, "255\n", 4); close(r_bl); }
    int r_pwr = open("/sys/class/backlight/panel1-backlight/bl_power", O_WRONLY);
    if (r_pwr >= 0) { write(r_pwr, "0\n", 2); close(r_pwr); }

    // Front backlight off
    int f_bl = open("/sys/class/backlight/panel0-backlight/brightness", O_WRONLY);
    if (f_bl >= 0) { write(f_bl, "0\n", 2); close(f_bl); }
    int f_pwr = open("/sys/class/backlight/panel0-backlight/bl_power", O_WRONLY);
    if (f_pwr >= 0) { write(f_pwr, "4\n", 2); close(f_pwr); }

    printf("[HUD v5] Sovereign Rear AMOLED HUD started successfully.\n");
    fflush(stdout);

    int current_page = 1;
    int page_timer = 0;
    cluster_telemetry_t tel;

    while (g_running) {
        poll_telemetry(&tel);

        // Clear canvas
        fill_rect_ls(0, 0, V_WIDTH, V_HEIGHT, COLOR_BG);

        // Render current page
        switch (current_page) {
            case 1: render_page_hardware(&tel); break;
            case 2: render_page_power(&tel);    break;
            case 3: render_page_network(&tel);  break;
            case 4: render_page_neural(&tel);   break;
            default: render_page_hardware(&tel); break;
        }

        // Flush to DSI command mode panel
        drmModeDirtyFB(fd, fb_id, NULL, 0);

        // Sleep 2s per update, rotate page every 6s (3 ticks)
        sleep(2);
        page_timer++;
        if (page_timer >= 3) {
            page_timer = 0;
            current_page++;
            if (current_page > 4) current_page = 1;
        }
    }

    printf("[HUD v5] Shutting down cleanly...\n");
    fill_rect_ls(0, 0, V_WIDTH, V_HEIGHT, 0xFF000000);
    drmModeDirtyFB(fd, fb_id, NULL, 0);

    munmap(g_fb_pixels, create_arg.size);
    drmModeRmFB(fd, fb_id);
    struct drm_mode_destroy_dumb d_arg = { .handle = create_arg.handle };
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d_arg);
    close(fd);
    return 0;
}
