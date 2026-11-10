use std::os::unix::net::UnixListener;
use std::io::{Read, Write};
use std::thread;
use std::time::SystemTime;
use crate::telemetry::SiliconTelemetry;

pub fn start_uds_server() {
    let socket_path = if std::fs::create_dir_all("/run/lupin").is_ok() {
        "/run/lupin/control.sock"
    } else {
        let _ = std::fs::create_dir_all("/tmp/lupin");
        "/tmp/lupin/control.sock"
    };

    let _ = std::fs::remove_file(socket_path);

    let listener = match UnixListener::bind(socket_path) {
        Ok(l) => {
            println!("⚡ [Control] Socket listening on {}", socket_path);
            l
        }
        Err(e) => {
            eprintln!("⚠️ [Control] Could not bind {}: {:?}. Control plane socket disabled.", socket_path, e);
            return;
        }
    };

    thread::spawn(move || {
        for stream in listener.incoming() {
            match stream {
                Ok(mut stream) => {
                    let mut buffer = [0; 1024];
                    if let Ok(size) = stream.read(&mut buffer) {
                        let req = String::from_utf8_lossy(&buffer[..size]).trim().to_string();
                        let parts: Vec<&str> = req.split_whitespace().collect();
                        
                        let response = if parts.is_empty() {
                            "ERROR: Empty command\n".to_string()
                        } else {
                            let timestamp = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap().as_secs();
                            let log_entry = format!("{{\"timestamp\":{}, \"cmd\":\"{}\"}}", timestamp, parts[0]);
                            println!("Structured Log: {}", log_entry);

                            let telem = SiliconTelemetry::collect();

                            match parts[0] {
                                "status" => {
                                    if parts.get(1) == Some(&"--json") || parts.get(1) == Some(&"json") {
                                        telem.to_json()
                                    } else {
                                        format!(
                                            "🐺 Lupin OS v6 Silicon Status:\n• CPU Temp: {:.1}°C (X1 @ {:.1} MHz)\n• Battery: {:.1}°C ({}%)\n• Mode: {}\n• RAM Avail: {} MB / {} MB\n",
                                            telem.cpu_temp_celsius,
                                            (telem.cpu_freqs_khz[7] as f32) / 1000.0,
                                            telem.battery_temp_celsius,
                                            telem.battery_capacity_percent,
                                            telem.thermal_mode,
                                            telem.mem_available_kb / 1024,
                                            telem.mem_total_kb / 1024
                                        )
                                    }
                                }
                                "power" => {
                                    if let Some(mode) = parts.get(1) {
                                        let bin = if std::path::Path::new("/sbin/lupin-power").exists() {
                                            "/sbin/lupin-power"
                                        } else if std::path::Path::new("/system/bin/lupin-power").exists() {
                                            "/system/bin/lupin-power"
                                        } else {
                                            ""
                                        };
                                        if !bin.is_empty() {
                                            match std::process::Command::new(bin).arg(mode).output() {
                                                Ok(o) => {
                                                    let msg = String::from_utf8_lossy(&o.stdout);
                                                    if msg.trim().is_empty() {
                                                        format!("Power mode set to {}\n", mode)
                                                    } else {
                                                        format!("{}\n", msg.trim())
                                                    }
                                                }
                                                Err(e) => format!("ERROR: Failed to run lupin-power: {}\n", e),
                                            }
                                        } else {
                                            let _ = std::fs::write("/tmp/power_state", format!("{}\n", mode));
                                            format!("Power mode mock-set to {}\n", mode)
                                        }
                                    } else {
                                        "ERROR: power requires mode (idle, balanced, burst)\n".to_string()
                                    }
                                }
                                "hud" => {
                                    if let Some(cmd) = parts.get(1) {
                                        let res = std::os::unix::net::UnixStream::connect("/run/lupin/hud.sock")
                                            .or_else(|_| std::os::unix::net::UnixStream::connect("/tmp/lupin/hud.sock"));
                                        match res {
                                            Ok(mut s) => {
                                                let _ = s.write_all(cmd.as_bytes());
                                                let mut buf = [0; 256];
                                                if let Ok(n) = s.read(&mut buf) {
                                                    String::from_utf8_lossy(&buf[..n]).to_string()
                                                } else {
                                                    format!("HUD set to {}\n", cmd)
                                                }
                                            }
                                            Err(_) => format!("HUD mock-set to {}\n", cmd),
                                        }
                                    } else {
                                        "ERROR: hud requires command (next, prev, dim, on, status)\n".to_string()
                                    }
                                }
                                "cam" => {
                                    if parts.get(1) == Some(&"shot") {
                                        let lens = parts.get(2).unwrap_or(&"wide");
                                        let bin = if std::path::Path::new("/sbin/lupin-cam").exists() {
                                            "/sbin/lupin-cam"
                                        } else if std::path::Path::new("/system/bin/lupin-cam").exists() {
                                            "/system/bin/lupin-cam"
                                        } else if std::path::Path::new("/data/lupin/bin/lupin-cam").exists() {
                                            "/data/lupin/bin/lupin-cam"
                                        } else {
                                            ""
                                        };
                                        if !bin.is_empty() {
                                            match std::process::Command::new(bin).arg("shot").arg(lens).output() {
                                                Ok(output) => {
                                                    let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
                                                    if stdout.is_empty() {
                                                        format!("{{\"status\":\"success\", \"lens\":\"{}\"}}\n", lens)
                                                    } else {
                                                        format!("{}\n", stdout)
                                                    }
                                                }
                                                Err(e) => format!("ERROR: Failed to run camera: {}\n", e),
                                            }
                                        } else {
                                            format!("{{\"status\":\"mock_success\", \"lens\":\"{}\"}}\n", lens)
                                        }
                                    } else {
                                        "ERROR: cam requires 'shot' subcommand (ex: cam shot wide)\n".to_string()
                                    }
                                }
                                "net" => format!("Net set to {:?}\n", parts.get(1)),
                                
                                "pc" => {
                                    if parts.len() >= 2 {
                                        let subcmd = parts[1];
                                        let mut cmd_exec = std::process::Command::new("./build/lupin-pc");
                                        cmd_exec.arg(subcmd);
                                        if parts.len() >= 3 {
                                            cmd_exec.arg(parts[2]);
                                        }
                                        if let Ok(out) = cmd_exec.output() {
                                            String::from_utf8_lossy(&out.stdout).to_string()
                                        } else {
                                            "ERROR: Failed to execute lupin-pc\n".to_string()
                                        }
                                    } else {
                                        "ERROR: Usage: pc <status|run|elf> [args...]\n".to_string()
                                    }
                                }
"npu" => format!("NPU set to {:?}\n", parts.get(1)),
                                "thermal" => format!("Thermal Mode: {}\n", telem.thermal_mode),
                                "peltier" => {
                                    match parts.get(1) {
                                        Some(&"on") => {
                                            let _ = std::fs::write("/tmp/lupin_peltier_override", "ON\n");
                                            let _ = std::fs::write("/tmp/lupin_peltier_state", "ACTIVE\n");
                                            "Peltier Cooler forced ON\n".to_string()
                                        }
                                        Some(&"off") => {
                                            let _ = std::fs::write("/tmp/lupin_peltier_override", "OFF\n");
                                            let _ = std::fs::write("/tmp/lupin_peltier_state", "STANDBY\n");
                                            "Peltier Cooler forced OFF\n".to_string()
                                        }
                                        Some(&"auto") => {
                                            let _ = std::fs::remove_file("/tmp/lupin_peltier_override");
                                            "Peltier Cooler set to AUTO (Governor closed-loop)\n".to_string()
                                        }
                                        _ => {
                                            let state = std::fs::read_to_string("/tmp/lupin_peltier_state")
                                                .unwrap_or_else(|_| "STANDBY\n".to_string());
                                            format!("Peltier State: {}", state)
                                        }
                                    }
                                }
                                "metrics" => telem.to_prometheus(),
                                "audio" => {
                                    let mut args = vec![];
                                    for i in 1..parts.len() {
                                        args.push(parts[i]);
                                    }
                                    let bin = if std::path::Path::new("/sbin/lupin-audio").exists() {
                                        "/sbin/lupin-audio"
                                    } else if std::path::Path::new("/data/lupin-audio").exists() {
                                        "/data/lupin-audio"
                                    } else if std::path::Path::new("/data/lupin/bin/lupin-audio").exists() {
                                        "/data/lupin/bin/lupin-audio"
                                    } else if std::path::Path::new("build/lupin-audio").exists() {
                                        "build/lupin-audio"
                                    } else {
                                        ""
                                    };
                                    if !bin.is_empty() {
                                        match std::process::Command::new(bin).args(&args).output() {
                                            Ok(output) => {
                                                let stdout = String::from_utf8_lossy(&output.stdout).to_string();
                                                let stderr = String::from_utf8_lossy(&output.stderr).to_string();
                                                if stdout.is_empty() && stderr.is_empty() {
                                                    "OK\n".to_string()
                                                } else if !stdout.is_empty() {
                                                    stdout
                                                } else {
                                                    format!("ERROR: {}\n", stderr)
                                                }
                                            }
                                            Err(e) => format!("ERROR: Failed to run lupin-audio: {}\n", e),
                                        }
                                    } else {
                                        format!("ERROR: lupin-audio binary not found\n")
                                    }
                                }
                                _ => "ERROR: Unknown command\n".to_string(),
                            }
                        };
                        let _ = stream.write_all(response.as_bytes());
                    }
                }
                Err(err) => {
                    eprintln!("Error accepting connection: {}", err);
                }
            }
        }
    });

    // Spawn a small TCP server for Prometheus metrics on 0.0.0.0:9100
    thread::spawn(|| {
        if let Ok(listener) = std::net::TcpListener::bind("0.0.0.0:9100") {
            println!("📊 [Prometheus] Metrics server listening on http://0.0.0.0:9100/metrics");
            for stream in listener.incoming() {
                if let Ok(mut stream) = stream {
                    let mut buffer = [0; 1024];
                    let _ = stream.read(&mut buffer);
                    let body = SiliconTelemetry::collect().to_prometheus();
                    let response = format!(
                        "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                        body.len(),
                        body
                    );
                    let _ = stream.write_all(response.as_bytes());
                }
            }
        }
    });
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_uds_command_parsing() {
        let cmd = "power burst";
        let parts: Vec<&str> = cmd.split_whitespace().collect();
        assert_eq!(parts[0], "power");
        assert_eq!(parts[1], "burst");

        let cam_cmd = "cam shot front";
        let cam_parts: Vec<&str> = cam_cmd.split_whitespace().collect();
        assert_eq!(cam_parts[0], "cam");
        assert_eq!(cam_parts[1], "shot");
        assert_eq!(cam_parts[2], "front");
    }
}
