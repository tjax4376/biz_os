// Configuration management for AI Service Daemon

use anyhow::{Context, Result};
use learning_engine::connector::ConnectorConfig;
use learning_engine::schema::{SchemaDefinition, SchemaReference, SchemaVersion};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub model_dir: String,
    pub cache_dir: String,
    pub pattern_db_path: String,
    pub knowledge_graph_path: String,
    pub gpu_enabled: bool,
    pub cpu_fallback: bool,
    pub log_level: String,
    #[serde(default)]
    pub connectors: Vec<ConnectorConfig>,
    #[serde(default)]
    pub schemas: Vec<SchemaDefinition>,
    #[serde(default = "EventMeshConfig::default")]
    pub event_mesh: EventMeshConfig,
    #[serde(default = "default_ingestion_bind")]
    pub ingestion_bind_addr: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EventMeshConfig {
    #[serde(default = "default_mesh_transport")]
    pub transport: MeshTransport,
    #[serde(default = "default_mesh_url")]
    pub url: String,
    #[serde(default = "default_mesh_subject")]
    pub subject: String,
}

impl EventMeshConfig {
    pub fn default() -> Self {
        Self {
            transport: MeshTransport::Nats,
            url: default_mesh_url(),
            subject: default_mesh_subject(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum MeshTransport {
    #[serde(rename = "nats")]
    Nats,
    #[serde(rename = "in_memory")]
    InMemory,
}

const fn default_mesh_subject() -> &'static str {
    "bizos.data.fabric"
}

fn default_mesh_url() -> String {
    "nats://127.0.0.1:4222".to_string()
}

fn default_mesh_transport() -> MeshTransport {
    MeshTransport::Nats
}

fn default_ingestion_bind() -> String {
    "0.0.0.0:7510".to_string()
}

impl Config {
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let content = std::fs::read_to_string(path.as_ref())
            .with_context(|| format!("Failed to read config file: {:?}", path.as_ref()))?;

        let config: Config = toml::from_str(&content).context("Failed to parse config file")?;

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
            connectors: Vec::new(),
            schemas: vec![SchemaDefinition {
                reference: SchemaReference {
                    namespace: "telemetry".into(),
                    name: "system_metrics".into(),
                    version: SchemaVersion::new(1, 0, 0),
                },
                required_fields: vec!["hostname".into(), "timestamp".into()],
                optional_fields: vec!["payload".into()],
                pii_fields: vec![],
            }],
            event_mesh: EventMeshConfig::default(),
            ingestion_bind_addr: default_ingestion_bind(),
        }
    }
}
