// Learning Engine - Real-time learning from user actions

use std::sync::Arc;

use anyhow::Result;
use learning_engine::context_graph::{ContextGraph, ContextNode, ContextNodeKind};
use learning_engine::event_mesh::EventPublisher;
use learning_engine::registry::ConnectorRegistry;
use learning_engine::schema::SchemaRegistry;
use serde_json::json;
use tokio::sync::Mutex;
use tracing::{info, warn};
use uuid::Uuid;

use crate::config::Config;
use crate::connectors;
use crate::ingestion;
use crate::model_manager::ModelManager;
use crate::workflow::{PolicyEngine, SandboxConfig, SandboxRunner};

pub struct LearningEngine {
    model_manager: Arc<ModelManager>,
    config: Arc<Config>,
    connector_registry: Arc<Mutex<ConnectorRegistry>>,
    schema_registry: SchemaRegistry,
    event_publisher: EventPublisher,
    context_graph: ContextGraph,
    policy_engine: Arc<PolicyEngine>,
    sandbox_runner: SandboxRunner,
}

impl LearningEngine {
    pub async fn new(model_manager: Arc<ModelManager>, config: &Config) -> Result<Self> {
        info!("Initializing learning engine");

        // Ensure pattern database directory exists
        if let Some(parent) = std::path::Path::new(&config.pattern_db_path).parent() {
            std::fs::create_dir_all(parent)?;
        }

        // Ensure knowledge graph directory exists
        if let Some(parent) = std::path::Path::new(&config.knowledge_graph_path).parent() {
            std::fs::create_dir_all(parent)?;
        }

        let schema_registry = SchemaRegistry::new();
        let context_graph = ContextGraph::new();
        let policy_engine = Arc::new(PolicyEngine::default());
        let sandbox_runner = SandboxRunner::new(SandboxConfig::default());
        ingestion::register_schema_defaults(&schema_registry, config).await;

        let event_publisher = ingestion::IngestionService::build_publisher(config).await?;
        let connector_registry = Arc::new(Mutex::new(ConnectorRegistry::new()));

        {
            let mut registry = connector_registry.lock().await;
            for connector_cfg in &config.connectors {
                match connectors::build_connector(connector_cfg) {
                    Ok(connector) => registry.register(connector_cfg.clone(), connector),
                    Err(err) => warn!(
                        connector = %connector_cfg.descriptor.namespaced_id(),
                        error = ?err,
                        "Failed to instantiate connector"
                    ),
                }
            }

            registry
                .start_all(schema_registry.clone(), event_publisher.clone())
                .await?;
        }

        seed_context_graph(&context_graph);

        Ok(Self {
            model_manager,
            config: Arc::new(config.clone()),
            connector_registry,
            schema_registry,
            event_publisher,
            context_graph,
            policy_engine,
            sandbox_runner,
        })
    }

    pub async fn process_event(&self, event: &str) -> Result<()> {
        // TODO: Implement event processing
        // This will be called by the monitoring system
        info!("Processing learning event: {}", event);
        Ok(())
    }

    pub async fn detect_patterns(&self) -> Result<Vec<String>> {
        // TODO: Implement pattern detection
        info!("Detecting patterns...");
        Ok(vec![])
    }

    pub fn schema_registry(&self) -> SchemaRegistry {
        self.schema_registry.clone()
    }

    pub fn event_publisher(&self) -> EventPublisher {
        self.event_publisher.clone()
    }

    pub fn context_graph(&self) -> ContextGraph {
        self.context_graph.clone()
    }

    pub fn policy_engine(&self) -> Arc<PolicyEngine> {
        self.policy_engine.clone()
    }

    pub fn sandbox_runner(&self) -> SandboxRunner {
        self.sandbox_runner.clone()
    }

    pub async fn shutdown(&self) {
        if let Ok(mut registry) = self.connector_registry.try_lock() {
            registry.stop_all().await;
        }
    }
}

fn seed_context_graph(context_graph: &ContextGraph) {
    let invoice_node = ContextNode {
        id: Uuid::new_v4(),
        kind: ContextNodeKind::Workflow,
        label: "Invoice approval workflow".to_string(),
        metadata: json!({
            "owner": "finance",
            "sla_minutes": 30
        }),
    };
    let inbox_node = ContextNode {
        id: Uuid::new_v4(),
        kind: ContextNodeKind::Communication,
        label: "Accounts payable inbox".to_string(),
        metadata: json!({
            "address": "ap@company",
            "priority": "high"
        }),
    };
    let agent_node = ContextNode {
        id: Uuid::new_v4(),
        kind: ContextNodeKind::Resource,
        label: "Agent: LedgerBot".to_string(),
        metadata: json!({
            "capabilities": ["classify_invoice", "route_to_owner"]
        }),
    };

    let invoice_id = context_graph
        .upsert_node(invoice_node, vec![0.9, 0.1, 0.3, 0.2])
        .unwrap();
    let inbox_id = context_graph
        .upsert_node(inbox_node, vec![0.8, 0.2, 0.4, 0.1])
        .unwrap();
    let agent_id = context_graph
        .upsert_node(agent_node, vec![0.4, 0.8, 0.2, 0.7])
        .unwrap();

    let _ = context_graph.relate(inbox_id, invoice_id, "feeds", 0.9);
    let _ = context_graph.relate(invoice_id, agent_id, "handled_by", 0.85);
}
