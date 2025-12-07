// User Action Monitor
// Monitors user actions for learning

use std::sync::Arc;
use tokio::sync::mpsc;
use tracing::{info, debug};
use anyhow::Result;

#[derive(Debug, Clone)]
pub struct UserEvent {
    pub event_type: EventType,
    pub timestamp: u64,
    pub data: Vec<u8>,
}

#[derive(Debug, Clone)]
pub enum EventType {
    ProcessExecution,
    FileOperation,
    NetworkActivity,
    UIInteraction,
}

pub struct Monitor {
    event_tx: mpsc::Sender<UserEvent>,
}

impl Monitor {
    pub fn new(event_tx: mpsc::Sender<UserEvent>) -> Self {
        Self { event_tx }
    }

    pub async fn record_event(&self, event: UserEvent) -> Result<()> {
        debug!("Recording event: {:?}", event.event_type);
        
        self.event_tx.send(event).await
            .map_err(|e| anyhow::anyhow!("Failed to send event: {}", e))?;
        
        Ok(())
    }

    pub async fn record_process_execution(&self, process_name: &str, args: &[String]) -> Result<()> {
        let event = UserEvent {
            event_type: EventType::ProcessExecution,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?
                .as_secs(),
            data: serde_json::to_vec(&serde_json::json!({
                "process": process_name,
                "args": args,
            }))?,
        };
        
        self.record_event(event).await
    }

    pub async fn record_file_operation(&self, operation: &str, path: &str) -> Result<()> {
        let event = UserEvent {
            event_type: EventType::FileOperation,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?
                .as_secs(),
            data: serde_json::to_vec(&serde_json::json!({
                "operation": operation,
                "path": path,
            }))?,
        };
        
        self.record_event(event).await
    }
}

