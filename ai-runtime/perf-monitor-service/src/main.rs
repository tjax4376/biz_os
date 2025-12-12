// BIZ_OS Performance Monitor Service
//
// Metadata:
// - version: 0.1.0
// - author: GPT-5.2 (Cursor agent)
// - timestamp: 2025-12-11
// - change_rationale: Provide an AI-assisted performance monitor (suggest-only)
//   that reuses ai-runtime patterns and can propose safe, allowlisted remediations.
//
// Security/Safety:
// - Suggest-only by default; no arbitrary command execution.
// - All APIs validate inputs and fail safely.

use anyhow::Result;
use std::{net::SocketAddr, sync::Arc, time::Duration};
use tokio::{signal, time};
use tracing::info;

mod api;
mod context_client;
mod detectors;
mod models;
mod syscalls;
mod ui;

use crate::{
    api::create_router,
    context_client::ContextClient,
    models::{AppState, AppStateInner, SnapshotStore},
};

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    let bind_addr: SocketAddr = std::env::var("BIZOS_PERF_MONITOR_BIND")
        .unwrap_or_else(|_| "127.0.0.1:8081".to_string())
        .parse()
        .unwrap_or_else(|_| SocketAddr::from(([127, 0, 0, 1], 8081)));

    let poll_interval_ms: u64 = std::env::var("BIZOS_PERF_MONITOR_POLL_MS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(1000);

    let history_capacity: usize = std::env::var("BIZOS_PERF_MONITOR_HISTORY_CAP")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(900); // ~15 min at 1s

    let store = SnapshotStore::new(history_capacity);
    let context_base =
        std::env::var("BIZOS_CONTEXT_BASE_URL").unwrap_or_else(|_| "http://127.0.0.1:8080".into());
    let context_client = ContextClient::new(context_base, Duration::from_secs(3));

    let state = AppState {
        inner: Arc::new(AppStateInner {
            store,
            suggest_only: true, // Per user choice (1)
            context_client,
        }),
    };

    // Background polling loop.
    {
        let state = state.clone();
        let interval = Duration::from_millis(poll_interval_ms);
        tokio::spawn(async move {
            let mut ticker = time::interval(interval);
            loop {
                ticker.tick().await;
                let snapshot = crate::models::Snapshot::collect().await;
                state.inner.store.push(snapshot).await;
            }
        });
    }

    let app = create_router(state);

    let listener = tokio::net::TcpListener::bind(bind_addr).await?;
    info!("perf-monitor-service listening on http://{}", bind_addr);

    let shutdown = async {
        signal::ctrl_c().await.expect("install Ctrl+C handler");
        info!("shutdown requested");
    };

    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown)
        .await?;

    Ok(())
}
