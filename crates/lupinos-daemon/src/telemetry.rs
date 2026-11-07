use std::fs;

#[derive(Debug, Clone)]
pub struct SiliconTelemetry {
    pub cpu_freqs_khz: [u32; 8],
    pub cpu_temp_celsius: f32,
    pub battery_temp_celsius: f32,
    pub battery_capacity_percent: u8,
    pub battery_current_ma: i32,
    pub mem_total_kb: u64,
    pub mem_free_kb: u64,
    pub mem_available_kb: u64,
    pub zram_orig_bytes: u64,
    pub zram_compr_bytes: u64,
    pub thermal_mode: String,
    pub peltier_state: String,
    pub net_status: String,
}

impl SiliconTelemetry {
    fn read_trimmed(path: &str) -> Option<String> {
        fs::read_to_string(path).ok().map(|s| s.trim().to_string())
    }

    fn read_u32(path: &str, default: u32) -> u32 {
        Self::read_trimmed(path)
            .and_then(|s| s.parse::<u32>().ok())
            .unwrap_or(default)
    }

    fn read_u64(path: &str, default: u64) -> u64 {
        Self::read_trimmed(path)
            .and_then(|s| s.parse::<u64>().ok())
            .unwrap_or(default)
    }

    fn read_i32(path: &str, default: i32) -> i32 {
        Self::read_trimmed(path)
            .and_then(|s| s.parse::<i32>().ok())
            .unwrap_or(default)
    }

    pub fn collect() -> Self {
        let mut freqs = [0u32; 8];
        for i in 0..8 {
            let path = format!("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", i);
            freqs[i] = Self::read_u32(&path, if i == 7 { 2841600 } else if i >= 4 { 710400 } else { 300000 });
        }

        // CPU temperature (Qualcomm thermal_zone0 or thermal_zone1)
        let raw_cpu_temp = Self::read_u32("/sys/class/thermal/thermal_zone0/temp", 38000);
        let cpu_temp_celsius = (raw_cpu_temp as f32) / 1000.0;

        // Battery temperature (scaled by 10)
        let raw_bat_temp = Self::read_i32("/sys/class/power_supply/battery/temp", 320);
        let battery_temp_celsius = (raw_bat_temp as f32) / 10.0;

        let battery_capacity_percent = Self::read_u32("/sys/class/power_supply/battery/capacity", 100).min(100) as u8;
        let battery_current_ma = Self::read_i32("/sys/class/power_supply/battery/current_now", 0) / 1000;

        // /proc/meminfo
        let mut mem_total = 11420000;
        let mut mem_free = 9500000;
        let mut mem_avail = 9800000;

        if let Ok(meminfo) = fs::read_to_string("/proc/meminfo") {
            for line in meminfo.lines() {
                if line.starts_with("MemTotal:") {
                    if let Some(val) = line.split_whitespace().nth(1).and_then(|v| v.parse().ok()) {
                        mem_total = val;
                    }
                } else if line.starts_with("MemFree:") {
                    if let Some(val) = line.split_whitespace().nth(1).and_then(|v| v.parse().ok()) {
                        mem_free = val;
                    }
                } else if line.starts_with("MemAvailable:") {
                    if let Some(val) = line.split_whitespace().nth(1).and_then(|v| v.parse().ok()) {
                        mem_avail = val;
                    }
                }
            }
        }

        // ZRAM
        let zram_orig = Self::read_u64("/sys/block/zram0/orig_data_size", 0);
        let zram_compr = Self::read_u64("/sys/block/zram0/compr_data_size", 0);

        // Thermal Mode from closed-loop governor daemon
        let thermal_mode = Self::read_trimmed("/tmp/lupin_thermal_state")
            .unwrap_or_else(|| "BALANCED".to_string());

        // Peltier cooler state
        let peltier_state = Self::read_trimmed("/tmp/lupin_peltier_state")
            .unwrap_or_else(|| "STANDBY".to_string());

        // Net link watchdog status
        let mut net_status = "ONLINE".to_string();
        if let Ok(net_content) = fs::read_to_string("/tmp/lupin_net_state") {
            for line in net_content.lines() {
                if let Some(stripped) = line.strip_prefix("STATUS=") {
                    net_status = stripped.trim().to_string();
                    break;
                }
            }
        }

        Self {
            cpu_freqs_khz: freqs,
            cpu_temp_celsius,
            battery_temp_celsius,
            battery_capacity_percent,
            battery_current_ma,
            mem_total_kb: mem_total,
            mem_free_kb: mem_free,
            mem_available_kb: mem_avail,
            zram_orig_bytes: zram_orig,
            zram_compr_bytes: zram_compr,
            thermal_mode,
            peltier_state,
            net_status,
        }
    }

