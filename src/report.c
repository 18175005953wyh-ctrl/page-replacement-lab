#include "report.h"
#include <inttypes.h>

void report_step(const SimulationStep *step, void *context)
{
    FILE *stream = context;
    size_t i;
    if (step->step == 1) fprintf(stream, "\n%s steps\nStep  Page        Result  Evicted     Frames\n", policy_name(step->policy));
    fprintf(stream, "%-5zu %-11" PRIu32 " %-7s ", step->step, step->page, step->hit ? "HIT" : "FAULT");
    if (step->evicted) fprintf(stream, "%-11" PRIu32 " ", step->evicted_page);
    else fprintf(stream, "%-11s ", "-");
    fputc('[', stream);
    for (i = 0; i < step->frame_count; ++i) {
        if (i != 0) fputc(' ', stream);
        if (step->frames[i].valid) fprintf(stream, "%" PRIu32, step->frames[i].page);
        else fputc('-', stream);
    }
    fputs("]\n", stream);
}

void report_summary(FILE *stream, const SimulationStats stats[], size_t count, ReplacementPolicy first)
{
    size_t i;
    fputs("\nPolicy  Accesses  Hits      Faults    Replacements  Fault rate\n", stream);
    for (i = 0; i < count; ++i) {
        fprintf(stream, "%-7s %-9zu %-9zu %-9zu %-13zu ", policy_name((ReplacementPolicy)((int)first + (int)i)),
                stats[i].accesses, stats[i].hits, stats[i].faults, stats[i].replacements);
        if (stats[i].accesses == 0) fputs("N/A\n", stream);
        else fprintf(stream, "%.2f%%\n", 100.0 * (double)stats[i].faults / (double)stats[i].accesses);
    }
}

void report_belady(FILE *stream, size_t min_frames, size_t max_frames, const SimulationStats (*rows)[3])
{
    size_t frames, anomalies = 0;
    fputs("Frames  FIFO faults  LRU faults  OPT faults\n", stream);
    for (frames = min_frames; frames <= max_frames; ++frames) {
        size_t row = frames - min_frames;
        fprintf(stream, "%-7zu %-12zu %-11zu %zu\n", frames,
                rows[row][POLICY_FIFO].faults, rows[row][POLICY_LRU].faults, rows[row][POLICY_OPT].faults);
    }
    for (frames = min_frames + 1; frames <= max_frames; ++frames) {
        size_t row = frames - min_frames;
        if (rows[row][POLICY_FIFO].faults > rows[row - 1][POLICY_FIFO].faults) {
            ++anomalies;
            fprintf(stream, "\nBelady anomaly detected:\nFIFO faults increased from %zu to %zu when frames increased from %zu to %zu.\n",
                    rows[row - 1][POLICY_FIFO].faults, rows[row][POLICY_FIFO].faults, frames - 1, frames);
        }
    }
    if (anomalies == 0) fputs("\nNo FIFO Belady anomaly detected in this range.\n", stream);
    fputs("LRU and OPT curves: non-increasing.\n", stream);
}
