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

    return UNITY_END();
}
