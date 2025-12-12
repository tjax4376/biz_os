// BIZ_OS AI Service Daemon
// Main entry point for the AI runtime service

use anyhow::Result;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::signal;
use tracing::{error, info};

mod config;
mod connectors;
mod context_api;
mod inference;
mod ingestion;
mod learning;
mod model_manager;
mod workflow;
mod workflow_api;

use config::Config;
use diagnostic_service::{DiagnosticAnalyzer, DiagnosticApi};

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize tracing
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    info!("Starting BIZ_OS AI Service Daemon (aiservd)");

    // Load configuration
    let config_path = std::env::var("BIZOS_CONFIG")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("/etc/bizos/aiservd.toml"));

    let config = Config::load(&config_path)?;
    info!("Configuration loaded from: {:?}", config_path);

    // Initialize model manager
    let model_manager = Arc::new(model_manager::ModelManager::new(&config).await?);
    info!("Model manager initialized");

    // Initialize inference engine
    let inference_engine =
        Arc::new(inference::InferenceEngine::new(model_manager.clone(), &config).await?);
    info!("Inference engine initialized");

    // Initialize learning engine
    let learning_engine =
        Arc::new(learning::LearningEngine::new(model_manager.clone(), &config).await?);
    info!("Learning engine initialized");

    // Initialize diagnostic service
    let diagnostic_analyzer = Arc::new(DiagnosticAnalyzer::new());
    let diagnostic_api = DiagnosticApi::new(diagnostic_analyzer.clone());
    info!("Diagnostic service initialized");

    // Start HTTP server for diagnostic API
    let api_handle = {
        let router = diagnostic_api
            .router
            .merge(context_api::router(learning_engine.clone()))
            .merge(workflow_api::router(learning_engine.clone()));
        tokio::spawn(async move {
            let listener = tokio::net::TcpListener::bind("0.0.0.0:8080")
                .await
                .expect("Failed to bind to port 8080");
            info!("Diagnostic API listening on http://0.0.0.0:8080");
            axum::serve(listener, router)
                .await
                .expect("HTTP server error");
        })
    };

    let ingestion_service = ingestion::IngestionService::new(
        learning_engine.event_publisher(),
        learning_engine.schema_registry(),
    );
    let mut ingestion_handle =
        ingestion::start_server(ingestion_service, &config.ingestion_bind_addr).await?;

    // Start services
    info!("Starting AI runtime services...");

    // Handle shutdown signals
    let shutdown_signal = async {
        signal::ctrl_c()
            .await
            .expect("Failed to install Ctrl+C handler");
        info!("Shutdown signal received");
    };

    // Run until shutdown
    tokio::select! {
        _ = shutdown_signal => {
            info!("Shutdown signal received");
        }
        _ = api_handle => {
            info!("API server stopped");
        }
        result = &mut ingestion_handle => {
            match result {
                Err(join_err) => error!("Ingestion server task failed: {:?}", join_err),
                Ok(Err(err)) => error!("Ingestion server stopped: {:?}", err),
                Ok(Ok(())) => info!("Ingestion server exited cleanly"),
            }
        }
    }

    if !ingestion_handle.is_finished() {
        ingestion_handle.abort();
    }

    learning_engine.shutdown().await;

    info!("Shutting down AI Service Daemon");
    Ok(())
}
