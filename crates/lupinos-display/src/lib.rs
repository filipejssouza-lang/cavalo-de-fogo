//! 🐺 Sovereign Linux DRM/KMS engine for Xiaomi Mi 11 Ultra secondary rear AMOLED display (126x294)
//! Directly communicates with `/dev/dri/card0` (Connector 64 / CRTC 208) using double-buffered
//! DRM dumb buffers (`CREATE_DUMB`, `ADDFB`, `MAP_DUMB`, `SETCRTC`, `DIRTYFB`, `PAGE_FLIP`).

use std::fs::{File, OpenOptions};
use std::io::Write;
use std::os::unix::io::AsRawFd;
use std::path::Path;
use std::ptr;

pub const REAR_WIDTH: usize = 126;
pub const REAR_HEIGHT: usize = 294;
const REAR_CONNECTOR_ID: u32 = 64;
const REAR_CRTC_ID: u32 = 208;

// Linux DRM ioctl structures (matching `include/uapi/drm/drm_mode.h`)
#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeCreateDumb {
    height: u32,
    width: u32,
    bpp: u32,
    flags: u32,
    handle: u32,
    pitch: u32,
    size: u64,
}

#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeMapDumb {
    handle: u32,
    pad: u32,
    offset: u64,
}

#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeDestroyDumb {
    handle: u32,
}

#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeFbCmd {
    fb_id: u32,
    width: u32,
    height: u32,
    pitch: u32,
    bpp: u32,
    depth: u32,
    handle: u32,
}

#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeFbDirtyCmd {
    fb_id: u32,
    flags: u32,
    color: u32,
    num_clips: u32,
    clips_ptr: u64,
}

#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeCrtcPageFlip {
    crtc_id: u32,
    fb_id: u32,
    flags: u32,
    reserved: u32,
    user_data: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct DrmModeModeInfo {
    clock: u32,
    hdisplay: u16,
    hsync_start: u16,
    hsync_end: u16,
    htotal: u16,
    hskew: u16,
    vdisplay: u16,
    vsync_start: u16,
    vsync_end: u16,
    vtotal: u16,
    vscan: u16,
    vrefresh: u32,
    flags: u32,
    mode_type: u32,
    name: [u8; 32],
}

impl Default for DrmModeModeInfo {
    fn default() -> Self {
        unsafe { std::mem::zeroed() }
    }
}

#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeGetConnector {
    encoders_ptr: u64,
    modes_ptr: u64,
    props_ptr: u64,
    prop_values_ptr: u64,
    count_modes: u32,
    count_props: u32,
    count_encoders: u32,
    encoder_id: u32,
    connector_id: u32,
    connector_type: u32,
    connector_type_id: u32,
    connection: u32,
    mm_width: u32,
    mm_height: u32,
    subpixel: u32,
    pad: u32,
}

#[repr(C)]
#[derive(Default, Copy, Clone)]
struct DrmModeCrtc {
    set_connectors_ptr: u64,
    count_connectors: u32,
    crtc_id: u32,
    fb_id: u32,
    x: u32,
    y: u32,
    gamma_size: u32,
    mode_valid: u32,
    mode: DrmModeModeInfo,
}

// 64-bit Linux DRM ioctl numbers ('d' = 0x64)
const DRM_IOCTL_MODE_SETCRTC: libc::c_int = 0xc06864a2_u32 as libc::c_int;
const DRM_IOCTL_MODE_GETCONNECTOR: libc::c_int = 0xc05064a7_u32 as libc::c_int;
const DRM_IOCTL_MODE_ADDFB: libc::c_int = 0xc01c64ae_u32 as libc::c_int;
const DRM_IOCTL_MODE_RMFB: libc::c_int = 0xc00464af_u32 as libc::c_int;
const DRM_IOCTL_MODE_PAGE_FLIP: libc::c_int = 0xc01864b0_u32 as libc::c_int;
const DRM_IOCTL_MODE_DIRTYFB: libc::c_int = 0xc01864b1_u32 as libc::c_int;
const DRM_IOCTL_MODE_CREATE_DUMB: libc::c_int = 0xc02064b2_u32 as libc::c_int;
const DRM_IOCTL_MODE_MAP_DUMB: libc::c_int = 0xc01064b3_u32 as libc::c_int;
const DRM_IOCTL_MODE_DESTROY_DUMB: libc::c_int = 0xc00464b4_u32 as libc::c_int;

