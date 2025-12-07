// Learning Engine Module
// Real-time learning from user actions

pub mod monitor;
pub mod collector;
pub mod processor;
pub mod pattern_detector;

pub use monitor::{Monitor, UserEvent, EventType};
pub use collector::{Collector, PrivacyLevel};

