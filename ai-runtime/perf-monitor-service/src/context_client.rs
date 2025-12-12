use std::time::Duration;

use anyhow::{anyhow, Context, Result};
use reqwest::Client;
use serde::{Deserialize, Serialize};

#[derive(Clone)]
pub struct ContextClient {
    http: Client,
    base_url: String,
}

impl ContextClient {
    pub fn new(base_url: String, timeout: Duration) -> Self {
        Self {
            http: Client::builder()
                .timeout(timeout)
                .build()
                .expect("failed to build http client"),
            base_url,
        }
    }

    pub async fn query(&self, req: ContextQueryRequest) -> Result<ContextQueryResponse> {
        let url = format!(
            "{}/api/v1/context/query",
            self.base_url.trim_end_matches('/')
        );
        let resp = self
            .http
            .post(url)
            .json(&req)
            .send()
            .await
            .context("context query request failed")?;

        if !resp.status().is_success() {
            return Err(anyhow!("context service returned {}", resp.status()));
        }

        Ok(resp.json().await.context("invalid context response")?)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContextQueryRequest {
    pub embedding: Vec<f32>,
    #[serde(default = "default_top_k")]
    pub top_k: usize,
}

fn default_top_k() -> usize {
    5
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContextQueryResponse {
    pub results: Vec<ContextMatch>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContextMatch {
    pub label: String,
    pub kind: String,
    pub score: f32,
    pub metadata: serde_json::Value,
}

impl Default for ContextClient {
    fn default() -> Self {
        Self::new("http://127.0.0.1:9090".into(), Duration::from_secs(3))
    }
}
