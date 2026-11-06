use std::fs;
use std::path::Path;

#[derive(Debug, Clone)]
pub struct LoraManager {
    pub active_lora: Option<String>,
    pub scale: f32,
}

impl LoraManager {
    pub fn new() -> Self {
        Self {
            active_lora: None,
            scale: 1.0,
        }
    }

    pub fn load_lora(&mut self, name: &str, scale: f32) -> Result<(), String> {
        self.active_lora = Some(name.to_string());
        self.scale = scale;
        Ok(())
    }

    pub fn unload_lora(&mut self) {
        self.active_lora = None;
        self.scale = 1.0;
    }

    pub fn list_loras(&self) -> String {
        let lora_dir = "/data/alpine/root/models/lora";
        if !Path::new(lora_dir).exists() {
            return "No LoRA directory found".to_string();
        }

        let mut loras = Vec::new();
        if let Ok(entries) = fs::read_dir(lora_dir) {
            for entry in entries {
                if let Ok(entry) = entry {
                    if entry.path().is_dir() {
                        if let Ok(name) = entry.file_name().into_string() {
                            loras.push(name);
                        }
                    }
                }
            }
        }
        loras.join(", ")
    }

    pub fn read_manifest(&self) -> String {
        let manifest_path = "/data/alpine/root/models/lora/manifest.json";
        if Path::new(manifest_path).exists() {
            fs::read_to_string(manifest_path).unwrap_or_else(|_| "{}".to_string())
        } else {
            "{}".to_string()
        }
    }
}
