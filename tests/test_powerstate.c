//cfusa:test REQ-PWR-001
//cfusa:test REQ-PWR-002
//cfusa:test REQ-PWR-003
//cfusa:test REQ-PWR-004
//cfusa:test REQ-PWR-005
//cfusa:test REQ-PWR-006
//cfusa:test REQ-PWR-007
//cfusa:test REQ-PWR-008
//cfusa:test REQ-PWR-009
//cfusa:test REQ-PWR-010
#include "unity.h"

#include <rcp/clock.h>
#include "legacy_mock.h"
#include <rcp/powerstate.h>
#include <rcp/rcp.h>

#include <string.h>

#include <stdlib.h>

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
static int test_atomic_add(volatile int *v, int n)
{
    return (int)InterlockedExchangeAdd((volatile LONG *)v, (LONG)n) + n;
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
static int test_atomic_add(volatile int *v, int n) { return __atomic_add_fetch(v, n, __ATOMIC_ACQ_REL); }
#endif

void setUp(void) {}
void tearDown(void) {}

static void test_sleep_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

/* ── AlwaysFail: fails every send with a fixed error and counts attempts ─── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
    int               err;
    volatile int      sends;
} always_fail_t;

static rcp_zone_t always_fail_zone(rcp_controller_t *self) { return ((always_fail_t *)self)->zone; }

static int always_fail_send(rcp_controller_t *self, const rcp_context_t *ctx,
                             const rcp_command_t *cmd, rcp_response_t *out)
{
    always_fail_t *af = (always_fail_t *)self;
    (void)ctx; (void)cmd; (void)out;
    test_atomic_add(&af->sends, 1);
    return af->err;
}

static int always_fail_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)ctx; (void)out;
    return ((always_fail_t *)self)->err;
}

static int always_fail_close(rcp_controller_t *self) { (void)self; return RCP_OK; }
static void always_fail_destroy(rcp_controller_t *self) { free(self); }

static const rcp_controller_vtable_t always_fail_vtable = {
    always_fail_zone, always_fail_send, always_fail_subscribe, always_fail_close, always_fail_destroy, NULL, NULL,
};

static rcp_controller_t *always_fail_new(rcp_zone_t z, int err)
{
    always_fail_t *af = (always_fail_t *)calloc(1, sizeof(*af));
    af->base.vt       = &always_fail_vtable;
    af->base.refcount = 1;
    af->zone = z;
    af->err  = err;
    return &af->base;
}

/* ── FailThenOk: fails the first fail_count sends, then succeeds ─────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
    volatile int      remaining;
} fail_then_ok_t;

static rcp_zone_t fail_then_ok_zone(rcp_controller_t *self) { return ((fail_then_ok_t *)self)->zone; }

static int fail_then_ok_send(rcp_controller_t *self, const rcp_context_t *ctx,
                              const rcp_command_t *cmd, rcp_response_t *out)
{
    fail_then_ok_t *ft = (fail_then_ok_t *)self;
    int new_val = test_atomic_add(&ft->remaining, -1);
    (void)ctx;
    if (new_val >= 0) return RCP_ERR_TIMEOUT;
    out->command_id = cmd->id;
    out->zone       = ft->zone;
    out->status     = RCP_RESPONSE_OK;
    return RCP_OK;
}

static int fail_then_ok_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_OK;
}

static int fail_then_ok_close(rcp_controller_t *self) { (void)self; return RCP_OK; }
static void fail_then_ok_destroy(rcp_controller_t *self) { free(self); }

static const rcp_controller_vtable_t fail_then_ok_vtable = {
    fail_then_ok_zone, fail_then_ok_send, fail_then_ok_subscribe, fail_then_ok_close, fail_then_ok_destroy, NULL, NULL,
};

static rcp_controller_t *fail_then_ok_new(rcp_zone_t z, int fail_count)
{
    fail_then_ok_t *ft = (fail_then_ok_t *)calloc(1, sizeof(*ft));
    ft->base.vt       = &fail_then_ok_vtable;
    ft->base.refcount = 1;
    ft->zone      = z;
    ft->remaining = fail_count;
    return &ft->base;
}

/* ── Sleep/Wake ───────────────────────────────────────────────────────────── */

static void capture_type_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    rcp_command_type_t *last_type = (rcp_command_type_t *)user_data;
    *last_type = cmd->type;
    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_OK;
}

static void test_sleep_sends_cmd_sleep_and_transitions_active_to_sleeping(void)
{
    rcp_command_type_t last_type = (rcp_command_type_t)-1;
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, capture_type_handler, &last_type);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_powerstate_manager_t *mgr = rcp_powerstate_manager_new(rcp_powerstate_default_config(), ctrls, 1);
    rcp_context_t ctx = rcp_context_background();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_powerstate_manager_sleep(mgr, &ctx, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_CMD_SLEEP, last_type);
    TEST_ASSERT_EQUAL(RCP_POWER_SLEEPING, rcp_powerstate_manager_state(mgr, RCP_ZONE_FRONT_LEFT));

    rcp_powerstate_manager_destroy(mgr);
    rcp_controller_release(ctrl);
}

