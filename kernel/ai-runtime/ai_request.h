/* BIZ_OS Kernel AI Request Structures
 * Core data structures for kernel-level AI request processing
 */

#ifndef _LINUX_AI_REQUEST_H
#define _LINUX_AI_REQUEST_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/ktime.h>
#include <linux/refcount.h>

/* Request Priority Levels */
#define AI_PRIO_REALTIME    0  /* Highest priority, real-time processing */
#define AI_PRIO_HIGH        1  /* High priority, low latency */
#define AI_PRIO_NORMAL      2  /* Normal priority, default */
#define AI_PRIO_BACKGROUND  3  /* Background priority, can be delayed */
#define AI_PRIO_IDLE        4  /* Idle priority, only when system idle */

/* Request Flags */
#define AI_FLAG_ASYNC       (1 << 0)  /* Asynchronous request */
#define AI_FLAG_BATCH       (1 << 1)  /* Batch processing */
#define AI_FLAG_PRIORITY    (1 << 2)  /* High priority */
#define AI_FLAG_NO_CACHE    (1 << 3)  /* Don't use cached results */
#define AI_FLAG_GPU_ONLY    (1 << 4)  /* Require GPU execution */
#define AI_FLAG_CPU_FALLBACK (1 << 5) /* Allow CPU fallback */

/* Request Status */
enum ai_request_status {
    AI_REQ_PENDING = 0,      /* Request queued, waiting for processing */
    AI_REQ_PROCESSING,       /* Request being processed */
    AI_REQ_COMPLETED,        /* Request completed successfully */
    AI_REQ_FAILED,          /* Request failed */
    AI_REQ_CANCELLED,       /* Request cancelled */
    AI_REQ_TIMEOUT,         /* Request timed out */
};

/* Model Information */
struct ai_model_info {
    u32 model_id;            /* Unique model identifier */
    char name[64];          /* Model name */
    u32 version;            /* Model version */
    u64 size_bytes;         /* Model size in bytes */
    u32 format;             /* Model format (GGUF, ONNX, etc.) */
    void *weights;          /* Pointer to model weights (kernel memory) */
    u64 weights_size;       /* Size of weights */
    void *gpu_memory;       /* GPU memory pointer (if loaded on GPU) */
    u32 refcount;           /* Reference count */
    ktime_t load_time;      /* When model was loaded */
    struct list_head list;   /* List linkage */
};

/* AI Request Structure */
struct ai_request {
    /* Identification */
    u64 request_id;         /* Unique request identifier */
    u32 model_id;          /* Model to use for inference */
    
    /* Priority and Flags */
    u32 priority;          /* Request priority */
    u32 flags;            /* Request flags */
    enum ai_request_status status; /* Request status */
    
    /* Input/Output Data */
    void *input_data;      /* Input data buffer (kernel memory) */
    size_t input_len;      /* Input data length */
    void *output_buffer;   /* Output buffer (kernel memory) */
    size_t output_len;     /* Output buffer size */
    size_t output_actual;  /* Actual output length */
    
    /* Completion */
    struct completion *completion; /* Completion mechanism */
    int result_fd;         /* File descriptor for async result (eventfd) */
    
    /* Timing */
    ktime_t timestamp;     /* Request creation timestamp */
    ktime_t start_time;    /* Processing start time */
    ktime_t end_time;      /* Processing end time */
    
    /* Error Handling */
    int error_code;        /* Error code if request failed */
    char error_msg[128];   /* Error message */
    
    /* Queue Management */
    struct list_head list;  /* Queue linkage */
    struct list_head batch_list; /* Batch linkage */
    
    /* Reference Counting */
    refcount_t refcount;   /* Reference count */
    
    /* Worker Assignment */
    struct ai_worker *worker; /* Assigned worker thread */
    
    /* Statistics */
    u32 retry_count;       /* Number of retries */
    u32 batch_index;       /* Index in batch */
};

/* Request Queue Structure */
struct ai_request_queue {
    /* Priority Queues */
    struct list_head queues[5]; /* One queue per priority level */
    
    /* Statistics */
    u64 total_requests;    /* Total requests processed */
    u64 pending_requests;  /* Currently pending requests */
    u64 completed_requests; /* Completed requests */
    u64 failed_requests;   /* Failed requests */
    
    /* Locking */
    spinlock_t lock;       /* Queue lock */
    
    /* Configuration */
    u32 max_pending;      /* Maximum pending requests */
    u32 max_batch_size;   /* Maximum batch size */
    
    /* Request ID Generation */
    atomic64_t next_request_id; /* Next request ID */
};

/* Model Registry Structure */
struct ai_model_registry {
    struct list_head models;  /* List of registered models */
    spinlock_t lock;          /* Registry lock */
    u32 model_count;          /* Number of registered models */
    atomic64_t next_model_id; /* Next model ID */
};

/* Request Statistics */
struct ai_request_stats {
    u64 total_requests;
    u64 completed_requests;
    u64 failed_requests;
    u64 avg_latency_us;    /* Average latency in microseconds */
    u64 max_latency_us;    /* Maximum latency */
    u64 min_latency_us;    /* Minimum latency */
    u64 throughput_rps;    /* Throughput (requests per second) */
};

/* Function Declarations */

/* Request Queue Functions */
int ai_queue_init(struct ai_request_queue *queue);
void ai_queue_destroy(struct ai_request_queue *queue);
struct ai_request *ai_request_alloc(gfp_t gfp_flags);
void ai_request_free(struct ai_request *req);
int ai_request_enqueue(struct ai_request_queue *queue, struct ai_request *req);
struct ai_request *ai_request_dequeue(struct ai_request_queue *queue, u32 priority);
struct ai_request *ai_request_find(struct ai_request_queue *queue, u64 request_id);
void ai_queue_get_stats(struct ai_request_queue *queue, struct ai_request_stats *stats);

/* Model Registry Functions */
int ai_model_registry_init(struct ai_model_registry *registry);
void ai_model_registry_destroy(struct ai_model_registry *registry);
int ai_model_register(struct ai_model_registry *registry, struct ai_model_info *model);
struct ai_model_info *ai_model_find(struct ai_model_registry *registry, u32 model_id);
void ai_model_unregister(struct ai_model_registry *registry, u32 model_id);
int ai_model_load(struct ai_model_info *model, const char *path);
void ai_model_unload(struct ai_model_info *model);

/* Request Lifecycle Functions */
u64 ai_request_create(u32 model_id, u32 priority, u32 flags);
int ai_request_set_input(struct ai_request *req, const void *data, size_t len);
int ai_request_get_output(struct ai_request *req, void *buffer, size_t *len);
int ai_request_wait(struct ai_request *req, unsigned long timeout_ms);
int ai_request_cancel(u64 request_id);
void ai_request_complete(struct ai_request *req, int error_code);

#endif /* _LINUX_AI_REQUEST_H */

