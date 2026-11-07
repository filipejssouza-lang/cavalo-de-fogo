/*
 * src/camera/lupin_cam_shot.c - Capturador Soberano Bare-Metal HAL3 (Qualcomm CamX / Spectra 580)
 * ===============================================================================================
 * Opera diretamente sobre a ABI C do Android Camera HAL3 (camera.qcom.so -> símbolo HMI)
 * e Gralloc 4.0 (libui.so VNDK v30), sem Android Java, sem system_server e sem cameraserver.
 *
 * Modos:
 *   lupin_cam_shot probe [--hal /vendor/lib64/hw/camera.qcom.so]
 *   lupin_cam_shot shot --cam <id> --out <arquivo.jpg> [--width W --height H]
 *                       [--warmup N] [--quality Q] [--full] [--single-stream]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

/* ============================================================================
 * Definições exatas da ABI Android 11 (API 30 / HAL 3.5 - AArch64 LP64)
 * Verificadas contra desmontagem real de camera.qcom.so / libui.so / libcamera_metadata.so
 * ============================================================================ */

typedef struct native_handle {
    int version;
    int numFds;
    int numInts;
    int data[0];
} native_handle_t;

typedef const native_handle_t *buffer_handle_t;
typedef void camera_metadata_t;

enum {
    TYPE_BYTE     = 0,
    TYPE_INT32    = 1,
    TYPE_FLOAT    = 2,
    TYPE_INT64    = 3,
    TYPE_DOUBLE   = 4,
    TYPE_RATIONAL = 5
};

typedef struct camera_metadata_ro_entry {
    size_t   index;
    uint32_t tag;
    uint8_t  type;
    uint8_t  _pad[3];
    size_t   count;
    union {
        const uint8_t *u8;
        const int32_t *i32;
        const float   *f;
        const int64_t *i64;
        const double  *d;
    } data;
} camera_metadata_ro_entry_t;

_Static_assert(offsetof(camera_metadata_ro_entry_t, tag) == 8, "ro_entry.tag offset");
_Static_assert(offsetof(camera_metadata_ro_entry_t, type) == 12, "ro_entry.type offset");
_Static_assert(offsetof(camera_metadata_ro_entry_t, count) == 16, "ro_entry.count offset");
_Static_assert(offsetof(camera_metadata_ro_entry_t, data) == 24, "ro_entry.data offset");

struct hw_module_t;
struct hw_device_t;

typedef struct hw_module_methods_t {
    int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
} hw_module_methods_t;

typedef struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    hw_module_methods_t *methods;
    void *dso;
    uint64_t reserved[25];
} hw_module_t;

_Static_assert(sizeof(hw_module_t) == 248, "sizeof(hw_module_t) must be 248");

typedef struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device_t *device);
} hw_device_t;

_Static_assert(sizeof(hw_device_t) == 120, "sizeof(hw_device_t) must be 120");

typedef struct camera_info {
    int facing;
    int orientation;
    uint32_t device_version;
    uint32_t _pad0;
    const camera_metadata_t *static_camera_characteristics;
    int resource_cost;
    uint32_t _pad1;
    char **conflicting_devices;
    size_t conflicting_devices_length;
    void *reserved[4];
} camera_info_t;

typedef struct camera_module_callbacks {
    void (*camera_device_status_change)(const struct camera_module_callbacks *, int camera_id, int new_status);
    void (*torch_mode_status_change)(const struct camera_module_callbacks *, const char *camera_id, int new_status);
} camera_module_callbacks_t;

typedef struct vendor_tag_ops {
    int (*get_tag_count)(const struct vendor_tag_ops *v);
    void (*get_all_tags)(const struct vendor_tag_ops *v, uint32_t *tag_array);
    const char *(*get_section_name)(const struct vendor_tag_ops *v, uint32_t tag);
    const char *(*get_tag_name)(const struct vendor_tag_ops *v, uint32_t tag);
    int (*get_tag_type)(const struct vendor_tag_ops *v, uint32_t tag);
    void *reserved[8];
} vendor_tag_ops_t;

typedef struct camera_module {
    hw_module_t common;
    int (*get_number_of_cameras)(void);
    int (*get_camera_info)(int camera_id, camera_info_t *info);
    int (*set_callbacks)(const camera_module_callbacks_t *callbacks);
    void (*get_vendor_tag_ops)(vendor_tag_ops_t *ops);
    int (*open_legacy)(const hw_module_t *module, const char *id, uint32_t halVersion, hw_device_t **device);
    int (*set_torch_mode)(const char *camera_id, bool enabled);
    int (*init)(void);
    int (*get_physical_camera_info)(int physical_camera_id, camera_metadata_t **static_metadata);
    int (*is_stream_combination_supported)(int camera_id, const void *streams);
    void (*notify_device_state_change)(uint64_t deviceState);
    void *reserved[2];
} camera_module_t;

_Static_assert(offsetof(camera_module_t, get_camera_info) == 256, "get_camera_info offset");

/* Constantes HAL3 / Gralloc */
#define CAMERA3_STREAM_OUTPUT             0
#define HAL_PIXEL_FORMAT_BLOB             0x21
#define HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED 0x22
#define HAL_PIXEL_FORMAT_YCbCr_420_888    0x23

#define HAL_DATASPACE_UNKNOWN             0x0
#define HAL_DATASPACE_V0_JFIF             0x08c20000U

#define GRALLOC_USAGE_SW_READ_OFTEN       0x00000003U
#define GRALLOC_USAGE_HW_TEXTURE          0x00000100U
#define GRALLOC_USAGE_HW_CAMERA_WRITE     0x00020000U
#define GRALLOC_USAGE_HW_CAMERA_READ      0x00040000U

#define CAMERA3_TEMPLATE_PREVIEW          1
#define CAMERA3_TEMPLATE_STILL_CAPTURE    2

