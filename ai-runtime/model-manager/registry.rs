// Model Registry
// Maintains a registry of available and loaded models

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::RwLock;
use tracing::info;

#[derive(Debug, Clone)]
pub struct ModelEntry {
    pub id: String,
    pub path: PathBuf,
    pub loaded: bool,
    pub size_bytes: u64,
}

pub struct ModelRegistry {
    models: Arc<RwLock<HashMap<String, ModelEntry>>>,
}

impl ModelRegistry {
    pub fn new() -> Self {
        Self {
            models: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    pub async fn register(&self, entry: ModelEntry) {
        let mut models = self.models.write().await;
        info!("Registering model: {}", entry.id);
        models.insert(entry.id.clone(), entry);
    }

    pub async fn get(&self, model_id: &str) -> Option<ModelEntry> {
        let models = self.models.read().await;
        models.get(model_id).cloned()
    }

    pub async fn list(&self) -> Vec<String> {
        let models = self.models.read().await;
        models.keys().cloned().collect()
    }

    pub async fn mark_loaded(&self, model_id: &str, loaded: bool) {
        let mut models = self.models.write().await;
        if let Some(entry) = models.get_mut(model_id) {
            entry.loaded = loaded;
        }
    }
}

impl Default for ModelRegistry {
    fn default() -> Self {
        Self::new()
    }
}

