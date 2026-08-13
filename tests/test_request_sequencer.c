/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-SEQ-001
//cfusa:test REQ-SEQ-002
//cfusa:test REQ-SEQ-003
//cfusa:test REQ-SEQ-004
//cfusa:test REQ-SEQ-005
//cfusa:test REQ-SEQ-006
//cfusa:test REQ-SEQ-007
//cfusa:test REQ-SEQ-008
//cfusa:test REQ-SEQ-009
//cfusa:test REQ-SEQ-010
//cfusa:test REQ-SEQ-011
//cfusa:test REQ-SEQ-013
#include "unity.h"

#include <rcp/request_sequencer.h>

void setUp(void) {}
void tearDown(void) {}

/* ── rcp_sequencer_table_new() ────────────────────────────────────────────── */

static void test_table_new_zero_count_is_unsupported_not_a_failure(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(0);

    TEST_ASSERT_NULL(table.state);
    TEST_ASSERT_EQUAL_UINT16(0, table.count);
    TEST_ASSERT_TRUE(rcp_sequencer_table_unsupported(&table));

    rcp_sequencer_table_free(&table);
}

static void test_table_new_nonzero_count_initializes_power_on_state(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    uint16_t i;

    TEST_ASSERT_NOT_NULL(table.state);
    TEST_ASSERT_EQUAL_UINT16(4, table.count);
    TEST_ASSERT_FALSE(rcp_sequencer_table_unsupported(&table));

    for (i = 0; i < table.count; i++) {
        TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, table.state[i]);
    }

    rcp_sequencer_table_free(&table);
}

/* ── rcp_sequencer_table_unsupported() ────────────────────────────────────── */

static void test_table_unsupported_true_only_for_zero_count(void)
{
    rcp_sequencer_table_t zero = rcp_sequencer_table_new(0);
    rcp_sequencer_table_t four = rcp_sequencer_table_new(4);

    TEST_ASSERT_TRUE(rcp_sequencer_table_unsupported(&zero));
    TEST_ASSERT_FALSE(rcp_sequencer_table_unsupported(&four));

    rcp_sequencer_table_free(&zero);
    rcp_sequencer_table_free(&four);
}

/* ── rcp_sequencer_table_reset() ──────────────────────────────────────────── */

static void test_table_reset_restores_power_on_state(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 200));
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 3, 42));

    rcp_sequencer_table_reset(&table);

    uint8_t got;
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 3, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);

    rcp_sequencer_table_free(&table);
}

static void test_table_reset_on_unsupported_table_is_a_no_op(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(0);

    rcp_sequencer_table_reset(&table); /* must not crash */

    TEST_ASSERT_NULL(table.state);
    TEST_ASSERT_EQUAL_UINT16(0, table.count);

    rcp_sequencer_table_free(&table);
}

/* ── rcp_sequencer_table_free() ───────────────────────────────────────────── */

static void test_table_free_zeroes_and_is_idempotent(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);

    rcp_sequencer_table_free(&table);
    TEST_ASSERT_NULL(table.state);
    TEST_ASSERT_EQUAL_UINT16(0, table.count);

    rcp_sequencer_table_free(&table); /* safe on an already-zeroed table */
    TEST_ASSERT_NULL(table.state);
    TEST_ASSERT_EQUAL_UINT16(0, table.count);
}

/* ── rcp_sequencer_index_valid() ──────────────────────────────────────────── */

static void test_index_valid_bounds(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);

    TEST_ASSERT_TRUE(rcp_sequencer_index_valid(&table, 0));
    TEST_ASSERT_TRUE(rcp_sequencer_index_valid(&table, 3));
    TEST_ASSERT_FALSE(rcp_sequencer_index_valid(&table, 4));
    TEST_ASSERT_FALSE(rcp_sequencer_index_valid(&table, 65535));

    rcp_sequencer_table_free(&table);
}

static void test_index_valid_always_false_for_unsupported_table(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(0);

    TEST_ASSERT_FALSE(rcp_sequencer_index_valid(&table, 0));

    rcp_sequencer_table_free(&table);
}

/* ── rcp_sequencer_get_state() / rcp_sequencer_set_state() ───────────────── */

static void test_get_state_valid_index(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    uint8_t got = 0;

    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 2, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);

    rcp_sequencer_table_free(&table);
}

static void test_get_state_invalid_index_fails_and_leaves_out_untouched(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    uint8_t got = 77;

    TEST_ASSERT_FALSE(rcp_sequencer_get_state(&table, 4, &got));
    TEST_ASSERT_EQUAL_UINT8(77, got);

    rcp_sequencer_table_free(&table);
}

static void test_set_state_valid_index_applies(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    uint8_t got = 0;

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 1, 9));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 1, &got));
    TEST_ASSERT_EQUAL_UINT8(9, got);

    rcp_sequencer_table_free(&table);
}

static void test_set_state_invalid_index_fails_and_leaves_table_unchanged(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    uint8_t got = 0;

    TEST_ASSERT_FALSE(rcp_sequencer_set_state(&table, 4, 9));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);

    rcp_sequencer_table_free(&table);
}

/* ── REQ-SEQ-013: ownership (owner get/set, access_permitted) ────────────── */

