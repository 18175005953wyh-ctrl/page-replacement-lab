#ifndef REPLACEMENT_H
#define REPLACEMENT_H
#include "trace.h"

#define MAX_FRAMES 1024
typedef enum { POLICY_FIFO, POLICY_LRU, POLICY_OPT } ReplacementPolicy;
typedef struct { size_t accesses, hits, faults, replacements; } SimulationStats;
typedef struct { uint32_t page; int valid; uint64_t last_used; size_t next_use; } PageFrame;
typedef struct {
    ReplacementPolicy policy;
    size_t step;
    uint32_t page;
    int hit;
    int evicted;
    uint32_t evicted_page;
    const PageFrame *frames;
    size_t frame_count;
} SimulationStep;
/* Frame pointers are borrowed and only valid during this callback. */
typedef void (*StepObserver)(const SimulationStep *step, void *context);

const char *policy_name(ReplacementPolicy policy);
int stats_are_valid(const SimulationStats *stats);
int simulate(const Trace *trace, size_t frames, ReplacementPolicy policy,
             SimulationStats *stats, StepObserver observer, void *context,
             char *error, size_t error_size);
int compare_policies(const Trace *trace, size_t frames, SimulationStats stats[3],
                     StepObserver observer, void *context, char *error, size_t error_size);
/* Rows correspond to min_frames..max_frames. Caller provides enough rows. */
int scan_frame_range(const Trace *trace, size_t min_frames, size_t max_frames,
                     SimulationStats (*rows)[3], char *error, size_t error_size);
#endif
