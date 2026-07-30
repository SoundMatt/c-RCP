//cfusa:test REQ-E2E-001
//cfusa:test REQ-E2E-002
//cfusa:test REQ-E2E-003
//cfusa:test REQ-E2E-004
//cfusa:test REQ-E2E-005
//cfusa:test REQ-E2E-006
//cfusa:test REQ-E2E-007
//cfusa:test REQ-E2E-008
//cfusa:test REQ-E2E-009
//cfusa:test REQ-E2E-010
//cfusa:test REQ-E2E-011
//cfusa:test REQ-E2E-012
//cfusa:test REQ-E2E-013
//cfusa:test REQ-E2E-014
//cfusa:test REQ-E2E-015
//cfusa:test REQ-E2E-016
//cfusa:test REQ-E2E-017
//cfusa:test REQ-E2E-018
//cfusa:test REQ-E2E-019
//cfusa:test REQ-E2E-020
//cfusa:test REQ-E2E-021
//cfusa:test REQ-E2E-022
//cfusa:test REQ-E2E-023
//cfusa:test REQ-E2E-024
//cfusa:test REQ-E2E-025
//cfusa:test REQ-E2E-026
//cfusa:test REQ-E2E-027
//cfusa:test REQ-WIREERR-003
#include "unity.h"

#include <rcp/e2e.h>
#include <rcp/request_sequencer.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    const char *a   = rcp_e2e_strerror(RCP_E2E_OK);
    const char *b   = rcp_e2e_strerror(RCP_E2E_ERR_SHORT_FRAME);
    const char *c   = rcp_e2e_strerror(RCP_E2E_ERR_CRC_MISMATCH);
    const char *unk = rcp_e2e_strerror((rcp_e2e_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
}

/* ── wire error code mapping (c-RCP-04) ───────────────────────────────────── */

static void test_wire_error_crc_mismatch_maps_to_poci_failure(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_POCI_FAILURE,
                           rcp_e2e_wire_error(RCP_E2E_ERR_CRC_MISMATCH));
}

static void test_wire_error_ok_and_short_frame_map_to_none(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_NONE, rcp_e2e_wire_error(RCP_E2E_OK));
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_NONE, rcp_e2e_wire_error(RCP_E2E_ERR_SHORT_FRAME));
}

/* ── CRC32 known-answer test ───────────────────────────────────────────────── */

static void test_crc32_known_answer_vector(void)
{
    /* CRC-32/AUTOSAR's published check value -- this module's parameter
     * set (poly 0xF4ACFB13, init/xorout 0xFFFFFFFF, refin/refout true)
     * happens to coincide with that catalog entry exactly. */
    const uint8_t vec[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0x1697D06Au, rcp_e2e_crc32(vec, 9));
}

static void test_crc32_empty_input(void)
{
    /* CRC of nothing is init XORed with final XOR, i.e. 0. */
    TEST_ASSERT_EQUAL_HEX32(0u, rcp_e2e_crc32(NULL, 0));
}

static void test_crc32_differs_for_different_data(void)
{
    const uint8_t a[] = {0x01, 0x02, 0x03};
    const uint8_t b[] = {0x01, 0x02, 0x04};
    TEST_ASSERT_NOT_EQUAL(rcp_e2e_crc32(a, sizeof(a)), rcp_e2e_crc32(b, sizeof(b)));
}

/* ── compute_crc coverage span ────────────────────────────────────────────── */

static void test_compute_crc_matches_manual_concatenation(void)
{
    uint8_t concat[8 + 4 + 5];
    uint8_t acf_frame[5] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    uint64_t stream_id       = 0x0102030405060708ULL;
    uint32_t avtp_timestamp  = 0x11223344u;
    size_t i;

    for (i = 0; i < 8; i++) concat[i]     = (uint8_t)(stream_id >> (56 - 8 * i));
    for (i = 0; i < 4; i++) concat[8 + i] = (uint8_t)(avtp_timestamp >> (24 - 8 * i));
    memcpy(concat + 12, acf_frame, sizeof(acf_frame));

    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_crc32(concat, sizeof(concat)),
                             rcp_e2e_compute_crc(stream_id, avtp_timestamp, acf_frame,
                                                     sizeof(acf_frame)));
}

