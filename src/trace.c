#include "trace.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

void trace_init(Trace *trace) { trace->pages = NULL; trace->count = trace->capacity = 0; }
void trace_free(Trace *trace) { free(trace->pages); trace_init(trace); }

static int append(Trace *trace, uint32_t page)
{
    if (trace->count == trace->capacity) {
        size_t capacity;
        uint32_t *grown;
        if (trace->capacity == 0) capacity = 64;
        else {
            if (trace->capacity > SIZE_MAX / 2) return 0;
            capacity = trace->capacity * 2;
        }
        if (capacity > SIZE_MAX / sizeof *grown) return 0;
        grown = realloc(trace->pages, capacity * sizeof *grown);
        if (grown == NULL) return 0;
        trace->pages = grown; trace->capacity = capacity;
    }
    trace->pages[trace->count++] = page;
    return 1;
}

static int parse_line(const char *line, Trace *trace)
{
    const unsigned char *p = (const unsigned char *)line;
    uint32_t value = 0;
    while (isspace(*p)) ++p;
    if (*p == '\0' || *p == '#') return 1;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (UINT32_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
        ++p;
    }
    while (isspace(*p)) ++p;
    if (*p != '\0') return 0;
    return append(trace, value) ? 1 : -1;
}

int trace_load(const char *path, Trace *output, char *error, size_t error_size)
{
    FILE *file;
    Trace temporary;
    size_t row = 1, length = 0;
    char line[TRACE_LINE_LIMIT + 1];
    int ch, status = 1;
    const char *reason = NULL;
    trace_init(output); trace_init(&temporary);
    file = fopen(path, "rb");
    if (file == NULL) { (void)snprintf(error, error_size, "Cannot open trace: %s", path); return 0; }
    while ((ch = fgetc(file)) != EOF) {
        if (ch == 0) { reason = "embedded NUL is not a decimal page"; break; }
        if (ch == '\n') {
            /* CRLF terminator is not part of the content length. */
            if (length > 0 && line[length - 1] == '\r') --length;
            line[length] = '\0';
            status = parse_line(line, &temporary);
            if (status != 1) { reason = status == 0 ? "expected decimal page 0..4294967295" : "trace allocation/capacity failure"; break; }
            if (row == SIZE_MAX) { reason = "too many physical lines"; break; }
            ++row; length = 0;
        } else {
            if (length == TRACE_LINE_LIMIT) { reason = "line too long (maximum 255 bytes including optional CR)"; break; }
            line[length++] = (char)ch;
        }
    }
    if (reason == NULL && ferror(file)) reason = "trace read failed";
    if (reason == NULL && length > 0) {
        line[length] = '\0';
        status = parse_line(line, &temporary);
        if (status != 1) reason = status == 0 ? "expected decimal page 0..4294967295" : "trace allocation/capacity failure";
    }
    if (fclose(file) != 0 && reason == NULL) reason = "trace close failed";
    if (reason != NULL) {
        (void)snprintf(error, error_size, "%s line %zu: %s", path, row, reason);
        trace_free(&temporary);
        return 0;
    }
    *output = temporary;
    return 1;
}

static int compare_page(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static size_t compressed_id(const uint32_t *unique, size_t count, uint32_t page)
{
    size_t left = 0, right = count;
    while (left < right) {
        size_t middle = left + (right - left) / 2;
        if (unique[middle] < page) left = middle + 1;
        else right = middle;
    }
    return left;
}

int trace_next_use(const Trace *trace, size_t **output, char *error, size_t error_size)
{
    uint32_t *unique = NULL;
    size_t *last = NULL, *next = NULL, count = 0, i;
    *output = NULL;
    if (trace == NULL || (trace->count > 0 && trace->pages == NULL)) {
        (void)snprintf(error, error_size, "Invalid trace for next-use preprocessing"); return 0;
    }
    if (trace->count == 0) return 1;
    if (trace->count > SIZE_MAX / sizeof *unique || trace->count > SIZE_MAX / sizeof *next) goto fail;
    unique = malloc(trace->count * sizeof *unique);
    next = malloc(trace->count * sizeof *next);
    if (unique == NULL || next == NULL) goto fail;
    for (i = 0; i < trace->count; ++i) unique[i] = trace->pages[i];
    qsort(unique, trace->count, sizeof *unique, compare_page);
    for (i = 0; i < trace->count; ++i)
        if (count == 0 || unique[i] != unique[count - 1]) unique[count++] = unique[i];
    last = malloc(count * sizeof *last);
    if (last == NULL) goto fail;
    for (i = 0; i < count; ++i) last[i] = SIZE_MAX;
    for (i = trace->count; i-- > 0;) {
        size_t id = compressed_id(unique, count, trace->pages[i]);
        next[i] = last[id]; last[id] = i;
    }
    free(unique); free(last); *output = next;
    return 1;
fail:
    free(unique); free(last); free(next);
    (void)snprintf(error, error_size, "OPT preprocessing allocation/capacity failure");
    return 0;
}
