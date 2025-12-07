// Configuration management for AI Service Daemon

use serde::{Deserialize, Serialize};
use std::path::Path;
use anyhow::{Context, Result};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub model_dir: String,
    pub cache_dir: String,
    pub pattern_db_path: String,
    pub knowledge_graph_path: String,
    pub gpu_enabled: bool,
    pub cpu_fallback: bool,
    pub log_level: String,
}

impl Config {
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let content = std::fs::read_to_string(path.as_ref())
            .with_context(|| format!("Failed to read config file: {:?}", path.as_ref()))?;
        
        let config: Config = toml::from_str(&content)
            .context("Failed to parse config file")?;
        
        Ok(config)
    }

    pub fn default() -> Self {
        Self {
            model_dir: "/var/lib/bizos/models".to_string(),
            cache_dir: "/var/cache/bizos/models".to_string(),
            pattern_db_path: "/var/lib/bizos/patterns/patterns.db".to_string(),
            knowledge_graph_path: "/var/lib/bizos/knowledge/graph.db".to_string(),
            gpu_enabled: true,
            cpu_fallback: true,
            log_level: "info".to_string(),
        }
    }
}