static void test_compute_crc_zero_stream_and_timestamp_ntscf_standin(void)
{
    /* The all-zero StreamID/avtp_timestamp stand-in an NTSCF-framed
     * message uses should compute identically to an explicit
     * zero-filled 12-byte prefix (8-byte StreamID + 4-byte
     * avtp_timestamp). */
    uint8_t acf_frame[3] = {0x01, 0x02, 0x03};
    uint8_t zero_prefix[12] = {0};
    uint8_t concat[12 + 3];

    memcpy(concat, zero_prefix, 12);
    memcpy(concat + 12, acf_frame, 3);

    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_crc32(concat, sizeof(concat)),
                             rcp_e2e_compute_crc(0, 0, acf_frame, 3));
}

static void test_compute_crc_timestamp_is_4_octets_not_8(void)
{
    /* c-RCP-01: the CRC must fold avtp_timestamp in as exactly 4
     * octets (its real IEEE 1722 wire width), not 8. A timestamp value
     * with any of its high 32 bits set is meaningless for a uint32_t
     * parameter and can't be constructed here, but this pins the
     * *width actually fed to the CRC*: the result must match a manual
     * concatenation using a 4-byte big-endian timestamp, and must NOT
     * match one using an 8-byte big-endian encoding of the same value
     * (the pre-fix behavior). */
    uint8_t acf_frame[2] = {0x55, 0x66};
    uint32_t avtp_timestamp = 0xCAFEBABEu;
    uint8_t concat4[8 + 4 + 2];
    uint8_t concat8[8 + 8 + 2];
    size_t i;

    memset(concat4, 0, sizeof(concat4));
    memset(concat8, 0, sizeof(concat8));
    for (i = 0; i < 4; i++) concat4[8 + i] = (uint8_t)(avtp_timestamp >> (24 - 8 * i));
    memcpy(concat4 + 12, acf_frame, sizeof(acf_frame));

    for (i = 0; i < 4; i++) concat8[8 + 4 + i] = (uint8_t)(avtp_timestamp >> (24 - 8 * i));
    memcpy(concat8 + 16, acf_frame, sizeof(acf_frame));

    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_crc32(concat4, sizeof(concat4)),
                             rcp_e2e_compute_crc(0, avtp_timestamp, acf_frame, sizeof(acf_frame)));
    TEST_ASSERT_NOT_EQUAL(rcp_e2e_crc32(concat8, sizeof(concat8)),
                           rcp_e2e_compute_crc(0, avtp_timestamp, acf_frame, sizeof(acf_frame)));
}

/* ── length_with_crc ───────────────────────────────────────────────────────── */

static void test_length_with_crc_adds_trailer_size(void)
{
    TEST_ASSERT_EQUAL_UINT((size_t)0 + RCP_E2E_CRC_LEN, rcp_e2e_length_with_crc(0));
    TEST_ASSERT_EQUAL_UINT((size_t)100 + RCP_E2E_CRC_LEN, rcp_e2e_length_with_crc(100));
}

static void test_length_with_crc_saturates(void)
{
    TEST_ASSERT_EQUAL_UINT((size_t)-1, rcp_e2e_length_with_crc((size_t)-1));
    TEST_ASSERT_EQUAL_UINT((size_t)-1, rcp_e2e_length_with_crc((size_t)-1 - 1));
}

/* ── wrap / unwrap round trip ──────────────────────────────────────────────── */

/* A minimal, syntactically-plausible ACF_ABB header-and-payload region:
 * byte 0 = acf_msg_type (arbitrary here, this module doesn't inspect
 * it), bytes 1-2 = acf_msg_length (the field rcp_e2e_wrap()/_unwrap()
 * adapt), followed by whatever the rest of the header-and-payload
 * happens to be. acf_msg_length is set to payload_len (5, matching
 * acf.c's own encode_abb() convention of payload-only, header
 * excluded) so the adaptation assertions below have a real value to
 * check against.
 */
static void make_test_acf_frame(uint8_t *out, size_t out_len)
{
    size_t i;
    TEST_ASSERT_TRUE(out_len >= 3);
    out[0] = 0x0Eu; /* RCP_ACF_MSG_TYPE_ABB, this module doesn't care */
    out[1] = 0x00u;
    out[2] = (uint8_t)(out_len - 8u); /* pretend header is 8 bytes; payload = rest */
    for (i = 3; i < out_len; i++) out[i] = (uint8_t)(i & 0xFFu);
}