static void test_wake_sends_cmd_wake_and_transitions_sleeping_to_active(void)
{
    rcp_command_type_t last_type = (rcp_command_type_t)-1;
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, capture_type_handler, &last_type);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_powerstate_manager_t *mgr = rcp_powerstate_manager_new(rcp_powerstate_default_config(), ctrls, 1);
    rcp_context_t ctx = rcp_context_background();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_powerstate_manager_sleep(mgr, &ctx, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_POWER_SLEEPING, rcp_powerstate_manager_state(mgr, RCP_ZONE_FRONT_LEFT));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_powerstate_manager_wake(mgr, &ctx, RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL(RCP_CMD_WAKE, last_type);
    TEST_ASSERT_EQUAL(RCP_POWER_ACTIVE, rcp_powerstate_manager_state(mgr, RCP_ZONE_FRONT_LEFT));

    rcp_powerstate_manager_destroy(mgr);
    rcp_controller_release(ctrl);
}

/* ── Subscribe ────────────────────────────────────────────────────────────── */

typedef struct {
    rcp_power_event_t events[4];
    size_t             len;
} captured_events_t;

static void capture_event_cb(const rcp_power_event_t *ev, void *user_data)
{
    captured_events_t *ce = (captured_events_t *)user_data;
    ce->events[ce->len++] = *ev;
}

static void test_subscribe_delivers_transition_events_to_multiple_callbacks(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_powerstate_manager_t *mgr = rcp_powerstate_manager_new(rcp_powerstate_default_config(), ctrls, 1);
    rcp_context_t ctx = rcp_context_background();
    captured_events_t first;
    captured_events_t second;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    /* Two subscriptions force callbacks_append() to grow its backing array
     * beyond a single entry, and both must be invoked on the same
     * transition. */
    TEST_ASSERT_TRUE(rcp_powerstate_manager_subscribe(mgr, capture_event_cb, &first));
    TEST_ASSERT_TRUE(rcp_powerstate_manager_subscribe(mgr, capture_event_cb, &second));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_powerstate_manager_sleep(mgr, &ctx, RCP_ZONE_FRONT_LEFT));

    TEST_ASSERT_EQUAL_UINT(1, first.len);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, first.events[0].zone);
    TEST_ASSERT_EQUAL(RCP_POWER_SLEEPING, first.events[0].state);
    TEST_ASSERT_EQUAL_UINT(1, second.len);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, second.events[0].zone);
    TEST_ASSERT_EQUAL(RCP_POWER_SLEEPING, second.events[0].state);

    rcp_powerstate_manager_destroy(mgr);
    rcp_controller_release(ctrl);
}

/* ── BusOff ───────────────────────────────────────────────────────────────── */

static void test_command_failure_yields_bus_off(void)
{
    rcp_controller_t *ctrl = always_fail_new(RCP_ZONE_CENTRAL, RCP_ERR_TIMEOUT);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_powerstate_config_t cfg = rcp_powerstate_default_config();
    rcp_powerstate_manager_t *mgr;
    rcp_context_t ctx = rcp_context_background();

    cfg.recovery_interval_ms = 3600000; /* effectively disable recovery for this test */
    mgr = rcp_powerstate_manager_new(cfg, ctrls, 1);

    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_powerstate_manager_sleep(mgr, &ctx, RCP_ZONE_CENTRAL));
    TEST_ASSERT_EQUAL(RCP_POWER_BUS_OFF, rcp_powerstate_manager_state(mgr, RCP_ZONE_CENTRAL));

    rcp_powerstate_manager_destroy(mgr);
    rcp_controller_release(ctrl);
}

static bool poll_for_power_state(rcp_powerstate_manager_t *mgr, rcp_zone_t z, rcp_power_state_t want)
{
    int elapsed_ms = 0;
    while (elapsed_ms < 5000) {
        if (rcp_powerstate_manager_state(mgr, z) == want) return true;
        test_sleep_ms(10);
        elapsed_ms += 10;
    }
    return false;
}

static void test_recover_loop_retries_bus_off_zones(void)
{
    /* First send (the sleep) fails -> BusOff; the recovery loop's Wake succeeds. */
    rcp_controller_t *ctrl = fail_then_ok_new(RCP_ZONE_REAR_LEFT, 1);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_powerstate_config_t cfg = rcp_powerstate_default_config();
    rcp_powerstate_manager_t *mgr;
    rcp_context_t ctx = rcp_context_background();

    cfg.recovery_interval_ms = 10;
    cfg.recovery_timeout_ms  = 10;
    mgr = rcp_powerstate_manager_new(cfg, ctrls, 1);

    TEST_ASSERT_NOT_EQUAL(RCP_OK, rcp_powerstate_manager_sleep(mgr, &ctx, RCP_ZONE_REAR_LEFT)); /* fails -> BusOff */
    TEST_ASSERT_EQUAL(RCP_POWER_BUS_OFF, rcp_powerstate_manager_state(mgr, RCP_ZONE_REAR_LEFT));

    TEST_ASSERT_TRUE(poll_for_power_state(mgr, RCP_ZONE_REAR_LEFT, RCP_POWER_ACTIVE));

    rcp_powerstate_manager_destroy(mgr);
    rcp_controller_release(ctrl);
}