static void test_table_new_initializes_owner_unclaimed(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    uint8_t got = 0xFFu;

    TEST_ASSERT_TRUE(rcp_sequencer_get_owner(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_OWNER_UNCLAIMED, got);
    TEST_ASSERT_TRUE(rcp_sequencer_get_owner(&table, 3, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_OWNER_UNCLAIMED, got);

    rcp_sequencer_table_free(&table);
}

static void test_get_owner_invalid_index_fails_and_leaves_out_untouched(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);
    uint8_t got = 0xABu;

    TEST_ASSERT_FALSE(rcp_sequencer_get_owner(&table, 2, &got));
    TEST_ASSERT_EQUAL_UINT8(0xABu, got);

    rcp_sequencer_table_free(&table);
}

static void test_set_owner_valid_index_applies(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);
    uint8_t got = 0;

    TEST_ASSERT_TRUE(rcp_sequencer_set_owner(&table, 1, 5u));
    TEST_ASSERT_TRUE(rcp_sequencer_get_owner(&table, 1, &got));
    TEST_ASSERT_EQUAL_UINT8(5u, got);
    /* sequencer 0 unaffected */
    TEST_ASSERT_TRUE(rcp_sequencer_get_owner(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_OWNER_UNCLAIMED, got);

    rcp_sequencer_table_free(&table);
}

static void test_set_owner_invalid_index_fails_and_leaves_table_unchanged(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);
    uint8_t got = 0;

    TEST_ASSERT_FALSE(rcp_sequencer_set_owner(&table, 2, 5u));
    TEST_ASSERT_TRUE(rcp_sequencer_get_owner(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_OWNER_UNCLAIMED, got);

    rcp_sequencer_table_free(&table);
}

/* TC18 §12.7.10 Table 28's own access-control rule: an unclaimed
 * sequencer (the power-on default) is fail-closed, not open-access --
 * see RCP_SEQUENCER_OWNER_UNCLAIMED's own doc comment. */
static void test_access_permitted_false_when_unclaimed_regardless_of_requester(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);

    TEST_ASSERT_FALSE(rcp_sequencer_access_permitted(&table, 0, 1u));
    TEST_ASSERT_FALSE(rcp_sequencer_access_permitted(&table, 0, RCP_SEQUENCER_OWNER_UNCLAIMED));

    rcp_sequencer_table_free(&table);
}

static void test_access_permitted_true_only_for_the_recorded_owner(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);

    TEST_ASSERT_TRUE(rcp_sequencer_set_owner(&table, 0, 7u));
    TEST_ASSERT_TRUE(rcp_sequencer_access_permitted(&table, 0, 7u));
    TEST_ASSERT_FALSE(rcp_sequencer_access_permitted(&table, 0, 8u));
    /* a different, unclaimed sequencer is unaffected by sequencer 0's
     * own ownership */
    TEST_ASSERT_FALSE(rcp_sequencer_access_permitted(&table, 1, 7u));

    rcp_sequencer_table_free(&table);
}

static void test_access_permitted_invalid_index_is_false(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);

    TEST_ASSERT_FALSE(rcp_sequencer_access_permitted(&table, 2, 1u));

    rcp_sequencer_table_free(&table);
}

/* Table 28's own literal scope ("all sequencer state values are set to
 * 1") names Seq_state only -- ownership (Request_stream_index)
 * deliberately survives a state-only reset, matching every other
 * ordinary R/W* config field's own persistence. */
static void test_table_reset_does_not_clear_owner(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);
    uint8_t got = 0;

    TEST_ASSERT_TRUE(rcp_sequencer_set_owner(&table, 0, 3u));
    rcp_sequencer_table_reset(&table);
    TEST_ASSERT_TRUE(rcp_sequencer_get_owner(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(3u, got);

    rcp_sequencer_table_free(&table);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_table_new_zero_count_is_unsupported_not_a_failure);
    RUN_TEST(test_table_new_nonzero_count_initializes_power_on_state);

    RUN_TEST(test_table_unsupported_true_only_for_zero_count);

    RUN_TEST(test_table_reset_restores_power_on_state);
    RUN_TEST(test_table_reset_on_unsupported_table_is_a_no_op);

    RUN_TEST(test_table_free_zeroes_and_is_idempotent);

    RUN_TEST(test_index_valid_bounds);
    RUN_TEST(test_index_valid_always_false_for_unsupported_table);

    RUN_TEST(test_get_state_valid_index);
    RUN_TEST(test_get_state_invalid_index_fails_and_leaves_out_untouched);
    RUN_TEST(test_set_state_valid_index_applies);
    RUN_TEST(test_set_state_invalid_index_fails_and_leaves_table_unchanged);

    RUN_TEST(test_table_new_initializes_owner_unclaimed);
    RUN_TEST(test_get_owner_invalid_index_fails_and_leaves_out_untouched);
    RUN_TEST(test_set_owner_valid_index_applies);
    RUN_TEST(test_set_owner_invalid_index_fails_and_leaves_table_unchanged);
    RUN_TEST(test_access_permitted_false_when_unclaimed_regardless_of_requester);
    RUN_TEST(test_access_permitted_true_only_for_the_recorded_owner);
    RUN_TEST(test_access_permitted_invalid_index_is_false);
    RUN_TEST(test_table_reset_does_not_clear_owner);

    return UNITY_END();
}
