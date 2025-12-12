// User-space syscall wrappers for BIZ_OS custom syscalls.
//
// Metadata:
// - version: 0.1.0
// - author: GPT-5.2 (Cursor agent)
// - timestamp: 2025-12-11
// - change_rationale: Provide safe wrappers to collect system metrics and AI perf stats.
//
// IMPORTANT:
// - These syscall numbers are project-defined (ai-runtime/include/syscalls.h).
// - On non-Linux targets (e.g. macOS dev), wrappers fail gracefully.

use crate::models::{AiPerfStats, SystemMetrics};

// Keep these in sync with ai-runtime/include/syscalls.h
#[cfg(target_os = "linux")]
const __NR_SYS_GET_SYSTEM_METRICS: libc::c_long = 403;
#[cfg(target_os = "linux")]
const __NR_SYS_GET_AI_PERF_STATS: libc::c_long = 405;

#[cfg(target_os = "linux")]
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct SysMetricsC {
    cpu_usage_percent: libc::c_ulong,
    memory_total_kb: libc::c_ulong,
    memory_free_kb: libc::c_ulong,
    memory_used_kb: libc::c_ulong,
    processes_count: libc::c_ulong,
    network_rx_bytes: libc::c_ulong,
    network_tx_bytes: libc::c_ulong,
    disk_read_bytes: libc::c_ulong,
    disk_write_bytes: libc::c_ulong,
    timestamp: libc::c_ulong,
}

#[cfg(target_os = "linux")]
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct AiPerfStatsC {
    total_requests: u64,
    completed_requests: u64,
    failed_requests: u64,
    cancelled_requests: u64,

    avg_latency_ns: u64,
    min_latency_ns: u64,
    max_latency_ns: u64,
    avg_queue_wait_ns: u64,
    avg_processing_ns: u64,

    requests_per_second: u64,
    peak_requests_per_second: u64,

    gpu_inferences: u64,
    gpu_memory_allocated: u64,
    gpu_memory_used: u64,
    gpu_kernel_launches: u64,
    gpu_errors: u64,
    gpu_utilization_percent: u32,

    queue_depth_avg: u64,
    queue_depth_max: u64,

    worker_threads_active: u32,
    worker_threads_idle: u32,
    worker_load_avg: u32,

    batches_processed: u64,
    avg_batch_size: u64,
    batch_efficiency_percent: u32,

    cache_hits: u64,
    cache_misses: u64,
    cache_hit_rate_percent: u32,
}

#[cfg(target_os = "linux")]
pub fn get_system_metrics() -> Result<SystemMetrics, String> {
    let mut c = SysMetricsC::default();
    let mut len: usize = std::mem::size_of::<SysMetricsC>();

    let rc = unsafe {
        libc::syscall(
            __NR_SYS_GET_SYSTEM_METRICS,
            &mut c as *mut SysMetricsC,
            &mut len as *mut usize,
        )
    };

    if rc < 0 {
        return Err(format!(
            "sys_get_system_metrics failed: {}",
            std::io::Error::last_os_error()
        ));
    }

    Ok(SystemMetrics {
        cpu_usage_percent: c.cpu_usage_percent as u64,
        memory_total_kb: c.memory_total_kb as u64,
        memory_free_kb: c.memory_free_kb as u64,
        memory_used_kb: c.memory_used_kb as u64,
        processes_count: c.processes_count as u64,
        network_rx_bytes: c.network_rx_bytes as u64,
        network_tx_bytes: c.network_tx_bytes as u64,
        disk_read_bytes: c.disk_read_bytes as u64,
        disk_write_bytes: c.disk_write_bytes as u64,
        timestamp: c.timestamp as u64,
    })
}

#[cfg(not(target_os = "linux"))]
pub fn get_system_metrics() -> Result<SystemMetrics, String> {
    Err("syscalls not supported on non-Linux targets".to_string())
}

#[cfg(target_os = "linux")]
pub fn get_ai_perf_stats() -> Result<AiPerfStats, String> {
    let mut c = AiPerfStatsC::default();
    let mut len: usize = std::mem::size_of::<AiPerfStatsC>();

    let rc = unsafe {
        libc::syscall(
            __NR_SYS_GET_AI_PERF_STATS,
            &mut c as *mut AiPerfStatsC,
            &mut len as *mut usize,
        )
    };

    if rc < 0 {
        return Err(format!(
            "sys_get_ai_perf_stats failed: {}",
            std::io::Error::last_os_error()
        ));
    }

    Ok(AiPerfStats {
        total_requests: c.total_requests,
        completed_requests: c.completed_requests,
        failed_requests: c.failed_requests,
        cancelled_requests: c.cancelled_requests,

        avg_latency_ns: c.avg_latency_ns,
        min_latency_ns: c.min_latency_ns,
        max_latency_ns: c.max_latency_ns,
        avg_queue_wait_ns: c.avg_queue_wait_ns,
        avg_processing_ns: c.avg_processing_ns,

        requests_per_second: c.requests_per_second,
        peak_requests_per_second: c.peak_requests_per_second,

        gpu_inferences: c.gpu_inferences,
        gpu_memory_allocated: c.gpu_memory_allocated,
        gpu_memory_used: c.gpu_memory_used,
        gpu_kernel_launches: c.gpu_kernel_launches,
        gpu_errors: c.gpu_errors,
        gpu_utilization_percent: c.gpu_utilization_percent,

        queue_depth_avg: c.queue_depth_avg,
        queue_depth_max: c.queue_depth_max,

        worker_threads_active: c.worker_threads_active,
        worker_threads_idle: c.worker_threads_idle,
        worker_load_avg: c.worker_load_avg,

        batches_processed: c.batches_processed,
        avg_batch_size: c.avg_batch_size,
        batch_efficiency_percent: c.batch_efficiency_percent,

        cache_hits: c.cache_hits,
        cache_misses: c.cache_misses,
        cache_hit_rate_percent: c.cache_hit_rate_percent,
    })
}

#[cfg(not(target_os = "linux"))]
pub fn get_ai_perf_stats() -> Result<AiPerfStats, String> {
    Err("syscalls not supported on non-Linux targets".to_string())
}
