use anyhow::{anyhow, Result};
use serde::Serialize;
use tokio::time::{sleep, Duration};

use super::dsl::{WorkflowDefinition, WorkflowStep};

#[derive(Debug, Clone, Serialize)]
pub struct SimulationResult {
    pub executed_steps: Vec<String>,
    pub total_latency_ms: u64,
}

#[derive(Debug, Clone)]
pub struct SandboxConfig {
    pub max_steps: usize,
    pub max_latency_ms: u64,
}

impl Default for SandboxConfig {
    fn default() -> Self {
        Self {
            max_steps: 16,
            max_latency_ms: 5_000,
        }
    }
}

#[derive(Clone)]
pub struct SandboxRunner {
    config: SandboxConfig,
}

impl SandboxRunner {
    pub fn new(config: SandboxConfig) -> Self {
        Self { config }
    }

    pub async fn simulate(&self, workflow: &WorkflowDefinition) -> Result<SimulationResult> {
        if workflow.steps.len() > self.config.max_steps {
            return Err(anyhow!("workflow exceeds step budget"));
        }

        let mut executed = Vec::new();
        let mut total_latency_ms = 0u64;

        for step in &workflow.steps {
            let latency = match step {
                WorkflowStep::HttpCall { id, .. } => {
                    executed.push(format!("http:{}", id));
                    150
                }
                WorkflowStep::ShellCommand { id, .. } => {
                    executed.push(format!("shell:{}", id));
                    80
                }
                WorkflowStep::HumanApproval { id, .. } => {
                    executed.push(format!("human:{}", id));
                    500
                }
            };

            total_latency_ms += latency;
            if total_latency_ms > self.config.max_latency_ms {
                return Err(anyhow!("simulation exceeded latency budget"));
            }

            sleep(Duration::from_millis(1)).await;
        }

        Ok(SimulationResult {
            executed_steps: executed,
            total_latency_ms,
        })
    }
}