/* ── Thread safety ────────────────────────────────────────────────────────── */

typedef struct {
    rcp_powerstate_manager_t *mgr;
    volatile int               stop;
} reader_args_t;

#if defined(_WIN32)
static DWORD WINAPI reader_thread(void *arg)
#else
static void *reader_thread(void *arg)
#endif
{
    reader_args_t *a = (reader_args_t *)arg;
    while (!a->stop) {
        rcp_power_state_t s = rcp_powerstate_manager_state(a->mgr, RCP_ZONE_FRONT_RIGHT);
        TEST_ASSERT_TRUE(s == RCP_POWER_ACTIVE || s == RCP_POWER_SLEEPING || s == RCP_POWER_BUS_OFF);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_state_is_thread_safe(void)
{
    rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_RIGHT, NULL, NULL);
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_powerstate_manager_t *mgr = rcp_powerstate_manager_new(rcp_powerstate_default_config(), ctrls, 1);
    rcp_context_t ctx = rcp_context_background();
    reader_args_t args;
    test_thread_t readers[4];
    int i;

    args.mgr  = mgr;
    args.stop = 0;
    for (i = 0; i < 4; i++) readers[i] = test_thread_spawn(reader_thread, &args);

    for (i = 0; i < 50; i++) {
        int e1 = rcp_powerstate_manager_sleep(mgr, &ctx, RCP_ZONE_FRONT_RIGHT); (void)e1;
        int e2 = rcp_powerstate_manager_wake(mgr, &ctx, RCP_ZONE_FRONT_RIGHT);  (void)e2;
    }
    args.stop = 1;
    for (i = 0; i < 4; i++) test_thread_join(readers[i]);

    rcp_powerstate_manager_destroy(mgr);
    rcp_controller_release(ctrl);
}

/* ── Close ────────────────────────────────────────────────────────────────── */

static void test_close_stops_the_recovery_loop(void)
{
    rcp_controller_t *ctrl = always_fail_new(RCP_ZONE_REAR_RIGHT, RCP_ERR_TIMEOUT);
    always_fail_t *af = (always_fail_t *)ctrl;
    rcp_controller_t *ctrls[] = {ctrl};
    rcp_powerstate_config_t cfg = rcp_powerstate_default_config();
    rcp_powerstate_manager_t *mgr;
    rcp_context_t ctx = rcp_context_background();
    int elapsed_ms;
    int after_close;

    cfg.recovery_interval_ms = 10;
    cfg.recovery_timeout_ms  = 10;
    mgr = rcp_powerstate_manager_new(cfg, ctrls, 1);

    TEST_ASSERT_NOT_EQUAL(RCP_OK, rcp_powerstate_manager_sleep(mgr, &ctx, RCP_ZONE_REAR_RIGHT)); /* -> BusOff, recovery starts retrying */

    /* Wait until the recovery loop is observably active. */
    elapsed_ms = 0;
    while (af->sends < 3 && elapsed_ms < 5000) {
        test_sleep_ms(10);
        elapsed_ms += 10;
    }
    TEST_ASSERT_TRUE(af->sends >= 3);

    rcp_powerstate_manager_close(mgr);
    test_sleep_ms(50); /* let any in-flight attempt settle */
    after_close = af->sends;
    test_sleep_ms(100); /* several recovery_intervals */
    /* No further send attempts once the loop has stopped (allow one in-flight). */
    TEST_ASSERT_TRUE(af->sends <= after_close + 1);

    rcp_powerstate_manager_destroy(mgr);
    rcp_controller_release(ctrl);
}

static void test_power_state_string_unique_nonempty(void)
{
    const rcp_power_state_t states[] = {
        RCP_POWER_ACTIVE, RCP_POWER_SLEEPING, RCP_POWER_BUS_OFF,
    };
    const size_t n = sizeof(states) / sizeof(states[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_power_state_string(states[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(s, rcp_power_state_string(states[j])) != 0 ? 1 : 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sleep_sends_cmd_sleep_and_transitions_active_to_sleeping);
    RUN_TEST(test_wake_sends_cmd_wake_and_transitions_sleeping_to_active);
    RUN_TEST(test_subscribe_delivers_transition_events_to_multiple_callbacks);
    RUN_TEST(test_command_failure_yields_bus_off);
    RUN_TEST(test_recover_loop_retries_bus_off_zones);
    RUN_TEST(test_state_is_thread_safe);
    RUN_TEST(test_close_stops_the_recovery_loop);
    RUN_TEST(test_power_state_string_unique_nonempty);

    return UNITY_END();
}
