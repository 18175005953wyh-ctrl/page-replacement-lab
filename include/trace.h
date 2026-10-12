#ifndef TRACE_H
#define TRACE_H
#include <stddef.h>
#include <stdint.h>

#define TRACE_LINE_LIMIT 255
typedef struct { uint32_t *pages; size_t count; size_t capacity; } Trace;

void trace_init(Trace *trace);
void trace_free(Trace *trace);
/* On failure, output is empty. Initialize/free it before loading again. */
int trace_load(const char *path, Trace *output, char *error, size_t error_size);
/* Caller owns the returned array. SIZE_MAX means no future reference. */
int trace_next_use(const Trace *trace, size_t **output, char *error, size_t error_size);
#endif