pub struct TelemetryData {
    pub title: String,
    pub ip: String,
    pub cpu_temp: f32,
    pub battery_temp: f32,
    pub ram_free_gb: f32,
    pub zram_free_gb: f32,
    pub gflops: f32,
    pub moe_status: String,
}

pub struct RearDisplay {
    buffer: Vec<u32>, // ARGB8888 pixels
    drm_file: Option<File>,
    handles: [u32; 2],
    fb_ids: [u32; 2],
    mapped_ptrs: [*mut u8; 2],
    pitch: usize,
    map_size: usize,
    back_idx: usize,
    drm_active: bool,
    backlight_path: &'static str,
}

unsafe impl Send for RearDisplay {}

impl RearDisplay {
    pub fn new() -> Self {
        let mut disp = Self {
            buffer: vec![0xFF050B14; REAR_WIDTH * REAR_HEIGHT],
            drm_file: None,
            handles: [0; 2],
            fb_ids: [0; 2],
            mapped_ptrs: [ptr::null_mut(); 2],
            pitch: REAR_WIDTH * 4,
            map_size: REAR_WIDTH * REAR_HEIGHT * 4,
            back_idx: 0,
            drm_active: false,
            backlight_path: "/sys/class/backlight/panel1-backlight/brightness",
        };

        // Respect external C HUD daemon if `LUPIN_DISABLE_RUST_HUD=1` is set
        if std::env::var("LUPIN_DISABLE_RUST_HUD").ok().as_deref() == Some("1") {
            return disp;
        }

        if let Ok(file) = OpenOptions::new().read(true).write(true).open("/dev/dri/card0") {
            let fd = file.as_raw_fd();
            let mut mode = DrmModeModeInfo::default();
            let mut conn = DrmModeGetConnector {
                connector_id: REAR_CONNECTOR_ID,
                modes_ptr: (&mut mode as *mut DrmModeModeInfo) as u64,
                count_modes: 1,
                ..Default::default()
            };
            unsafe {
                let _ = libc::ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &mut conn);
            }

            let mut ok = true;
            for i in 0..2 {
                let mut create = DrmModeCreateDumb {
                    width: REAR_WIDTH as u32,
                    height: REAR_HEIGHT as u32,
                    bpp: 32,
                    ..Default::default()
                };
                let rc = unsafe { libc::ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &mut create) };
                if rc < 0 {
                    ok = false;
                    break;
                }
                if i == 0 {
                    disp.pitch = create.pitch as usize;
                    disp.map_size = create.size as usize;
                }
                disp.handles[i] = create.handle;

                let mut add_fb = DrmModeFbCmd {
                    width: REAR_WIDTH as u32,
                    height: REAR_HEIGHT as u32,
                    pitch: create.pitch,
                    bpp: 32,
                    depth: 24,
                    handle: create.handle,
                    ..Default::default()
                };
                if unsafe { libc::ioctl(fd, DRM_IOCTL_MODE_ADDFB, &mut add_fb) } == 0 {
                    disp.fb_ids[i] = add_fb.fb_id;
                }

                let mut map_arg = DrmModeMapDumb {
                    handle: create.handle,
                    ..Default::default()
                };
                if unsafe { libc::ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mut map_arg) } < 0 {
                    ok = false;
                    break;
                }

