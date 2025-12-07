// BIZ_OS System Metrics Collection
// Collects system metrics for AI diagnostics

use serde::{Deserialize, Serialize};
use std::time::{SystemTime, UNIX_EPOCH};

/// System metrics structure matching kernel sys_metrics
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SystemMetrics {
    pub cpu_usage_percent: u64,
    pub memory_total_kb: u64,
    pub memory_free_kb: u64,
    pub memory_used_kb: u64,
    pub processes_count: u64,
    pub network_rx_bytes: u64,
    pub network_tx_bytes: u64,
    pub disk_read_bytes: u64,
    pub disk_write_bytes: u64,
    pub timestamp: u64,
}

impl SystemMetrics {
    /// Create a new SystemMetrics instance
    pub fn new() -> Self {
        Self {
            cpu_usage_percent: 0,
            memory_total_kb: 0,
            memory_free_kb: 0,
            memory_used_kb: 0,
            processes_count: 0,
            network_rx_bytes: 0,
            network_tx_bytes: 0,
            disk_read_bytes: 0,
            disk_write_bytes: 0,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_secs(),
        }
    }

    /// Collect current system metrics
    /// This will use the kernel syscall sys_get_system_metrics
    pub fn collect() -> Result<Self, String> {
        // TODO: Call sys_get_system_metrics syscall
        // For now, use platform-specific methods
        
        #[cfg(target_os = "linux")]
        {
            Self::collect_linux()
        }
        
        #[cfg(not(target_os = "linux"))]
        {
            // Fallback for non-Linux systems (development)
            Ok(Self::new())
        }
    }

    #[cfg(target_os = "linux")]
    fn collect_linux() -> Result<Self, String> {
        use std::fs;
        use std::io::Read;
        
        let mut metrics = Self::new();
        
        // Read /proc/meminfo for memory metrics
        if let Ok(meminfo) = fs::read_to_string("/proc/meminfo") {
            for line in meminfo.lines() {
                if line.starts_with("MemTotal:") {
                    if let Some(kb) = line.split_whitespace().nth(1) {
                        metrics.memory_total_kb = kb.parse().unwrap_or(0);
                    }
                } else if line.starts_with("MemFree:") {
                    if let Some(kb) = line.split_whitespace().nth(1) {
                        metrics.memory_free_kb = kb.parse().unwrap_or(0);
                    }
                }
            }
            metrics.memory_used_kb = metrics.memory_total_kb.saturating_sub(metrics.memory_free_kb);
        }
        
        // Count processes from /proc
        if let Ok(entries) = fs::read_dir("/proc") {
            metrics.processes_count = entries
                .filter_map(|e| e.ok())
                .filter(|e| {
                    e.file_name()
                        .to_string_lossy()
                        .chars()
                        .all(|c| c.is_ascii_digit())
                })
                .count() as u64;
        }
        
        // TODO: Collect CPU usage, network, and disk I/O metrics
        // These require more complex parsing or syscalls
        
        Ok(metrics)
    }

    /// Format metrics as a human-readable string for AI analysis
    pub fn to_analysis_string(&self) -> String {
        format!(
            "System Metrics:\n\
            - CPU Usage: {}%\n\
            - Memory: {} MB used / {} MB total ({}% used)\n\
            - Processes: {}\n\
            - Network: {} bytes received, {} bytes sent\n\
            - Disk I/O: {} bytes read, {} bytes written\n\
            - Timestamp: {}",
            self.cpu_usage_percent,
            self.memory_used_kb / 1024,
            self.memory_total_kb / 1024,
            if self.memory_total_kb > 0 {
                (self.memory_used_kb * 100) / self.memory_total_kb
            } else {
                0
            },
            self.processes_count,
            self.network_rx_bytes,
            self.network_tx_bytes,
            self.disk_read_bytes,
            self.disk_write_bytes,
            self.timestamp
        )
    }
}

impl Default for SystemMetrics {
    fn default() -> Self {
        Self::new()
    }
}
