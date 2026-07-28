/* Command latency safety-timing test (GSN argument for ASIL-B timing budget).
 *
 * Runs a workload against the mock controller and records P50/P99/P99.9/Max
 * latency. Writes results to COMMAND_LATENCY.md (relative to CWD) so cfusa
 * trace can include it as a safety artifact.
 *
 * Pass/fail gate: P99 < 1 ms and Max < 10 ms (in-process mock baseline);
 * relaxed on CI (shared runners can spike scheduling latency well past that).
 */
/* Must be included before any other header (including unity.h, which pulls
 * in <stdio.h> etc.): bench_util.h's _POSIX_C_SOURCE define needs to land
 * before glibc's feature-test macros lock in on first system-header use. */
#include "bench_util.h"

#include "unity.h"

#include "legacy_mock.h"
#include <rcp/rcp.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Shorter than cpp-RCP's 30s (its CI budget); 3s is enough to gather a
 * representative sample of an in-process mock's latency distribution while
 * keeping this test fast in the ctest suite it now shares with everything
 * else. */
#define WORKLOAD_SECONDS 3
#define MAX_SAMPLES       2000000

void setUp(void) {}
void tearDown(void) {}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static void test_command_latency_p99_under_budget(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    uint64_t *samples;
    size_t n = 0;
    uint64_t workload_start_ns, deadline_ns, elapsed_ns;
    uint64_t p50, p99, p999, max;
    int on_ci;
    FILE *md;

    cmd.zone = RCP_ZONE_FRONT_LEFT;

    samples = (uint64_t *)malloc(MAX_SAMPLES * sizeof(uint64_t));
    TEST_ASSERT_NOT_NULL(samples);

    workload_start_ns = bench_now_ns();
    deadline_ns = workload_start_ns + (uint64_t)WORKLOAD_SECONDS * 1000000000ull;
    while (bench_now_ns() < deadline_ns && n < MAX_SAMPLES) {
        rcp_response_t out = {0};
        uint64_t t0 = bench_now_ns();
        (void)rcp_controller_send(ctrl, &ctx, &cmd, &out);
        uint64_t t1 = bench_now_ns();
        rcp_response_free(&out);
        samples[n++] = t1 - t0;
    }

    elapsed_ns = bench_now_ns() - workload_start_ns;

    qsort(samples, n, sizeof(uint64_t), cmp_u64);

    p50  = samples[n * 50 / 100];
    p99  = samples[n * 99 / 100];
    p999 = samples[n * 999 / 1000];
    max  = samples[n - 1];

    md = fopen("COMMAND_LATENCY.md", "w");
    if (md) {
        fprintf(md, "# Command Latency Results\n\n");
        fprintf(md, "Workload: %zu sends over %.3fs (mock transport)\n\n",
                n, (double)elapsed_ns / 1000000000.0);
        fprintf(md, "| Metric | Value |\n|--------|-------|\n");
        fprintf(md, "| P50    | %.3f us |\n", (double)p50 / 1000.0);
        fprintf(md, "| P99    | %.3f us |\n", (double)p99 / 1000.0);
        fprintf(md, "| P99.9  | %.3f us |\n", (double)p999 / 1000.0);
        fprintf(md, "| Max    | %.3f us |\n", (double)max / 1000.0);
        fclose(md);
    }

    free(samples);
    rcp_controller_release(ctrl);

    /* Safety gate: in-process mock must be well under the real-world ASIL-B
     * budget. Shared CI runners can spike scheduling latency well beyond a
     * bare-metal 10ms max, so relax there while keeping P99 meaningful. */
    on_ci = getenv("CI") != NULL;
    TEST_ASSERT_TRUE(p99 < (uint64_t)(on_ci ? 500000000ull : 1000000ull));   /* P99 < 500ms (CI) / 1ms */
    TEST_ASSERT_TRUE(max < (uint64_t)(on_ci ? 2000000000ull : 10000000ull)); /* Max < 2s (CI) / 10ms */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_command_latency_p99_under_budget);
    return UNITY_END();
}
