//! Lightweight schema registry to keep connector payloads versioned and audited.

use std::collections::HashMap;
use std::sync::Arc;

use anyhow::{anyhow, Result};
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tracing::debug;

/// Semantic version used for payload schemas.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct SchemaVersion {
    pub major: u16,
    pub minor: u16,
    pub patch: u16,
}

impl SchemaVersion {
    pub const fn new(major: u16, minor: u16, patch: u16) -> Self {
        Self {
            major,
            minor,
            patch,
        }
    }

    pub fn compatible_with(&self, other: &Self) -> bool {
        self.major == other.major && self.minor >= other.minor
    }
}

/// Reference to a schema stored inside the registry.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct SchemaReference {
    pub namespace: String,
    pub name: String,
    pub version: SchemaVersion,
}

impl SchemaReference {
    fn key(&self) -> String {
        format!(
            "{}::{}::{}.{}.{}",
            self.namespace, self.name, self.version.major, self.version.minor, self.version.patch
        )
    }
}

/// Minimal schema definition – intentionally simple so we can validate payloads
/// without requiring a full JSON schema implementation inside the agent.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SchemaDefinition {
    pub reference: SchemaReference,
    pub required_fields: Vec<String>,
    pub optional_fields: Vec<String>,
    pub pii_fields: Vec<String>,
}

#[derive(Clone, Default)]
pub struct SchemaRegistry {
    inner: Arc<RwLock<HashMap<String, SchemaDefinition>>>,
}

impl SchemaRegistry {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register(&self, schema: SchemaDefinition) {
        debug!(
            namespace = %schema.reference.namespace,
            name = %schema.reference.name,
            "Registering schema",
        );
        self.inner.write().insert(schema.reference.key(), schema);
    }

    pub fn get(&self, reference: &SchemaReference) -> Option<SchemaDefinition> {
        self.inner.read().get(&reference.key()).cloned()
    }

    pub fn list(&self, namespace: Option<&str>) -> Vec<SchemaDefinition> {
        self.inner
            .read()
            .values()
            .filter(|schema| {
                namespace
                    .map(|ns| schema.reference.namespace == ns)
                    .unwrap_or(true)
            })
            .cloned()
            .collect()
    }

    pub fn validate(&self, reference: &SchemaReference, payload: &Value) -> Result<()> {
        let schema = self
            .get(reference)
            .ok_or_else(|| anyhow!("schema {:?} not found", reference.key()))?;

        let obj = payload
            .as_object()
            .ok_or_else(|| anyhow!("payload must be a JSON object"))?;

        for field in &schema.required_fields {
            if !obj.contains_key(field) {
                return Err(anyhow!("missing required field '{}'", field));
            }
        }

        Ok(())
    }
}
