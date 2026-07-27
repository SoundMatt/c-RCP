//cfusa:test REQ-AUTH-001
//cfusa:test REQ-AUTH-002
//cfusa:test REQ-AUTH-003
//cfusa:test REQ-AUTH-004
//cfusa:test REQ-AUTH-005
//cfusa:test REQ-AUTH-006
//cfusa:test REQ-AUTH-007
//cfusa:test REQ-AUTH-008
#include "unity.h"

#include <rcp/authz.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>

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

static rcp_controller_t *make_ctrl(rcp_zone_t z)
{
    return rcp_mock_controller_new(z, NULL, NULL);
}

/* ── Basic permit/deny ────────────────────────────────────────────────────── */

static void test_permitted_identity_succeeds(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_zone_t zones[] = {RCP_ZONE_FRONT_LEFT};
    rcp_command_type_t types[] = {RCP_CMD_SET};
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", zones, 1, types, 1));
    ctrl = rcp_authz_controller_new(inner, policy, NULL, NULL);
    rcp_authz_controller_set_identity(ctrl, "alice");

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_SET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

static void test_denied_identity_returns_forbidden(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_zone_t zones[] = {RCP_ZONE_FRONT_LEFT};
    rcp_command_type_t types[] = {RCP_CMD_SET};
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", zones, 1, types, 1));
    ctrl = rcp_authz_controller_new(inner, policy, NULL, NULL);
    rcp_authz_controller_set_identity(ctrl, "eve");

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_SET;
    TEST_ASSERT_EQUAL(RCP_ERR_FORBIDDEN, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

/* ── Wildcards ────────────────────────────────────────────────────────────── */

static void test_empty_zones_types_means_all_allowed(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_CENTRAL);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "admin", NULL, 0, NULL, 0));
    ctrl = rcp_authz_controller_new(inner, policy, NULL, NULL);
    rcp_authz_controller_set_identity(ctrl, "admin");

    cmd.zone = RCP_ZONE_CENTRAL;
    cmd.type = RCP_CMD_RESET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

static void test_wrong_zone_is_forbidden(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_zone_t zones[] = {RCP_ZONE_FRONT_LEFT};
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_REAR_RIGHT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", zones, 1, NULL, 0));
    ctrl = rcp_authz_controller_new(inner, policy, NULL, NULL);
    rcp_authz_controller_set_identity(ctrl, "alice");

    cmd.zone = RCP_ZONE_REAR_RIGHT;
    cmd.type = RCP_CMD_SET;
    TEST_ASSERT_EQUAL(RCP_ERR_FORBIDDEN, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

/* ── identity_fn override ─────────────────────────────────────────────────── */

static const char *dynamic_identity_fn(void *user_data)
{
    (void)user_data;
    return "dynamic";
}

static void test_identity_fn_takes_priority_over_fixed_identity(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl;
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "dynamic", NULL, 0, NULL, 0));
    ctrl = rcp_authz_controller_new(inner, policy, dynamic_identity_fn, NULL);
    rcp_authz_controller_set_identity(ctrl, "wrong");

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    cmd.type = RCP_CMD_GET;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));

    rcp_response_free(&resp);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

/* ── Passthrough ──────────────────────────────────────────────────────────── */

static void test_zone_delegates_to_inner(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_REAR_LEFT);
    rcp_controller_t *ctrl = rcp_authz_controller_new(inner, policy, NULL, NULL);

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_LEFT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

static void test_subscribe_delegates_to_inner(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_authz_controller_new(inner, policy, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

static void test_close_delegates_to_inner(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_controller_t *inner = make_ctrl(RCP_ZONE_FRONT_LEFT);
    rcp_controller_t *ctrl = rcp_authz_controller_new(inner, policy, NULL, NULL);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    /* After the inner controller is closed, sends through it report RCP_ERR_CLOSED. */
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(inner, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
    rcp_authz_policy_release(policy);
}

/* ── Distinct error code ──────────────────────────────────────────────────── */

static void test_forbidden_is_a_distinct_error_code(void)
{
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_FORBIDDEN);
    TEST_ASSERT_NOT_EQUAL(RCP_ERR_CLOSED, RCP_ERR_FORBIDDEN);
    TEST_ASSERT_NOT_EQUAL(RCP_ERR_BUSY, RCP_ERR_FORBIDDEN);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

typedef struct {
    rcp_authz_policy_t *policy;
    int                   permits; /* only written by this thread, read after join */
} permit_worker_args_t;

#if defined(_WIN32)
static DWORD WINAPI permit_worker(void *arg)
#else
static void *permit_worker(void *arg)
#endif
{
    permit_worker_args_t *a = (permit_worker_args_t *)arg;
    rcp_zone_t central[] = {RCP_ZONE_CENTRAL};
    int i;

    for (i = 0; i < 5000; i++) {
        if (rcp_authz_policy_permit(a->policy, "alice", RCP_ZONE_FRONT_LEFT, RCP_CMD_SET)) {
            a->permits++;
        }
        if ((i & 0x3ff) == 0) {
            (void)rcp_authz_policy_allow(a->policy, "tmp", central, 1, NULL, 0);
        }
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_policy_permits_concurrently_without_data_races(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_zone_t zones[] = {RCP_ZONE_FRONT_LEFT};
    rcp_command_type_t types[] = {RCP_CMD_SET};
    permit_worker_args_t args[8];
    test_thread_t threads[8];
    int total = 0;
    int i;

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", zones, 1, types, 1));

    for (i = 0; i < 8; i++) {
        args[i].policy  = policy;
        args[i].permits = 0;
        threads[i] = test_thread_spawn(permit_worker, &args[i]);
    }
    for (i = 0; i < 8; i++) test_thread_join(threads[i]);
    for (i = 0; i < 8; i++) total += args[i].permits;

    TEST_ASSERT_EQUAL(8 * 5000, total);

    rcp_authz_policy_release(policy);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_permitted_identity_succeeds);
    RUN_TEST(test_denied_identity_returns_forbidden);
    RUN_TEST(test_empty_zones_types_means_all_allowed);
    RUN_TEST(test_wrong_zone_is_forbidden);
    RUN_TEST(test_identity_fn_takes_priority_over_fixed_identity);
    RUN_TEST(test_zone_delegates_to_inner);
    RUN_TEST(test_subscribe_delegates_to_inner);
    RUN_TEST(test_close_delegates_to_inner);
    RUN_TEST(test_forbidden_is_a_distinct_error_code);
    RUN_TEST(test_policy_permits_concurrently_without_data_races);

    return UNITY_END();
}
