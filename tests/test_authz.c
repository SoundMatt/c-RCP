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

static rcp_avtp_addr_t make_addr(uint16_t unique_id, uint8_t byte_bus_id)
{
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    rcp_avtp_addr_t a;
    a.stream_id   = rcp_stream_id_make(mac, unique_id);
    a.byte_bus_id = byte_bus_id;
    return a;
}

/* ── Basic permit/deny ────────────────────────────────────────────────────── */

//cfusa:test REQ-AUTH-001
static void test_permitted_identity_succeeds(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_avtp_addr_t addrs[] = {make_addr(1, 3)};
    uint8_t types[] = {0x00};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", addrs, 1, types, 1));
    TEST_ASSERT_TRUE(rcp_authz_policy_permit(policy, "alice", make_addr(1, 3), 0x00));

    rcp_authz_policy_release(policy);
}

//cfusa:test REQ-AUTH-002
//cfusa:test REQ-AUTH-007
static void test_denied_identity_is_not_permitted(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_avtp_addr_t addrs[] = {make_addr(1, 3)};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", addrs, 1, NULL, 0));

    /* "eve" never appears in any policy entry: default-deny. */
    TEST_ASSERT_FALSE(rcp_authz_policy_permit(policy, "eve", make_addr(1, 3), 0x00));

    rcp_authz_policy_release(policy);
}

/* ── Wildcards ────────────────────────────────────────────────────────────── */

//cfusa:test REQ-AUTH-003
static void test_empty_addrs_and_types_means_all_allowed(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "admin", NULL, 0, NULL, 0));

    TEST_ASSERT_TRUE(rcp_authz_policy_permit(policy, "admin", make_addr(99, 7), 0x0F));
    TEST_ASSERT_TRUE(rcp_authz_policy_permit(policy, "admin", make_addr(1, 0), 0x00));

    rcp_authz_policy_release(policy);
}

//cfusa:test REQ-AUTH-005
static void test_wrong_address_is_forbidden(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_avtp_addr_t addrs[] = {make_addr(1, 3)};

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", addrs, 1, NULL, 0));

    /* Same stream_id, different byte_bus_id -- not a match. */
    TEST_ASSERT_FALSE(rcp_authz_policy_permit(policy, "alice", make_addr(1, 4), 0x00));
    /* Different stream_id, same byte_bus_id -- not a match either. */
    TEST_ASSERT_FALSE(rcp_authz_policy_permit(policy, "alice", make_addr(2, 3), 0x00));

    rcp_authz_policy_release(policy);
}

//cfusa:test REQ-AUTH-006
static void test_wrong_request_type_is_forbidden(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    uint8_t types[] = {0x0F}; /* compound, per request_compound.h */

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", NULL, 0, types, 1));

    TEST_ASSERT_TRUE(rcp_authz_policy_permit(policy, "alice", make_addr(1, 3), 0x0F));
    TEST_ASSERT_FALSE(rcp_authz_policy_permit(policy, "alice", make_addr(1, 3), 0x00));

    rcp_authz_policy_release(policy);
}

/* ── allow() copies its arrays by value ──────────────────────────────────── */

//cfusa:test REQ-AUTH-008
static void test_allow_copies_addrs_and_types_by_value(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_avtp_addr_t addrs[1];
    uint8_t types[1];

    addrs[0] = make_addr(5, 1);
    types[0] = 0x01;
    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", addrs, 1, types, 1));

    /* Mutating the caller's own arrays after allow() must not affect the
     * policy's stored entry -- it copied them by value, not by reference. */
    addrs[0] = make_addr(6, 2);
    types[0] = 0x02;

    TEST_ASSERT_TRUE(rcp_authz_policy_permit(policy, "alice", make_addr(5, 1), 0x01));
    TEST_ASSERT_FALSE(rcp_authz_policy_permit(policy, "alice", make_addr(6, 2), 0x02));

    rcp_authz_policy_release(policy);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

typedef struct {
    rcp_authz_policy_t *policy;
    int                  permits; /* only written by this thread, read after join */
} permit_worker_args_t;

#if defined(_WIN32)
static DWORD WINAPI permit_worker(void *arg)
#else
static void *permit_worker(void *arg)
#endif
{
    permit_worker_args_t *a = (permit_worker_args_t *)arg;
    rcp_avtp_addr_t tmp[] = {make_addr(42, 9)};
    int i;

    for (i = 0; i < 5000; i++) {
        if (rcp_authz_policy_permit(a->policy, "alice", make_addr(1, 3), 0x00)) {
            a->permits++;
        }
        if ((i & 0x3ff) == 0) {
            (void)rcp_authz_policy_allow(a->policy, "tmp", tmp, 1, NULL, 0);
        }
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

//cfusa:test REQ-AUTH-004
static void test_policy_permits_concurrently_without_data_races(void)
{
    rcp_authz_policy_t *policy = rcp_authz_policy_new();
    rcp_avtp_addr_t addrs[] = {make_addr(1, 3)};
    permit_worker_args_t args[8];
    test_thread_t threads[8];
    int total = 0;
    int i;

    TEST_ASSERT_TRUE(rcp_authz_policy_allow(policy, "alice", addrs, 1, NULL, 0));

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
    RUN_TEST(test_denied_identity_is_not_permitted);
    RUN_TEST(test_empty_addrs_and_types_means_all_allowed);
    RUN_TEST(test_wrong_address_is_forbidden);
    RUN_TEST(test_wrong_request_type_is_forbidden);
    RUN_TEST(test_allow_copies_addrs_and_types_by_value);
    RUN_TEST(test_policy_permits_concurrently_without_data_races);

    return UNITY_END();
}