static void test_wrap_appends_crc_len_bytes(void)
{
    uint8_t acf_frame[14];
    rcp_bytes_t out;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    out = rcp_e2e_wrap(42, 7, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(out.data);
    TEST_ASSERT_EQUAL_UINT(sizeof(acf_frame) + RCP_E2E_CRC_LEN, out.len);

    rcp_bytes_free(&out);
}

static void test_wrap_null_frame_nonzero_len_fails_safe(void)
{
    rcp_bytes_t out = rcp_e2e_wrap(1, 1, NULL, 4);
    TEST_ASSERT_NULL(out.data);
    TEST_ASSERT_EQUAL_UINT(0, out.len);
}

static void test_wrap_too_short_for_length_field_fails_safe(void)
{
    /* c-RCP-02: the adaptation needs at least 3 octets (offset 1-2) to
     * write into. A 0/1/2-byte "frame" has nowhere for acf_msg_length
     * to live, so wrap must fail safe rather than silently skip the
     * adaptation and produce a non-conformant frame. */
    uint8_t tiny[2] = {0xAA, 0xBB};
    rcp_bytes_t out = rcp_e2e_wrap(1, 1, tiny, sizeof(tiny));
    TEST_ASSERT_NULL(out.data);
    TEST_ASSERT_EQUAL_UINT(0, out.len);
}

/* c-RCP-02: wrap() must adapt acf_msg_length by +1 quadlet before
 * appending the trailer, and unwrap() must adapt it back down -- this
 * is the actual defect this test targets, distinct from the
 * "round-trips at all" check below. */
static void test_wrap_adapts_acf_msg_length_by_one_quadlet(void)
{
    uint8_t acf_frame[14];
    uint16_t original_len;
    uint16_t adapted_len;
    rcp_bytes_t wrapped;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));
    original_len = (uint16_t)(((uint16_t)acf_frame[1] << 8) | acf_frame[2]);

    wrapped = rcp_e2e_wrap(1, 1, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    adapted_len = (uint16_t)(((uint16_t)wrapped.data[1] << 8) | wrapped.data[2]);
    TEST_ASSERT_EQUAL_UINT16(original_len + RCP_E2E_CRC_LEN, adapted_len);

    /* The caller's original frame must be untouched -- wrap() adapts a
     * copy, never the input. */
    TEST_ASSERT_EQUAL_UINT16(original_len,
                              (uint16_t)(((uint16_t)acf_frame[1] << 8) | acf_frame[2]));

    rcp_bytes_free(&wrapped);
}

static void test_wrap_unwrap_round_trip_ok(void)
{
    uint8_t acf_frame[10];
    rcp_bytes_t wrapped;
    rcp_bytes_t body = {0};
    rcp_e2e_errc_t rc;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    wrapped = rcp_e2e_wrap(0xDEADBEEFu, 0xCAFEu, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    rc = rcp_e2e_unwrap(0xDEADBEEFu, 0xCAFEu, wrapped.data, wrapped.len, &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK, rc);
    TEST_ASSERT_EQUAL_UINT(sizeof(acf_frame), body.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(acf_frame, body.data, body.len);

    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&body);
}

static void test_unwrap_short_frame(void)
{
    uint8_t buf[3] = {0};
    rcp_bytes_t body = {0};

    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_SHORT_FRAME,
                           rcp_e2e_unwrap(0, 0, buf, sizeof(buf), &body));
    TEST_ASSERT_NULL(body.data);
}

static void test_unwrap_crc_mismatch_on_corruption(void)
{
    uint8_t acf_frame[8];
    rcp_bytes_t wrapped;
    rcp_bytes_t body = {0};
    rcp_e2e_errc_t rc;

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    wrapped = rcp_e2e_wrap(5, 5, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    wrapped.data[3] ^= 0xFFu; /* corrupt the payload, not the header/trailer */

    rc = rcp_e2e_unwrap(5, 5, wrapped.data, wrapped.len, &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH, rc);

    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&body);
}

