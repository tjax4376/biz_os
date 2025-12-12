use anyhow::{anyhow, Result};

use super::{GeneratedComponent, LayoutPlanner};

pub fn verify_layout(planner: &LayoutPlanner, components: &[GeneratedComponent]) -> Result<()> {
    let allowlist = planner.allowlist();
    if components.is_empty() {
        return Err(anyhow!("layout must include at least one component"));
    }

    for component in components {
        if !allowlist.contains(&component.component_type) {
            return Err(anyhow!(
                "component {:?} not in allowlist",
                component.component_type
            ));
        }

        if component.constraints.colspan == 0 || component.constraints.colspan > 3 {
            return Err(anyhow!("invalid colspan for {}", component.id));
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_invalid_constraints() {
        let planner = LayoutPlanner::default();
        let mut layout = planner.plan("diagnose", "desktop", &[]);
        layout[0].constraints.colspan = 0;
        assert!(verify_layout(&planner, &layout).is_err());
    }
}
