use std::collections::HashMap;
use std::sync::{RwLock};

#[derive(Clone, Debug)]
pub struct LoraAdapter {
    pub name: String,
    pub path: String,
}

pub struct LoraManager {
    adapters: RwLock<HashMap<String, LoraAdapter>>,
    active_adapter: RwLock<Option<String>>,
}

impl LoraManager {
    pub fn new() -> Self {
        Self {
            adapters: RwLock::new(HashMap::new()),
            active_adapter: RwLock::new(None),
        }
    }

    pub fn load(&self, name: String, path: String) -> Result<(), String> {
        let mut adapters = self.adapters.write().map_err(|_| "Failed to acquire lock")?;

        let converted_path = if path.ends_with(".bin") || path.ends_with(".safetensors") {
            format!("{}.gguf", path)
        } else {
            path
        };

        adapters.insert(name.clone(), LoraAdapter { name: name.clone(), path: converted_path });
        let mut active = self.active_adapter.write().map_err(|_| "Failed to acquire lock")?;
        *active = Some(name);
        Ok(())
    }

    pub fn list(&self) -> Result<Vec<LoraAdapter>, String> {
        let adapters = self.adapters.read().map_err(|_| "Failed to acquire lock")?;
        Ok(adapters.values().cloned().collect())
    }

    pub fn unload(&self, name: &str) -> Result<(), String> {
        let mut adapters = self.adapters.write().map_err(|_| "Failed to acquire lock")?;
        adapters.remove(name);
        let mut active = self.active_adapter.write().map_err(|_| "Failed to acquire lock")?;
        if active.as_deref() == Some(name) {
            *active = None;
        }
        Ok(())
    }

    pub fn get_active(&self) -> Result<Option<LoraAdapter>, String> {
        let active = self.active_adapter.read().map_err(|_| "Failed to acquire lock")?;
        if let Some(name) = active.as_ref() {
            let adapters = self.adapters.read().map_err(|_| "Failed to acquire lock")?;
            Ok(adapters.get(name).cloned())
        } else {
            Ok(None)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lora_manager_load_list_unload() {
        let manager = LoraManager::new();

        assert_eq!(manager.list().unwrap().len(), 0);
        assert!(manager.get_active().unwrap().is_none());

        manager.load("test_adapter".to_string(), "/models/test.gguf".to_string()).unwrap();

        let adapters = manager.list().unwrap();
        assert_eq!(adapters.len(), 1);
        assert_eq!(adapters[0].name, "test_adapter");
        assert_eq!(adapters[0].path, "/models/test.gguf");

        let active = manager.get_active().unwrap().unwrap();
        assert_eq!(active.name, "test_adapter");

        manager.load("peft_adapter".to_string(), "/models/test.safetensors".to_string()).unwrap();
        let active_peft = manager.get_active().unwrap().unwrap();
        assert_eq!(active_peft.name, "peft_adapter");
        assert_eq!(active_peft.path, "/models/test.safetensors.gguf");

        manager.unload("peft_adapter").unwrap();
        assert!(manager.get_active().unwrap().is_none());
        assert_eq!(manager.list().unwrap().len(), 1);

        manager.unload("test_adapter").unwrap();
        assert_eq!(manager.list().unwrap().len(), 0);
    }
}
