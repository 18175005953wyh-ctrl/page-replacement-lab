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
    size_t *next_use = NULL;
    if (stats != NULL) memset(stats, 0, sizeof *stats);
    if (trace == NULL || stats == NULL || (trace->count > 0 && trace->pages == NULL) ||
        frame_count < 1 || frame_count > MAX_FRAMES ||
        (policy != POLICY_FIFO && policy != POLICY_LRU && policy != POLICY_OPT)) {
        (void)snprintf(error, error_size, "Invalid simulation trace, frame count or policy"); return 0;
    }
    frames = calloc(frame_count, sizeof *frames);
    if (frames == NULL) { (void)snprintf(error, error_size, "Frame allocation failed"); return 0; }
    if (policy == POLICY_OPT && !trace_next_use(trace, &next_use, error, error_size)) { free(frames); return 0; }
    for (i = 0; i < trace->count; ++i) {
        size_t slot = frame_count, empty = frame_count, j;
        SimulationStep step = {0};
        if (policy == POLICY_LRU) {
            if (clock == UINT64_MAX) {
                free(frames); free(next_use); memset(stats, 0, sizeof *stats);
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
                } else if (policy == POLICY_OPT) {
                    slot = 0;
                    for (j = 1; j < frame_count; ++j)
                        if (frames[j].next_use > frames[slot].next_use) slot = j;
                }
                step.evicted = 1; step.evicted_page = frames[slot].page;
                ++stats->replacements;
            }
            frames[slot].valid = 1; frames[slot].page = trace->pages[i];
            if (policy == POLICY_FIFO) fifo = (slot + 1) % frame_count;
        }
        if (policy == POLICY_LRU) frames[slot].last_used = clock;
        if (policy == POLICY_OPT) frames[slot].next_use = next_use[i];
        ++stats->accesses;
        step.frames = frames; step.frame_count = frame_count;
        if (observer != NULL) observer(&step, context);
    }
    free(frames); free(next_use);
    if (!stats_are_valid(stats)) {
        (void)snprintf(error, error_size, "Internal statistics invariant failed"); return 0;
    }
    return 1;
}

int compare_policies(const Trace *trace, size_t frames, SimulationStats stats[3],
                     StepObserver observer, void *context, char *error, size_t error_size)
{
    int policy;
    for (policy = POLICY_FIFO; policy <= POLICY_OPT; ++policy)
        if (!simulate(trace, frames, (ReplacementPolicy)policy, &stats[policy], observer, context, error, error_size)) return 0;
    if (stats[POLICY_OPT].faults > stats[POLICY_FIFO].faults || stats[POLICY_OPT].faults > stats[POLICY_LRU].faults) {
        (void)snprintf(error, error_size, "Internal comparison invariant failed: OPT has too many faults"); return 0;
    }
    return 1;
}

int scan_frame_range(const Trace *trace, size_t min_frames, size_t max_frames,
                     SimulationStats (*rows)[3], char *error, size_t error_size)
{
    size_t frames;
    if (rows == NULL || min_frames < 1 || min_frames > max_frames || max_frames > MAX_FRAMES) {
        (void)snprintf(error, error_size, "Invalid Belady frame range"); return 0;
    }
    for (frames = min_frames; frames <= max_frames; ++frames) {
        size_t row = frames - min_frames;
        if (!compare_policies(trace, frames, rows[row], NULL, NULL, error, error_size)) return 0;
        if (row > 0 && (rows[row][POLICY_LRU].faults > rows[row - 1][POLICY_LRU].faults ||
                        rows[row][POLICY_OPT].faults > rows[row - 1][POLICY_OPT].faults)) {
            (void)snprintf(error, error_size, "Internal stack-algorithm invariant failed: LRU/OPT faults increased"); return 0;
        }
    }
    return 1;
}
