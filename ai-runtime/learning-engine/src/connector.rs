
//! Connector SDK primitives for the BIZ_OS data fabric.
//! The SDK ensures every connector follows the same contract-driven,
//! policy-aware pattern before it can publish events into the mesh.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use anyhow::Result;
use async_trait::async_trait;
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use tokio::{sync::mpsc, task::JoinHandle};
use uuid::Uuid;

use crate::collector::PrivacyLevel;
use crate::event_mesh::{EventEnvelope, EventPublisher};
use crate::schema::{SchemaReference, SchemaRegistry};

/// Metadata describing a connector implementation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConnectorDescriptor {
    pub id: String,
    pub name: String,
    pub kind: ConnectorKind,
    /// Human friendly description of the integration.
    pub description: String,
    /// Data domains emitted by this connector ("email", "crm", ...).
    pub domains: Vec<String>,
    /// Maximum rate (events/second) permitted before throttling.
    pub max_rate_per_sec: u32,
    /// Default privacy tag applied when the connector does not provide an override.
    pub default_privacy: PrivacyLevel,
}

impl ConnectorDescriptor {
    pub fn namespaced_id(&self) -> String {
        format!("{}:{}", self.kind.as_str(), self.id)
    }
}

/// Supported connector transport styles.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum ConnectorKind {
    Email,
    Database,
    RestApi,
    SaaSLog,
    Filesystem,
    Custom,
}

impl ConnectorKind {
    pub const fn as_str(&self) -> &'static str {
        match self {
            ConnectorKind::Email => "email",
            ConnectorKind::Database => "database",
            ConnectorKind::RestApi => "rest_api",
            ConnectorKind::SaaSLog => "saas_log",
            ConnectorKind::Filesystem => "filesystem",
            ConnectorKind::Custom => "custom",
        }
    }
}

/// Authentication strategies supported by the SDK.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum AuthStrategy {
    OAuth2ClientCredentials,
    ApiKey,
    SignedRequest,
    None,
}

/// Connector-scoped configuration loaded from TOML/YAML files.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConnectorConfig {
    pub descriptor: ConnectorDescriptor,
    pub auth: AuthDetails,
    pub settings: HashMap<String, serde_json::Value>,
    pub schemas: Vec<SchemaReference>,
    /// Optional rate limit override (per second).
    pub rate_limit_override: Option<u32>,
    /// Optional custom retention window for emitted events.
    pub retention_secs: Option<u64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AuthDetails {
    pub strategy: AuthStrategy,
    pub secrets_path: String,
    pub scopes: Vec<String>,
}

/// Canonical event representation before hitting the event mesh.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConnectorEvent {
    pub id: Uuid,
    pub schema: SchemaReference,
    pub namespace: String,
    pub payload: serde_json::Value,
    pub received_at: DateTime<Utc>,
    pub privacy: PrivacyLevel,
    pub source_descriptor: ConnectorDescriptor,
    pub annotations: HashMap<String, String>,
}

impl ConnectorEvent {
    pub fn envelope(&self) -> EventEnvelope {
        EventEnvelope {
            id: self.id,
            schema: self.schema.clone(),
            namespace: self.namespace.clone(),
            payload: self.payload.clone(),
            received_at: self.received_at,
            privacy: self.privacy,
            annotations: self.annotations.clone(),
        }
    }
}

/// Runtime context that every connector receives when being started.
pub struct ConnectorContext {
    pub config: ConnectorConfig,
    pub schema_registry: SchemaRegistry,
    pub publisher: EventPublisher,
    pub event_tx: mpsc::Sender<ConnectorEvent>,
}

impl ConnectorContext {
    pub fn new(
        config: ConnectorConfig,
        schema_registry: SchemaRegistry,
        publisher: EventPublisher,
    ) -> (Self, mpsc::Receiver<ConnectorEvent>) {
        let (tx, rx) = mpsc::channel(512);
        (
            Self {
                config,
                schema_registry,
                publisher,
                event_tx: tx,
            },
            rx,
        )
    }
}

#[derive(Debug, thiserror::Error)]
pub enum ConnectorError {
    #[error("authentication failed: {0}")]
    Authentication(String),
    #[error("schema validation failed: {0}")]
    Schema(String),
    #[error("rate limit exceeded (max {max} per second)")]
    RateLimit { max: u32 },
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

pub type ConnectorResult<T> = std::result::Result<T, ConnectorError>;

/// Handle returned by a running connector. Dropping the handle signals shutdown.
pub struct ConnectorHandle {
    pub descriptor: ConnectorDescriptor,
    pub join_handle: JoinHandle<Result<()>>,
}

impl ConnectorHandle {
    pub fn new(descriptor: ConnectorDescriptor, task: JoinHandle<Result<()>>) -> Self {
        Self {
            descriptor,
            join_handle: task,
        }
    }
}

#[async_trait]
pub trait Connector: Send + Sync + 'static {
    fn descriptor(&self) -> &ConnectorDescriptor;

    async fn authorize(&self, config: &ConnectorConfig) -> ConnectorResult<()>;

    async fn spawn(self: Arc<Self>, ctx: ConnectorContext) -> ConnectorResult<ConnectorHandle>;
}

impl ConnectorHandle {
    /// Awaits connector completion, ensuring errors propagate.
    pub async fn wait(self) -> Result<()> {
        self.join_handle.await??;
        Ok(())
    }
}

/// Utility to pipe connector events into the mesh while performing throttling
/// and schema validation up-front.
pub async fn forward_events(
    mut rx: mpsc::Receiver<ConnectorEvent>,
    publisher: EventPublisher,
    schema_registry: SchemaRegistry,
    throttle: Option<u32>,
) -> Result<()> {
    use tokio::time::{interval, Instant};

    let limit = throttle.unwrap_or(500);
    let mut window_start = Instant::now();
    let mut sent_this_window = 0u32;

    while let Some(event) = rx.recv().await {
        if sent_this_window >= limit {
            let mut ticker = interval(Duration::from_secs(1));
            ticker.tick().await;
            window_start = Instant::now();
            sent_this_window = 0;
        }

        schema_registry
            .validate(&event.schema, &event.payload)
            .map_err(|err| ConnectorError::Schema(err.to_string()))?;

        publisher.publish(event.envelope()).await?;

        sent_this_window += 1;

        if window_start.elapsed() >= Duration::from_secs(1) {
            window_start = Instant::now();
            sent_this_window = 0;
        }
    }

    Ok(())
}