#define CAMERA3_BUFFER_STATUS_OK          0
#define CAMERA3_JPEG_BLOB_ID              0x00FFU

typedef struct camera3_stream {
    int stream_type;
    uint32_t width;
    uint32_t height;
    int format;
    uint32_t usage;
    uint32_t max_buffers;
    void *priv;
    uint32_t data_space;
    int rotation;
    const char *physical_camera_id;
    void *reserved[6];
} camera3_stream_t;

_Static_assert(offsetof(camera3_stream_t, format) == 12, "stream.format offset");
_Static_assert(offsetof(camera3_stream_t, data_space) == 32, "stream.data_space offset");

typedef struct camera3_stream_configuration {
    uint32_t num_streams;
    uint32_t _pad0;
    camera3_stream_t **streams;
    uint32_t operation_mode;
    uint32_t _pad1;
    const camera_metadata_t *session_parameters;
} camera3_stream_configuration_t;

typedef struct camera3_stream_buffer {
    camera3_stream_t *stream;
    buffer_handle_t *buffer;
    int status;
    int acquire_fence;
    int release_fence;
    int _pad0;
} camera3_stream_buffer_t;

_Static_assert(sizeof(camera3_stream_buffer_t) == 32, "sizeof(camera3_stream_buffer_t) must be 32");

typedef struct camera3_capture_request {
    uint32_t frame_number;
    uint32_t _pad0;
    const camera_metadata_t *settings;
    camera3_stream_buffer_t *input_buffer;
    uint32_t num_output_buffers;
    uint32_t _pad1;
    const camera3_stream_buffer_t *output_buffers;
    uint32_t num_physcam_settings;
    uint32_t _pad2;
    const char **physcam_ids;
    const camera_metadata_t **physcam_settings;
} camera3_capture_request_t;

typedef struct camera3_capture_result {
    uint32_t frame_number;
    uint32_t _pad0;
    const camera_metadata_t *result;
    uint32_t num_output_buffers;
    uint32_t _pad1;
    const camera3_stream_buffer_t *output_buffers;
    const camera3_stream_buffer_t *input_buffer;
    uint32_t partial_result;
    uint32_t num_physcam_metadata;
    const char **physcam_ids;
    const camera_metadata_t **physcam_metadata;
} camera3_capture_result_t;

typedef struct camera3_notify_msg {
    int type;
    int _pad0;
    uint64_t data[6];
} camera3_notify_msg_t;

typedef struct camera3_callback_ops {
    void (*process_capture_result)(const struct camera3_callback_ops *, const camera3_capture_result_t *);
    void (*notify)(const struct camera3_callback_ops *, const camera3_notify_msg_t *);
    void *request_stream_buffers;
    void *return_stream_buffers;
} camera3_callback_ops_t;

struct camera3_device;
typedef struct camera3_device_ops {
    int (*initialize)(const struct camera3_device *, const camera3_callback_ops_t *callback_ops);
    int (*configure_streams)(const struct camera3_device *, camera3_stream_configuration_t *stream_list);
    void *register_stream_buffers;
    const camera_metadata_t *(*construct_default_request_settings)(const struct camera3_device *, int type);
    int (*process_capture_request)(const struct camera3_device *, camera3_capture_request_t *request);
    void (*get_metadata_vendor_tag_ops)(const struct camera3_device *, vendor_tag_ops_t *ops);
    void (*dump)(const struct camera3_device *, int fd);
    int (*flush)(const struct camera3_device *);
    void *signal_stream_buffers;
    void *reserved[8];
} camera3_device_ops_t;

typedef struct camera3_device {
    hw_device_t common;
    camera3_device_ops_t *ops;
    void *priv;
} camera3_device_t;

_Static_assert(offsetof(camera3_device_t, ops) == 120, "camera3_device.ops offset");

typedef struct camera3_jpeg_blob {
    uint16_t jpeg_blob_id;
    uint32_t jpeg_size;
} camera3_jpeg_blob_t;

/* ============================================================================
 * Ponteiros dinâmicos para libcamera_metadata.so e libui.so (VNDK v30)
 * ============================================================================ */

static size_t (*fn_meta_entry_count)(const camera_metadata_t *) = NULL;
static int (*fn_meta_ro_entry)(const camera_metadata_t *, size_t, camera_metadata_ro_entry_t *) = NULL;
static int (*fn_meta_find_ro)(const camera_metadata_t *, uint32_t, camera_metadata_ro_entry_t *) = NULL;
static const char *(*fn_meta_sec_name)(uint32_t) = NULL;
static const char *(*fn_meta_tag_name)(uint32_t) = NULL;
static camera_metadata_t *(*fn_meta_clone)(const camera_metadata_t *) = NULL;
static camera_metadata_t *(*fn_meta_alloc)(size_t, size_t) = NULL;
static int (*fn_meta_append)(camera_metadata_t *, const camera_metadata_t *) = NULL;
static int (*fn_meta_update)(camera_metadata_t *, size_t, const void *, size_t, void *) = NULL;
static int (*fn_meta_add)(camera_metadata_t *, uint32_t, const void *, size_t) = NULL;
static void (*fn_meta_free)(camera_metadata_t *) = NULL;
static int (*fn_set_vendor_ops)(const vendor_tag_ops_t *) = NULL;
static size_t (*fn_meta_size)(const camera_metadata_t *) = NULL;

static void (*fn_gb_ctor)(void *self) = NULL;
static int (*fn_gb_realloc)(void *self, uint32_t w, uint32_t h, int32_t fmt, uint32_t layers, uint64_t usage) = NULL;
static int (*fn_gb_lock)(void *self, uint32_t usage, void **vaddr, int32_t *bpp, int32_t *stride) = NULL;
static int (*fn_gb_unlock)(void *self) = NULL;
static void (*fn_ref_inc)(const void *self, const void *id) = NULL;
static void (*fn_cfg_rpc_pool)(size_t max_threads, bool caller_will_join) = NULL;

