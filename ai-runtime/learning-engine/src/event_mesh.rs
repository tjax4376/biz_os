//! Event mesh abstraction. By default connectors publish into an in-memory bus
//! for local testing while production deployments can switch to NATS/JetStream
//! without modifying connector code.

use std::sync::Arc;

use anyhow::Result;
use async_nats::Client;
use async_trait::async_trait;
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use tokio::sync::Mutex;
use tracing::debug;

use crate::collector::PrivacyLevel;
use crate::schema::SchemaReference;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EventEnvelope {
    pub id: uuid::Uuid,
    pub schema: SchemaReference,
    pub namespace: String,
    pub payload: serde_json::Value,
    pub received_at: DateTime<Utc>,
    pub privacy: PrivacyLevel,
    pub annotations: std::collections::HashMap<String, String>,
}

#[async_trait]
pub trait MeshPublisher: Send + Sync {
    async fn publish(&self, event: EventEnvelope) -> Result<()>;
}

#[derive(Clone)]
pub struct EventPublisher {
    inner: Arc<dyn MeshPublisher>,
}

impl EventPublisher {
    pub fn new(inner: Arc<dyn MeshPublisher>) -> Self {
        Self { inner }
    }

    pub async fn publish(&self, event: EventEnvelope) -> Result<()> {
        self.inner.publish(event).await
    }
}

struct InMemoryPublisher {
    buffer: Arc<Mutex<Vec<EventEnvelope>>>,
}

#[async_trait]
impl MeshPublisher for InMemoryPublisher {
    async fn publish(&self, event: EventEnvelope) -> Result<()> {
        debug!("in-memory mesh event", ns = %event.namespace);
        self.buffer.lock().await.push(event);
        Ok(())
    }
}

struct NatsPublisher {
    client: Client,
    subject: String,
}

#[async_trait]
impl MeshPublisher for NatsPublisher {
    async fn publish(&self, event: EventEnvelope) -> Result<()> {
        let bytes = serde_json::to_vec(&event)?;
        self.client
            .publish(self.subject.clone(), bytes.into())
            .await?;
        Ok(())
    }
}

pub struct EventMeshBuilder;

impl EventMeshBuilder {
    pub fn in_memory() -> EventPublisher {
        EventPublisher::new(Arc::new(InMemoryPublisher {
            buffer: Arc::new(Mutex::new(Vec::new())),
        }))
    }

    pub async fn nats(url: &str, subject: &str) -> Result<EventPublisher> {
        let client = async_nats::connect(url).await?;
        Ok(EventPublisher::new(Arc::new(NatsPublisher {
            client,
            subject: subject.to_string(),
        })))
    }
}
