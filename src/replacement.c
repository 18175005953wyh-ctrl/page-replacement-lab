#include "replacement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *policy_name(ReplacementPolicy policy)
{
    switch (policy) {
    case POLICY_FIFO: return "FIFO";
    case POLICY_LRU: return "LRU";
    case POLICY_OPT: return "OPT";
    default: return "UNKNOWN";
    }
}

int stats_are_valid(const SimulationStats *stats)
{
    return stats != NULL && stats->hits <= stats->accesses &&
           stats->faults == stats->accesses - stats->hits && stats->replacements <= stats->faults;
}

int simulate(const Trace *trace, size_t frame_count, ReplacementPolicy policy,
             SimulationStats *stats, StepObserver observer, void *context,
             char *error, size_t error_size)
{
    PageFrame *frames;
    uint64_t clock = 0;
    size_t i, fifo = 0;
    if (stats != NULL) memset(stats, 0, sizeof *stats);
    if (trace == NULL || stats == NULL || (trace->count > 0 && trace->pages == NULL) ||
        frame_count < 1 || frame_count > MAX_FRAMES || (policy != POLICY_FIFO && policy != POLICY_LRU)) {
        (void)snprintf(error, error_size, "Invalid simulation trace, frame count or policy"); return 0;
    }
    frames = calloc(frame_count, sizeof *frames);
    if (frames == NULL) { (void)snprintf(error, error_size, "Frame allocation failed"); return 0; }
    for (i = 0; i < trace->count; ++i) {
        size_t slot = frame_count, empty = frame_count, j;
        SimulationStep step = {0};
        if (policy == POLICY_LRU) {
            if (clock == UINT64_MAX) {
                free(frames); memset(stats, 0, sizeof *stats);
                (void)snprintf(error, error_size, "LRU clock overflow"); return 0;
            }
            ++clock;
        }
        for (j = 0; j < frame_count; ++j) {
            if (frames[j].valid && frames[j].page == trace->pages[i]) { slot = j; break; }
            if (!frames[j].valid && empty == frame_count) empty = j;
        }
        step.policy = policy; step.step = i + 1; step.page = trace->pages[i];
        step.hit = slot < frame_count;
        if (step.hit) ++stats->hits;
        else {
            ++stats->faults;
            if (empty < frame_count) slot = empty;
            else {
                slot = fifo;
                if (policy == POLICY_LRU) {
                    slot = 0;
                    for (j = 1; j < frame_count; ++j)
                        if (frames[j].last_used < frames[slot].last_used) slot = j;
                }
                step.evicted = 1; step.evicted_page = frames[slot].page;
                ++stats->replacements;
            }
            frames[slot].valid = 1; frames[slot].page = trace->pages[i];
            if (policy == POLICY_FIFO) fifo = (slot + 1) % frame_count;
        }
        if (policy == POLICY_LRU) frames[slot].last_used = clock;
        ++stats->accesses;
        step.frames = frames; step.frame_count = frame_count;
        if (observer != NULL) observer(&step, context);
    }
    free(frames);
    return stats_are_valid(stats);
}