static vendor_tag_ops_t g_vtag_ops;

static int load_runtime_libs(void) {
    void *h_meta = dlopen("libcamera_metadata.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h_meta) {
        fprintf(stderr, "[ERRO] dlopen libcamera_metadata.so: %s\n", dlerror());
        return -1;
    }
    fn_meta_entry_count = dlsym(h_meta, "get_camera_metadata_entry_count");
    fn_meta_ro_entry    = dlsym(h_meta, "get_camera_metadata_ro_entry");
    fn_meta_find_ro     = dlsym(h_meta, "find_camera_metadata_ro_entry");
    fn_meta_sec_name    = dlsym(h_meta, "get_camera_metadata_section_name");
    fn_meta_tag_name    = dlsym(h_meta, "get_camera_metadata_tag_name");
    fn_meta_clone       = dlsym(h_meta, "clone_camera_metadata");
    fn_meta_alloc       = dlsym(h_meta, "allocate_camera_metadata");
    fn_meta_append      = dlsym(h_meta, "append_camera_metadata");
    fn_meta_update      = dlsym(h_meta, "update_camera_metadata_entry");
    fn_meta_add         = dlsym(h_meta, "add_camera_metadata_entry");
    fn_meta_free        = dlsym(h_meta, "free_camera_metadata");
    fn_set_vendor_ops   = dlsym(h_meta, "set_camera_metadata_vendor_ops");
    fn_meta_size        = dlsym(h_meta, "get_camera_metadata_size");

    void *h_ui = dlopen("libui.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h_ui) {
        fprintf(stderr, "[ERRO] dlopen libui.so: %s\n", dlerror());
        return -1;
    }
    fn_gb_ctor    = dlsym(h_ui, "_ZN7android13GraphicBufferC1Ev");
    fn_gb_realloc = dlsym(h_ui, "_ZN7android13GraphicBuffer10reallocateEjjijm");
    fn_gb_lock    = dlsym(h_ui, "_ZN7android13GraphicBuffer4lockEjPPvPiS3_");
    fn_gb_unlock  = dlsym(h_ui, "_ZN7android13GraphicBuffer6unlockEv");

    void *h_utils = dlopen("libutils.so", RTLD_NOW | RTLD_GLOBAL);
    if (h_utils) {
        fn_ref_inc = dlsym(h_utils, "_ZNK7android7RefBase9incStrongEPKv");
    }

    void *h_hidl = dlopen("libhidlbase.so", RTLD_NOW | RTLD_GLOBAL);
    if (h_hidl) {
        fn_cfg_rpc_pool = dlsym(h_hidl, "_ZN7android8hardware22configureRpcThreadpoolEmb");
        if (fn_cfg_rpc_pool) {
            fn_cfg_rpc_pool(6, false);
        }
    }

    if (!fn_meta_entry_count || !fn_meta_ro_entry || !fn_gb_ctor || !fn_gb_realloc || !fn_gb_lock || !fn_gb_unlock) {
        fprintf(stderr, "[ERRO] Símbolos essenciais ausentes em libcamera_metadata.so / libui.so\n");
        return -1;
    }
    return 0;
}

static bool find_entry_by_name(const camera_metadata_t *meta, const char *sec, const char *name, camera_metadata_ro_entry_t *out) {
    if (!meta) return false;
    size_t n = fn_meta_entry_count(meta);
    for (size_t i = 0; i < n; i++) {
        camera_metadata_ro_entry_t e;
        memset(&e, 0, sizeof(e));
        if (fn_meta_ro_entry(meta, i, &e) == 0) {
            const char *s = fn_meta_sec_name ? fn_meta_sec_name(e.tag) : NULL;
            const char *t = fn_meta_tag_name ? fn_meta_tag_name(e.tag) : NULL;
            if (s && t && strcmp(s, sec) == 0 && strcmp(t, name) == 0) {
                *out = e;
                return true;
            }
        }
    }
    return false;
}

static camera_metadata_t *clone_and_set_u8(const camera_metadata_t *src, const char *sec, const char *name, uint8_t val) {
    if (!src) return NULL;
    camera_metadata_ro_entry_t e;
    if (!find_entry_by_name(src, sec, name, &e)) {
        return fn_meta_clone ? fn_meta_clone(src) : NULL;
    }
    size_t entries = fn_meta_entry_count(src) + 8;
    size_t bytes = (fn_meta_size ? fn_meta_size(src) : 16384) + 512;
    camera_metadata_t *dst = fn_meta_alloc(entries, bytes);
    if (!dst) return fn_meta_clone(src);
    if (fn_meta_append(dst, src) != 0) {
        fn_meta_free(dst);
        return fn_meta_clone(src);
    }
    camera_metadata_ro_entry_t de;
    if (find_entry_by_name(dst, sec, name, &de) && fn_meta_update) {
        fn_meta_update(dst, de.index, &val, 1, NULL);
    }
    return dst;
}

/* Dummy callbacks do módulo */
static void on_dev_status_change(const camera_module_callbacks_t *cb, int cam_id, int status) {
    (void)cb; (void)cam_id; (void)status;
}
static void on_torch_status_change(const camera_module_callbacks_t *cb, const char *cam_id, int status) {
    (void)cb; (void)cam_id; (void)status;
}
static const camera_module_callbacks_t g_mod_cbs = {
    .camera_device_status_change = on_dev_status_change,
    .torch_mode_status_change    = on_torch_status_change,
};

/* ============================================================================
 * Alocação de GraphicBuffer (Gralloc 4.0 / ION)
 * ============================================================================ */

