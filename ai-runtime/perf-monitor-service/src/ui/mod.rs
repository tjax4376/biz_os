pub mod verification;

use serde::Serialize;

use crate::context_client::ContextMatch;

#[derive(Debug, Clone, Serialize)]
pub struct GeneratedComponent {
    pub id: String,
    pub component_type: ComponentType,
    pub title: String,
    pub data: serde_json::Value,
    pub constraints: LayoutConstraints,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ComponentType {
    SummaryCard,
    Timeline,
    ContextPanel,
    Chart,
}

#[derive(Debug, Clone, Serialize)]
pub struct LayoutConstraints {
    pub row: u8,
    pub column: u8,
    pub colspan: u8,
}

pub struct LayoutPlanner {
    component_allowlist: Vec<ComponentType>,
}

impl Default for LayoutPlanner {
    fn default() -> Self {
        Self {
            component_allowlist: vec![
                ComponentType::SummaryCard,
                ComponentType::Timeline,
                ComponentType::ContextPanel,
                ComponentType::Chart,
            ],
        }
    }
}

impl LayoutPlanner {
    pub fn plan(
        &self,
        intent: &str,
        screen: &str,
        context: &[ContextMatch],
    ) -> Vec<GeneratedComponent> {
        let mut components = Vec::new();
        let base_row = if screen == "mobile" { 0 } else { 1 };

        components.push(GeneratedComponent {
            id: "summary".into(),
            component_type: ComponentType::SummaryCard,
            title: format!("Summary for {intent}"),
            data: serde_json::json!({
                "intent": intent,
                "screen": screen,
                "context_count": context.len()
            }),
            constraints: LayoutConstraints {
                row: base_row,
                column: 0,
                colspan: if screen == "mobile" { 1 } else { 2 },
            },
        });

        if !context.is_empty() {
            components.push(GeneratedComponent {
                id: "context".into(),
                component_type: ComponentType::ContextPanel,
                title: "Related context".into(),
                data: serde_json::json!({ "matches": context }),
                constraints: LayoutConstraints {
                    row: base_row + 1,
                    column: 0,
                    colspan: 1,
                },
            });
        }

        components.push(GeneratedComponent {
            id: "timeline".into(),
            component_type: ComponentType::Timeline,
            title: "Recent performance".into(),
            data: serde_json::json!({ "points": [0.2, 0.4, 0.6, 0.7] }),
            constraints: LayoutConstraints {
                row: base_row + 1,
                column: if screen == "mobile" { 0 } else { 1 },
                colspan: 1,
            },
        });

        components
    }

    pub fn allowlist(&self) -> &[ComponentType] {
        &self.component_allowlist
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn planner_generates_components() {
        let planner = LayoutPlanner::default();
        let comps = planner.plan("diagnose gpu", "desktop", &[]);
        assert!(!comps.is_empty());
        assert!(comps
            .iter()
            .all(|c| planner.allowlist().contains(&c.component_type)));
    }
}
