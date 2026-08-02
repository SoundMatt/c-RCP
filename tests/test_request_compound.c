/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-CMP-001
//cfusa:test REQ-CMP-002
//cfusa:test REQ-CMP-003
//cfusa:test REQ-CMP-004
//cfusa:test REQ-CMP-005
//cfusa:test REQ-CMP-006
//cfusa:test REQ-CMP-007
//cfusa:test REQ-CMP-008
//cfusa:test REQ-CMP-009
//cfusa:test REQ-CMP-010
//cfusa:test REQ-CMP-011
//cfusa:test REQ-CMP-012
//cfusa:test REQ-CMP-013
//cfusa:test REQ-CMP-014
//cfusa:test REQ-CMP-015
//cfusa:test REQ-CMP-016
//cfusa:test REQ-CMP-017
//cfusa:test REQ-CMP-018
//cfusa:test REQ-CMP-019
//cfusa:test REQ-CMP-020
//cfusa:test REQ-CMP-021
//cfusa:test REQ-CMP-022
//cfusa:test REQ-CMP-023
//cfusa:test REQ-CMP-024
//cfusa:test REQ-CMP-025
//cfusa:test REQ-CMP-026
//cfusa:test REQ-CMP-027
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/request_compound.h>
#include <rcp/rcp.h>
#include <rcp/request_sequencer.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── request_type classification ──────────────────────────────────────────── */

static void test_is_safety(void)
{
    TEST_ASSERT_FALSE(rcp_request_type_is_safety(RCP_REQUEST_TYPE_COMPOUND));
    TEST_ASSERT_TRUE(rcp_request_type_is_safety(RCP_REQUEST_TYPE_COMPOUND_SAFETY));
    TEST_ASSERT_FALSE(rcp_request_type_is_safety(RCP_REQUEST_TYPE_COMPOUND_WAIT));
    TEST_ASSERT_TRUE(rcp_request_type_is_safety(RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY));
    TEST_ASSERT_FALSE(rcp_request_type_is_safety(RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE));
}

static void test_is_compound(void)
{
    TEST_ASSERT_TRUE(rcp_request_type_is_compound(RCP_REQUEST_TYPE_COMPOUND));
    TEST_ASSERT_TRUE(rcp_request_type_is_compound(RCP_REQUEST_TYPE_COMPOUND_SAFETY));
    TEST_ASSERT_FALSE(rcp_request_type_is_compound(RCP_REQUEST_TYPE_COMPOUND_WAIT));
    TEST_ASSERT_FALSE(rcp_request_type_is_compound(RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE));
}

