//! Connector registry orchestrates lifecycle for all configured integrations.

use std::collections::HashMap;
use std::sync::Arc;

use anyhow::{Context, Result};
use tokio::task::JoinHandle;
use tracing::{error, info};

use crate::connector::{
    forward_events, Connector, ConnectorConfig, ConnectorDescriptor, ConnectorHandle,
};
use crate::event_mesh::EventPublisher;
use crate::schema::SchemaRegistry;

struct RegisteredConnector {
    pub config: ConnectorConfig,
    pub implementation: Arc<dyn Connector>,
    pub handle: Option<ConnectorHandle>,
}

impl RegisteredConnector {
    fn descriptor(&self) -> &ConnectorDescriptor {
        self.implementation.descriptor()
    }
}

#[derive(Default)]
pub struct ConnectorRegistry {
    connectors: HashMap<String, RegisteredConnector>,
    forwarders: Vec<JoinHandle<()>>,
}

impl ConnectorRegistry {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register(&mut self, config: ConnectorConfig, connector: Arc<dyn Connector>) {
        let key = connector.descriptor().namespaced_id();
        info!(connector = %key, "Registering connector");
        self.connectors.insert(
            key,
            RegisteredConnector {
                config,
                implementation: connector,
                handle: None,
            },
        );
    }

    pub async fn start_all(
        &mut self,
        schema_registry: SchemaRegistry,
        publisher: EventPublisher,
    ) -> Result<()> {
        for (key, entry) in self.connectors.iter_mut() {
            let conn = entry.implementation.clone();
            let cfg = entry.config.clone();
            conn.authorize(&cfg)
                .await
                .with_context(|| format!("connector {} failed auth", key))?;

            let (ctx, rx) = crate::connector::ConnectorContext::new(
                cfg.clone(),
                schema_registry.clone(),
                publisher.clone(),
            );

            let handle = conn.spawn(ctx).await?;
            let descriptor = handle.descriptor.clone();

            let forward_publisher = publisher.clone();
            let forward_registry = schema_registry.clone();
            let throttle = cfg
                .rate_limit_override
                .or(Some(entry.config.descriptor.max_rate_per_sec));
            self.forwarders.push(tokio::spawn(async move {
                if let Err(err) = forward_events(
                    rx,
                    forward_publisher,
                    forward_registry,
                    throttle,
                )
                .await
                {
                    error!(connector = %descriptor.namespaced_id(), error = ?err, "forwarder failed");
                }
            }));

            entry.handle = Some(handle);
        }

        Ok(())
    }

    pub async fn stop_all(&mut self) {
        for (key, entry) in self.connectors.iter_mut() {
            if let Some(handle) = entry.handle.take() {
                if let Err(err) = handle.wait().await {
                    error!(connector = key, error = ?err, "connector shutdown failed");
                }
            }
        }

        while let Some(handle) = self.forwarders.pop() {
            handle.abort();
        }
    }

    pub fn descriptors(&self) -> Vec<ConnectorDescriptor> {
        self.connectors
            .values()
            .map(|entry| entry.descriptor().clone())
            .collect()
    }
}