static void test_unwrap_crc_mismatch_on_wrong_stream_id(void)
{
    uint8_t acf_frame[8];
    rcp_bytes_t wrapped;
    rcp_bytes_t body = {0};

    make_test_acf_frame(acf_frame, sizeof(acf_frame));

    wrapped = rcp_e2e_wrap(1, 1, acf_frame, sizeof(acf_frame));
    TEST_ASSERT_NOT_NULL(wrapped.data);

    /* Same bytes, but unwrapped against the wrong stream_id -- the
     * coverage span includes stream_id, so this must fail too. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                           rcp_e2e_unwrap(2, 1, wrapped.data, wrapped.len, &body));

    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&body);
}

/* ── fragmentation/CRC interaction rule ───────────────────────────────────── */

static void test_fragment_carries_crc_only_when_last(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_fragment_carries_crc(true));
    TEST_ASSERT_FALSE(rcp_e2e_fragment_carries_crc(false));
}

/* ── safety-tagged request classification ─────────────────────────────────── */

static void test_is_safety_request(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_is_safety_request(0x8Fu));  /* compound, safety */
    TEST_ASSERT_TRUE(rcp_e2e_is_safety_request(0x8Bu));  /* compound-wait, safety */
    TEST_ASSERT_TRUE(rcp_e2e_is_safety_request(0x8Eu));  /* triggered, safety */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x0Fu)); /* compound */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x0Bu)); /* compound-wait */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x0Eu)); /* triggered */
    TEST_ASSERT_FALSE(rcp_e2e_is_safety_request(0x00u)); /* standard */
}

static void test_request_may_execute_safety_gated_on_safe_state(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x8Fu, true));
    TEST_ASSERT_FALSE(rcp_e2e_request_may_execute(0x8Fu, false));
}

static void test_request_may_execute_non_safety_always_permitted(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x0Fu, true));
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x0Fu, false));
}

/* ── watchdog-purge-vs-safety-survive ─────────────────────────────────────── */

static void test_watchdog_purge_should_keep_only_safety_tagged(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_watchdog_purge_should_keep(0x8Fu));
    TEST_ASSERT_TRUE(rcp_e2e_watchdog_purge_should_keep(0x8Bu));
    TEST_ASSERT_TRUE(rcp_e2e_watchdog_purge_should_keep(0x8Eu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x0Fu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x0Bu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x0Eu));
    TEST_ASSERT_FALSE(rcp_e2e_watchdog_purge_should_keep(0x00u));
}

static void test_watchdog_purge_classify_matches_per_entry_rule(void)
{
    const uint8_t types[6] = {0x00u, 0x8Fu, 0x0Bu, 0x8Bu, 0x0Eu, 0x8Eu};
    bool keep[6] = {false};

    rcp_e2e_watchdog_purge_classify(types, 6, keep);

    TEST_ASSERT_FALSE(keep[0]);
    TEST_ASSERT_TRUE(keep[1]);
    TEST_ASSERT_FALSE(keep[2]);
    TEST_ASSERT_TRUE(keep[3]);
    TEST_ASSERT_FALSE(keep[4]);
    TEST_ASSERT_TRUE(keep[5]);
}

static void test_watchdog_purge_classify_zero_count_is_noop(void)
{
    rcp_e2e_watchdog_purge_classify(NULL, 0, NULL);
    /* No crash, nothing to assert -- this is the "may be NULL" contract. */
}

/* ── configured safe state ─────────────────────────────────────────────────── */

static void test_measure_valid(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_measure_valid((uint8_t)RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE));
    TEST_ASSERT_TRUE(rcp_e2e_measure_valid((uint8_t)RCP_E2E_MEASURE_SEQUENCER));
    TEST_ASSERT_FALSE(rcp_e2e_measure_valid(2));
    TEST_ASSERT_FALSE(rcp_e2e_measure_valid(0xFFu));
}

static void test_endpoint_in_safe_state_force_high_impedance_always_true(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE, NULL, 0, 0));
}

static void test_endpoint_in_safe_state_sequencer_matches_target_state(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    TEST_ASSERT_EQUAL_UINT16(4, table.count);

    /* Powers on to RCP_SEQUENCER_POWER_ON_STATE (1), not yet "5". */
    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 2, 5));

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 2, 5));

    TEST_ASSERT_TRUE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 2, 5));

    rcp_sequencer_table_free(&table);
}

static void test_endpoint_in_safe_state_fails_closed_on_bad_measure(void)
{
    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(0xFFu, NULL, 0, 0));
}