                let ptr = unsafe {
                    libc::mmap(
                        ptr::null_mut(),
                        disp.map_size,
                        libc::PROT_READ | libc::PROT_WRITE,
                        libc::MAP_SHARED,
                        fd,
                        map_arg.offset as libc::off_t,
                    )
                };
                if ptr == libc::MAP_FAILED {
                    ok = false;
                    break;
                }
                disp.mapped_ptrs[i] = ptr as *mut u8;
            }

            if ok && disp.fb_ids[0] != 0 {
                let mut conn_id = REAR_CONNECTOR_ID;
                let mut set_crtc = DrmModeCrtc {
                    set_connectors_ptr: (&mut conn_id as *mut u32) as u64,
                    count_connectors: 1,
                    crtc_id: REAR_CRTC_ID,
                    fb_id: disp.fb_ids[0],
                    mode_valid: 1,
                    mode,
                    ..Default::default()
                };
                if unsafe { libc::ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &mut set_crtc) } >= 0 {
                    disp.drm_active = true;
                    disp.back_idx = 1;
                }
            }
            disp.drm_file = Some(file);
        }

        disp
    }

    pub fn set_backlight(&self, level: u8) {
        if Path::new(self.backlight_path).exists() {
            if let Ok(mut f) = OpenOptions::new().write(true).open(self.backlight_path) {
                let _ = writeln!(f, "{}", level);
            }
        }
    }

    pub fn clear(&mut self, color: u32) {
        for p in self.buffer.iter_mut() {
            *p = color;
        }
    }

    pub fn draw_pixel(&mut self, x: usize, y: usize, color: u32) {
        if x < REAR_WIDTH && y < REAR_HEIGHT {
            self.buffer[y * REAR_WIDTH + x] = color;
        }
    }

    pub fn draw_rect(&mut self, x: usize, y: usize, w: usize, h: usize, color: u32) {
        for cy in y..(y + h).min(REAR_HEIGHT) {
            for cx in x..(x + w).min(REAR_WIDTH) {
                self.draw_pixel(cx, cy, color);
            }
        }
    }

    pub fn draw_char(&mut self, c: char, x: usize, y: usize, color: u32) {
        let glyph = get_glyph(c);
        for row in 0..7 {
            let bits = glyph[row];
            for col in 0..5 {
                if (bits & (1 << (4 - col))) != 0 {
                    self.draw_pixel(x + col, y + row, color);
                }
            }
        }
    }

    pub fn draw_text(&mut self, text: &str, mut x: usize, y: usize, color: u32) {
        for c in text.chars() {
            self.draw_char(c, x, y, color);
            x += 6;
            if x + 5 >= REAR_WIDTH {
                break;
            }
        }
    }

    pub fn present(&mut self) {
        let idx = self.back_idx;
        let dst_base = self.mapped_ptrs[idx];
        if dst_base.is_null() {
            return;
        }

        unsafe {
            for y in 0..REAR_HEIGHT {
                let src_row = self.buffer.as_ptr().add(y * REAR_WIDTH);
                let dst_row = dst_base.add(y * self.pitch) as *mut u32;
                ptr::copy_nonoverlapping(src_row, dst_row, REAR_WIDTH);
            }
        }

        if let Some(ref file) = self.drm_file {
            let fd = file.as_raw_fd();
            let fb_id = self.fb_ids[idx];
            if fb_id != 0 {
                let mut dirty = DrmModeFbDirtyCmd {
                    fb_id,
                    ..Default::default()
                };
                unsafe {
                    let _ = libc::ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &mut dirty);
                }

                if self.drm_active {
                    let mut flip = DrmModeCrtcPageFlip {
                        crtc_id: REAR_CRTC_ID,
                        fb_id,
                        flags: 0,
                        ..Default::default()
                    };
                    unsafe {
                        let _ = libc::ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &mut flip);
                    }
                }
                self.back_idx = (self.back_idx + 1) % 2;
            }
        }
    }

    pub fn render_telemetry(&mut self, data: &TelemetryData) {
        self.clear(0xFF070C18);

        self.draw_rect(0, 0, REAR_WIDTH, 2, 0xFF00FFCC);
        self.draw_rect(0, 32, REAR_WIDTH, 1, 0xFF1A3050);

        self.draw_text(&data.title, 8, 8, 0xFF00FFCC);
        self.draw_text(&data.ip, 8, 20, 0xFF88AACC);

        let mut y = 42;

        self.draw_text("CORE 7 (X1):", 6, y, 0xFFFFAA00);
        y += 12;
        let gflops_str = format!("{:.1} GFLOPS", data.gflops);
        self.draw_text(&gflops_str, 12, y, 0xFFFFFFFF);
        y += 18;

        self.draw_text("TEMPERATURA:", 6, y, 0xFF00EEFF);
        y += 12;
        let temp_str = format!("CPU:{:.1}C", data.cpu_temp);
        self.draw_text(&temp_str, 12, y, 0xFFFFFFFF);
        y += 12;
        let pelt_str = format!("BAT:{:.1}C", data.battery_temp);
        self.draw_text(&pelt_str, 12, y, 0xFF88EE88);
        y += 18;

        self.draw_text("MEMORIA (24G):", 6, y, 0xFFEE88FF);
        y += 12;
        let ram_str = format!("RAM:{:.1}G LV", data.ram_free_gb);
        self.draw_text(&ram_str, 12, y, 0xFFFFFFFF);
        y += 12;
        let zram_str = format!("ZRM:{:.1}G LV", data.zram_free_gb);
        self.draw_text(&zram_str, 12, y, 0xFFDD99EE);
        y += 18;

        self.draw_text("HEXAGON NPU:", 6, y, 0xFF00FF88);
        y += 12;
        self.draw_text("26 TOPS @ 2W", 12, y, 0xFFFFFFFF);
        y += 18;

        self.draw_text("ROTEADOR MoE:", 6, y, 0xFFFF0055);
        y += 12;
        self.draw_text(&data.moe_status, 12, y, 0xFF00FFCC);

        self.draw_rect(0, REAR_HEIGHT - 3, REAR_WIDTH, 3, 0xFF00FFCC);

        // Flush and page-flip to physical DSI-2 panel
        self.present();
    }
}

