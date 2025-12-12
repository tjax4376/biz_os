// Learning Engine Module
// Real-time learning from user actions

pub mod collector;
pub mod connector;
pub mod context_graph;
pub mod event_mesh;
pub mod monitor;
pub mod pattern_detector;
pub mod processor;
pub mod registry;
pub mod schema;

pub use collector::{Collector, PrivacyLevel};
pub use monitor::{EventType, Monitor, UserEvent};