static void test_endpoint_in_safe_state_fails_closed_on_null_table(void)
{
    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, NULL, 0, 1));
}

static void test_endpoint_in_safe_state_fails_closed_on_invalid_index(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(2);

    TEST_ASSERT_FALSE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 99, 1));

    rcp_sequencer_table_free(&table);
}

/* ── rx_enforce_e2e: drop vs. latch ────────────────────────────────────────── */

static void test_crc_error_action_maps_rx_enforce_e2e(void)
{
    TEST_ASSERT_EQUAL_INT(RCP_E2E_CRC_ACTION_DROP_REQUEST, rcp_e2e_crc_error_action(false));
    TEST_ASSERT_EQUAL_INT(RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT, rcp_e2e_crc_error_action(true));
}

static void test_stream_fault_drop_request_never_latches(void)
{
    rcp_e2e_stream_fault_t f;
    rcp_e2e_stream_fault_init(&f);

    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, false));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));

    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, false));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));
}

static void test_stream_fault_latches_and_stays_latched_until_reset(void)
{
    rcp_e2e_stream_fault_t f;
    rcp_e2e_stream_fault_init(&f);
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));

    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_is_faulted(&f));

    /* Stays latched even across a later drop-mode call (state doesn't
     * un-latch just because a caller's config later says drop). */
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, false));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_is_faulted(&f));

    rcp_e2e_stream_fault_reset(&f);
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));
}

/* ── per-stream watchdog ───────────────────────────────────────────────────── */

static void test_wd_evaluate_disabled_never_overflows(void)
{
    rcp_e2e_wd_result_t r = rcp_e2e_wd_evaluate(false, 10, true, true, 1000000);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);
}

static void test_wd_evaluate_below_timeout_no_overflow(void)
{
    rcp_e2e_wd_result_t r = rcp_e2e_wd_evaluate(true, 100, true, true, 99);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);
}

static void test_wd_evaluate_at_and_beyond_timeout_overflows(void)
{
    rcp_e2e_wd_result_t r1 = rcp_e2e_wd_evaluate(true, 100, true, true, 100);
    rcp_e2e_wd_result_t r2 = rcp_e2e_wd_evaluate(true, 100, true, true, 500);
    TEST_ASSERT_TRUE(r1.overflowed);
    TEST_ASSERT_TRUE(r2.overflowed);
}

static void test_wd_evaluate_enter_safe_state_and_notify_are_independent(void)
{
    rcp_e2e_wd_result_t both = rcp_e2e_wd_evaluate(true, 10, true, true, 50);
    rcp_e2e_wd_result_t only_safestate = rcp_e2e_wd_evaluate(true, 10, true, false, 50);
    rcp_e2e_wd_result_t only_notify = rcp_e2e_wd_evaluate(true, 10, false, true, 50);
    rcp_e2e_wd_result_t neither = rcp_e2e_wd_evaluate(true, 10, false, false, 50);

    TEST_ASSERT_TRUE(both.enter_safe_state);
    TEST_ASSERT_TRUE(both.notify);

    TEST_ASSERT_TRUE(only_safestate.enter_safe_state);
    TEST_ASSERT_FALSE(only_safestate.notify);

    TEST_ASSERT_FALSE(only_notify.enter_safe_state);
    TEST_ASSERT_TRUE(only_notify.notify);

    TEST_ASSERT_FALSE(neither.enter_safe_state);
    TEST_ASSERT_FALSE(neither.notify);
}

/* ── The primary safety mechanism, end to end ─────────────────────────────── */

