use std::sync::Arc;

use axum::{extract::State, http::StatusCode, response::Json, routing::post, Router};
use serde::{Deserialize, Serialize};
use tracing::warn;

use crate::learning::LearningEngine;

pub fn router(engine: Arc<LearningEngine>) -> Router {
    Router::new()
        .route("/api/v1/context/query", post(query_context))
        .with_state(engine)
}

#[derive(Debug, Clone, Deserialize)]
pub struct ContextQueryRequest {
    pub embedding: Vec<f32>,
    #[serde(default = "default_top_k")]
    pub top_k: usize,
}

fn default_top_k() -> usize {
    5
}

#[derive(Debug, Clone, Serialize)]
pub struct ContextQueryResponse {
    pub results: Vec<ContextMatch>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ContextMatch {
    pub label: String,
    pub kind: String,
    pub score: f32,
    pub metadata: serde_json::Value,
}

async fn query_context(
    State(engine): State<Arc<LearningEngine>>,
    Json(req): Json<ContextQueryRequest>,
) -> Result<Json<ContextQueryResponse>, StatusCode> {
    if req.embedding.is_empty() || req.embedding.len() > 1024 {
        return Err(StatusCode::BAD_REQUEST);
    }

    let context_graph = engine.context_graph();
    let results = context_graph.query_similar(&req.embedding, req.top_k.min(25));

    if results.is_empty() {
        warn!("context graph returned no matches");
    }

    Ok(Json(ContextQueryResponse {
        results: results
            .into_iter()
            .map(|item| ContextMatch {
                label: item.node.label,
                kind: format!("{:?}", item.node.kind),
                score: item.score,
                metadata: item.node.metadata,
            })
            .collect(),
    }))
}
