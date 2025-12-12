// BIZ_OS Diagnostic API
// REST API endpoint for natural language diagnostic queries

use super::{analyzer::DiagnosticAnalyzer, metrics::SystemMetrics};
use anyhow::Result;
use axum::{
    extract::Extension,
    http::StatusCode,
    response::Json,
    routing::post,
    Router,
};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

/// Diagnostic query request
#[derive(Debug, Deserialize)]
pub struct DiagnosticQuery {
    pub query: String,
}

/// Diagnostic response
#[derive(Debug, Serialize)]
pub struct DiagnosticResponse {
    pub result: super::analyzer::DiagnosticResult,
    pub metrics: SystemMetrics,
}

/// API state
#[derive(Clone)]
pub struct DiagnosticApiState {
    pub analyzer: Arc<DiagnosticAnalyzer>,
}

/// Create diagnostic API router
pub fn create_router(state: DiagnosticApiState) -> Router {
    Router::new()
        .route("/api/v1/diagnose", post(handle_diagnostic_query))
        // Use Extension to avoid Router<S> state generic, keeping the router serveable.
        .layer(Extension(state))
}

/// Handle diagnostic query endpoint
async fn handle_diagnostic_query(
    Extension(state): Extension<DiagnosticApiState>,
    Json(request): Json<DiagnosticQuery>,
) -> Result<Json<DiagnosticResponse>, StatusCode> {
    // Collect current system metrics
    let metrics = SystemMetrics::collect()
        .map_err(|e| {
            tracing::error!("Failed to collect system metrics: {}", e);
            StatusCode::INTERNAL_SERVER_ERROR
        })?;

    // Analyze using AI
    let result = state
        .analyzer
        .analyze(&request.query, &metrics)
        .await
        .map_err(|e| {
            tracing::error!("Failed to analyze system: {}", e);
            StatusCode::INTERNAL_SERVER_ERROR
        })?;

    Ok(Json(DiagnosticResponse { result, metrics }))
}

/// Diagnostic API wrapper
pub struct DiagnosticApi {
    pub router: Router,
}

impl DiagnosticApi {
    /// Create a new DiagnosticApi
    pub fn new(analyzer: Arc<DiagnosticAnalyzer>) -> Self {
        let state = DiagnosticApiState { analyzer };
        let router = create_router(state);
        
        Self { router }
    }
}
