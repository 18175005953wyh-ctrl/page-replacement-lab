#ifndef REPORT_H
#define REPORT_H
#include "replacement.h"
#include <stdio.h>
void report_step(const SimulationStep *step, void *context);
void report_summary(FILE *stream, const SimulationStats stats[], size_t count, ReplacementPolicy first);
void report_belady(FILE *stream, size_t min_frames, size_t max_frames, const SimulationStats (*rows)[3]);
#endif
