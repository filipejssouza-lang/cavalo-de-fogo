//! 🐺 Sovereign Linux DRM/KMS engine for Xiaomi Mi 11 Ultra secondary rear AMOLED display (126x294)
//! Directly communicates with `/dev/dri/card0` and `/sys/class/backlight/panel1-backlight`.

use std::fs::{File, OpenOptions};
use std::io::Write;
use std::os::unix::io::{AsRawFd, RawFd};
use std::path::Path;

pub const REAR_WIDTH: usize = 126;
pub const REAR_HEIGHT: usize = 294;

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
    backlight_path: &'static str,
}

impl RearDisplay {
    pub fn new() -> Self {
        let drm_file = OpenOptions::new()
            .read(true)
            .write(true)
            .open("/dev/dri/card0")
            .ok();

        Self {
            buffer: vec![0xFF050B14; REAR_WIDTH * REAR_HEIGHT], // Dark cyberpunk blue-black
            drm_file,
            backlight_path: "/sys/class/backlight/panel1-backlight/brightness",
        }
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

    // Clean 5x7 ASCII microfont rasterizer
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

    pub fn render_telemetry(&mut self, data: &TelemetryData) {
        // Background: Deep Cyberpunk Gradient
        self.clear(0xFF070C18);

        // Header Border
        self.draw_rect(0, 0, REAR_WIDTH, 2, 0xFF00FFCC); // Neon Cyan line
        self.draw_rect(0, 32, REAR_WIDTH, 1, 0xFF1A3050);

        // Header
        self.draw_text(&data.title, 8, 8, 0xFF00FFCC);
        self.draw_text(&data.ip, 8, 20, 0xFF88AACC);

        // Metrics Section
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

        // Footer Bar
        self.draw_rect(0, REAR_HEIGHT - 3, REAR_WIDTH, 3, 0xFF00FFCC);
    }
}

// Built-in 5x7 ASCII bitmap font data for standalone execution
fn get_glyph(c: char) -> [u8; 7] {
    match c {
        '0'..='9' => NUMBERS[(c as u8 - b'0') as usize],
        'A'..='Z' => LETTERS_UPPER[(c as u8 - b'A') as usize],
        'a'..='z' => LETTERS_UPPER[(c as u8 - b'a') as usize], // simple fallback
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
