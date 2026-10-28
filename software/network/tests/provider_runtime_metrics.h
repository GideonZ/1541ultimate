#ifndef PROVIDER_RUNTIME_METRICS_H
#define PROVIDER_RUNTIME_METRICS_H
#include <stdbool.h>
#include <stdint.h>

struct provider_runtime_metrics {
    uint32_t free_heap, largest_block, tasks;
    uint32_t active, time_wait, bound, pcb_size, pcb_limit;
};
#ifdef __cplusplus
extern "C" {
#endif
bool provider_runtime_snapshot(struct provider_runtime_metrics *result);
#ifdef __cplusplus
}
#endif
#endif
