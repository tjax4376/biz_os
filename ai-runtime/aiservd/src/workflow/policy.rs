use std::collections::{HashMap, HashSet};

use serde::{Deserialize, Serialize};

use super::dsl::{WorkflowDefinition, WorkflowStep};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PolicyRule {
    pub id: String,
    pub allowed_methods: HashSet<String>,
    pub allowed_domains: HashSet<String>,
}

#[derive(Debug, Clone)]
pub struct PolicyEngine {
    rules: HashMap<String, PolicyRule>,
}

#[derive(Debug, Clone, Serialize)]
pub struct PolicyDecision {
    pub approved: bool,
    pub violations: Vec<String>,
}

impl PolicyEngine {
    pub fn new(rules: Vec<PolicyRule>) -> Self {
        let map = rules
            .into_iter()
            .map(|rule| (rule.id.clone(), rule))
            .collect();
        Self { rules: map }
    }

    pub fn evaluate(&self, workflow: &WorkflowDefinition) -> PolicyDecision {
        let mut violations = Vec::new();
        for step in &workflow.steps {
            match step {
                WorkflowStep::HttpCall {
                    id,
                    method,
                    url,
                    allowlist_ref,
                } => {
                    if let Some(rule) = self.rules.get(allowlist_ref) {
                        if !rule.allowed_methods.contains(&method.to_uppercase()) {
                            violations
                                .push(format!("Step {} uses disallowed method {}", id, method));
                        }
                        if !rule
                            .allowed_domains
                            .iter()
                            .any(|domain| url.starts_with(domain))
                        {
                            violations.push(format!(
                                "Step {} targets domain outside rule {}",
                                id, allowlist_ref
                            ));
                        }
                    } else {
                        violations.push(format!(
                            "Step {} references unknown rule {}",
                            id, allowlist_ref
                        ));
                    }
                }
                WorkflowStep::ShellCommand {
                    id, allowlist_ref, ..
                } => {
                    if !self.rules.contains_key(allowlist_ref) {
                        violations.push(format!(
                            "Shell step {} missing allowlist {}",
                            id, allowlist_ref
                        ));
                    }
                }
                WorkflowStep::HumanApproval { .. } => {}
            }
        }

        PolicyDecision {
            approved: violations.is_empty(),
            violations,
        }
    }
}

impl Default for PolicyEngine {
    fn default() -> Self {
        Self::new(vec![PolicyRule {
            id: "default_http".into(),
            allowed_methods: HashSet::from(["GET".into(), "POST".into()]),
            allowed_domains: HashSet::from(["https://api.company.com".into()]),
        }])
    }
}
