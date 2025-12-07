/* BIZ_OS Custom System Call Definitions
 * User-space header for AI runtime system calls
 */

#ifndef BIZOS_SYSCALLS_H
#define BIZOS_SYSCALLS_H

#include <sys/types.h>
#include <stdint.h>

/* System call numbers (to be defined in kernel) */
#define __NR_ai_inference    400
#define __NR_ai_learn        401
#define __NR_ai_get_pattern  402

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

#endif /* BIZOS_SYSCALLS_H */