static void test_is_compound_wait(void)
{
    TEST_ASSERT_TRUE(rcp_request_type_is_compound_wait(RCP_REQUEST_TYPE_COMPOUND_WAIT));
    TEST_ASSERT_TRUE(rcp_request_type_is_compound_wait(RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY));
    TEST_ASSERT_FALSE(rcp_request_type_is_compound_wait(RCP_REQUEST_TYPE_COMPOUND));
    TEST_ASSERT_FALSE(rcp_request_type_is_compound_wait(RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    const char *a = rcp_compound_strerror(RCP_COMPOUND_OK);
    const char *b = rcp_compound_strerror(RCP_COMPOUND_ERR_SHORT_FRAME);
    const char *c = rcp_compound_strerror(RCP_COMPOUND_ERR_BAD_MSG_TYPE);
    const char *d = rcp_compound_strerror(RCP_COMPOUND_ERR_NOT_REPURPOSED);
    const char *e = rcp_compound_strerror(RCP_COMPOUND_ERR_UNKNOWN_TYPE);
    const char *unk = rcp_compound_strerror((rcp_compound_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
    TEST_ASSERT_TRUE(strcmp(c, d) != 0);
    TEST_ASSERT_TRUE(strcmp(d, e) != 0);
}

/* ── rcp_compound_peek_request_type() ─────────────────────────────────────── */

static void test_peek_request_type_reads_compound_opcode(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;
    uint8_t rt = 0;

    step.sequencer_index = 1;
    step.start_state     = 1;
    step.next_state       = 2;
    step.exec_delay    = 500;
    step.repeat_count     = 0;

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 3, &step, 0u, 7, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_OK, rcp_compound_peek_request_type(frame.data, frame.len, &rt));
    TEST_ASSERT_EQUAL_UINT8(RCP_REQUEST_TYPE_COMPOUND, rt);

    rcp_bytes_free(&frame);
}

static void test_peek_request_type_short_frame(void)
{
    uint8_t rt = 0;
    uint8_t buf[4] = {0};

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_SHORT_FRAME,
                           rcp_compound_peek_request_type(buf, sizeof(buf), &rt));
}

static void test_peek_request_type_bad_msg_type(void)
{
    uint8_t rt = 0;
    uint8_t buf[RCP_ACF_GBB_HEADER_LEN];

    memset(buf, 0, sizeof(buf));
    buf[0] = RCP_ACF_MSG_TYPE_ABB; /* not GBB */

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_BAD_MSG_TYPE,
                           rcp_compound_peek_request_type(buf, sizeof(buf), &rt));
}

static void test_peek_request_type_not_repurposed_when_mtv_nonzero(void)
{
    uint8_t rt = 0;
    uint8_t buf[RCP_ACF_GBB_HEADER_LEN];

    memset(buf, 0, sizeof(buf));
    buf[0] = (uint8_t)(RCP_ACF_MSG_TYPE_GBB << 1); /* acf_msg_type: octet 0 bits 7:1 */
    buf[2] = (uint8_t)(RCP_ACF_MTV_VALID << 5); /* mtv = VALID(1), not UNTIMED: octet 2 bit 5 */

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_NOT_REPURPOSED,
                           rcp_compound_peek_request_type(buf, sizeof(buf), &rt));
}

/* ── Compound / compound-wait request encode/decode ───────────────────────── */

static void test_compound_request_round_trip(void)
{
    rcp_compound_step_t step = {0};
    rcp_compound_step_t out_step = {0};
    rcp_bytes_t frame;
    uint8_t payload[3] = {0xAA, 0xBB, 0xCC};
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;

    step.sequencer_index = 12;
    step.start_state     = 1;
    step.next_state       = 2;
    step.exec_delay    = 1500;
    step.repeat_count     = 3;

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 9, &step, 0u, 42,
                                         payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_OK,
                           rcp_compound_decode_request(frame.data, frame.len, &out_rt, &out_bus,
                                                        &out_step, &out_evt, &out_payload,
                                                        &out_payload_len, &out_tn));

    TEST_ASSERT_EQUAL_UINT8(RCP_REQUEST_TYPE_COMPOUND, out_rt);
    TEST_ASSERT_EQUAL_UINT8(9, out_bus);
    TEST_ASSERT_EQUAL_UINT8(42, out_tn);
    TEST_ASSERT_EQUAL_UINT8(12, out_step.sequencer_index);
    TEST_ASSERT_EQUAL_UINT8(1, out_step.start_state);
    TEST_ASSERT_EQUAL_UINT8(2, out_step.next_state);
    TEST_ASSERT_EQUAL_UINT16(1500, out_step.exec_delay);
    TEST_ASSERT_EQUAL_UINT16(3, out_step.repeat_count);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), out_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out_payload, sizeof(payload));

    rcp_bytes_free(&frame);
}

