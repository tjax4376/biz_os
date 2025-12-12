use std::str::FromStr;
use std::sync::Arc;

use anyhow::Result;
use learning_engine::collector::PrivacyLevel;
use learning_engine::event_mesh::{EventEnvelope, EventMeshBuilder, EventPublisher};
use learning_engine::schema::{SchemaReference, SchemaRegistry, SchemaVersion};
use tonic::{Request, Response, Status};
use tracing::info;
use uuid::Uuid;

use crate::config::{Config, MeshTransport};

pub mod proto {
    tonic::include_proto!("bizos.ingestion.v1");
}

use proto::event_ingestion_server::{EventIngestion, EventIngestionServer};
use proto::{EventRequest, PublishResponse};

pub struct IngestionService {
    publisher: EventPublisher,
    schema_registry: SchemaRegistry,
}

impl IngestionService {
    pub fn new(publisher: EventPublisher, schema_registry: SchemaRegistry) -> Self {
        Self {
            publisher,
            schema_registry,
        }
    }

    pub fn into_server(self) -> EventIngestionServer<Self> {
        EventIngestionServer::new(self)
    }

    pub async fn build_publisher(config: &Config) -> Result<EventPublisher> {
        match config.event_mesh.transport {
            MeshTransport::InMemory => Ok(EventMeshBuilder::in_memory()),
            MeshTransport::Nats => {
                EventMeshBuilder::nats(&config.event_mesh.url, &config.event_mesh.subject).await
            }
        }
    }
}

#[tonic::async_trait]
impl EventIngestion for IngestionService {
    async fn publish_event(
        &self,
        request: Request<EventRequest>,
    ) -> Result<Response<PublishResponse>, Status> {
        let payload = request.into_inner();

        let schema = SchemaReference {
            namespace: payload.schema_namespace,
            name: payload.schema_name,
            version: SchemaVersion::new(
                payload.schema_major as u16,
                payload.schema_minor as u16,
                payload.schema_patch as u16,
            ),
        };

        let json_payload: serde_json::Value = serde_json::from_str(&payload.payload_json)
            .map_err(|err| Status::invalid_argument(format!("invalid json payload: {}", err)))?;

        self.schema_registry
            .validate(&schema, &json_payload)
            .map_err(|err| {
                Status::invalid_argument(format!("schema validation failed: {}", err))
            })?;

        let privacy = PrivacyLevel::from_str(&payload.privacy)
            .map_err(|_| Status::invalid_argument("unknown privacy level"))?;

        let envelope = EventEnvelope {
            id: Uuid::new_v4(),
            schema,
            namespace: payload.namespace,
            payload: json_payload,
            received_at: chrono::Utc::now(),
            privacy,
            annotations: Default::default(),
        };

        self.publisher
            .publish(envelope.clone())
            .await
            .map_err(|err| Status::internal(format!("failed to publish event: {}", err)))?;

        info!(ns = envelope.namespace, "Event ingested via gRPC");

        Ok(Response::new(PublishResponse {
            event_id: envelope.id.to_string(),
            status: "accepted".into(),
        }))
    }
}

impl FromStr for PrivacyLevel {
    type Err = ();

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "public" => Ok(PrivacyLevel::Public),
            "private" => Ok(PrivacyLevel::Private),
            "sensitive" => Ok(PrivacyLevel::Sensitive),
            _ => Err(()),
        }
    }
}

pub async fn register_schema_defaults(schema_registry: &SchemaRegistry, config: &Config) {
    for schema in &config.schemas {
        schema_registry.register(schema.clone());
    }
}

pub async fn start_server(
    service: IngestionService,
    bind_addr: &str,
) -> Result<tokio::task::JoinHandle<Result<()>>> {
    let addr: std::net::SocketAddr = bind_addr.parse()?;
    let server = service.into_server();
    let handle = tokio::spawn(async move {
        info!(%addr, "Starting ingestion gRPC server");
        tonic::transport::Server::builder()
            .add_service(server)
            .serve(addr)
            .await
            .map_err(anyhow::Error::from)
    });
    Ok(handle)
}