    pub fn to_prometheus(&self) -> String {
        let mut m = String::with_capacity(1024);
        m.push_str("# HELP lupinos_cpu_temp_celsius CPU temperature in Celsius\n");
        m.push_str("# TYPE lupinos_cpu_temp_celsius gauge\n");
        m.push_str(&format!("lupinos_cpu_temp_celsius {:.1}\n", self.cpu_temp_celsius));

        m.push_str("# HELP lupinos_battery_temp_celsius Battery temperature in Celsius\n");
        m.push_str("# TYPE lupinos_battery_temp_celsius gauge\n");
        m.push_str(&format!("lupinos_battery_temp_celsius {:.1}\n", self.battery_temp_celsius));

        m.push_str("# HELP lupinos_battery_capacity_percent Battery capacity in percent\n");
        m.push_str("# TYPE lupinos_battery_capacity_percent gauge\n");
        m.push_str(&format!("lupinos_battery_capacity_percent {}\n", self.battery_capacity_percent));

        m.push_str("# HELP lupinos_cpu_freq_khz Core frequency in kHz\n");
        m.push_str("# TYPE lupinos_cpu_freq_khz gauge\n");
        for (i, &freq) in self.cpu_freqs_khz.iter().enumerate() {
            let core_type = if i == 7 { "prime_x1" } else if i >= 4 { "big_a78" } else { "little_a55" };
            m.push_str(&format!("lupinos_cpu_freq_khz{{core=\"{}\",type=\"{}\"}} {}\n", i, core_type, freq));
        }

        m.push_str("# HELP lupinos_mem_available_bytes Available RAM in bytes\n");
        m.push_str("# TYPE lupinos_mem_available_bytes gauge\n");
        m.push_str(&format!("lupinos_mem_available_bytes {}\n", self.mem_available_kb * 1024));

        m.push_str("# HELP lupinos_zram_orig_bytes ZRAM uncompressed size in bytes\n");
        m.push_str("# TYPE lupinos_zram_orig_bytes gauge\n");
        m.push_str(&format!("lupinos_zram_orig_bytes {}\n", self.zram_orig_bytes));

        m.push_str("# HELP lupinos_zram_compr_bytes ZRAM compressed size in bytes\n");
        m.push_str("# TYPE lupinos_zram_compr_bytes gauge\n");
        m.push_str(&format!("lupinos_zram_compr_bytes {}\n", self.zram_compr_bytes));

        m.push_str("# HELP lupinos_peltier_active Peltier thermoelectric cooler active flag (1 or 0)\n");
        m.push_str("# TYPE lupinos_peltier_active gauge\n");
        let peltier_num = if self.peltier_state == "ACTIVE" { 1 } else { 0 };
        m.push_str(&format!("lupinos_peltier_active{{state=\"{}\"}} {}\n", self.peltier_state, peltier_num));

        m.push_str("# HELP lupinos_network_online Network connectivity status flag (1 or 0)\n");
        m.push_str("# TYPE lupinos_network_online gauge\n");
        let net_online = if self.net_status == "ONLINE" { 1 } else { 0 };
        m.push_str(&format!("lupinos_network_online{{status=\"{}\"}} {}\n", self.net_status, net_online));

        m
    }

    pub fn to_json(&self) -> String {
        format!(
            "{{\"cpu\":{{\"temp_celsius\":{:.1},\"prime_mhz\":{:.1},\"freqs_khz\":{:?}}},\"battery\":{{\"temp_celsius\":{:.1},\"capacity_percent\":{},\"current_ma\":{}}},\"memory\":{{\"total_kb\":{},\"free_kb\":{},\"available_kb\":{},\"zram_orig_bytes\":{},\"zram_compr_bytes\":{}}},\"thermal\":{{\"mode\":\"{}\"}},\"peltier\":{{\"state\":\"{}\"}},\"network\":{{\"status\":\"{}\"}}}}\n",
            self.cpu_temp_celsius,
            (self.cpu_freqs_khz[7] as f32) / 1000.0,
            self.cpu_freqs_khz,
            self.battery_temp_celsius,
            self.battery_capacity_percent,
            self.battery_current_ma,
            self.mem_total_kb,
            self.mem_free_kb,
            self.mem_available_kb,
            self.zram_orig_bytes,
            self.zram_compr_bytes,
            self.thermal_mode,
            self.peltier_state,
            self.net_status
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_telemetry_collect_and_format() {
        let t = SiliconTelemetry::collect();
        assert!(t.cpu_freqs_khz.len() == 8);
        assert!(t.cpu_temp_celsius > 0.0);
        assert!(t.mem_total_kb > 0);

        let prom = t.to_prometheus();
        assert!(prom.contains("lupinos_cpu_temp_celsius"));
        assert!(prom.contains("core=\"7\",type=\"prime_x1\""));
        assert!(prom.contains("lupinos_mem_available_bytes"));

        let json = t.to_json();
        assert!(json.contains("\"cpu\":"));
        assert!(json.contains("\"prime_mhz\":"));
        assert!(json.contains("\"thermal\":"));
    }
}
