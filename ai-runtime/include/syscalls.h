/* BIZ_OS Custom System Call Definitions
 * User-space header for AI runtime system calls
 */

#ifndef BIZOS_SYSCALLS_H
#define BIZOS_SYSCALLS_H

#include <sys/types.h>
#include <stdint.h>

/* System call numbers (to be defined in kernel) */
#define __NR_ai_inference        400
#define __NR_ai_learn            401
#define __NR_ai_get_pattern      402
#define __NR_sys_get_system_metrics  403
#define __NR_sys_diagnose_system     404
#define __NR_sys_get_ai_perf_stats   405

/* AI Inference System Call
 * model_id: Model identifier
 * input: Input data buffer
 * input_len: Length of input data
 * output: Output data buffer
 * output_len: Pointer to output buffer length (in/out)
 * Returns: 0 on success, negative error code on failure
 */
long sys_ai_inference(
    unsigned long model_id,
    const void *input,
    size_t input_len,
    void *output,
    size_t *output_len
);

/* AI Learning System Call
 * data: Learning data buffer
 * len: Length of data
 * pattern_type: Type of pattern being learned
 * Returns: Pattern ID on success, negative error code on failure
 */
long sys_ai_learn(
    const void *data,
    size_t len,
    unsigned long pattern_type
);

/* Get Pattern System Call
 * pattern_id: Pattern identifier
 * buffer: Output buffer for pattern data
 * len: Buffer length (in/out)
 * Returns: 0 on success, negative error code on failure
 */
long sys_ai_get_pattern(
    unsigned long pattern_id,
    void *buffer,
    size_t *len
);

/* Pattern types */
#define AI_PATTERN_REPETITIVE_TASK    1
#define AI_PATTERN_DATA_ENTRY         2
#define AI_PATTERN_COMMUNICATION      3
#define AI_PATTERN_WORKFLOW           4
#define AI_PATTERN_DECISION           5

/* System Metrics Structure */
struct sys_metrics {
    unsigned long cpu_usage_percent;
    unsigned long memory_total_kb;
    unsigned long memory_free_kb;
    unsigned long memory_used_kb;
    unsigned long processes_count;
    unsigned long network_rx_bytes;
    unsigned long network_tx_bytes;
    unsigned long disk_read_bytes;
    unsigned long disk_write_bytes;
    unsigned long timestamp;
};

/* Get System Metrics System Call
 * metrics_buffer: Buffer to receive system metrics
 * metrics_len: Pointer to buffer length (in/out)
 * Returns: 0 on success, negative error code on failure
 */
long sys_get_system_metrics(
    void *metrics_buffer,
    size_t *metrics_len
);

/* AI Perf Stats Structure (mirrors kernel struct ai_perf_stats) */
struct ai_perf_stats {
    /* Request statistics */
    uint64_t total_requests;
    uint64_t completed_requests;
    uint64_t failed_requests;
    uint64_t cancelled_requests;

    /* Latency statistics */
    uint64_t avg_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t avg_queue_wait_ns;
    uint64_t avg_processing_ns;

    /* Throughput statistics */
    uint64_t requests_per_second;
    uint64_t peak_requests_per_second;

    /* GPU statistics */
    uint64_t gpu_inferences;
    uint64_t gpu_memory_allocated;
    uint64_t gpu_memory_used;
    uint64_t gpu_kernel_launches;
    uint64_t gpu_errors;
    uint32_t gpu_utilization_percent;

    /* Queue statistics */
    uint64_t queue_depth_avg;
    uint64_t queue_depth_max;

    /* Worker statistics */
    uint32_t worker_threads_active;
    uint32_t worker_threads_idle;
    uint32_t worker_load_avg;

    /* Batch statistics */
    uint64_t batches_processed;
    uint64_t avg_batch_size;
    uint32_t batch_efficiency_percent;

    /* Cache statistics */
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t cache_hit_rate_percent;
};

/* Get AI Perf Stats System Call
 * stats_buffer: Buffer to receive ai_perf_stats
 * stats_len: Pointer to buffer length (in/out)
 * Returns: 0 on success, negative error code on failure
 */
long sys_get_ai_perf_stats(
    void *stats_buffer,
    size_t *stats_len
);

/* Diagnose System System Call
 * query: Natural language query string
 * query_len: Length of query string
 * response: Buffer for AI-generated diagnostic response
 * response_len: Pointer to response buffer length (in/out)
 * Returns: 0 on success, negative error code on failure
 */
long sys_diagnose_system(
    const char *query,
    size_t query_len,
    char *response,
    size_t *response_len
);

#endif /* BIZOS_SYSCALLS_H */

