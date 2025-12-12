// BIZ_OS AI Diagnostic Analyzer
// Uses Mistral LLM to analyze system metrics and answer diagnostic queries

use super::metrics::SystemMetrics;
use anyhow::Result;
use serde::{Deserialize, Serialize};

/// Diagnostic analysis result
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiagnosticResult {
    pub diagnosis: String,
    pub severity: Severity,
    pub recommendations: Vec<String>,
    pub affected_components: Vec<String>,
}

/// Severity level for issues
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum Severity {
    Info,
    Warning,
    Error,
    Critical,
}

/// AI-powered diagnostic analyzer
pub struct DiagnosticAnalyzer {
    // Will use the inference engine to call Mistral LLM
    // For now, placeholder structure
}

impl DiagnosticAnalyzer {
    /// Create a new DiagnosticAnalyzer
    pub fn new() -> Self {
        Self {}
    }

    /// Analyze system metrics and answer a diagnostic query
    /// 
    /// # Arguments
    /// * `query` - Natural language query about system health
    /// * `metrics` - Current system metrics
    /// 
    /// # Returns
    /// Diagnostic result with AI-generated analysis
    pub async fn analyze(
        &self,
        query: &str,
        metrics: &SystemMetrics,
    ) -> Result<DiagnosticResult> {
        // Build prompt for Mistral LLM
        let _prompt = self.build_diagnostic_prompt(query, metrics);
        
        // TODO: Call Mistral LLM through inference engine
        // For now, return a basic analysis based on metrics
        
        let diagnosis = self.generate_basic_diagnosis(query, metrics);
        let severity = self.determine_severity(metrics);
        let recommendations = self.generate_recommendations(metrics);
        let affected_components = self.identify_affected_components(metrics);
        
        Ok(DiagnosticResult {
            diagnosis,
            severity,
            recommendations,
            affected_components,
        })
    }

    /// Build prompt for Mistral LLM
    fn build_diagnostic_prompt(&self, query: &str, metrics: &SystemMetrics) -> String {
        format!(
            "You are a system administrator AI assistant. Analyze the following system metrics and answer the user's question about system health.\n\n\
            User Question: {}\n\n\
            Current System Metrics:\n{}\n\n\
            Provide a clear diagnosis, identify any issues, and suggest actionable recommendations.",
            query,
            metrics.to_analysis_string()
        )
    }

    /// Generate basic diagnosis (placeholder until LLM integration)
    fn generate_basic_diagnosis(&self, query: &str, metrics: &SystemMetrics) -> String {
        let mut issues = Vec::new();
        
        // Check memory usage
        if metrics.memory_total_kb > 0 {
            let memory_percent = (metrics.memory_used_kb * 100) / metrics.memory_total_kb;
            if memory_percent > 90 {
                issues.push(format!("High memory usage: {}%", memory_percent));
            }
        }
        
        // Check process count
        if metrics.processes_count > 500 {
            issues.push(format!("High number of processes: {}", metrics.processes_count));
        }
        
        if issues.is_empty() {
            format!("System appears healthy. No critical issues detected based on current metrics.")
        } else {
            format!(
                "Analysis of your query '{}':\n\nDetected issues:\n{}",
                query,
                issues.join("\n")
            )
        }
    }

    /// Determine severity based on metrics
    fn determine_severity(&self, metrics: &SystemMetrics) -> Severity {
        if metrics.memory_total_kb > 0 {
            let memory_percent = (metrics.memory_used_kb * 100) / metrics.memory_total_kb;
            if memory_percent > 95 {
                return Severity::Critical;
            } else if memory_percent > 85 {
                return Severity::Warning;
            }
        }
        
        if metrics.processes_count > 1000 {
            return Severity::Warning;
        }
        
        Severity::Info
    }

    /// Generate recommendations based on metrics
    fn generate_recommendations(&self, metrics: &SystemMetrics) -> Vec<String> {
        let mut recommendations = Vec::new();
        
        if metrics.memory_total_kb > 0 {
            let memory_percent = (metrics.memory_used_kb * 100) / metrics.memory_total_kb;
            if memory_percent > 90 {
                recommendations.push("Consider closing unnecessary applications to free memory".to_string());
                recommendations.push("Check for memory leaks in running processes".to_string());
            }
        }
        
        if metrics.processes_count > 500 {
            recommendations.push("Review running processes and terminate unnecessary ones".to_string());
        }
        
        if recommendations.is_empty() {
            recommendations.push("System is operating normally. Continue monitoring.".to_string());
        }
        
        recommendations
    }

    /// Identify affected system components
    fn identify_affected_components(&self, metrics: &SystemMetrics) -> Vec<String> {
        let mut components = Vec::new();
        
        if metrics.memory_total_kb > 0 {
            let memory_percent = (metrics.memory_used_kb * 100) / metrics.memory_total_kb;
            if memory_percent > 85 {
                components.push("Memory".to_string());
            }
        }
        
        if metrics.processes_count > 500 {
            components.push("Process Management".to_string());
        }
        
        if metrics.cpu_usage_percent > 80 {
            components.push("CPU".to_string());
        }
        
        components
    }
}

impl Default for DiagnosticAnalyzer {
    fn default() -> Self {
        Self::new()
    }
}