impl Drop for RearDisplay {
    fn drop(&mut self) {
        if let Some(ref file) = self.drm_file {
            let fd = file.as_raw_fd();
            for i in 0..2 {
                if !self.mapped_ptrs[i].is_null() {
                    unsafe {
                        let _ = libc::munmap(self.mapped_ptrs[i] as *mut libc::c_void, self.map_size);
                    }
                    self.mapped_ptrs[i] = ptr::null_mut();
                }
                if self.fb_ids[i] != 0 {
                    let mut fb_id = self.fb_ids[i];
                    unsafe {
                        let _ = libc::ioctl(fd, DRM_IOCTL_MODE_RMFB, &mut fb_id);
                    }
                }
                if self.handles[i] != 0 {
                    let mut destroy = DrmModeDestroyDumb {
                        handle: self.handles[i],
                    };
                    unsafe {
                        let _ = libc::ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &mut destroy);
                    }
                }
            }
        }
    }
}

// Built-in 5x7 ASCII bitmap font data for standalone execution
fn get_glyph(c: char) -> [u8; 7] {
    match c {
        '0'..='9' => NUMBERS[(c as u8 - b'0') as usize],
        'A'..='Z' => LETTERS_UPPER[(c as u8 - b'A') as usize],
        'a'..='z' => LETTERS_UPPER[(c as u8 - b'a') as usize],
        ':' => [0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00],
        '.' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00],
        '-' => [0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00],
        '(' => [0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02],
        ')' => [0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08],
        '@' => [0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0F],
        ' ' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
        _ => [0x1F, 0x11, 0x15, 0x15, 0x15, 0x11, 0x1F],
    }
}

const NUMBERS: [[u8; 7]; 10] = [
    [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E], // 0
    [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E], // 1
    [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F], // 2
    [0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E], // 3
    [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02], // 4
    [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E], // 5
    [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E], // 6
    [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08], // 7
    [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E], // 8
    [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C], // 9
];

const LETTERS_UPPER: [[u8; 7]; 26] = [
    [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11], // A
    [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E], // B
    [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E], // C
    [0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C], // D
    [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F], // E
    [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10], // F
    [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F], // G
    [0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11], // H
    [0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E], // I
    [0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C], // J
    [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11], // K
    [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F], // L
    [0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11], // M
    [0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11], // N
    [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E], // O
    [0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10], // P
    [0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D], // Q
    [0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11], // R
    [0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E], // S
    [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04], // T
    [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E], // U
    [0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04], // V
    [0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11], // W
    [0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11], // X
    [0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04], // Y
    [0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F], // Z
];