typedef struct {
    void *gb_obj;
    buffer_handle_t *handle_slot;
    uint32_t alloc_w;
    uint32_t alloc_h;
    bool in_use;
} lupin_gbuf_t;

static int alloc_gbuf(lupin_gbuf_t *b, uint32_t w, uint32_t h, int32_t fmt, uint64_t usage) {
    memset(b, 0, sizeof(*b));
    void *mem = NULL;
    if (posix_memalign(&mem, 16, 1024) != 0 || !mem) return -1;
    memset(mem, 0, 1024);

    fn_gb_ctor(mem);
    if (fn_ref_inc) {
        fn_ref_inc(mem, mem);
    }

    int rc = fn_gb_realloc(mem, w, h, fmt, 1, usage);
    if (rc != 0) {
        fprintf(stderr, "[ERRO] GraphicBuffer::reallocate(%ux%u, fmt=0x%x, usage=0x%llx) falhou: %d\n",
                w, h, fmt, (unsigned long long)usage, rc);
        return -1;
    }

    /* Offset verificado em libui_vndk30.so: ANativeWindowBuffer @ +0x10, handle @ +0x70 */
    b->gb_obj = mem;
    b->handle_slot = (buffer_handle_t *)((uint8_t *)mem + 0x70);
    if (!b->handle_slot || !(*b->handle_slot)) {
        fprintf(stderr, "[ERRO] GraphicBuffer handle nulo após reallocate!\n");
        return -1;
    }
    b->alloc_w = w;
    b->alloc_h = h;
    b->in_use = false;
    return 0;
}

static void wait_and_close_fence(int fence_fd, int timeout_ms) {
    if (fence_fd < 0) return;
    struct pollfd pfd = { .fd = fence_fd, .events = POLLIN };
    poll(&pfd, 1, timeout_ms);
    close(fence_fd);
}

/* ============================================================================
 * Sincronização Assíncrona de Captura HAL3
 * ============================================================================ */

#define MAX_PREV_BUFS 20

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv  = PTHREAD_COND_INITIALIZER;

static camera3_stream_t g_prev_stream;
static camera3_stream_t g_jpeg_stream;
static lupin_gbuf_t     g_prev_bufs[MAX_PREV_BUFS];
static int              g_num_prev_bufs = 12;
static lupin_gbuf_t     g_jpeg_buf;

static bool g_jpeg_done   = false;
static int  g_jpeg_status = -1;
static int  g_jpeg_fence  = -1;
static int  g_prev_completed = 0;

static void hal3_process_capture_result(const camera3_callback_ops_t *ops, const camera3_capture_result_t *res) {
    (void)ops;
    if (!res) return;

    if (res->input_buffer && res->input_buffer->release_fence >= 0) {
        close(res->input_buffer->release_fence);
    }

    if (res->num_output_buffers == 0 || !res->output_buffers) return;

    pthread_mutex_lock(&g_mtx);
    for (uint32_t i = 0; i < res->num_output_buffers; i++) {
        const camera3_stream_buffer_t *sb = &res->output_buffers[i];
        if (sb->stream == &g_jpeg_stream) {
            g_jpeg_status = sb->status;
            g_jpeg_fence  = sb->release_fence;
            g_jpeg_done   = true;
            pthread_cond_broadcast(&g_cv);
        } else if (sb->stream == &g_prev_stream) {
            int fence = sb->release_fence;
            pthread_mutex_unlock(&g_mtx);
            wait_and_close_fence(fence, 1000);
            pthread_mutex_lock(&g_mtx);
            for (int k = 0; k < g_num_prev_bufs; k++) {
                if (sb->buffer == g_prev_bufs[k].handle_slot) {
                    g_prev_bufs[k].in_use = false;
                }
            }
            g_prev_completed++;
            pthread_cond_broadcast(&g_cv);
        } else if (sb->release_fence >= 0) {
            close(sb->release_fence);
        }
    }
    pthread_mutex_unlock(&g_mtx);
}

static void hal3_notify(const camera3_callback_ops_t *ops, const camera3_notify_msg_t *msg) {
    (void)ops;
    (void)msg;
}

static const camera3_callback_ops_t g_hal3_cbs = {
    .process_capture_result = hal3_process_capture_result,
    .notify                 = hal3_notify,
    .request_stream_buffers = NULL,
    .return_stream_buffers  = NULL,
};

/* ============================================================================
 * Inicialização do Módulo HAL3
 * ============================================================================ */

static camera_module_t *open_hal_module(const char *hal_path, int *out_num_cams) {
    void *dso = dlopen(hal_path, RTLD_NOW);
    if (!dso) {
        fprintf(stderr, "[ERRO] dlopen(%s) falhou: %s\n", hal_path, dlerror());
        return NULL;
    }
    camera_module_t *mod = (camera_module_t *)dlsym(dso, "HMI");
    if (!mod) {
        fprintf(stderr, "[ERRO] Símbolo HMI não encontrado em %s\n", hal_path);
        return NULL;
    }

    if (mod->common.module_api_version >= 0x0204 && mod->init) {
        int irc = mod->init();
        if (irc != 0) {
            fprintf(stderr, "[AVISO] mod->init() retornou %d\n", irc);
        }
    }

    if (mod->get_vendor_tag_ops) {
        memset(&g_vtag_ops, 0, sizeof(g_vtag_ops));
        mod->get_vendor_tag_ops(&g_vtag_ops);
        if (fn_set_vendor_ops && g_vtag_ops.get_tag_count) {
            fn_set_vendor_ops(&g_vtag_ops);
        }
    }

    int n = mod->get_number_of_cameras();
    if (mod->set_callbacks) {
        mod->set_callbacks(&g_mod_cbs);
    }
    if (out_num_cams) *out_num_cams = n;
    return mod;
}

/* ============================================================================
 * Comando: probe
 * ============================================================================ */