static void test_compound_wait_safety_request_round_trip(void)
{
    rcp_compound_step_t step = {0};
    rcp_compound_step_t out_step = {0};
    rcp_bytes_t frame;
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;

    step.sequencer_index = 255;
    step.start_state     = 200;
    step.next_state       = 201;
    step.exec_delay    = 65535;
    step.repeat_count     = RCP_COMPOUND_REPEAT_INFINITE;

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY, 1, &step, 0u, 5,
                                         NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_OK,
                           rcp_compound_decode_request(frame.data, frame.len, &out_rt, &out_bus,
                                                        &out_step, &out_evt, &out_payload,
                                                        &out_payload_len, &out_tn));

    TEST_ASSERT_EQUAL_UINT8(RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY, out_rt);
    TEST_ASSERT_TRUE(rcp_request_type_is_safety(out_rt));
    TEST_ASSERT_EQUAL_UINT8(255, out_step.sequencer_index);
    TEST_ASSERT_EQUAL_UINT8(200, out_step.start_state);
    TEST_ASSERT_EQUAL_UINT8(201, out_step.next_state);
    TEST_ASSERT_EQUAL_UINT16(65535, out_step.exec_delay);
    TEST_ASSERT_EQUAL_UINT16(RCP_COMPOUND_REPEAT_INFINITE, out_step.repeat_count);
    TEST_ASSERT_EQUAL_UINT32(0, out_payload_len);

    rcp_bytes_free(&frame);
}

static void test_encode_request_rejects_unrecognized_request_type(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE, 0,
                                                      &step, 0u, 0, NULL, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_decode_request_rejects_short_frame(void)
{
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    rcp_compound_step_t out_step = {0};
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;
    uint8_t buf[4] = {0};

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_SHORT_FRAME,
                           rcp_compound_decode_request(buf, sizeof(buf), &out_rt, &out_bus, &out_step,
                                                        &out_evt, &out_payload, &out_payload_len,
                                                        &out_tn));
}

static void test_decode_request_rejects_bad_msg_type(void)
{
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    rcp_compound_step_t out_step = {0};
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;
    uint8_t buf[RCP_ACF_GBB_HEADER_LEN];

    memset(buf, 0, sizeof(buf));
    buf[0] = RCP_ACF_MSG_TYPE_ABB;

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_BAD_MSG_TYPE,
                           rcp_compound_decode_request(buf, sizeof(buf), &out_rt, &out_bus, &out_step,
                                                        &out_evt, &out_payload, &out_payload_len,
                                                        &out_tn));
}

static void test_decode_request_rejects_non_repurposed_timestamp(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    rcp_compound_step_t out_step = {0};
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 0, &step, 0u, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    /* Flip mtv to VALID(1) directly on the wire (octet 2 bit 5, Table 4),
     * simulating a genuinely timed ACF_GBB message that happens to carry
     * a request_type-shaped byte pattern in message_timestamp -- must not
     * be misread as a compound request. */
    frame.data[2] = (uint8_t)(RCP_ACF_MTV_VALID << 5);

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_NOT_REPURPOSED,
                           rcp_compound_decode_request(frame.data, frame.len, &out_rt, &out_bus,
                                                        &out_step, &out_evt, &out_payload,
                                                        &out_payload_len, &out_tn));

    rcp_bytes_free(&frame);
}

static void test_decode_request_rejects_unknown_request_type(void)
{
    rcp_bytes_t frame;
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    rcp_compound_step_t out_step = {0};
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;

    /* clear-non-safestate is a request_type this decoder does not accept
     * (it belongs to rcp_compound_decode_clear_non_safestate() instead). */
    frame = rcp_compound_encode_clear_non_safestate(0, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_UNKNOWN_TYPE,
                           rcp_compound_decode_request(frame.data, frame.len, &out_rt, &out_bus,
                                                        &out_step, &out_evt, &out_payload,
                                                        &out_payload_len, &out_tn));

    rcp_bytes_free(&frame);
}

/* ── clear-non-safestate (0x06) ────────────────────────────────────────────── */

static void test_clear_non_safestate_round_trip(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t out_bus = 0;
    uint8_t out_tn = 0;

    frame = rcp_compound_encode_clear_non_safestate(11, 22);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT32(RCP_ACF_GBB_HEADER_LEN, frame.len);

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_OK,
                           rcp_compound_decode_clear_non_safestate(frame.data, frame.len, &out_bus,
                                                                    &out_tn));
    TEST_ASSERT_EQUAL_UINT8(11, out_bus);
    TEST_ASSERT_EQUAL_UINT8(22, out_tn);

    rcp_bytes_free(&frame);
}

