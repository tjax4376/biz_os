// REST API for perf-monitor-service
//
// Metadata:
// - version: 0.1.0
// - author: GPT-5.2 (Cursor agent)
// - timestamp: 2025-12-11
// - change_rationale: Expose metrics, issues, and remediation planning over REST.

use axum::{
    extract::State,
    http::StatusCode,
    response::Json,
    routing::{get, post},
    Router,
};

use crate::{
    context_client::{ContextMatch, ContextQueryRequest, ContextQueryResponse},
    detectors,
    models::{
        AnalyzeRequest, AnalyzeResponse, AppState, Issue, RemediationAction,
        RemediationPlanRequest, RemediationPlanResponse,
    },
    ui::{verification, LayoutPlanner},
};

#[derive(serde::Serialize)]
struct HealthResponse {
    status: &'static str,
}

pub fn create_router(state: AppState) -> Router {
    Router::new()
        .route("/healthz", get(healthz))
        .route("/api/v1/metrics/current", get(get_system_metrics))
        .route("/api/v1/ai-perf/current", get(get_ai_perf_stats))
        .route("/api/v1/issues", get(get_issues))
        .route("/api/v1/analyze", post(analyze))
        .route("/api/v1/remediation/plan", post(remediation_plan))
        .route("/api/v1/remediation/execute", post(remediation_execute))
        .route("/api/v1/context/query", post(query_context))
        .route("/api/v1/ui/generate", post(generate_ui_layout))
        .with_state(state)
}

async fn healthz() -> Json<HealthResponse> {
    Json(HealthResponse { status: "ok" })
}

async fn get_system_metrics(
    State(state): State<AppState>,
) -> Result<Json<crate::models::SystemMetrics>, StatusCode> {
    let snap = state
        .inner
        .store
        .latest()
        .await
        .ok_or(StatusCode::SERVICE_UNAVAILABLE)?;
    Ok(Json(snap.system))
}

async fn get_ai_perf_stats(
    State(state): State<AppState>,
) -> Result<Json<crate::models::AiPerfStats>, StatusCode> {
    let snap = state
        .inner
        .store
        .latest()
        .await
        .ok_or(StatusCode::SERVICE_UNAVAILABLE)?;
    Ok(Json(snap.ai_perf))
}

async fn get_issues(State(state): State<AppState>) -> Result<Json<Vec<Issue>>, StatusCode> {
    let snap = state
        .inner
        .store
        .latest()
        .await
        .ok_or(StatusCode::SERVICE_UNAVAILABLE)?;
    Ok(Json(detectors::detect(&snap)))
}

async fn analyze(
    State(state): State<AppState>,
    Json(req): Json<AnalyzeRequest>,
) -> Result<Json<AnalyzeResponse>, StatusCode> {
    if req.query.trim().is_empty() || req.query.len() > 4096 {
        return Err(StatusCode::BAD_REQUEST);
    }

    let snap = state
        .inner
        .store
        .latest()
        .await
        .ok_or(StatusCode::SERVICE_UNAVAILABLE)?;
    let history_samples = state.inner.store.len().await;

    let issues = detectors::detect(&snap);

    // AI narration placeholder (safe): narrative is derived from deterministic issues.
    let narrative = if issues.is_empty() {
        format!(
            "Query: '{}'. No issues detected from current counters.",
            req.query
        )
    } else {
        format!(
            "Query: '{}'. Detected {} issue(s): {}.",
            req.query,
            issues.len(),
            issues
                .iter()
                .map(|i| format!("{}({:?})", i.title, i.severity))
                .collect::<Vec<_>>()
                .join(", ")
        )
    };

    Ok(Json(AnalyzeResponse {
        narrative,
        issues,
        snapshot: snap,
        history_samples,
    }))
}

