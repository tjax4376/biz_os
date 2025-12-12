// Model Manager - Handles loading, unloading, and management of AI models

use crate::config::Config;
use anyhow::{Context, Result};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::sync::RwLock;
use tracing::{info, warn};

#[derive(Clone)]
pub struct ModelInfo {
    pub id: String,
    pub path: PathBuf,
    pub model_type: ModelType,
    pub loaded: bool,
    pub size_bytes: u64,
}

#[derive(Debug, Clone)]
pub enum ModelType {
    LLM,           // Large Language Model (Mistral)
    PatternRecognition,
    UIGeneration,
    ProcessMining,
}

pub struct ModelManager {
    config: Arc<Config>,
    models: Arc<RwLock<HashMap<String, ModelInfo>>>,
}

impl ModelManager {
    pub async fn new(config: &Config) -> Result<Self> {
        let model_dir = Path::new(&config.model_dir);
        
        // Ensure model directory exists
        std::fs::create_dir_all(model_dir)
            .with_context(|| format!("Failed to create model directory: {:?}", model_dir))?;

        // Ensure cache directory exists
        std::fs::create_dir_all(&config.cache_dir)
            .with_context(|| format!("Failed to create cache directory: {}", config.cache_dir))?;

        let manager = Self {
            config: Arc::new(config.clone()),
            models: Arc::new(RwLock::new(HashMap::new())),
        };

        // Scan for available models
        manager.scan_models().await?;

        Ok(manager)
    }

    async fn scan_models(&self) -> Result<()> {
        let model_dir = Path::new(&self.config.model_dir);
        
        if !model_dir.exists() {
            warn!("Model directory does not exist: {:?}", model_dir);
            return Ok(());
        }

        info!("Scanning for models in: {:?}", model_dir);

        // Look for Mistral model
        let mistral_path = model_dir.join("mistral7b");
        if mistral_path.exists() {
            let model_info = ModelInfo {
                id: "mistral7b".to_string(),
                path: mistral_path.clone(),
                model_type: ModelType::LLM,
                loaded: false,
                size_bytes: Self::calculate_size(&mistral_path)?,
            };
            
            self.models.write().await.insert("mistral7b".to_string(), model_info);
            info!("Found Mistral 7B model");
        } else {
            warn!("Mistral 7B model not found at: {:?}", mistral_path);
            info!("To download Mistral 7B, see QUICK_START.md");
        }

        Ok(())
    }

    fn calculate_size(path: &Path) -> Result<u64> {
        let mut total = 0u64;
        
        if path.is_file() {
            total += std::fs::metadata(path)?.len();
        } else if path.is_dir() {
            for entry in std::fs::read_dir(path)? {
                let entry = entry?;
                total += Self::calculate_size(&entry.path())?;
            }
        }
        
        Ok(total)
    }

    pub async fn get_model(&self, model_id: &str) -> Option<ModelInfo> {
        self.models.read().await.get(model_id).cloned()
    }

    pub async fn list_models(&self) -> Vec<String> {
        self.models.read().await.keys().cloned().collect()
    }

    pub async fn load_model(&self, model_id: &str) -> Result<()> {
        let mut models = self.models.write().await;
        
        if let Some(model) = models.get_mut(model_id) {
            if model.loaded {
                info!("Model {} already loaded", model_id);
                return Ok(());
            }

            info!("Loading model: {} from {:?}", model_id, model.path);
            // TODO: Implement actual model loading
            // This will integrate with llama.cpp or similar for Mistral
            model.loaded = true;
            info!("Model {} loaded successfully", model_id);
        } else {
            anyhow::bail!("Model not found: {}", model_id);
        }

        Ok(())
    }

    pub async fn unload_model(&self, model_id: &str) -> Result<()> {
        let mut models = self.models.write().await;
        
        if let Some(model) = models.get_mut(model_id) {
            if !model.loaded {
                warn!("Model {} not loaded", model_id);
                return Ok(());
            }

            info!("Unloading model: {}", model_id);
            // TODO: Implement actual model unloading
            model.loaded = false;
            info!("Model {} unloaded", model_id);
        } else {
            anyhow::bail!("Model not found: {}", model_id);
        }

        Ok(())
    }
}

