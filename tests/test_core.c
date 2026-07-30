//cfusa:test REQ-ERR-001
//cfusa:test REQ-ERR-002
//cfusa:test REQ-ERR-003
//cfusa:test REQ-ERR-004
//cfusa:test REQ-ERR-005
//cfusa:test REQ-ERR-006
//cfusa:test REQ-ERR-010
//cfusa:test REQ-ERR-012
//cfusa:test REQ-RELAY-014
//cfusa:test REQ-CORE-001
//cfusa:test REQ-CORE-002
/* Tests rcp.h/rcp.c's surviving surface: the shared, protocol-agnostic
 * primitives (base rcp_errc_t sentinels, rcp_strerror, relay_strerror,
 * rcp_bytes_t) that are all that remains of this header after the retired
 * pre-TC18 Zone/Command/Response/Status/Controller/Registry object model
 * was removed with no compatibility shim (RELAY spec §15.5). Replaces the
 * subset of the old test_rcp.c that covered this surviving surface; the
 * rest of test_rcp.c (which certified only the removed model) was deleted
 * along with rcp.h's Zone/Controller/Registry types. */
#include "unity.h"

#include <string.h>

#include <rcp/rcp.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Sentinel error codes ──────────────────────────────────────────────────── */

static void test_sentinel_errors_nonzero(void)
{
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_CLOSED);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_NOT_FOUND);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_ALREADY_EXISTS);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_TIMEOUT);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, RCP_ERR_BUSY);
}

static void test_sentinel_errors_distinct(void)
{
    const rcp_errc_t sentinels[] = {
        RCP_ERR_CLOSED, RCP_ERR_NOT_FOUND, RCP_ERR_ALREADY_EXISTS,
        RCP_ERR_TIMEOUT, RCP_ERR_BUSY, RCP_ERR_NOT_SUPPORTED, RCP_ERR_FORBIDDEN,
    };
    const size_t n = sizeof(sentinels) / sizeof(sentinels[0]);
    size_t i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i != j) TEST_ASSERT_NOT_EQUAL(sentinels[i], sentinels[j]);
        }
    }
}

static void test_timeout_detectable_by_value_comparison(void)
{
    int rc = RCP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rc);
    TEST_ASSERT_NOT_EQUAL(RCP_OK, rc);
}

static void test_rcp_strerror_unique_nonempty(void)
{
    const rcp_errc_t codes[] = {
        RCP_OK, RCP_ERR_CLOSED, RCP_ERR_NOT_FOUND, RCP_ERR_ALREADY_EXISTS,
        RCP_ERR_TIMEOUT, RCP_ERR_BUSY, RCP_ERR_NOT_SUPPORTED, RCP_ERR_FORBIDDEN,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_strerror(codes[j])) != 0);
        }
    }
}

static void test_relay_strerror_unique_nonempty(void)
{
    const relay_errc_t codes[] = {
        RELAY_ERRC_CLOSED, RELAY_ERRC_NOT_CONNECTED,
        RELAY_ERRC_TIMEOUT, RELAY_ERRC_PAYLOAD_TOO_LARGE,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = relay_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, relay_strerror(codes[j])) != 0);
        }
    }
}

/* ── rcp_bytes_t ───────────────────────────────────────────────────────────── */

static void test_bytes_dup_copies_data(void)
{
    const uint8_t src[] = {1, 2, 3, 4, 5};
    rcp_bytes_t b = rcp_bytes_dup(src, sizeof(src));

    TEST_ASSERT_NOT_NULL(b.data);
    TEST_ASSERT_EQUAL_UINT(sizeof(src), b.len);
    TEST_ASSERT_EQUAL_MEMORY(src, b.data, sizeof(src));
    TEST_ASSERT_TRUE(src != b.data); /* must be a copy, not an alias */

    rcp_bytes_free(&b);
}

static void test_bytes_dup_zero_len_returns_zeroed(void)
{
    const uint8_t src[] = {0xAA};
    rcp_bytes_t b = rcp_bytes_dup(src, 0);

    TEST_ASSERT_NULL(b.data);
    TEST_ASSERT_EQUAL_UINT(0, b.len);
}

static void test_bytes_free_zeroes_struct(void)
{
    const uint8_t src[] = {9, 8, 7};
    rcp_bytes_t b = rcp_bytes_dup(src, sizeof(src));

    rcp_bytes_free(&b);
    TEST_ASSERT_NULL(b.data);
    TEST_ASSERT_EQUAL_UINT(0, b.len);
}

static void test_bytes_free_safe_on_already_freed(void)
{
    rcp_bytes_t b;
    b.data = NULL;
    b.len  = 0;

    rcp_bytes_free(&b); /* must not crash */
    TEST_ASSERT_NULL(b.data);
    TEST_ASSERT_EQUAL_UINT(0, b.len);
}

static void test_bytes_free_safe_on_null_pointer(void)
{
    rcp_bytes_free(NULL); /* must not crash */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sentinel_errors_nonzero);
    RUN_TEST(test_sentinel_errors_distinct);
    RUN_TEST(test_timeout_detectable_by_value_comparison);
    RUN_TEST(test_rcp_strerror_unique_nonempty);
    RUN_TEST(test_relay_strerror_unique_nonempty);

    RUN_TEST(test_bytes_dup_copies_data);
    RUN_TEST(test_bytes_dup_zero_len_returns_zeroed);
    RUN_TEST(test_bytes_free_zeroes_struct);
    RUN_TEST(test_bytes_free_safe_on_already_freed);
    RUN_TEST(test_bytes_free_safe_on_null_pointer);

    return UNITY_END();
}