async fn remediation_plan(
    State(_state): State<AppState>,
    Json(req): Json<RemediationPlanRequest>,
) -> Result<Json<RemediationPlanResponse>, StatusCode> {
    if req.issue_id.trim().is_empty() || req.issue_id.len() > 256 {
        return Err(StatusCode::BAD_REQUEST);
    }

    // Allowlisted remediation actions (planning only).
    let mut actions: Vec<RemediationAction> = Vec::new();

    // Throttle suggestion for queue saturation.
    if req.issue_id == "ai_queue_saturation" {
        actions.push(RemediationAction {
            action_id: "throttle_ai_requests".to_string(),
            params: serde_json::json!({"percent": 20, "duration_s": 60}),
            expected_impact: "Reduce queue depth and latency by lowering incoming load".to_string(),
            risk: "Low".to_string(),
            rollback: "Remove throttle after duration or reduce percent".to_string(),
            preconditions: vec![
                "Throttling capability available in runtime/edge gateway".to_string()
            ],
        });
    }

    // Cache clear suggestion when cache hit rate is low / errors present.
    actions.push(RemediationAction {
        action_id: "clear_ai_cache".to_string(),
        params: serde_json::json!({}),
        expected_impact: "Eliminate stale/poisoned cache entries; may increase compute temporarily"
            .to_string(),
        risk: "Low".to_string(),
        rollback: "None required (cache repopulates)".to_string(),
        preconditions: vec!["AI cache clear endpoint/syscall exists".to_string()],
    });

    Ok(Json(RemediationPlanResponse {
        plan_id: format!("plan-{}", uuid_suffix()),
        issue_id: req.issue_id,
        actions,
        notes: vec![
            "Suggest-only mode: plans are not executed by this service by default.".to_string(),
            "All actions must be allowlisted and pass policy checks before any future execution feature is enabled.".to_string(),
        ],
    }))
}

async fn remediation_execute(
    State(state): State<AppState>,
) -> Result<Json<serde_json::Value>, StatusCode> {
    // Per user choice (1): suggest-only.
    if state.inner.suggest_only {
        return Err(StatusCode::FORBIDDEN);
    }

    // Even if enabled in future, execution must be allowlisted + authenticated.
    Err(StatusCode::NOT_IMPLEMENTED)
}

async fn query_context(
    State(state): State<AppState>,
    Json(req): Json<ContextQueryRequest>,
) -> Result<Json<ContextQueryResponse>, StatusCode> {
    if req.embedding.is_empty() || req.embedding.len() > 1024 {
        return Err(StatusCode::BAD_REQUEST);
    }

    state
        .inner
        .context_client
        .query(req)
        .await
        .map(Json)
        .map_err(|err| {
            tracing::error!("context query failed: {err:?}");
            StatusCode::BAD_GATEWAY
        })
}

#[derive(Debug, Clone, serde::Deserialize)]
struct UiLayoutRequest {
    intent: String,
    screen: String,
    #[serde(default)]
    embedding: Vec<f32>,
    #[serde(default = "default_ui_top_k")]
    top_k: usize,
}

fn default_ui_top_k() -> usize {
    5
}

#[derive(Debug, Clone, serde::Serialize)]
struct UiLayoutResponse {
    layout: Vec<crate::ui::GeneratedComponent>,
}

async fn generate_ui_layout(
    State(state): State<AppState>,
    Json(req): Json<UiLayoutRequest>,
) -> Result<Json<UiLayoutResponse>, StatusCode> {
    if req.intent.trim().is_empty() || req.intent.len() > 1024 {
        return Err(StatusCode::BAD_REQUEST);
    }

    let screen_trimmed = req.screen.trim();
    if screen_trimmed.is_empty() || screen_trimmed.len() > 32 {
        return Err(StatusCode::BAD_REQUEST);
    }
    let normalized_screen = screen_trimmed.to_ascii_lowercase();
    if normalized_screen != "mobile" && normalized_screen != "desktop" {
        return Err(StatusCode::BAD_REQUEST);
    }

    let mut context_matches: Vec<ContextMatch> = Vec::new();
    if !req.embedding.is_empty() {
        let ctx_response = state
            .inner
            .context_client
            .query(ContextQueryRequest {
                embedding: req.embedding.clone(),
                top_k: req.top_k,
            })
            .await
            .map_err(|err| {
                tracing::error!("context enrichment failed: {err:?}");
                StatusCode::BAD_GATEWAY
            })?;
        context_matches = ctx_response.results;
    }

    let planner = LayoutPlanner::default();
    let layout = planner.plan(&req.intent, &normalized_screen, &context_matches);
    verification::verify_layout(&planner, &layout).map_err(|_| StatusCode::UNPROCESSABLE_ENTITY)?;

    Ok(Json(UiLayoutResponse { layout }))
}

fn uuid_suffix() -> String {
    // Lightweight, non-crypto identifier suffix for logs.
    // Security-sensitive IDs must use a CSPRNG; this one is NOT used for auth.
    let n = (std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos()
        % 1_000_000_000) as u64;
    format!("{}", n)
}
