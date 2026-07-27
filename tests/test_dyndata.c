//cfusa:test REQ-DYN-001
//cfusa:test REQ-DYN-002
//cfusa:test REQ-DYN-003
//cfusa:test REQ-DYN-004
//cfusa:test REQ-DYN-005
//cfusa:test REQ-DYN-006
#include "unity.h"

#include <rcp/dyndata.h>
#include <rcp/rcp.h>

#include <stdint.h>
#include <string.h>

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

static void test_schema_registry_add_and_lookup(void)
{
    rcp_schema_registry_t *reg = rcp_schema_registry_new();
    rcp_field_descriptor_t fields[2];
    rcp_schema_entry_t out;

    memset(fields, 0, sizeof(fields));
    strncpy(fields[0].name, "angle", sizeof(fields[0].name) - 1);
    strncpy(fields[0].type, "float", sizeof(fields[0].type) - 1);
    fields[0].offset = 0;
    fields[0].size   = 4;
    strncpy(fields[1].name, "height", sizeof(fields[1].name) - 1);
    strncpy(fields[1].type, "float", sizeof(fields[1].type) - 1);
    fields[1].offset = 4;
    fields[1].size   = 4;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_schema_registry_add(reg, 0x0001, "SeatPosition", fields, 2));
    TEST_ASSERT_EQUAL_UINT(1, rcp_schema_registry_size(reg));

    TEST_ASSERT_TRUE(rcp_schema_registry_lookup(reg, 0x0001, &out));
    TEST_ASSERT_EQUAL_STRING("SeatPosition", out.name);
    TEST_ASSERT_EQUAL_UINT(2, out.fields_len);

    rcp_schema_entry_free(&out);
    rcp_schema_registry_destroy(reg);
}

static void test_duplicate_schema_returns_already_exists(void)
{
    rcp_schema_registry_t *reg = rcp_schema_registry_new();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_schema_registry_add(reg, 0x0002, "A", NULL, 0));
    TEST_ASSERT_EQUAL(RCP_ERR_ALREADY_EXISTS, rcp_schema_registry_add(reg, 0x0002, "B", NULL, 0));

    rcp_schema_registry_destroy(reg);
}

static void test_dynamic_payload_encode_decode_round_trip(void)
{
    rcp_dynamic_payload_t dp;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t wire;
    rcp_dynamic_payload_t dp2;

    dp.schema_id = 0xDEADBEEF;
    dp.data.data = data;
    dp.data.len  = sizeof(data);

    wire = rcp_dynamic_payload_encode(&dp);
    TEST_ASSERT_EQUAL_UINT(8, wire.len); /* 4-byte schema_id + 4 bytes data */

    dp2 = rcp_dynamic_payload_decode(wire.data, wire.len);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, dp2.schema_id);
    TEST_ASSERT_EQUAL_UINT(4, dp2.data.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, dp2.data.data, 4);

    rcp_bytes_free(&dp2.data);
    rcp_bytes_free(&wire);
}

static void test_decode_too_short_buffer_returns_zero_schema_id(void)
{
    uint8_t raw[] = {0x01, 0x02}; /* too short */
    rcp_dynamic_payload_t dp = rcp_dynamic_payload_decode(raw, sizeof(raw));

    TEST_ASSERT_EQUAL_UINT32(0, dp.schema_id);
    TEST_ASSERT_EQUAL_UINT(0, dp.data.len);
    TEST_ASSERT_NULL(dp.data.data);
}

static void test_unknown_schema_lookup_returns_false(void)
{
    rcp_schema_registry_t *reg = rcp_schema_registry_new();
    rcp_schema_entry_t out;

    TEST_ASSERT_FALSE(rcp_schema_registry_lookup(reg, 0xFFFF, &out));

    rcp_schema_registry_destroy(reg);
}

static void test_encode_prepends_schema_id_big_endian(void)
{
    rcp_dynamic_payload_t dp;
    uint8_t data[] = {0xAA, 0xBB};
    rcp_bytes_t wire;

    dp.schema_id = 0x01020304;
    dp.data.data = data;
    dp.data.len  = sizeof(data);

    wire = rcp_dynamic_payload_encode(&dp);
    TEST_ASSERT_EQUAL_UINT(6, wire.len);
    /* Most-significant byte first. */
    TEST_ASSERT_EQUAL_HEX8(0x01, wire.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, wire.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, wire.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, wire.data[3]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, wire.data[4]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, wire.data[5]);

    rcp_bytes_free(&wire);
}

/* ── Concurrency ──────────────────────────────────────────────────────────── */

#define KTHREADS 8
#define KPER_THREAD 500

static rcp_schema_registry_t *g_reg;

#if defined(_WIN32)
static DWORD WINAPI schema_worker(void *arg)
#else
static void *schema_worker(void *arg)
#endif
{
    int t = (int)(intptr_t)arg;
    int i;

    for (i = 0; i < KPER_THREAD; i++) {
        rcp_schema_id_t id = (rcp_schema_id_t)(t * KPER_THREAD + i + 1);
        rcp_schema_entry_t out;
        bool found;

        (void)rcp_schema_registry_add(g_reg, id, "s", NULL, 0);
        found = rcp_schema_registry_lookup(g_reg, id, &out);
        if (found) rcp_schema_entry_free(&out);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_schema_registry_is_thread_safe(void)
{
    test_thread_t threads[KTHREADS];
    int i;

    g_reg = rcp_schema_registry_new();

    for (i = 0; i < KTHREADS; i++) {
#if defined(_WIN32)
        threads[i] = test_thread_spawn(schema_worker, (void *)(intptr_t)i);
#else
        threads[i] = test_thread_spawn(schema_worker, (void *)(intptr_t)i);
#endif
    }
    for (i = 0; i < KTHREADS; i++) test_thread_join(threads[i]);

    /* Every distinct id was added exactly once, with no lost updates or crashes. */
    TEST_ASSERT_EQUAL_UINT(KTHREADS * KPER_THREAD, rcp_schema_registry_size(g_reg));

    rcp_schema_registry_destroy(g_reg);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_schema_registry_add_and_lookup);
    RUN_TEST(test_duplicate_schema_returns_already_exists);
    RUN_TEST(test_dynamic_payload_encode_decode_round_trip);
    RUN_TEST(test_decode_too_short_buffer_returns_zero_schema_id);
    RUN_TEST(test_unknown_schema_lookup_returns_false);
    RUN_TEST(test_encode_prepends_schema_id_big_endian);
    RUN_TEST(test_schema_registry_is_thread_safe);

    return UNITY_END();
}
