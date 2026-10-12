#include "report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ARG_TRACE = 1, ARG_FRAMES = 2, ARG_POLICY = 4, ARG_VERBOSE = 8, ARG_COMPARE = 16, ARG_BELADY = 32 };
typedef struct {
    const char *trace_path;
    size_t frames, min_frames, max_frames;
    ReplacementPolicy policy;
    unsigned flags;
} Options;

static void usage(FILE *stream)
{
    fputs("Usage:\n"
          "  page_replacement_lab --trace FILE --frames N [--policy fifo|lru|opt] [--verbose]\n"
          "  page_replacement_lab --trace FILE --frames N --compare [--verbose]\n"
          "  page_replacement_lab --trace FILE --belady MIN MAX\n"
          "Frames: 1..1024. Default policy: fifo. --help must be used alone.\n", stream);
}

static int parse_frames(const char *text, size_t *result)
{
    size_t value = 0;
    const unsigned char *p = (const unsigned char *)text;
    if (*p == '\0') return 0;
    for (; *p; ++p) {
        size_t digit;
        if (*p < '0' || *p > '9') return 0;
        digit = (size_t)(*p - '0');
        if (value > (MAX_FRAMES - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    if (value < 1 || value > MAX_FRAMES) return 0;
    *result = value;
    return 1;
}

static int has_value(int index, int argc, char *argv[])
{
    return index < argc && argv[index][0] != '\0' && strncmp(argv[index], "--", 2) != 0;
}

static int parse_options(int argc, char *argv[], Options *options)
{
    int i;
    memset(options, 0, sizeof *options);
    options->policy = POLICY_FIFO;
    for (i = 1; i < argc; ++i) {
        unsigned flag;
        if (strcmp(argv[i], "--trace") == 0) flag = ARG_TRACE;
        else if (strcmp(argv[i], "--frames") == 0) flag = ARG_FRAMES;
        else if (strcmp(argv[i], "--policy") == 0) flag = ARG_POLICY;
        else if (strcmp(argv[i], "--verbose") == 0) flag = ARG_VERBOSE;
        else if (strcmp(argv[i], "--compare") == 0) flag = ARG_COMPARE;
        else if (strcmp(argv[i], "--belady") == 0) flag = ARG_BELADY;
        else return 0;
        if (options->flags & flag) return 0;
        options->flags |= flag;
        if (flag == ARG_VERBOSE || flag == ARG_COMPARE) continue;
        if (!has_value(i + 1, argc, argv)) return 0;
        ++i;
        if (flag == ARG_TRACE) options->trace_path = argv[i];
        else if (flag == ARG_FRAMES) { if (!parse_frames(argv[i], &options->frames)) return 0; }
        else if (flag == ARG_POLICY) {
            if (strcmp(argv[i], "fifo") == 0) options->policy = POLICY_FIFO;
            else if (strcmp(argv[i], "lru") == 0) options->policy = POLICY_LRU;
            else if (strcmp(argv[i], "opt") == 0) options->policy = POLICY_OPT;
            else return 0;
        } else {
            if (!parse_frames(argv[i], &options->min_frames) || !has_value(i + 1, argc, argv)) return 0;
            ++i;
            if (!parse_frames(argv[i], &options->max_frames) || options->min_frames > options->max_frames) return 0;
        }
    }
    if (!(options->flags & ARG_TRACE)) return 0;
    if (options->flags & ARG_BELADY)
        return !(options->flags & (ARG_FRAMES | ARG_POLICY | ARG_VERBOSE | ARG_COMPARE));
    return (options->flags & ARG_FRAMES) && !((options->flags & ARG_COMPARE) && (options->flags & ARG_POLICY));
}

int main(int argc, char *argv[])
{
    Options options;
    Trace trace;
    SimulationStats stats[3];
    SimulationStats (*rows)[3] = NULL;
    char error[1024] = "Simulation failed";
    int ok;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(stdout); return EXIT_SUCCESS; }
    if (!parse_options(argc, argv, &options)) {
        fputs("Invalid arguments: use unique options, decimal frames 1..1024 and compatible modes.\n", stderr);
        usage(stderr); return EXIT_FAILURE;
    }
    trace_init(&trace);
    if (!trace_load(options.trace_path, &trace, error, sizeof error)) {
        fprintf(stderr, "%s\n", error); return EXIT_FAILURE;
    }
    if (options.flags & ARG_BELADY) {
        size_t count = options.max_frames - options.min_frames + 1;
        rows = calloc(count, sizeof *rows);
        if (rows == NULL) { trace_free(&trace); fputs("Scan allocation failed\n", stderr); return EXIT_FAILURE; }
        ok = scan_frame_range(&trace, options.min_frames, options.max_frames, rows, error, sizeof error);
        if (ok) report_belady(stdout, options.min_frames, options.max_frames, (const SimulationStats (*)[3])rows);
    } else if (options.flags & ARG_COMPARE) {
        ok = compare_policies(&trace, options.frames, stats, (options.flags & ARG_VERBOSE) ? report_step : NULL,
                              stdout, error, sizeof error);
        if (ok) {
            report_summary(stdout, stats, 3, POLICY_FIFO);
            puts("OPT bound: verified (faults <= FIFO and LRU).");
        }
    } else {
        ok = simulate(&trace, options.frames, options.policy, &stats[0],
                      (options.flags & ARG_VERBOSE) ? report_step : NULL, stdout, error, sizeof error);
        if (ok) report_summary(stdout, stats, 1, options.policy);
    }
    free(rows); trace_free(&trace);
    if (!ok) { fprintf(stderr, "%s\n", error); return EXIT_FAILURE; }
    if (fflush(stdout) != 0 || ferror(stdout)) { fputs("Output write failed\n", stderr); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
