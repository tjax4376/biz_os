pub mod file_replay;

use std::sync::Arc;

use anyhow::{anyhow, Result};
use learning_engine::connector::{Connector, ConnectorConfig, ConnectorKind};

use self::file_replay::FileReplayConnector;

pub fn build_connector(config: &ConnectorConfig) -> Result<Arc<dyn Connector>> {
    match config.descriptor.kind {
        ConnectorKind::Filesystem | ConnectorKind::Custom => Ok(Arc::new(
            FileReplayConnector::new(config.descriptor.clone()),
        )),
        _ => Err(anyhow!(
            "connector kind {:?} not yet implemented",
            config.descriptor.kind
        )),
    }
}