static void test_clear_non_safestate_decode_rejects_compound_request(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;
    rcp_byte_bus_id_t out_bus = 0;
    uint8_t out_tn = 0;

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 0, &step, 0u, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_ERR_UNKNOWN_TYPE,
                           rcp_compound_decode_clear_non_safestate(frame.data, frame.len, &out_bus,
                                                                    &out_tn));

    rcp_bytes_free(&frame);
}

/* ── The advance guard, delay timer, and tick ─────────────────────────────── */

static void test_advance_guard_true_when_in_start_state(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};

    step.sequencer_index = 0;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state       = 2;

    TEST_ASSERT_TRUE(rcp_compound_advance_guard(&table, &step));

    rcp_sequencer_table_free(&table);
}

static void test_advance_guard_false_when_not_in_start_state(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 9));

    step.sequencer_index = 0;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state       = 2;

    TEST_ASSERT_FALSE(rcp_compound_advance_guard(&table, &step));

    rcp_sequencer_table_free(&table);
}

static void test_advance_guard_false_for_invalid_sequencer_index(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};

    step.sequencer_index = 99;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;

    TEST_ASSERT_FALSE(rcp_compound_advance_guard(&table, &step));

    rcp_sequencer_table_free(&table);
}

static void test_exec_delay_elapsed(void)
{
    rcp_compound_step_t step = {0};
    step.exec_delay = 1000;

    TEST_ASSERT_FALSE(rcp_compound_exec_delay_elapsed(&step, 999));
    TEST_ASSERT_TRUE(rcp_compound_exec_delay_elapsed(&step, 1000));
    TEST_ASSERT_TRUE(rcp_compound_exec_delay_elapsed(&step, 1001));
}

static void test_compound_tick_advances_only_after_delay_and_guard_both_hold(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};
    uint8_t got = 0;

    step.sequencer_index = 1;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state       = 5;
    step.exec_delay    = 1000;

    /* Not yet elapsed: no advance. */
    TEST_ASSERT_FALSE(rcp_compound_tick(&table, &step, 500));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 1, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);

    /* Elapsed and guard holds: advances. */
    TEST_ASSERT_TRUE(rcp_compound_tick(&table, &step, 1000));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 1, &got));
    TEST_ASSERT_EQUAL_UINT8(5, got);

    rcp_sequencer_table_free(&table);
}

static void test_compound_tick_guard_blocks_advance_even_after_delay_elapses(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};
    uint8_t got = 0;

    step.sequencer_index = 0;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state       = 5;
    step.exec_delay    = 100;

    /* Some other request already moved this sequencer away from
     * start_state before the timer elapsed. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 42));

    TEST_ASSERT_FALSE(rcp_compound_tick(&table, &step, 100));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(42, got);

    rcp_sequencer_table_free(&table);
}

static void test_compound_wait_tick_advances_only_on_condition_met_and_guard(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};
    uint8_t got = 0;

    step.sequencer_index = 2;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state       = 9;
    step.exec_delay    = 5000; /* deliberately irrelevant to this tick */

    /* No match: never advances, however long exec_delay is. */
    TEST_ASSERT_FALSE(rcp_compound_wait_tick(&table, &step, false));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 2, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);

    /* Match, guard holds: advances immediately, independent of elapsed time. */
    TEST_ASSERT_TRUE(rcp_compound_wait_tick(&table, &step, true));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 2, &got));
    TEST_ASSERT_EQUAL_UINT8(9, got);

    rcp_sequencer_table_free(&table);
}

static void test_compound_wait_tick_guard_blocks_advance_even_on_match(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};
    uint8_t got = 0;

    step.sequencer_index = 0;
    step.start_state     = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state       = 9;

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 3));

    TEST_ASSERT_FALSE(rcp_compound_wait_tick(&table, &step, true));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(3, got);

    rcp_sequencer_table_free(&table);
}

