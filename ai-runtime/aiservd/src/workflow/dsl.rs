use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WorkflowDefinition {
    pub name: String,
    pub version: String,
    pub steps: Vec<WorkflowStep>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum WorkflowStep {
    HttpCall {
        id: String,
        method: String,
        url: String,
        allowlist_ref: String,
    },
    ShellCommand {
        id: String,
        command: String,
        allowlist_ref: String,
    },
    HumanApproval {
        id: String,
        approver: String,
    },
}

impl WorkflowDefinition {
    pub fn manifest_id(&self) -> String {
        format!("{}-{}-{}", self.name, self.version, Uuid::new_v4())
    }
}
