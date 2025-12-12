use std::collections::HashMap;
use std::sync::Arc;

use anyhow::{anyhow, Result};
use parking_lot::RwLock;
use petgraph::stable_graph::{NodeIndex, StableGraph};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Hash)]
pub enum ContextNodeKind {
    Task,
    Communication,
    Workflow,
    Resource,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContextNode {
    pub id: Uuid,
    pub kind: ContextNodeKind,
    pub label: String,
    pub metadata: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContextEdge {
    pub relation: String,
    pub weight: f32,
}

#[derive(Debug, Clone)]
pub struct ContextResult {
    pub node: ContextNode,
    pub score: f32,
}

#[derive(Clone, Default)]
pub struct ContextGraph {
    graph: Arc<RwLock<StableGraph<ContextNode, ContextEdge>>>,
    node_lookup: Arc<RwLock<HashMap<Uuid, NodeIndex>>>,
    embeddings: Arc<RwLock<HashMap<Uuid, Vec<f32>>>>,
}

impl ContextGraph {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn upsert_node(&self, node: ContextNode, embedding: Vec<f32>) -> Result<Uuid> {
        let mut graph = self.graph.write();
        let mut lookup = self.node_lookup.write();
        let node_id = node.id;

        let index = if let Some(index) = lookup.get(&node_id) {
            let mut existing = graph.node_weight_mut(*index).expect("node index valid");
            *existing = node.clone();
            *index
        } else {
            let idx = graph.add_node(node.clone());
            lookup.insert(node_id, idx);
            idx
        };

        self.embeddings.write().insert(node_id, embedding);

        Ok(node_id)
    }

    pub fn relate(&self, from: Uuid, to: Uuid, relation: &str, weight: f32) -> Result<()> {
        let graph = &mut *self.graph.write();
        let lookup = self.node_lookup.read();
        let from_idx = *lookup
            .get(&from)
            .ok_or_else(|| anyhow!("unknown from node {}", from))?;
        let to_idx = *lookup
            .get(&to)
            .ok_or_else(|| anyhow!("unknown to node {}", to))?;

        graph.add_edge(
            from_idx,
            to_idx,
            ContextEdge {
                relation: relation.to_string(),
                weight,
            },
        );
        Ok(())
    }

    pub fn query_similar(&self, embedding: &[f32], top_k: usize) -> Vec<ContextResult> {
        let embeddings = self.embeddings.read();
        let lookup = self.node_lookup.read();
        let graph = self.graph.read();
        let mut scored: Vec<ContextResult> = embeddings
            .iter()
            .filter_map(|(id, stored)| {
                if stored.is_empty() || stored.len() != embedding.len() {
                    return None;
                }
                let score = cosine_similarity(stored, embedding);
                if score.is_nan() {
                    return None;
                }
                let node = lookup.get(id).and_then(|idx| graph.node_weight(*idx))?;
                Some(ContextResult {
                    node: node.clone(),
                    score,
                })
            })
            .collect();

        scored.sort_by(|a, b| {
            b.score
                .partial_cmp(&a.score)
                .unwrap_or(std::cmp::Ordering::Equal)
        });
        scored.truncate(top_k);
        scored
    }
}

fn cosine_similarity(a: &[f32], b: &[f32]) -> f32 {
    let dot = a.iter().zip(b.iter()).map(|(x, y)| x * y).sum::<f32>();
    let norm_a = a.iter().map(|x| x * x).sum::<f32>().sqrt();
    let norm_b = b.iter().map(|x| x * x).sum::<f32>().sqrt();
    if norm_a == 0.0 || norm_b == 0.0 {
        return 0.0;
    }
    dot / (norm_a * norm_b)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn similarity_ranking() {
        let graph = ContextGraph::new();
        let node = ContextNode {
            id: Uuid::new_v4(),
            kind: ContextNodeKind::Task,
            label: "Process invoices".into(),
            metadata: serde_json::json!({"owner": "ops"}),
        };
        graph
            .upsert_node(node.clone(), vec![0.0, 1.0, 0.5])
            .unwrap();
        let results = graph.query_similar(&[0.0, 0.8, 0.4], 1);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].node.label, node.label);
        assert!(results[0].score > 0.99);
    }
}