static int cmd_probe(const char *hal_path) {
    int num_cams = 0;
    camera_module_t *mod = open_hal_module(hal_path, &num_cams);
    if (!mod) return 1;

    printf("================================================================\n");
    printf(" LUPIN BARE-METAL CAMERA PROBE — %s\n", mod->common.name ? mod->common.name : "CamX HAL3");
    printf(" Total de câmeras expostas pelo HAL: %d\n", num_cams);
    printf("================================================================\n");

    for (int id = 0; id < num_cams; id++) {
        camera_info_t info;
        memset(&info, 0, sizeof(info));
        if (mod->get_camera_info(id, &info) != 0) {
            printf(" [Cam %d] Erro ao ler camera_info\n", id);
            continue;
        }

        const char *facing_str = (info.facing == 0) ? "TRASEIRA (BACK)" :
                                 (info.facing == 1) ? "FRONTAL (FRONT)" : "EXTERNA";

        int32_t px_w = 0, px_h = 0, max_jpeg = 0;
        float focal = 0.0f, aperture = 0.0f;
        camera_metadata_ro_entry_t e;

        if (find_entry_by_name(info.static_camera_characteristics, "android.sensor.info", "pixelArraySize", &e) && e.count >= 2) {
            px_w = e.data.i32[0];
            px_h = e.data.i32[1];
        }
        if (find_entry_by_name(info.static_camera_characteristics, "android.lens.info", "availableFocalLengths", &e) && e.count >= 1) {
            focal = e.data.f[0];
        }
        if (find_entry_by_name(info.static_camera_characteristics, "android.lens.info", "availableApertures", &e) && e.count >= 1) {
            aperture = e.data.f[0];
        }
        if (find_entry_by_name(info.static_camera_characteristics, "android.jpeg", "maxSize", &e) && e.count >= 1) {
            max_jpeg = e.data.i32[0];
        }

        double mp = ((double)px_w * (double)px_h) / 1000000.0;
        printf("\n [Câmera ID %d] %s | Rot=%d° | f=%.2fmm (f/%.2f) | Array=%dx%d (%.1f MP) | MaxJPEG=%d B\n",
               id, facing_str, info.orientation, focal, aperture, px_w, px_h, mp, max_jpeg);

        if (find_entry_by_name(info.static_camera_characteristics, "android.logicalMultiCamera", "physicalIds", &e) && e.count > 0) {
            printf("   Lógica Multi-Cam (IDs físicos: ");
            for (size_t k = 0; k < e.count; k++) {
                putchar(e.data.u8[k] ? (char)e.data.u8[k] : ' ');
            }
            printf(")\n");
        }

        if (find_entry_by_name(info.static_camera_characteristics, "android.scaler", "availableStreamConfigurations", &e)) {
            printf("   Resoluções JPEG (BLOB): ");
            int printed = 0;
            for (size_t k = 0; k + 3 < e.count; k += 4) {
                int32_t fmt = e.data.i32[k];
                int32_t w   = e.data.i32[k + 1];
                int32_t h   = e.data.i32[k + 2];
                int32_t dir = e.data.i32[k + 3];
                if (fmt == HAL_PIXEL_FORMAT_BLOB && dir == 0 && printed < 8) {
                    printf("%dx%d ", w, h);
                    printed++;
                }
            }
            printf("\n");
        }
    }
    printf("================================================================\n");
    fflush(stdout);
    _exit(0);
}

/* ============================================================================
 * Extração segura do JPEG a partir do buffer BLOB do Spectra 580
 * ============================================================================ */

static size_t extract_jpeg_slice(const uint8_t *buf, size_t alloc_sz, const uint8_t **out_start) {
    size_t start = 0;
    bool found_soi = false;
    for (size_t i = 0; i + 1 < alloc_sz && i < 256; i++) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8) {
            start = i;
            found_soi = true;
            break;
        }
    }
    if (!found_soi) return 0;
    *out_start = buf + start;

    /* 1. Verifica rodapé oficial camera3_jpeg_blob_t (8 bytes alinhados ou 6 bytes packed) */
    if (alloc_sz >= 8) {
        uint16_t id8 = 0;
        uint32_t sz8 = 0;
        memcpy(&id8, buf + alloc_sz - 8, 2);
        memcpy(&sz8, buf + alloc_sz - 4, 4);
        if (id8 == CAMERA3_JPEG_BLOB_ID && sz8 > 16 && start + sz8 <= alloc_sz) {
            return (size_t)sz8;
        }

        uint16_t id6 = 0;
        uint32_t sz6 = 0;
        memcpy(&id6, buf + alloc_sz - 6, 2);
        memcpy(&sz6, buf + alloc_sz - 4, 4);
        if (id6 == CAMERA3_JPEG_BLOB_ID && sz6 > 16 && start + sz6 <= alloc_sz) {
            return (size_t)sz6;
        }
    }

    /* 2. Fallback: procura marcador EOI (FF D9) do fim para o começo */
    for (size_t i = alloc_sz - 2; i > start + 2; i--) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD9) {
            return (i + 2) - start;
        }
    }
    return 0;
}

/* ============================================================================
 * Comando: shot
 * ============================================================================ */