static void test_watchdog_overflow_drives_the_full_purge_and_survive_flow(void)
{
    /* This is the scenario the roadmap calls out explicitly: on watchdog
     * overflow, normal requests are purged while the safety variants
     * survive and become what actually drives the endpoint through its
     * configured safe state. */
    const uint8_t queued[4] = {0x0Fu /* compound */, 0x8Fu /* compound, safety */,
                                0x00u /* standard */,  0x8Eu /* triggered, safety */};
    bool keep[4] = {false};
    rcp_e2e_wd_result_t wd;
    rcp_sequencer_table_t table = rcp_sequencer_table_new(1);

    wd = rcp_e2e_wd_evaluate(true, 50, true, false, 200);
    TEST_ASSERT_TRUE(wd.overflowed);
    TEST_ASSERT_TRUE(wd.enter_safe_state);

    rcp_e2e_watchdog_purge_classify(queued, 4, keep);
    TEST_ASSERT_FALSE(keep[0]);
    TEST_ASSERT_TRUE(keep[1]);
    TEST_ASSERT_FALSE(keep[2]);
    TEST_ASSERT_TRUE(keep[3]);

    /* Before the endpoint reaches its configured safe state, the
     * survivors still may not execute... */
    TEST_ASSERT_FALSE(rcp_e2e_request_may_execute(0x8Fu, false));
    TEST_ASSERT_FALSE(rcp_e2e_request_may_execute(0x8Eu, false));

    /* ...once the sequencer-based safe state is actually reached, they
     * may. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 9));
    TEST_ASSERT_TRUE(rcp_e2e_endpoint_in_safe_state(
        (uint8_t)RCP_E2E_MEASURE_SEQUENCER, &table, 0, 9));
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x8Fu, true));
    TEST_ASSERT_TRUE(rcp_e2e_request_may_execute(0x8Eu, true));

    rcp_sequencer_table_free(&table);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_wire_error_crc_mismatch_maps_to_poci_failure);
    RUN_TEST(test_wire_error_ok_and_short_frame_map_to_none);

    RUN_TEST(test_crc32_known_answer_vector);
    RUN_TEST(test_crc32_empty_input);
    RUN_TEST(test_crc32_differs_for_different_data);

    RUN_TEST(test_compute_crc_matches_manual_concatenation);
    RUN_TEST(test_compute_crc_zero_stream_and_timestamp_ntscf_standin);
    RUN_TEST(test_compute_crc_timestamp_is_4_octets_not_8);

    RUN_TEST(test_length_with_crc_adds_trailer_size);
    RUN_TEST(test_length_with_crc_saturates);

    RUN_TEST(test_wrap_appends_crc_len_bytes);
    RUN_TEST(test_wrap_null_frame_nonzero_len_fails_safe);
    RUN_TEST(test_wrap_too_short_for_length_field_fails_safe);
    RUN_TEST(test_wrap_adapts_acf_msg_length_by_one_quadlet);
    RUN_TEST(test_wrap_unwrap_round_trip_ok);
    RUN_TEST(test_unwrap_short_frame);
    RUN_TEST(test_unwrap_crc_mismatch_on_corruption);
    RUN_TEST(test_unwrap_crc_mismatch_on_wrong_stream_id);

    RUN_TEST(test_fragment_carries_crc_only_when_last);

    RUN_TEST(test_is_safety_request);
    RUN_TEST(test_request_may_execute_safety_gated_on_safe_state);
    RUN_TEST(test_request_may_execute_non_safety_always_permitted);

    RUN_TEST(test_watchdog_purge_should_keep_only_safety_tagged);
    RUN_TEST(test_watchdog_purge_classify_matches_per_entry_rule);
    RUN_TEST(test_watchdog_purge_classify_zero_count_is_noop);

    RUN_TEST(test_measure_valid);
    RUN_TEST(test_endpoint_in_safe_state_force_high_impedance_always_true);
    RUN_TEST(test_endpoint_in_safe_state_sequencer_matches_target_state);
    RUN_TEST(test_endpoint_in_safe_state_fails_closed_on_bad_measure);
    RUN_TEST(test_endpoint_in_safe_state_fails_closed_on_null_table);
    RUN_TEST(test_endpoint_in_safe_state_fails_closed_on_invalid_index);

    RUN_TEST(test_crc_error_action_maps_rx_enforce_e2e);
    RUN_TEST(test_stream_fault_drop_request_never_latches);
    RUN_TEST(test_stream_fault_latches_and_stays_latched_until_reset);

    RUN_TEST(test_wd_evaluate_disabled_never_overflows);
    RUN_TEST(test_wd_evaluate_below_timeout_no_overflow);
    RUN_TEST(test_wd_evaluate_at_and_beyond_timeout_overflows);
    RUN_TEST(test_wd_evaluate_enter_safe_state_and_notify_are_independent);

    RUN_TEST(test_watchdog_overflow_drives_the_full_purge_and_survive_flow);

    return UNITY_END();
}