/* ── Literal wire layout ──────────────────────────────────────────────────────
 *
 * These assertions are written from the TC18 v0.5.1_RC specification's own
 * compound-request figure and field table (§11.2.2.1, Figure 8 / Table 6)
 * and compound-wait's (§11.2.2.2, Figure 9 / Table 7), NOT copied back out
 * of this encoder's output. Both figures lay the repurposed
 * message_timestamp region out as six sub-fields in this order:
 *
 *   offset 0     request_type   (one octet)
 *   offset 1     start_state    (one octet)
 *   offset 2     next_state     (one octet)
 *   offset 3     sequencer      (one octet)
 *   offsets 4..5 exec_delay     (two octets, big-endian)
 *   offsets 6..7 repetitions    (two octets, big-endian)
 *
 * The whole point of asserting literal octets here is that a pure
 * encode/decode round-trip cannot detect a self-consistently *wrong*
 * layout: every round-trip test in this file passed against the
 * pre-v0.102.0 encoder, which packed sequencer as 16 bits at offset 1 and
 * repetitions as 8 bits at offset 7. */

/* Offset of the repurposed message_timestamp region within an ACF_GBB
 * message: it directly follows the 8-octet shared byte_message_info
 * header. */
#define TS_OFF RCP_ACF_ABB_HEADER_LEN

static void test_compound_wire_sub_field_offsets(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;

    step.start_state     = 0x11;
    step.next_state       = 0x22;
    step.sequencer_index  = 0x33;
    step.exec_delay       = 0x4455;
    step.repeat_count     = 0x6677;

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 7, &step, 0u, 1, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len >= TS_OFF + 8u);

    TEST_ASSERT_EQUAL_HEX8(0x0F, frame.data[TS_OFF + 0]); /* request_type = compound */
    TEST_ASSERT_EQUAL_HEX8(0x11, frame.data[TS_OFF + 1]); /* cmp_start_state */
    TEST_ASSERT_EQUAL_HEX8(0x22, frame.data[TS_OFF + 2]); /* cmp_next_state */
    TEST_ASSERT_EQUAL_HEX8(0x33, frame.data[TS_OFF + 3]); /* cmp_sequencer */
    TEST_ASSERT_EQUAL_HEX8(0x44, frame.data[TS_OFF + 4]); /* cmp_exec_delay hi */
    TEST_ASSERT_EQUAL_HEX8(0x55, frame.data[TS_OFF + 5]); /* cmp_exec_delay lo */
    TEST_ASSERT_EQUAL_HEX8(0x66, frame.data[TS_OFF + 6]); /* cmp_repetitions hi */
    TEST_ASSERT_EQUAL_HEX8(0x77, frame.data[TS_OFF + 7]); /* cmp_repetitions lo */

    rcp_bytes_free(&frame);
}

static void test_compound_wait_safety_wire_sub_field_offsets(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;

    step.start_state     = 0xA1;
    step.next_state       = 0xB2;
    step.sequencer_index  = 0xC3;
    step.exec_delay       = 0x0001;
    step.repeat_count     = 0x0002;

    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT_SAFETY, 3, &step, 0u, 1,
                                         NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_HEX8(0x8B, frame.data[TS_OFF + 0]); /* compound wait, safety-tagged */
    TEST_ASSERT_EQUAL_HEX8(0xA1, frame.data[TS_OFF + 1]);
    TEST_ASSERT_EQUAL_HEX8(0xB2, frame.data[TS_OFF + 2]);
    TEST_ASSERT_EQUAL_HEX8(0xC3, frame.data[TS_OFF + 3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame.data[TS_OFF + 5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 6]);
    TEST_ASSERT_EQUAL_HEX8(0x02, frame.data[TS_OFF + 7]);

    rcp_bytes_free(&frame);
}

/* evt is the ACF header's own byte_message_info.evt field (Table 4 octet
 * 4 bits 7:4), NOT part of the repurposed message_timestamp sub-fields --
 * a compound-wait's §13.5.1 comparison mode must round-trip through it
 * independently of every rcp_compound_step_t sub-field. */
//cfusa:test REQ-CMP-026
//cfusa:test REQ-CMP-027
static void test_evt_round_trips_independently_of_step_subfields(void)
{
    rcp_compound_step_t step = {0};
    rcp_compound_step_t out_step = {0};
    rcp_bytes_t frame;
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;

    step.sequencer_index = 3;
    step.start_state     = 1;
    step.next_state       = 2;

    /* evt[2:0] = 110b, the last-two-bytes >= comparison mode -- a value
     * that would be silently lost (encoded as 0) before this fix. */
    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 4, &step, 0x6u, 8,
                                         NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    /* evt lives at octet 4 bits 7:4 of the shared byte_message_info header
     * (Table 4), independent of the repurposed message_timestamp region
     * this module's own sub-fields live in. */
    TEST_ASSERT_EQUAL_HEX8(0x60, (uint8_t)(frame.data[4] & 0xF0u));

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_OK,
                           rcp_compound_decode_request(frame.data, frame.len, &out_rt, &out_bus,
                                                        &out_step, &out_evt, &out_payload,
                                                        &out_payload_len, &out_tn));
    TEST_ASSERT_EQUAL_HEX8(0x6u, out_evt);
    TEST_ASSERT_EQUAL_UINT8(3, out_step.sequencer_index); /* untouched by evt */

    rcp_bytes_free(&frame);
}