static void select_resolutions(const camera_metadata_t *meta, bool want_full,
                               uint32_t *io_w, uint32_t *io_h,
                               uint32_t *out_prev_w, uint32_t *out_prev_h,
                               uint32_t *out_max_jpeg) {
    camera_metadata_ro_entry_t e;
    uint32_t max_jpeg = 24 * 1024 * 1024;
    if (find_entry_by_name(meta, "android.jpeg", "maxSize", &e) && e.count >= 1 && e.data.i32[0] > 0) {
        max_jpeg = (uint32_t)e.data.i32[0];
    }

    uint32_t best_w = 0, best_h = 0;
    uint64_t best_px = 0;
    uint32_t prev_w = 1280, prev_h = 960;
    bool found_prev = false;

    if (find_entry_by_name(meta, "android.scaler", "availableStreamConfigurations", &e)) {
        if (*io_w == 0 || *io_h == 0) {
            for (size_t k = 0; k + 3 < e.count; k += 4) {
                int32_t fmt = e.data.i32[k];
                uint32_t w  = (uint32_t)e.data.i32[k + 1];
                uint32_t h  = (uint32_t)e.data.i32[k + 2];
                int32_t dir = e.data.i32[k + 3];
                if (fmt == HAL_PIXEL_FORMAT_BLOB && dir == 0) {
                    uint64_t px = (uint64_t)w * (uint64_t)h;
                    if (!want_full && px > 18000000ULL && best_px > 0) {
                        continue;
                    }
                    if (px > best_px) {
                        best_px = px;
                        best_w = w;
                        best_h = h;
                    }
                }
            }
            if (best_w > 0 && best_h > 0) {
                *io_w = best_w;
                *io_h = best_h;
            }
        }

        /* Seleciona stream YUV de preview <= 1920x1440 com proporção próxima */
        uint64_t best_prev_px = 0;
        for (size_t k = 0; k + 3 < e.count; k += 4) {
            int32_t fmt = e.data.i32[k];
            uint32_t w  = (uint32_t)e.data.i32[k + 1];
            uint32_t h  = (uint32_t)e.data.i32[k + 2];
            int32_t dir = e.data.i32[k + 3];
            if (fmt == HAL_PIXEL_FORMAT_YCbCr_420_888 && dir == 0 && w <= 1920 && h <= 1440) {
                uint64_t px = (uint64_t)w * (uint64_t)h;
                if (px > best_prev_px) {
                    best_prev_px = px;
                    prev_w = w;
                    prev_h = h;
                    found_prev = true;
                }
            }
        }
    }

    if (*io_w == 0 || *io_h == 0) {
        *io_w = 4000;
        *io_h = 3000;
    }
    if (!found_prev) {
        prev_w = 1280;
        prev_h = 960;
    }
    /* Garante buffer BLOB amplo para resoluções altas */
    uint64_t min_blob = (uint64_t)(*io_w) * (uint64_t)(*io_h) * 3ULL / 2ULL;
    if (max_jpeg < min_blob && min_blob <= 64ULL * 1024 * 1024) {
        max_jpeg = (uint32_t)((min_blob + 4095ULL) & ~4095ULL);
    }

    *out_prev_w   = prev_w;
    *out_prev_h   = prev_h;
    *out_max_jpeg = max_jpeg;
}

