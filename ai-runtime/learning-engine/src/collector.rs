// Event Collector
// Collects and stores user events for learning

use super::monitor::{EventType, UserEvent};
use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio::sync::RwLock;
use tracing::{info, warn};

#[derive(Debug, Serialize, Deserialize)]
struct StoredEvent {
    event: UserEvent,
    privacy_level: PrivacyLevel,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub enum PrivacyLevel {
    Public,
    Private,
    Sensitive,
}

pub struct Collector {
    event_rx: mpsc::Receiver<UserEvent>,
    storage_path: PathBuf,
    events: Arc<RwLock<Vec<StoredEvent>>>,
    privacy_settings: Arc<RwLock<PrivacySettings>>,
}

#[derive(Debug, Clone)]
struct PrivacySettings {
    default_level: PrivacyLevel,
    per_event_type: std::collections::HashMap<EventType, PrivacyLevel>,
}

impl Collector {
    pub fn new(event_rx: mpsc::Receiver<UserEvent>, storage_path: PathBuf) -> Self {
        Self {
            event_rx,
            storage_path,
            events: Arc::new(RwLock::new(Vec::new())),
            privacy_settings: Arc::new(RwLock::new(PrivacySettings {
                default_level: PrivacyLevel::Private,
                per_event_type: std::collections::HashMap::new(),
            })),
        }
    }

    pub async fn run(&mut self) -> Result<()> {
        info!("Starting event collector");

        // Ensure storage directory exists
        if let Some(parent) = self.storage_path.parent() {
            std::fs::create_dir_all(parent)?;
        }

        while let Some(event) = self.event_rx.recv().await {
            self.process_event(event).await?;
        }

        Ok(())
    }

    async fn process_event(&self, event: UserEvent) -> Result<()> {
        let privacy_level = self.determine_privacy_level(&event).await;

        let stored = StoredEvent {
            event,
            privacy_level,
        };

        // Store event
        let mut events = self.events.write().await;
        events.push(stored);

        // TODO: Persist to disk periodically
        // For now, keep in memory

        Ok(())
    }

    async fn determine_privacy_level(&self, event: &UserEvent) -> PrivacyLevel {
        let settings = self.privacy_settings.read().await;

        settings
            .per_event_type
            .get(&event.event_type)
            .copied()
            .unwrap_or(settings.default_level)
    }

    pub async fn get_events(&self, limit: usize) -> Vec<StoredEvent> {
        let events = self.events.read().await;
        events.iter().take(limit).cloned().collect()
    }
}
