// Data models for perf-monitor-service
//
// Metadata:
// - version: 0.1.0
// - author: GPT-5.2 (Cursor agent)
// - timestamp: 2025-12-11
// - change_rationale: Define shared types for snapshots, issues, and remediation plans.

use serde::{Deserialize, Serialize};
use std::{
    collections::VecDeque,
    sync::Arc,
    time::{SystemTime, UNIX_EPOCH},
};
use tokio::sync::RwLock;

use crate::{context_client::ContextClient, syscalls};

#[derive(Clone)]
pub struct AppState {
    pub inner: Arc<AppStateInner>,
}

pub struct AppStateInner {
    pub store: SnapshotStore,
    pub suggest_only: bool,
    pub context_client: ContextClient,
}

#[derive(Clone)]
pub struct SnapshotStore {
    capacity: usize,
    inner: Arc<RwLock<VecDeque<Snapshot>>>,
}

impl SnapshotStore {
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity: capacity.max(1),
            inner: Arc::new(RwLock::new(VecDeque::new())),
        }
    }

    pub async fn push(&self, snap: Snapshot) {
        let mut guard = self.inner.write().await;
        guard.push_back(snap);
        while guard.len() > self.capacity {
            guard.pop_front();
        }
    }

    pub async fn latest(&self) -> Option<Snapshot> {
        let guard = self.inner.read().await;
        guard.back().cloned()
    }

    pub async fn len(&self) -> usize {
        let guard = self.inner.read().await;
        guard.len()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct SystemMetrics {
    pub cpu_usage_percent: u64,
    pub memory_total_kb: u64,
    pub memory_free_kb: u64,
    pub memory_used_kb: u64,
    pub processes_count: u64,
    pub network_rx_bytes: u64,
    pub network_tx_bytes: u64,
    pub disk_read_bytes: u64,
    pub disk_write_bytes: u64,
    pub timestamp: u64,
}

impl SystemMetrics {
    pub async fn collect() -> Result<Self, String> {
        // Prefer the kernel syscall when available.
        match syscalls::get_system_metrics() {
            Ok(m) => Ok(m),
            Err(e) => {
                // Fail safe: return minimal metrics rather than crashing.
                tracing::warn!("system metrics syscall failed: {}", e);
                Ok(Self::default_with_timestamp())
            }
        }
    }

    fn default_with_timestamp() -> Self {
        Self {
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs(),
            ..Default::default()
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct AiPerfStats {
    pub total_requests: u64,
    pub completed_requests: u64,
    pub failed_requests: u64,
    pub cancelled_requests: u64,

    pub avg_latency_ns: u64,
    pub min_latency_ns: u64,
    pub max_latency_ns: u64,
    pub avg_queue_wait_ns: u64,
    pub avg_processing_ns: u64,

    pub requests_per_second: u64,
    pub peak_requests_per_second: u64,

    pub gpu_inferences: u64,
    pub gpu_memory_allocated: u64,
    pub gpu_memory_used: u64,
    pub gpu_kernel_launches: u64,
    pub gpu_errors: u64,
    pub gpu_utilization_percent: u32,

    pub queue_depth_avg: u64,
    pub queue_depth_max: u64,

    pub worker_threads_active: u32,
    pub worker_threads_idle: u32,
    pub worker_load_avg: u32,

    pub batches_processed: u64,
    pub avg_batch_size: u64,
    pub batch_efficiency_percent: u32,

    pub cache_hits: u64,
    pub cache_misses: u64,
    pub cache_hit_rate_percent: u32,
}

impl AiPerfStats {
    pub async fn collect() -> Result<Self, String> {
        match syscalls::get_ai_perf_stats() {
            Ok(s) => Ok(s),
            Err(e) => {
                tracing::warn!("ai perf stats syscall failed: {}", e);
                Ok(Self::default())
            }
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Snapshot {
    pub system: SystemMetrics,
    pub ai_perf: AiPerfStats,
    pub collected_at_epoch_s: u64,
}

impl Snapshot {
    pub async fn collect() -> Self {
        let system = SystemMetrics::collect().await.unwrap_or_default();
        let ai_perf = AiPerfStats::collect().await.unwrap_or_default();
        let collected_at_epoch_s = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        Self {
            system,
            ai_perf,
            collected_at_epoch_s,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum Severity {
    Info,
    Warning,
    Error,
    Critical,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Issue {
    pub id: String,
    pub title: String,
    pub severity: Severity,
    pub description: String,
    pub evidence: Vec<String>,
    pub remediation_suggestions: Vec<String>,
    pub first_seen_epoch_s: u64,
    pub last_seen_epoch_s: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AnalyzeRequest {
    pub query: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AnalyzeResponse {
    pub narrative: String,
    pub issues: Vec<Issue>,
    pub snapshot: Snapshot,
    pub history_samples: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RemediationPlanRequest {
    pub issue_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RemediationAction {
    pub action_id: String,
    pub params: serde_json::Value,
    pub expected_impact: String,
    pub risk: String,
    pub rollback: String,
    pub preconditions: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RemediationPlanResponse {
    pub plan_id: String,
    pub issue_id: String,
    pub actions: Vec<RemediationAction>,
    pub notes: Vec<String>,
}
