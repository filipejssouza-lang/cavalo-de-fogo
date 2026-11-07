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
                                "power" => format!("Power mode set to {:?}\n", parts.get(1)),
                                "net" => format!("Net set to {:?}\n", parts.get(1)),
                                "hud" => format!("HUD set to {:?}\n", parts.get(1)),
                                "npu" => format!("NPU set to {:?}\n", parts.get(1)),
                                "thermal" => format!("Thermal Mode: {}\n", telem.thermal_mode),
                                "metrics" => telem.to_prometheus(),
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
