#include "report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *fixtures = "tests/fixtures";
static int passed = 0, failed = 0;
static char error[1024];
#define REQUIRE(c) do { if (!(c)) { printf("  line %d: %s (%s)\n", __LINE__, #c, error); return 0; } } while (0)
static const uint32_t classic[] = {1,2,3,4,1,2,5,1,2,3,4,5};

static Trace borrowed(const uint32_t *pages, size_t count)
{
    Trace trace;
    /* Algorithms treat this borrowed test input as const; never trace_free it. */
    trace.pages = (uint32_t *)pages; trace.count = trace.capacity = count;
    return trace;
}

static int write_bytes(const void *text, size_t length)
{
    FILE *file = fopen("page-test-input.trace", "wb");
    int ok;
    if (file == NULL) return 0;
    ok = fwrite(text, 1, length, file) == length;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int reject_text(const char *text)
{
    Trace trace;
    REQUIRE(write_bytes(text, strlen(text)));
    trace_init(&trace);
    REQUIRE(!trace_load("page-test-input.trace", &trace, error, sizeof error));
    REQUIRE(strstr(error, "line 2:") != NULL);
    REQUIRE(trace.pages == NULL && trace.count == 0);
    return 1;
}

static int test_load(void)
{
    Trace trace;
    char path[1024];
    (void)snprintf(path, sizeof path, "%s/valid.trace", fixtures);
    trace_init(&trace);
    REQUIRE(trace_load(path, &trace, error, sizeof error));
    REQUIRE(trace.count == 4 && trace.pages[0] == 0 && trace.pages[1] == UINT32_MAX && trace.pages[3] == 7);
    trace_free(&trace);
    REQUIRE(trace.pages == NULL && trace.count == 0 && trace.capacity == 0);
    return 1;
}
static int test_negative(void) { return reject_text("1\n-1\n"); }
static int test_hex(void) { return reject_text("1\n0x10\n"); }
static int test_fraction(void) { return reject_text("1\n1.5\n"); }
static int test_trailing(void) { return reject_text("1\n2x\n"); }
static int test_extra_page(void) { return reject_text("1\n2 3\n"); }
static int test_inline_comment(void) { return reject_text("1\n2 # note\n"); }
static int test_plus_sign(void) { return reject_text("1\n+2\n"); }
static int test_out_of_range(void) { return reject_text("1\n4294967296\n"); }

static int test_physical_lines(void)
{
    Trace trace;
    const char *text = "# comment\n\n  # another\n1\ninvalid\n";
    REQUIRE(write_bytes(text, strlen(text)));
    trace_init(&trace);
    REQUIRE(!trace_load("page-test-input.trace", &trace, error, sizeof error));
    REQUIRE(strstr(error, "line 5:") != NULL && trace.count == 0 && trace.pages == NULL);
    return 1;
}

static int test_long_line(void)
{
    char line[300];
    memset(line, '0', sizeof line); line[0] = '1'; line[1] = '\n'; line[299] = '\0';
    REQUIRE(reject_text(line));
    REQUIRE(strstr(error, "line too long") != NULL);
    return 1;
}

static int test_embedded_nul(void)
{
    Trace trace;
    const char bytes[] = {'1','\n','2','\0','9','\n'};
    REQUIRE(write_bytes(bytes, sizeof bytes));
    trace_init(&trace);
    REQUIRE(!trace_load("page-test-input.trace", &trace, error, sizeof error));
    REQUIRE(strstr(error, "line 2:") && strstr(error, "NUL") && trace.count == 0);
    return 1;
}

static int test_crlf_and_final_line(void)
{
    Trace trace;
    const char *text = "\r\n  # comment\r\n  1 \r\n2";
    REQUIRE(write_bytes(text, strlen(text)));
    trace_init(&trace);
    REQUIRE(trace_load("page-test-input.trace", &trace, error, sizeof error));
    REQUIRE(trace.count == 2 && trace.pages[0] == 1 && trace.pages[1] == 2);
    trace_free(&trace);
    return 1;
}

static int test_growth(void)
{
    FILE *file = fopen("page-test-input.trace", "wb");
    Trace trace;
    REQUIRE(file != NULL);
    for (unsigned i = 0; i < 10000; ++i) REQUIRE(fprintf(file, "%u\n", i) > 0);
    REQUIRE(fclose(file) == 0);
    trace_init(&trace);
    REQUIRE(trace_load("page-test-input.trace", &trace, error, sizeof error));
    REQUIRE(trace.count == 10000 && trace.capacity >= trace.count);
    for (size_t i = 0; i < trace.count; ++i) REQUIRE(trace.pages[i] == i);
    trace_free(&trace);
    return 1;
}

static int test_missing_trace(void)
{
    Trace trace;
    trace_init(&trace);
    REQUIRE(!trace_load("page-test-no-such-directory/missing.trace", &trace, error, sizeof error));
    REQUIRE(strstr(error, "Cannot open trace") != NULL && trace.pages == NULL);
    return 1;
}

static int test_empty(void)
{
    Trace trace;
    SimulationStats stats[3];
    REQUIRE(write_bytes("", 0));
    trace_init(&trace);
    REQUIRE(trace_load("page-test-input.trace", &trace, error, sizeof error));
    REQUIRE(trace.count == 0);
    REQUIRE(compare_policies(&trace, 3, stats, NULL, NULL, error, sizeof error));
    for (int p = 0; p < 3; ++p) REQUIRE(stats[p].accesses == 0 && stats[p].faults == 0 && stats_are_valid(&stats[p]));
    trace_free(&trace);
    return 1;
}

static int test_comments_only(void)
{
    Trace trace;
    REQUIRE(write_bytes("# only\n\n  # more\n", 17));
    trace_init(&trace);
    REQUIRE(trace_load("page-test-input.trace", &trace, error, sizeof error) && trace.count == 0);
    trace_free(&trace);
    return 1;
}

static int test_repeated_page(void)
{
    const uint32_t pages[] = {0,0,0,0};
    Trace trace = borrowed(pages, 4);
    SimulationStats stats[3];
    REQUIRE(compare_policies(&trace, 1, stats, NULL, NULL, error, sizeof error));
    for (int p = 0; p < 3; ++p) REQUIRE(stats[p].faults == 1 && stats[p].hits == 3 && stats[p].replacements == 0);
    return 1;
}

static int test_many_frames(void)
{
    const uint32_t pages[] = {0,UINT32_MAX,1,UINT32_MAX,0,1};
    Trace trace = borrowed(pages, 6);
    SimulationStats stats[3];
    REQUIRE(compare_policies(&trace, MAX_FRAMES, stats, NULL, NULL, error, sizeof error));
    for (int p = 0; p < 3; ++p) REQUIRE(stats[p].faults == 3 && stats[p].hits == 3 && stats[p].replacements == 0);
    return 1;
}

typedef struct { uint32_t evicted[32]; int has_evicted[32]; PageFrame frames[32][5]; size_t calls; } Capture;
static void capture(const SimulationStep *step, void *context)
{
    Capture *data = context;
    size_t row = step->step - 1;
    if (row < 32 && step->frame_count <= 5) {
        data->evicted[row] = step->evicted_page; data->has_evicted[row] = step->evicted;
        for (size_t j = 0; j < step->frame_count; ++j) data->frames[row][j] = step->frames[j];
    }
    ++data->calls;
}

static int test_fifo_hit(void)
{
    const uint32_t pages[] = {1,2,1,3};
    Trace trace = borrowed(pages, 4);
    SimulationStats stats;
    Capture steps = {0};
    REQUIRE(simulate(&trace, 2, POLICY_FIFO, &stats, capture, &steps, error, sizeof error));
    REQUIRE(steps.calls == 4 && steps.has_evicted[3] && steps.evicted[3] == 1);
    REQUIRE(stats.hits == 1 && stats.faults == 3 && stats.replacements == 1);
    return 1;
}

static int test_fifo_three(void)
{
    Trace trace = borrowed(classic, 12);
    SimulationStats stats;
    REQUIRE(simulate(&trace, 3, POLICY_FIFO, &stats, NULL, NULL, error, sizeof error));
    REQUIRE(stats.faults == 9 && stats.hits == 3 && stats.replacements == 6);
    return 1;
}

static int test_fifo_four(void)
{
    Trace trace = borrowed(classic, 12);
    SimulationStats stats;
    REQUIRE(simulate(&trace, 4, POLICY_FIFO, &stats, NULL, NULL, error, sizeof error));
    REQUIRE(stats.faults == 10 && stats.hits == 2 && stats.replacements == 6);
    return 1;
}

static int test_lru_hit(void)
{
    const uint32_t pages[] = {1,2,1,3};
    Trace trace = borrowed(pages, 4);
    SimulationStats stats;
    Capture steps = {0};
    REQUIRE(simulate(&trace, 2, POLICY_LRU, &stats, capture, &steps, error, sizeof error));
    REQUIRE(steps.has_evicted[3] && steps.evicted[3] == 2);
    REQUIRE(steps.frames[2][0].last_used > steps.frames[1][0].last_used);
    REQUIRE(stats.faults == 3 && stats.replacements == 1);
    return 1;
}

static int test_opt_never_again(void)
{
    const uint32_t pages[] = {1,2,3,1};
    Trace trace = borrowed(pages, 4);
    SimulationStats stats;
    Capture steps = {0};
    REQUIRE(simulate(&trace, 2, POLICY_OPT, &stats, capture, &steps, error, sizeof error));
    REQUIRE(steps.evicted[2] == 2 && stats.faults == 3 && stats.hits == 1);
    return 1;
}

static int test_opt_farthest(void)
{
    const uint32_t pages[] = {1,2,3,1,2};
    Trace trace = borrowed(pages, 5);
    SimulationStats stats;
    Capture steps = {0};
    REQUIRE(simulate(&trace, 2, POLICY_OPT, &stats, capture, &steps, error, sizeof error));
    REQUIRE(steps.evicted[2] == 2);
    REQUIRE(stats.faults == 4 && stats.hits == 1);
    return 1;
}

static int test_next_use(void)
{
    const uint32_t pages[] = {UINT32_MAX,0,UINT32_MAX,1,0};
    Trace trace = borrowed(pages, 5);
    size_t *next = NULL;
    REQUIRE(trace_next_use(&trace, &next, error, sizeof error));
    REQUIRE(next[0] == 2 && next[1] == 4 && next[2] == SIZE_MAX && next[3] == SIZE_MAX && next[4] == SIZE_MAX);
    free(next);
    return 1;
}

static int test_next_use_against_scan(void)
{
    uint32_t pages[257];
    Trace trace = borrowed(pages, 257);
    size_t *next = NULL;
    for (size_t i = 0; i < 257; ++i) pages[i] = (uint32_t)((i * i + i * 17) % 31);
    REQUIRE(trace_next_use(&trace, &next, error, sizeof error));
    for (size_t i = 0; i < trace.count; ++i) {
        size_t expected = SIZE_MAX;
        for (size_t j = i + 1; j < trace.count; ++j) if (pages[j] == pages[i]) { expected = j; break; }
        REQUIRE(next[i] == expected);
    }
    free(next);
    return 1;
}

static int test_preprocessing_overflow(void)
{
    uint32_t page = 1;
    Trace impossible = borrowed(&page, SIZE_MAX);
    size_t *next = NULL;
    REQUIRE(!trace_next_use(&impossible, &next, error, sizeof error));
    REQUIRE(next == NULL && strstr(error, "capacity") != NULL);
    return 1;
}

static int test_compare_independent(void)
{
    Trace trace = borrowed(classic, 12);
    SimulationStats stats[3], alone;
    REQUIRE(compare_policies(&trace, 3, stats, NULL, NULL, error, sizeof error));
    for (int p = 0; p < 3; ++p) {
        REQUIRE(simulate(&trace, 3, (ReplacementPolicy)p, &alone, NULL, NULL, error, sizeof error));
        REQUIRE(stats[p].hits == alone.hits && stats[p].faults == alone.faults && stats[p].replacements == alone.replacements);
        REQUIRE(stats_are_valid(&stats[p]));
    }
    REQUIRE(stats[POLICY_OPT].faults <= stats[POLICY_FIFO].faults && stats[POLICY_OPT].faults <= stats[POLICY_LRU].faults);
    REQUIRE(memcmp(trace.pages, classic, sizeof classic) == 0);
    return 1;
}

static int test_scan(void)
{
    Trace trace = borrowed(classic, 12);
    SimulationStats rows[5][3];
    REQUIRE(scan_frame_range(&trace, 2, 6, rows, error, sizeof error));
    REQUIRE(rows[1][POLICY_FIFO].faults == 9 && rows[2][POLICY_FIFO].faults == 10);
    for (int i = 1; i < 5; ++i) {
        REQUIRE(rows[i][POLICY_LRU].faults <= rows[i-1][POLICY_LRU].faults);
        REQUIRE(rows[i][POLICY_OPT].faults <= rows[i-1][POLICY_OPT].faults);
    }
    return 1;
}

static int test_invalid_arguments(void)
{
    Trace trace = borrowed(classic, 12);
    SimulationStats stats, rows[1][3];
    REQUIRE(!simulate(&trace, 0, POLICY_FIFO, &stats, NULL, NULL, error, sizeof error));
    REQUIRE(!simulate(&trace, MAX_FRAMES + 1, POLICY_FIFO, &stats, NULL, NULL, error, sizeof error));
    REQUIRE(!simulate(&trace, 1, (ReplacementPolicy)99, &stats, NULL, NULL, error, sizeof error));
    REQUIRE(!simulate(NULL, 1, POLICY_FIFO, &stats, NULL, NULL, error, sizeof error));
    REQUIRE(!simulate(&trace, 1, POLICY_FIFO, NULL, NULL, NULL, error, sizeof error));
    REQUIRE(!scan_frame_range(&trace, 2, 1, rows, error, sizeof error));
    REQUIRE(!scan_frame_range(&trace, 0, 1, rows, error, sizeof error));
    return 1;
}

/* Exhaustive independent optimum: branch over EVERY possible victim, without
   next-use predictions. Small inputs keep this exponential oracle tractable. */
static size_t optimal_faults(const uint32_t pages[], size_t count, size_t position, uint32_t frames[], size_t used, size_t capacity)
{
    size_t best = SIZE_MAX;
    if (position == count) return 0;
    for (size_t i = 0; i < used; ++i)
        if (frames[i] == pages[position]) return optimal_faults(pages, count, position + 1, frames, used, capacity);
    if (used < capacity) {
        frames[used] = pages[position];
        return 1 + optimal_faults(pages, count, position + 1, frames, used + 1, capacity);
    }
    for (size_t i = 0; i < capacity; ++i) {
        uint32_t old = frames[i];
        size_t candidate;
        frames[i] = pages[position];
        candidate = 1 + optimal_faults(pages, count, position + 1, frames, used, capacity);
        frames[i] = old;
        if (candidate < best) best = candidate;
    }
    return best;
}

static int test_exhaustive_optimum(void)
{
    uint32_t pages[6], oracle_frames[3];
    for (unsigned encoded = 0; encoded < 729; ++encoded) {
        unsigned remaining = encoded;
        Trace trace = borrowed(pages, 6);
        size_t previous_lru = SIZE_MAX, previous_opt = SIZE_MAX;
        for (int i = 0; i < 6; ++i) { pages[i] = remaining % 3; remaining /= 3; }
        for (size_t capacity = 1; capacity <= 3; ++capacity) {
            SimulationStats stats[3];
            REQUIRE(compare_policies(&trace, capacity, stats, NULL, NULL, error, sizeof error));
            REQUIRE(stats[POLICY_OPT].faults == optimal_faults(pages, 6, 0, oracle_frames, 0, capacity));
            REQUIRE(stats[POLICY_LRU].faults <= previous_lru && stats[POLICY_OPT].faults <= previous_opt);
            for (int p = 0; p < 3; ++p) REQUIRE(stats_are_valid(&stats[p]));
            previous_lru = stats[POLICY_LRU].faults; previous_opt = stats[POLICY_OPT].faults;
        }
    }
    return 1;
}

static int test_stat_validation(void)
{
    SimulationStats valid = {12,3,9,6}, overflow = {SIZE_MAX,SIZE_MAX,SIZE_MAX,0}, bad = {1,0,1,2};
    REQUIRE(stats_are_valid(&valid));
    REQUIRE(!stats_are_valid(&overflow) && !stats_are_valid(&bad));
    return 1;
}

static int test_empty_report(void)
{
    FILE *file = tmpfile();
    SimulationStats stats = {0};
    char text[512] = {0};
    REQUIRE(file != NULL);
    report_summary(file, &stats, 1, POLICY_FIFO);
    rewind(file);
    REQUIRE(fread(text, 1, sizeof text - 1, file) > 0);
    REQUIRE(fclose(file) == 0);
    REQUIRE(strstr(text, "N/A") != NULL && strstr(text, "nan") == NULL);
    return 1;
}

static int test_one_frame(void)
{
    const uint32_t pages[] = {1,2,2,3,1};
    Trace trace = borrowed(pages, 5);
    SimulationStats stats[3];
    REQUIRE(compare_policies(&trace, 1, stats, NULL, NULL, error, sizeof error));
    for (int p = 0; p < 3; ++p) REQUIRE(stats[p].faults == 4 && stats[p].hits == 1 && stats[p].replacements == 3);
    return 1;
}

static void run(const char *name, int (*test)(void))
{
    error[0] = '\0';
    if (test()) { ++passed; printf("[PASS] %s\n", name); }
    else { ++failed; printf("[FAIL] %s\n", name); }
}

int main(int argc, char *argv[])
{
    if (argc == 2) fixtures = argv[1];
    else if (argc != 1) return 1;
    run("normal trace including zero and UINT32_MAX", test_load);
    run("negative rejected", test_negative);
    run("hex rejected", test_hex);
    run("fraction rejected", test_fraction);
    run("trailing characters rejected", test_trailing);
    run("extra page rejected", test_extra_page);
    run("inline comment rejected", test_inline_comment);
    run("plus sign rejected", test_plus_sign);
    run("page overflow rejected", test_out_of_range);
    run("physical line number and no partial trace", test_physical_lines);
    run("overlong line rejected", test_long_line);
    run("embedded NUL rejected", test_embedded_nul);
    run("CRLF whitespace and final line", test_crlf_and_final_line);
    run("dynamic growth preserves 10000 pages", test_growth);
    run("missing trace rejected", test_missing_trace);
    run("empty trace and zero statistics", test_empty);
    run("comments only", test_comments_only);
    run("single page repeated", test_repeated_page);
    run("1024 frames and extreme page IDs", test_many_frames);
    run("FIFO hit preserves victim order", test_fifo_hit);
    run("FIFO classic three frames has nine faults", test_fifo_three);
    run("FIFO classic four frames has ten faults", test_fifo_four);
    run("LRU hit refreshes timestamp", test_lru_hit);
    run("OPT evicts never reused page", test_opt_never_again);
    run("OPT evicts farthest future page", test_opt_farthest);
    run("next-use array with uint32 extremes", test_next_use);
    run("next-use versus independent forward scan", test_next_use_against_scan);
    run("preprocessing size overflow rejected", test_preprocessing_overflow);
    run("comparison independent and input unchanged", test_compare_independent);
    run("Belady curve and stack monotonicity", test_scan);
    run("invalid simulation arguments", test_invalid_arguments);
    run("729 traces versus exhaustive optimal oracle", test_exhaustive_optimum);
    run("statistics invariant validation avoids overflow", test_stat_validation);
    run("empty report N/A", test_empty_report);
    run("one frame identical policies", test_one_frame);
    (void)remove("page-test-input.trace");
    printf("\n%d tests passed, %d tests failed.\n", passed, failed);
    return failed ? 1 : 0;
}