/* Table 6/Table 7: "If set to 0xFFFF it indicates infinite repetition" --
 * a two-octet all-ones value, not a one-octet 0xFF. */
static void test_repeat_infinite_sentinel_is_two_octets_of_ones(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;

    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, RCP_COMPOUND_REPEAT_INFINITE);

    step.repeat_count = RCP_COMPOUND_REPEAT_INFINITE;
    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 0, &step, 0u, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_HEX8(0xFF, frame.data[TS_OFF + 6]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame.data[TS_OFF + 7]);
    /* 0x00FF -- an 8-bit 0xFF in the low octet only -- must NOT be read as
     * the infinite sentinel. */
    TEST_ASSERT_NOT_EQUAL_UINT16(0x00FFu, RCP_COMPOUND_REPEAT_INFINITE);

    rcp_bytes_free(&frame);
}

/* Decoding a spec-shaped octet sequence built by hand, so decode is
 * verified against the specification independently of encode. */
static void test_decode_reads_hand_built_spec_layout(void)
{
    rcp_compound_step_t step = {0};
    rcp_bytes_t frame;
    rcp_compound_step_t out_step = {0};
    uint8_t out_rt = 0;
    rcp_byte_bus_id_t out_bus = 0;
    const uint8_t *out_payload = NULL;
    size_t out_payload_len = 0;
    uint8_t out_tn = 0;
    uint8_t out_evt = 0;

    /* Build a valid ACF_GBB shell via the encoder, then overwrite the
     * eight sub-field octets by hand with the spec-derived pattern. */
    frame = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 5, &step, 0u, 9, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    frame.data[TS_OFF + 0] = 0x0F;
    frame.data[TS_OFF + 1] = 0x07; /* start_state */
    frame.data[TS_OFF + 2] = 0x08; /* next_state */
    frame.data[TS_OFF + 3] = 0x09; /* sequencer */
    frame.data[TS_OFF + 4] = 0x01; /* exec_delay = 0x0102 */
    frame.data[TS_OFF + 5] = 0x02;
    frame.data[TS_OFF + 6] = 0x03; /* repetitions = 0x0304 */
    frame.data[TS_OFF + 7] = 0x04;

    TEST_ASSERT_EQUAL_INT(RCP_COMPOUND_OK,
                           rcp_compound_decode_request(frame.data, frame.len, &out_rt, &out_bus,
                                                        &out_step, &out_evt, &out_payload,
                                                        &out_payload_len, &out_tn));

    TEST_ASSERT_EQUAL_UINT8(0x07, out_step.start_state);
    TEST_ASSERT_EQUAL_UINT8(0x08, out_step.next_state);
    TEST_ASSERT_EQUAL_UINT8(0x09, out_step.sequencer_index);
    TEST_ASSERT_EQUAL_UINT16(0x0102, out_step.exec_delay);
    TEST_ASSERT_EQUAL_UINT16(0x0304, out_step.repeat_count);

    rcp_bytes_free(&frame);
}

