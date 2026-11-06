use std::os::unix::net::UnixListener;
use std::io::{Read, Write};
use std::thread;
use std::time::SystemTime;

fn generate_prometheus_metrics() -> String {
    let mut metrics = String::new();
    metrics.push_str("# HELP lupinos_cpu_temp CPU temperature in Celsius\n");
    metrics.push_str("# TYPE lupinos_cpu_temp gauge\n");
    metrics.push_str("lupinos_cpu_temp 45.5\n"); // Mocked for now, integrate with app state later
    metrics
}

pub fn start_uds_server() {
    let socket_path = "/run/lupin/control.sock";
    let _ = std::fs::remove_file(socket_path);

    let listener = UnixListener::bind(socket_path).unwrap();
    println!("Control socket listening on {}", socket_path);

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
                            // Struct logs string implementation manually
                            let timestamp = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap().as_secs();
                            let log_entry = format!("{{\"timestamp\":{}, \"cmd\":\"{}\"}}", timestamp, parts[0]);
                            println!("Structured Log: {}", log_entry); // This could be appended to a ring buffer in memory

                            match parts[0] {
                                "status" => "{\"status\":\"ok\"}\n".to_string(),
                                "power" => format!("Power mode set to {:?}\n", parts.get(1)),
                                "net" => format!("Net set to {:?}\n", parts.get(1)),
                                "hud" => format!("HUD set to {:?}\n", parts.get(1)),
                                "npu" => format!("NPU set to {:?}\n", parts.get(1)),
                                "thermal" => format!("Thermal set to {:?}\n", parts.get(1)),
                                "metrics" => generate_prometheus_metrics(),
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

    // Also spawn a small TCP server for Prometheus metrics on :9100
    thread::spawn(|| {
        if let Ok(listener) = std::net::TcpListener::bind("0.0.0.0:9100") {
            for stream in listener.incoming() {
                if let Ok(mut stream) = stream {
                    let mut buffer = [0; 1024];
                    let _ = stream.read(&mut buffer);
                    let body = generate_prometheus_metrics();
                    let response = format!("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", body.len(), body);
                    let _ = stream.write_all(response.as_bytes());
                }
            }
        }
    });
}
