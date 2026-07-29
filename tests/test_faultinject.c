//cfusa:test REQ-FI-001
//cfusa:test REQ-FI-002
//cfusa:test REQ-FI-003
//cfusa:test REQ-FI-004
//cfusa:test REQ-FI-005
//cfusa:test REQ-FI-006
//cfusa:test REQ-FI-007
//cfusa:test REQ-FI-008
//cfusa:test REQ-FI-009
//cfusa:test REQ-FI-010
#include "unity.h"

#include <rcp/faultinject.h>

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE test_thread_t;
static test_thread_t test_thread_spawn(DWORD(WINAPI *fn)(void *), void *arg)
{
    return CreateThread(NULL, 0, fn, arg, 0, NULL);
}
static void test_thread_join(test_thread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
#include <pthread.h>
typedef pthread_t test_thread_t;
static test_thread_t test_thread_spawn(void *(*fn)(void *), void *arg)
{
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void test_thread_join(test_thread_t t) { pthread_join(t, NULL); }
#endif

void setUp(void) {}
void tearDown(void) {}

/* ── No rules ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-FI-001
static void test_evaluate_returns_proceed_when_no_rules(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();

    TEST_ASSERT_EQUAL(RCP_FI_PROCEED, rcp_faultinject_evaluate(fi, NULL));

    rcp_faultinject_destroy(fi);
}

/* ── Individual rule types ────────────────────────────────────────────────── */

//cfusa:test REQ-FI-002
static void test_drop_rule(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    rcp_fi_rule_t rule = {RCP_FI_DROP, 0, -1};

    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));
    TEST_ASSERT_EQUAL(RCP_FI_DROP, rcp_faultinject_evaluate(fi, NULL));

    rcp_faultinject_destroy(fi);
}

//cfusa:test REQ-FI-003
static void test_error_rule(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    rcp_fi_rule_t rule = {RCP_FI_ERROR, 0, -1};

    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));
    TEST_ASSERT_EQUAL(RCP_FI_ERROR, rcp_faultinject_evaluate(fi, NULL));

    rcp_faultinject_destroy(fi);
}

//cfusa:test REQ-FI-004
static void test_slow_rule_reports_configured_latency(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    rcp_fi_rule_t rule = {RCP_FI_SLOW, 250, -1};
    uint64_t latency_ms = 0;

    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));
    TEST_ASSERT_EQUAL(RCP_FI_SLOW, rcp_faultinject_evaluate(fi, &latency_ms));
    TEST_ASSERT_EQUAL_UINT64(250, latency_ms);

    rcp_faultinject_destroy(fi);
}

//cfusa:test REQ-FI-007
static void test_timeout_rule(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    rcp_fi_rule_t rule = {RCP_FI_TIMEOUT, 0, -1};

    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));
    TEST_ASSERT_EQUAL(RCP_FI_TIMEOUT, rcp_faultinject_evaluate(fi, NULL));

    rcp_faultinject_destroy(fi);
}

/* ── Count-based expiry ───────────────────────────────────────────────────── */

//cfusa:test REQ-FI-005
static void test_count_based_rule_expires_after_n_firings(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    rcp_fi_rule_t rule = {RCP_FI_DROP, 0, 2};

    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));
    TEST_ASSERT_EQUAL(RCP_FI_DROP, rcp_faultinject_evaluate(fi, NULL));
    TEST_ASSERT_EQUAL(RCP_FI_DROP, rcp_faultinject_evaluate(fi, NULL));
    TEST_ASSERT_EQUAL(RCP_FI_PROCEED, rcp_faultinject_evaluate(fi, NULL));

    rcp_faultinject_destroy(fi);
}

/* ── clear_rules() ────────────────────────────────────────────────────────── */

//cfusa:test REQ-FI-006
static void test_clear_rules_removes_all_active_rules(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    rcp_fi_rule_t rule = {RCP_FI_DROP, 0, -1};

    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, rule));
    rcp_faultinject_clear_rules(fi);

    TEST_ASSERT_EQUAL(RCP_FI_PROCEED, rcp_faultinject_evaluate(fi, NULL));

    rcp_faultinject_destroy(fi);
}

/* ── FIFO order ───────────────────────────────────────────────────────────── */

//cfusa:test REQ-FI-008
static void test_rules_evaluated_in_fifo_order(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    rcp_fi_rule_t drop_once  = {RCP_FI_DROP, 0, 1};
    rcp_fi_rule_t error_rest = {RCP_FI_ERROR, 0, -1};

    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, drop_once));
    TEST_ASSERT_TRUE(rcp_faultinject_add_rule(fi, error_rest));

    TEST_ASSERT_EQUAL(RCP_FI_DROP, rcp_faultinject_evaluate(fi, NULL));  /* the first rule fires first */
    TEST_ASSERT_EQUAL(RCP_FI_ERROR, rcp_faultinject_evaluate(fi, NULL)); /* then the second, once the first expires */

    rcp_faultinject_destroy(fi);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

typedef struct {
    rcp_faultinject_t *fi;
} worker_args_t;

#if defined(_WIN32)
static DWORD WINAPI evaluate_worker(void *arg)
#else
static void *evaluate_worker(void *arg)
#endif
{
    worker_args_t *a = (worker_args_t *)arg;
    int i;

    for (i = 0; i < 2000; i++) {
        (void)rcp_faultinject_evaluate(a->fi, NULL);
        if ((i & 0x3f) == 0) {
            rcp_fi_rule_t rule = {RCP_FI_ERROR, 0, 1};
            (void)rcp_faultinject_add_rule(a->fi, rule);
        }
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

//cfusa:test REQ-FI-009
static void test_add_rule_is_safe_concurrently_with_evaluate(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    worker_args_t args;
    test_thread_t threads[4];
    int i;

    args.fi = fi;
    for (i = 0; i < 4; i++) threads[i] = test_thread_spawn(evaluate_worker, &args);
    for (i = 0; i < 4; i++) test_thread_join(threads[i]);

    rcp_faultinject_destroy(fi); /* must not crash (ASan/TSan-checked in CI) */
}

/* ── destroy() ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-FI-010
static void test_destroy_frees_every_rule(void)
{
    rcp_faultinject_t *fi = rcp_faultinject_new();
    int i;

    for (i = 0; i < 16; i++) {
        rcp_fi_rule_t rule = {RCP_FI_DROP, 0, 1};
        (void)rcp_faultinject_add_rule(fi, rule);
    }

    rcp_faultinject_destroy(fi); /* must not leak (ASan-checked in CI) */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_evaluate_returns_proceed_when_no_rules);
    RUN_TEST(test_drop_rule);
    RUN_TEST(test_error_rule);
    RUN_TEST(test_slow_rule_reports_configured_latency);
    RUN_TEST(test_timeout_rule);
    RUN_TEST(test_count_based_rule_expires_after_n_firings);
    RUN_TEST(test_clear_rules_removes_all_active_rules);
    RUN_TEST(test_rules_evaluated_in_fifo_order);
    RUN_TEST(test_add_rule_is_safe_concurrently_with_evaluate);
    RUN_TEST(test_destroy_frees_every_rule);

    return UNITY_END();
}