/* ── start_state == 0: "execute in any state" (§11.2.2.1) ─────────────────── */

static void test_start_condition_zero_start_state_matches_any_state(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};

    step.sequencer_index = 1;
    step.start_state     = 0; /* any state */

    TEST_ASSERT_TRUE(rcp_compound_start_condition_met(&table, &step));

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 1, 200));
    TEST_ASSERT_TRUE(rcp_compound_start_condition_met(&table, &step));

    /* The advance guard is a different question, and still requires the
     * sequencer to actually be sitting in start_state. */
    TEST_ASSERT_FALSE(rcp_compound_advance_guard(&table, &step));

    rcp_sequencer_table_free(&table);
}

static void test_start_condition_nonzero_start_state_requires_exact_match(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_compound_step_t step = {0};

    step.sequencer_index = 0;
    step.start_state     = 7;

    TEST_ASSERT_FALSE(rcp_compound_start_condition_met(&table, &step));
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 7));
    TEST_ASSERT_TRUE(rcp_compound_start_condition_met(&table, &step));

    rcp_sequencer_table_free(&table);
}

/* A sequencer this server does not have models a disabled sequencer,
 * which prohibits execution -- including for start_state == 0. */
static void test_start_condition_false_for_unknown_sequencer(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);
    rcp_compound_step_t step = {0};

    step.sequencer_index = 200;
    step.start_state     = 0;

    TEST_ASSERT_FALSE(rcp_compound_start_condition_met(&table, &step));

    rcp_sequencer_table_free(&table);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_is_safety);
    RUN_TEST(test_is_compound);
    RUN_TEST(test_is_compound_wait);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_peek_request_type_reads_compound_opcode);
    RUN_TEST(test_peek_request_type_short_frame);
    RUN_TEST(test_peek_request_type_bad_msg_type);
    RUN_TEST(test_peek_request_type_not_repurposed_when_mtv_nonzero);

    RUN_TEST(test_compound_request_round_trip);
    RUN_TEST(test_compound_wait_safety_request_round_trip);
    RUN_TEST(test_encode_request_rejects_unrecognized_request_type);
    RUN_TEST(test_decode_request_rejects_short_frame);
    RUN_TEST(test_decode_request_rejects_bad_msg_type);
    RUN_TEST(test_decode_request_rejects_non_repurposed_timestamp);
    RUN_TEST(test_decode_request_rejects_unknown_request_type);

    RUN_TEST(test_clear_non_safestate_round_trip);
    RUN_TEST(test_clear_non_safestate_decode_rejects_compound_request);

    RUN_TEST(test_advance_guard_true_when_in_start_state);
    RUN_TEST(test_advance_guard_false_when_not_in_start_state);
    RUN_TEST(test_advance_guard_false_for_invalid_sequencer_index);

    RUN_TEST(test_exec_delay_elapsed);

    RUN_TEST(test_compound_tick_advances_only_after_delay_and_guard_both_hold);
    RUN_TEST(test_compound_tick_guard_blocks_advance_even_after_delay_elapses);
    RUN_TEST(test_compound_wait_tick_advances_only_on_condition_met_and_guard);
    RUN_TEST(test_compound_wait_tick_guard_blocks_advance_even_on_match);

    RUN_TEST(test_compound_wire_sub_field_offsets);
    RUN_TEST(test_compound_wait_safety_wire_sub_field_offsets);
    RUN_TEST(test_evt_round_trips_independently_of_step_subfields);
    RUN_TEST(test_repeat_infinite_sentinel_is_two_octets_of_ones);
    RUN_TEST(test_decode_reads_hand_built_spec_layout);

    RUN_TEST(test_start_condition_zero_start_state_matches_any_state);
    RUN_TEST(test_start_condition_nonzero_start_state_requires_exact_match);
    RUN_TEST(test_start_condition_false_for_unknown_sequencer);

    return UNITY_END();
}