static int cmd_shot(const char *hal_path, int cam_id, const char *out_path,
                    uint32_t req_w, uint32_t req_h, int warmup_frames,
                    uint8_t jpeg_quality, bool want_full, bool single_stream) {
    int num_cams = 0;
    camera_module_t *mod = open_hal_module(hal_path, &num_cams);
    if (!mod) return 1;

    if (cam_id < 0 || cam_id >= num_cams) {
        fprintf(stderr, "[ERRO] Câmera ID %d fora do intervalo (0..%d)\n", cam_id, num_cams - 1);
        _exit(1);
    }

    camera_info_t info;
    memset(&info, 0, sizeof(info));
    if (mod->get_camera_info(cam_id, &info) != 0) {
        fprintf(stderr, "[ERRO] get_camera_info(%d) falhou\n", cam_id);
        _exit(1);
    }

    uint32_t w = req_w, h = req_h, prev_w = 0, prev_h = 0, max_jpeg = 0;
    select_resolutions(info.static_camera_characteristics, want_full, &w, &h, &prev_w, &prev_h, &max_jpeg);

    printf("[*] Abrindo Câmera ID %d via Spectra 580 ISP (%ux%u JPEG, Preview %ux%u, Warmup=%d)...\n",
           cam_id, w, h, prev_w, prev_h, warmup_frames);

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%d", cam_id);

    hw_device_t *hw_dev = NULL;
    int rc = mod->common.methods->open(&mod->common, id_str, &hw_dev);
    if (rc != 0 || !hw_dev) {
        fprintf(stderr, "[ERRO] module->open(%s) falhou: %d\n", id_str, rc);
        _exit(1);
    }

    camera3_device_t *dev = (camera3_device_t *)hw_dev;
    rc = dev->ops->initialize(dev, &g_hal3_cbs);
    if (rc != 0) {
        fprintf(stderr, "[ERRO] dev->ops->initialize() falhou: %d\n", rc);
        dev->common.close(&dev->common);
        _exit(1);
    }

    const camera_metadata_t *prev_tpl = dev->ops->construct_default_request_settings(dev, CAMERA3_TEMPLATE_PREVIEW);
    const camera_metadata_t *still_tpl = dev->ops->construct_default_request_settings(dev, CAMERA3_TEMPLATE_STILL_CAPTURE);
    if (!still_tpl) still_tpl = prev_tpl;
    if (!still_tpl) {
        fprintf(stderr, "[ERRO] construct_default_request_settings retornou NULL\n");
        dev->common.close(&dev->common);
        _exit(1);
    }

    memset(&g_prev_stream, 0, sizeof(g_prev_stream));
    g_prev_stream.stream_type = CAMERA3_STREAM_OUTPUT;
    g_prev_stream.width       = prev_w;
    g_prev_stream.height      = prev_h;
    g_prev_stream.format      = HAL_PIXEL_FORMAT_YCbCr_420_888;
    g_prev_stream.usage       = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE;
    g_prev_stream.data_space  = HAL_DATASPACE_V0_JFIF;

    memset(&g_jpeg_stream, 0, sizeof(g_jpeg_stream));
    g_jpeg_stream.stream_type = CAMERA3_STREAM_OUTPUT;
    g_jpeg_stream.width       = w;
    g_jpeg_stream.height      = h;
    g_jpeg_stream.format      = HAL_PIXEL_FORMAT_BLOB;
    g_jpeg_stream.usage       = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE;
    g_jpeg_stream.data_space  = HAL_DATASPACE_V0_JFIF;

    bool dual_stream = !single_stream;
    camera3_stream_t *streams_dual[2] = { &g_prev_stream, &g_jpeg_stream };
    camera3_stream_t *streams_one[1]  = { &g_jpeg_stream };

    camera3_stream_configuration_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.num_streams = dual_stream ? 2 : 1;
    cfg.streams     = dual_stream ? streams_dual : streams_one;
    cfg.operation_mode = 0;
    cfg.session_parameters = prev_tpl ? prev_tpl : still_tpl;

    rc = dev->ops->configure_streams(dev, &cfg);
    if (rc != 0 && dual_stream) {
        fprintf(stderr, "[AVISO] configure_streams dual falhou (%d); tentando stream único BLOB...\n", rc);
        dual_stream = false;
        memset(&cfg, 0, sizeof(cfg));
        cfg.num_streams = 1;
        cfg.streams     = streams_one;
        cfg.session_parameters = still_tpl;
        rc = dev->ops->configure_streams(dev, &cfg);
    }
    if (rc != 0) {
        fprintf(stderr, "[ERRO] configure_streams falhou: %d\n", rc);
        dev->common.close(&dev->common);
        _exit(1);
    }

    /* Aloca buffers Gralloc respeitando max_buffers negociado pelo CamX */
    if (dual_stream) {
        int needed = (int)g_prev_stream.max_buffers + 4;
        if (needed < 12) needed = 12;
        if (needed > MAX_PREV_BUFS) needed = MAX_PREV_BUFS;
        g_num_prev_bufs = needed;

        uint64_t p_usage = (uint64_t)g_prev_stream.usage | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE;
        for (int i = 0; i < g_num_prev_bufs; i++) {
            if (alloc_gbuf(&g_prev_bufs[i], prev_w, prev_h, HAL_PIXEL_FORMAT_YCbCr_420_888, p_usage) != 0) {
                dev->common.close(&dev->common);
                _exit(1);
            }
        }
    }

    uint64_t j_usage = (uint64_t)g_jpeg_stream.usage | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE;
    if (alloc_gbuf(&g_jpeg_buf, max_jpeg, 1, HAL_PIXEL_FORMAT_BLOB, j_usage) != 0) {
        dev->common.close(&dev->common);
        _exit(1);
    }

    uint32_t frame_no = 0;

    /* Fase 1: Warmup de 3A (Auto-Exposição, Balanço de Branco, Auto-Foco e OIS) */
    if (dual_stream && warmup_frames > 0 && prev_tpl) {
        for (int f = 0; f < warmup_frames; f++) {
            int slot = -1;
            pthread_mutex_lock(&g_mtx);
            while (slot < 0) {
                for (int k = 0; k < g_num_prev_bufs; k++) {
                    if (!g_prev_bufs[k].in_use) {
                        slot = k;
                        g_prev_bufs[k].in_use = true;
                        break;
                    }
                }
                if (slot < 0) {
                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_sec += 3;
                    if (pthread_cond_timedwait(&g_cv, &g_mtx, &ts) == ETIMEDOUT) {
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&g_mtx);

            if (slot < 0) {
                fprintf(stderr, "[AVISO] Timeout aguardando buffer de preview no frame %d\n", f);
                break;
            }

            camera3_stream_buffer_t sb_prev = {
                .stream        = &g_prev_stream,
                .buffer        = g_prev_bufs[slot].handle_slot,
                .status        = CAMERA3_BUFFER_STATUS_OK,
                .acquire_fence = -1,
                .release_fence = -1,
            };

            camera3_capture_request_t req;
            memset(&req, 0, sizeof(req));
            req.frame_number       = frame_no++;
            req.settings           = (f == 0) ? prev_tpl : NULL;
            req.num_output_buffers = 1;
            req.output_buffers     = &sb_prev;

            rc = dev->ops->process_capture_request(dev, &req);
            if (rc != 0) {
                fprintf(stderr, "[AVISO] process_capture_request(preview %d) retornou %d\n", f, rc);
                pthread_mutex_lock(&g_mtx);
                g_prev_bufs[slot].in_use = false;
                pthread_mutex_unlock(&g_mtx);
                break;
            }
            /* Ritmo suave (~30ms) para convergência real de exposição e foco */
            usleep(30000);
        }

        /* Aguarda retorno efetivo dos quadros de warmup do ISP antes do disparo still */
        int target_done = (warmup_frames > 4) ? (warmup_frames - 3) : 1;
        pthread_mutex_lock(&g_mtx);
        while (g_prev_completed < target_done) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 3;
            if (pthread_cond_timedwait(&g_cv, &g_mtx, &ts) == ETIMEDOUT) {
                break;
            }
        }
        pthread_mutex_unlock(&g_mtx);
    }

    /* Fase 2: Disparo Still JPEG de Hardware (Spectra 580 JPEG Encoder) */
    camera_metadata_t *still_meta = clone_and_set_u8(still_tpl, "android.jpeg", "quality", jpeg_quality);
    const camera_metadata_t *use_meta = still_meta ? still_meta : still_tpl;

    camera3_stream_buffer_t out_bufs[2];
    uint32_t n_out = 0;

    if (dual_stream) {
        int slot = -1;
        pthread_mutex_lock(&g_mtx);
        while (slot < 0) {
            for (int k = 0; k < g_num_prev_bufs; k++) {
                if (!g_prev_bufs[k].in_use) {
                    slot = k;
                    g_prev_bufs[k].in_use = true;
                    break;
                }
            }
            if (slot < 0) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 3;
                if (pthread_cond_timedwait(&g_cv, &g_mtx, &ts) == ETIMEDOUT) break;
            }
        }
        pthread_mutex_unlock(&g_mtx);

        if (slot >= 0) {
            out_bufs[n_out++] = (camera3_stream_buffer_t){
                .stream        = &g_prev_stream,
                .buffer        = g_prev_bufs[slot].handle_slot,
                .status        = CAMERA3_BUFFER_STATUS_OK,
                .acquire_fence = -1,
                .release_fence = -1,
            };
        }
    }

    out_bufs[n_out++] = (camera3_stream_buffer_t){
        .stream        = &g_jpeg_stream,
        .buffer        = g_jpeg_buf.handle_slot,
        .status        = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    };

    camera3_capture_request_t snap_req;
    memset(&snap_req, 0, sizeof(snap_req));
    snap_req.frame_number       = frame_no++;
    snap_req.settings           = use_meta;
    snap_req.num_output_buffers = n_out;
    snap_req.output_buffers     = out_bufs;

    rc = dev->ops->process_capture_request(dev, &snap_req);
    if (rc != 0) {
        fprintf(stderr, "[ERRO] process_capture_request(STILL_CAPTURE) falhou: %d\n", rc);
        dev->ops->flush(dev);
        dev->common.close(&dev->common);
        _exit(1);
    }

    pthread_mutex_lock(&g_mtx);
    while (!g_jpeg_done) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 10;
        if (pthread_cond_timedwait(&g_cv, &g_mtx, &ts) == ETIMEDOUT) {
            break;
        }
    }
    bool ok_done = g_jpeg_done;
    int j_status = g_jpeg_status;
    int j_fence  = g_jpeg_fence;
    pthread_mutex_unlock(&g_mtx);

    if (!ok_done || j_status != CAMERA3_BUFFER_STATUS_OK) {
        fprintf(stderr, "[ERRO] Captura JPEG não completou (done=%d, status=%d)\n", ok_done ? 1 : 0, j_status);
        dev->ops->flush(dev);
        dev->common.close(&dev->common);
        _exit(1);
    }

    wait_and_close_fence(j_fence, 5000);

    void *vaddr = NULL;
    int32_t bpp = 0, stride = 0;
    rc = fn_gb_lock(g_jpeg_buf.gb_obj, GRALLOC_USAGE_SW_READ_OFTEN, &vaddr, &bpp, &stride);
    if (rc != 0 || !vaddr) {
        fprintf(stderr, "[ERRO] GraphicBuffer::lock() no JPEG falhou: %d\n", rc);
        dev->ops->flush(dev);
        dev->common.close(&dev->common);
        _exit(1);
    }

    const uint8_t *jpeg_ptr = NULL;
    size_t jpeg_len = extract_jpeg_slice((const uint8_t *)vaddr, (size_t)max_jpeg, &jpeg_ptr);
    if (jpeg_len == 0 || !jpeg_ptr) {
        fprintf(stderr, "[ERRO] Marcadores JPEG SOI/EOI não encontrados no buffer BLOB!\n");
        fn_gb_unlock(g_jpeg_buf.gb_obj);
        dev->ops->flush(dev);
        dev->common.close(&dev->common);
        _exit(1);
    }

    int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out_fd < 0) {
        perror("[ERRO] open arquivo de saída");
        fn_gb_unlock(g_jpeg_buf.gb_obj);
        dev->ops->flush(dev);
        dev->common.close(&dev->common);
        _exit(1);
    }

    size_t written = 0;
    while (written < jpeg_len) {
        ssize_t nw = write(out_fd, jpeg_ptr + written, jpeg_len - written);
        if (nw <= 0) break;
        written += (size_t)nw;
    }
    fsync(out_fd);
    close(out_fd);

    fn_gb_unlock(g_jpeg_buf.gb_obj);
    if (still_meta && fn_meta_free) fn_meta_free(still_meta);

    dev->ops->flush(dev);
    dev->common.close(&dev->common);

    printf("[✓] Foto capturada com sucesso: %s (%zu bytes, %ux%u)\n", out_path, written, w, h);
    fflush(stdout);
    _exit(0);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "Uso:\n"
                "  %s probe [--hal <so>]\n"
                "  %s shot --cam <id> --out <arquivo.jpg> [--width W --height H] [--warmup N] [--quality Q] [--full] [--single-stream]\n",
                argv[0], argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *hal_path = "/vendor/lib64/hw/camera.qcom.so";
    const char *out_path = "/tmp/shot.jpg";
    int cam_id = 0;
    uint32_t req_w = 0, req_h = 0;
    int warmup = 15;
    int quality = 95;
    bool want_full = false;
    bool single_stream = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--hal") == 0 && i + 1 < argc) {
            hal_path = argv[++i];
        } else if (strcmp(argv[i], "--cam") == 0 && i + 1 < argc) {
            cam_id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            req_w = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            req_h = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
            quality = atoi(argv[++i]);
            if (quality < 1) quality = 1;
            if (quality > 100) quality = 100;
        } else if (strcmp(argv[i], "--full") == 0) {
            want_full = true;
        } else if (strcmp(argv[i], "--single-stream") == 0) {
            single_stream = true;
        }
    }

    if (load_runtime_libs() != 0) {
        return 1;
    }

    if (strcmp(mode, "probe") == 0) {
        return cmd_probe(hal_path);
    } else if (strcmp(mode, "shot") == 0) {
        return cmd_shot(hal_path, cam_id, out_path, req_w, req_h, warmup, (uint8_t)quality, want_full, single_stream);
    }

    fprintf(stderr, "[ERRO] Modo desconhecido: %s\n", mode);
    return 1;
}
