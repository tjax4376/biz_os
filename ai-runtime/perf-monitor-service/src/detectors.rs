// Deterministic detectors for performance issues.
//
// Metadata:
// - version: 0.1.0
// - author: GPT-5.2 (Cursor agent)
// - timestamp: 2025-12-11
// - change_rationale: Provide safe, deterministic signal extraction before AI narration.

use crate::models::{Issue, Severity, Snapshot};

pub fn detect(snapshot: &Snapshot) -> Vec<Issue> {
    let mut issues = Vec::new();
    let now = snapshot.collected_at_epoch_s;

    // Memory pressure detector.
    if snapshot.system.memory_total_kb > 0 {
        let used_pct = (snapshot.system.memory_used_kb.saturating_mul(100))
            / snapshot.system.memory_total_kb.max(1);
        if used_pct >= 95 {
            issues.push(Issue {
                id: "mem_pressure_critical".to_string(),
                title: "Critical memory pressure".to_string(),
                severity: Severity::Critical,
                description: "System memory usage is critically high.".to_string(),
                evidence: vec![format!(
                    "memory_used_percent={}%, used_kb={}, total_kb={}",
                    used_pct, snapshot.system.memory_used_kb, snapshot.system.memory_total_kb
                )],
                remediation_suggestions: vec![
                    "Identify top memory consumers (ps/top) and stop non-essential workloads"
                        .to_string(),
                    "Investigate memory leaks in long-running processes".to_string(),
                ],
                first_seen_epoch_s: now,
                last_seen_epoch_s: now,
            });
        } else if used_pct >= 85 {
            issues.push(Issue {
                id: "mem_pressure_warning".to_string(),
                title: "Elevated memory pressure".to_string(),
                severity: Severity::Warning,
                description: "System memory usage is high and may impact latency.".to_string(),
                evidence: vec![format!(
                    "memory_used_percent={}%, used_kb={}, total_kb={}",
                    used_pct, snapshot.system.memory_used_kb, snapshot.system.memory_total_kb
                )],
                remediation_suggestions: vec![
                    "Reduce concurrent workloads".to_string(),
                    "Monitor swap and reclaim activity".to_string(),
                ],
                first_seen_epoch_s: now,
                last_seen_epoch_s: now,
            });
        }
    }

    // AI runtime failure rate detector.
    let total = snapshot.ai_perf.total_requests;
    if total > 0 {
        let failed_pct = (snapshot.ai_perf.failed_requests.saturating_mul(100)) / total.max(1);
        if failed_pct >= 10 {
            issues.push(Issue {
                id: "ai_fail_rate_high".to_string(),
                title: "High AI request failure rate".to_string(),
                severity: if failed_pct >= 25 {
                    Severity::Critical
                } else {
                    Severity::Error
                },
                description: "A significant portion of AI requests are failing.".to_string(),
                evidence: vec![format!(
                    "failed_requests={}, total_requests={}, failed_percent={}%, gpu_errors={}",
                    snapshot.ai_perf.failed_requests,
                    total,
                    failed_pct,
                    snapshot.ai_perf.gpu_errors
                )],
                remediation_suggestions: vec![
                    "Inspect aiservd logs for model load/inference errors".to_string(),
                    "If GPU errors are increasing, consider forcing CPU inference temporarily"
                        .to_string(),
                ],
                first_seen_epoch_s: now,
                last_seen_epoch_s: now,
            });
        }
    }

    // Queue saturation heuristic.
    if snapshot.ai_perf.queue_depth_max >= 1000 {
        issues.push(Issue {
            id: "ai_queue_saturation".to_string(),
            title: "AI queue saturation".to_string(),
            severity: Severity::Warning,
            description:
                "AI request queue depth is high, suggesting saturation or downstream slowdowns."
                    .to_string(),
            evidence: vec![format!(
                "queue_depth_max={}, queue_depth_avg={}",
                snapshot.ai_perf.queue_depth_max, snapshot.ai_perf.queue_depth_avg
            )],
            remediation_suggestions: vec![
                "Consider throttling incoming AI requests".to_string(),
                "Check worker utilization and model execution latency".to_string(),
            ],
            first_seen_epoch_s: now,
            last_seen_epoch_s: now,
        });
    }

    // GPU error detector.
    if snapshot.ai_perf.gpu_errors > 0 {
        issues.push(Issue {
            id: "gpu_errors_present".to_string(),
            title: "GPU errors detected".to_string(),
            severity: Severity::Error,
            description: "GPU errors are present in AI runtime counters.".to_string(),
            evidence: vec![format!(
                "gpu_errors={}, gpu_utilization_percent={}%, gpu_memory_used={}",
                snapshot.ai_perf.gpu_errors,
                snapshot.ai_perf.gpu_utilization_percent,
                snapshot.ai_perf.gpu_memory_used
            )],
            remediation_suggestions: vec![
                "Check GPU driver health and aiservd GPU backend configuration".to_string(),
                "Consider fallback to CPU inference if errors persist".to_string(),
            ],
            first_seen_epoch_s: now,
            last_seen_epoch_s: now,
        });
    }

    issues
}
