use std::sync::Arc;

use anyhow::{Context, Result};
use async_trait::async_trait;
use chrono::Utc;
use learning_engine::connector::{
    Connector, ConnectorConfig, ConnectorContext, ConnectorDescriptor, ConnectorError,
    ConnectorEvent, ConnectorHandle, ConnectorResult,
};
use learning_engine::schema::SchemaReference;
use tokio::fs::File;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::task::JoinHandle;
use tracing::info;
use uuid::Uuid;

/// Simple connector that replays newline-delimited JSON events from disk.
pub struct FileReplayConnector {
    descriptor: ConnectorDescriptor,
}

impl FileReplayConnector {
    pub fn new(descriptor: ConnectorDescriptor) -> Self {
        Self { descriptor }
    }
}

#[async_trait]
impl Connector for FileReplayConnector {
    fn descriptor(&self) -> &ConnectorDescriptor {
        &self.descriptor
    }

    async fn authorize(&self, config: &ConnectorConfig) -> ConnectorResult<()> {
        config
            .settings
            .get("seed_file")
            .and_then(|value| value.as_str())
            .ok_or_else(|| ConnectorError::Other(anyhow::anyhow!("seed_file not configured")))?;
        Ok(())
    }

    async fn spawn(self: Arc<Self>, ctx: ConnectorContext) -> ConnectorResult<ConnectorHandle> {
        let path = ctx
            .config
            .settings
            .get("seed_file")
            .and_then(|value| value.as_str())
            .ok_or_else(|| ConnectorError::Other(anyhow::anyhow!("seed_file not configured")))?
            .to_string();

        let schema = ctx.config.schemas.get(0).cloned().ok_or_else(|| {
            ConnectorError::Other(anyhow::anyhow!("connector missing schema reference"))
        })?;

        let default_namespace = ctx.config.descriptor.namespaced_id();
        let privacy = ctx.config.descriptor.default_privacy;
        let tx = ctx.event_tx.clone();

        let task: JoinHandle<Result<()>> = tokio::spawn(async move {
            info!(path = %path, connector = %default_namespace, "Starting file replay connector");
            let file = File::open(&path).await.context("opening seed_file")?;
            let mut lines = BufReader::new(file).lines();

            while let Some(line) = lines.next_line().await? {
                if line.trim().is_empty() {
                    continue;
                }

                let payload: serde_json::Value = serde_json::from_str(&line)
                    .with_context(|| format!("invalid json in {}", path))?;

                let event = ConnectorEvent {
                    id: Uuid::new_v4(),
                    schema: schema.clone(),
                    namespace: default_namespace.clone(),
                    payload,
                    received_at: Utc::now(),
                    privacy,
                    source_descriptor: ctx.config.descriptor.clone(),
                    annotations: Default::default(),
                };

                tx.send(event)
                    .await
                    .map_err(|_| anyhow::anyhow!("event channel closed"))?;
            }

            Ok(())
        });

        Ok(ConnectorHandle::new(self.descriptor.clone(), task))
    }
}
