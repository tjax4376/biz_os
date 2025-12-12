use std::sync::Arc;

use axum::{extract::State, http::StatusCode, response::Json, routing::post, Router};
use serde::Serialize;
use tracing::error;

use crate::learning::LearningEngine;
use crate::workflow::{PolicyDecision, WorkflowDefinition};

pub fn router(engine: Arc<LearningEngine>) -> Router {
    Router::new()
        .route("/api/v1/workflows/simulate", post(simulate_workflow))
        .with_state(engine)
}

#[derive(Debug, Serialize)]
pub struct WorkflowSimulationResponse {
    pub manifest_id: String,
    pub policy: PolicyDecision,
    pub simulation: Option<crate::workflow::SimulationResult>,
}

async fn simulate_workflow(
    State(engine): State<Arc<LearningEngine>>,
    Json(workflow): Json<WorkflowDefinition>,
) -> Result<Json<WorkflowSimulationResponse>, StatusCode> {
    if workflow.steps.is_empty() {
        return Err(StatusCode::BAD_REQUEST);
    }

    let policy = engine.policy_engine().evaluate(&workflow);
    let mut simulation = None;

    if policy.approved {
        simulation = match engine.sandbox_runner().simulate(&workflow).await {
            Ok(result) => Some(result),
            Err(err) => {
                error!(?err, "workflow simulation failed");
                return Err(StatusCode::UNPROCESSABLE_ENTITY);
            }
        };
    }

    Ok(Json(WorkflowSimulationResponse {
        manifest_id: workflow.manifest_id(),
        policy,
        simulation,
    }))
}
