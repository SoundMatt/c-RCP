/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-TRIG-001
//cfusa:test REQ-TRIG-002
//cfusa:test REQ-TRIG-003
//cfusa:test REQ-TRIG-004
//cfusa:test REQ-TRIG-005
//cfusa:test REQ-TRIG-006
//cfusa:test REQ-TRIG-007
//cfusa:test REQ-TRIG-008
//cfusa:test REQ-TRIG-009
//cfusa:test REQ-TRIG-010
//cfusa:test REQ-TRIG-011
//cfusa:test REQ-TRIG-012
//cfusa:test REQ-TRIG-013
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/request.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── request_type classification ──────────────────────────────────────────── */

static void test_is_triggered(void)
{
    TEST_ASSERT_TRUE(rcp_request_type_is_triggered(RCP_REQUEST_TYPE_TRIGGERED));
    TEST_ASSERT_TRUE(rcp_request_type_is_triggered(RCP_REQUEST_TYPE_TRIGGERED_SAFETY));
    TEST_ASSERT_FALSE(rcp_request_type_is_triggered(0x0Fu));
    TEST_ASSERT_FALSE(rcp_request_type_is_triggered(0x00u));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    const char *a = rcp_triggered_strerror(RCP_TRIGGERED_OK);
    const char *b = rcp_triggered_strerror(RCP_TRIGGERED_ERR_SHORT_FRAME);
    const char *c = rcp_triggered_strerror(RCP_TRIGGERED_ERR_BAD_MSG_TYPE);
    const char *d = rcp_triggered_strerror(RCP_TRIGGERED_ERR_NOT_REPURPOSED);
    const char *e = rcp_triggered_strerror(RCP_TRIGGERED_ERR_UNKNOWN_TYPE);
    const char *unk = rcp_triggered_strerror((rcp_triggered_errc_t)999);

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

/* ── encode/decode round trip ─────────────────────────────────────────────── */

static void test_triggered_request_round_trip(void)
{
    rcp_triggered_step_t step = {0};
    rcp_bytes_t frame;
    uint8_t rt = 0;
    rcp_byte_bus_id_t bbid = 0;
    rcp_triggered_step_t got_step = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;
    uint8_t body[3] = {0xAAu, 0xBBu, 0xCCu};

    step.trigger_source_ep = 2;
    step.trigger_signal_nr = 1;
    step.trigger_threshold = 5;
    step.exec_delay         = 1234;
    step.repeat_count       = 7;

    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, 9, &step, 42, body,
                                          sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_OK,
                           rcp_triggered_decode_request(frame.data, frame.len, &rt, &bbid,
                                                         &got_step, &payload, &payload_len, &txn));

    TEST_ASSERT_EQUAL_UINT8(RCP_REQUEST_TYPE_TRIGGERED, rt);
    TEST_ASSERT_EQUAL_UINT8(9, bbid);
    TEST_ASSERT_EQUAL_UINT8(2, got_step.trigger_source_ep);
    TEST_ASSERT_EQUAL_UINT8(1, got_step.trigger_signal_nr);
    TEST_ASSERT_EQUAL_UINT8(5, got_step.trigger_threshold);
    TEST_ASSERT_EQUAL_UINT16(1234, got_step.exec_delay);
    TEST_ASSERT_EQUAL_UINT16(7, got_step.repeat_count);
    TEST_ASSERT_EQUAL_UINT8(42, txn);
    TEST_ASSERT_EQUAL_size_t(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    rcp_bytes_free(&frame);
}

static void test_safety_request_round_trip(void)
{
    rcp_triggered_step_t step = {0};
    rcp_bytes_t frame;
    uint8_t rt = 0;
    rcp_byte_bus_id_t bbid = 0;
    rcp_triggered_step_t got_step = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    step.trigger_source_ep = 0;
    step.trigger_signal_nr = 3;
    step.repeat_count       = RCP_TRIGGERED_REPEAT_INFINITE;

    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED_SAFETY, 1, &step, 5, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_OK,
                           rcp_triggered_decode_request(frame.data, frame.len, &rt, &bbid,
                                                         &got_step, &payload, &payload_len, &txn));
    TEST_ASSERT_EQUAL_UINT8(RCP_REQUEST_TYPE_TRIGGERED_SAFETY, rt);
    TEST_ASSERT_EQUAL_UINT16(RCP_TRIGGERED_REPEAT_INFINITE, got_step.repeat_count);
    TEST_ASSERT_EQUAL_size_t(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_encode_rejects_unrecognized_request_type(void)
{
    rcp_triggered_step_t step = {0};
    rcp_bytes_t frame = rcp_triggered_encode_request(0x0Fu, 1, &step, 0, NULL, 0);
    TEST_ASSERT_NULL(frame.data);
}

static void test_decode_rejects_short_frame(void)
{
    uint8_t buf[4] = {0};
    uint8_t rt = 0;
    rcp_byte_bus_id_t bbid = 0;
    rcp_triggered_step_t step = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_ERR_SHORT_FRAME,
                           rcp_triggered_decode_request(buf, sizeof(buf), &rt, &bbid, &step,
                                                         &payload, &payload_len, &txn));
}

static void test_decode_rejects_bad_msg_type(void)
{
    uint8_t buf[16] = {0};
    uint8_t rt = 0;
    rcp_byte_bus_id_t bbid = 0;
    rcp_triggered_step_t step = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    buf[0] = RCP_ACF_MSG_TYPE_ABB;

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_ERR_BAD_MSG_TYPE,
                           rcp_triggered_decode_request(buf, sizeof(buf), &rt, &bbid, &step,
                                                         &payload, &payload_len, &txn));
}

static void test_decode_rejects_non_repurposed(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_acf_gbb_header_t ghdr = {0};
    rcp_bytes_t frame;
    uint8_t rt = 0;
    rcp_byte_bus_id_t bbid = 0;
    rcp_triggered_step_t step = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    hdr.mtv = RCP_ACF_MTV_VALID;
    ghdr.info = hdr;
    ghdr.message_timestamp = 123u;

    frame = rcp_acf_encode_gbb(&ghdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_ERR_NOT_REPURPOSED,
                           rcp_triggered_decode_request(frame.data, frame.len, &rt, &bbid, &step,
                                                         &payload, &payload_len, &txn));

    rcp_bytes_free(&frame);
}

static void test_decode_rejects_unknown_request_type(void)
{
    rcp_triggered_step_t step = {0};
    rcp_bytes_t frame;
    uint8_t rt = 0;
    rcp_byte_bus_id_t bbid = 0;
    rcp_triggered_step_t got_step = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    /* A compound request's own opcode is not a triggered request_type. */
    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, 1, &step, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    frame.data[RCP_ACF_ABB_HEADER_LEN] = 0x0Fu; /* overwrite opcode byte */

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_ERR_UNKNOWN_TYPE,
                           rcp_triggered_decode_request(frame.data, frame.len, &rt, &bbid,
                                                         &got_step, &payload, &payload_len, &txn));

    rcp_bytes_free(&frame);
}

/* ── The trigger-occurrence counter and fire tick ─────────────────────────── */

static void test_enter_started_resets_counter(void)
{
    rcp_triggered_runtime_t rt;

    rt.occurrence_count = 99;
    rt.started            = false;

    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_EQUAL_UINT32(0, rt.occurrence_count);
    TEST_ASSERT_TRUE(rt.started);
}

static void test_record_occurrence_requires_started(void)
{
    rcp_triggered_runtime_t rt = {0};
    rcp_triggered_step_t step = {0};

    step.trigger_source_ep = 4;
    step.trigger_signal_nr = 2;

    TEST_ASSERT_FALSE(rcp_triggered_runtime_record_occurrence(&rt, &step, 4, 2));
    TEST_ASSERT_EQUAL_UINT32(0, rt.occurrence_count);

    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 4, 2));
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 4, 2));
    TEST_ASSERT_EQUAL_UINT32(2, rt.occurrence_count);
}

/* Table 8: trigger_source_ep "defines the endpoint which issues the
 * trigger to be used for this request" and trigger_signal_nr "defines the
 * trigger number of the endpoint addressed by trigger_source_ep" -- so an
 * occurrence from a different endpoint, or a different signal number of
 * the same endpoint, is not this request's trigger and must not count. */
static void test_record_occurrence_only_counts_the_selected_trigger(void)
{
    rcp_triggered_runtime_t rt = {0};
    rcp_triggered_step_t step = {0};

    step.trigger_source_ep = 4;
    step.trigger_signal_nr = 2;
    rcp_triggered_runtime_enter_started(&rt);

    /* Right endpoint, wrong signal number. */
    TEST_ASSERT_FALSE(rcp_triggered_runtime_record_occurrence(&rt, &step, 4, 3));
    /* Wrong endpoint, right signal number. */
    TEST_ASSERT_FALSE(rcp_triggered_runtime_record_occurrence(&rt, &step, 5, 2));
    /* Neither. */
    TEST_ASSERT_FALSE(rcp_triggered_runtime_record_occurrence(&rt, &step, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, rt.occurrence_count);

    /* Both match. */
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 4, 2));
    TEST_ASSERT_EQUAL_UINT32(1, rt.occurrence_count);
}

/* Table 8: trigger_threshold "defines how many trigger signals shall
 * occur before the request is executed. e.g. '0' results in execution
 * after one trigger signal while a '3' will cause an execution after four
 * occurrences." */
static void test_threshold_counts_occurrences_before_execution(void)
{
    rcp_triggered_runtime_t rt = {0};
    rcp_triggered_step_t step = {0};
    int i;

    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 0;

    /* threshold 0: satisfied by a single occurrence. */
    step.trigger_threshold = 0;
    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_FALSE(rcp_triggered_threshold_reached(&step, &rt));
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 1, 0));
    TEST_ASSERT_TRUE(rcp_triggered_threshold_reached(&step, &rt));

    /* threshold 3: satisfied by the fourth occurrence, not the third. */
    step.trigger_threshold = 3;
    rcp_triggered_runtime_enter_started(&rt);
    for (i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 1, 0));
        TEST_ASSERT_FALSE(rcp_triggered_threshold_reached(&step, &rt));
    }
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 1, 0));
    TEST_ASSERT_TRUE(rcp_triggered_threshold_reached(&step, &rt));
}

static void test_exec_delay_elapsed(void)
{
    rcp_triggered_step_t step = {0};
    step.exec_delay = 100;

    TEST_ASSERT_FALSE(rcp_triggered_exec_delay_elapsed(&step, 99));
    TEST_ASSERT_TRUE(rcp_triggered_exec_delay_elapsed(&step, 100));
    TEST_ASSERT_TRUE(rcp_triggered_exec_delay_elapsed(&step, 101));
}

static void test_tick_requires_started_and_threshold(void)
{
    rcp_triggered_step_t step = {0};
    rcp_triggered_runtime_t rt = {0};

    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 0;
    step.trigger_threshold = 0;
    step.exec_delay         = 0;

    /* Never started: never fires. */
    TEST_ASSERT_FALSE(rcp_triggered_tick(&step, &rt, 1000, true));

    rcp_triggered_runtime_enter_started(&rt);
    /* Started but no occurrence recorded yet: threshold unmet, never fires. */
    TEST_ASSERT_FALSE(rcp_triggered_tick(&step, &rt, 1000, true));
    TEST_ASSERT_TRUE(rt.started);
}

static void test_tick_fires_only_when_idle(void)
{
    rcp_triggered_step_t step = {0};
    rcp_triggered_runtime_t rt = {0};

    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 0;
    step.trigger_threshold = 0;
    step.exec_delay         = 0;

    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 1, 0));

    /* Threshold met, delay elapsed, but endpoint busy: never fires. */
    TEST_ASSERT_FALSE(rcp_triggered_tick(&step, &rt, 1000, false));
    TEST_ASSERT_TRUE(rt.started); /* untouched by a failed tick */
    TEST_ASSERT_EQUAL_UINT32(1, rt.occurrence_count);

    /* Now idle: fires, and the runtime resets. */
    TEST_ASSERT_TRUE(rcp_triggered_tick(&step, &rt, 1000, true));
    TEST_ASSERT_FALSE(rt.started);
    TEST_ASSERT_EQUAL_UINT32(0, rt.occurrence_count);
}

static void test_tick_blocked_until_exec_delay_elapses(void)
{
    rcp_triggered_step_t step = {0};
    rcp_triggered_runtime_t rt = {0};

    step.trigger_source_ep = 1;
    step.trigger_signal_nr = 0;
    step.trigger_threshold = 0;
    step.exec_delay         = 50;

    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 1, 0));

    TEST_ASSERT_FALSE(rcp_triggered_tick(&step, &rt, 49, true));
    TEST_ASSERT_TRUE(rcp_triggered_tick(&step, &rt, 50, true));
}

static void test_counter_free_runs_independent_of_idle(void)
{
    rcp_triggered_runtime_t rt = {0};
    rcp_triggered_step_t step = {0};

    step.trigger_source_ep = 7;
    step.trigger_signal_nr = 7;
    rcp_triggered_runtime_enter_started(&rt);

    /* Recording occurrences never itself checks idle/busy -- the file
     * header's "counts independent of the endpoint's idle/busy status"
     * rule. */
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 7, 7));
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 7, 7));
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt, &step, 7, 7));
    TEST_ASSERT_EQUAL_UINT32(3, rt.occurrence_count);
}

/* ── Literal wire layout ──────────────────────────────────────────────────────
 *
 * Written from the TC18 v0.5.1_RC triggered-request figure and field table
 * (§11.2.2.3, Figure 10 / Table 8), NOT copied back out of this encoder.
 * The figure lays the repurposed message_timestamp region out as:
 *
 *   offset 0     request_type        (one octet)
 *   offset 1     trigger_source_ep   (one octet)
 *   offset 2     trigger_signal_nr   (one octet)
 *   offset 3     trigger_threshold   (one octet)
 *   offsets 4..5 trigger_exec_delay  (two octets, big-endian)
 *   offsets 6..7 trigger_repetitions (two octets, big-endian)
 *
 * Before v0.102.0 this module packed compound's sequencer_index/
 * start_state/next_state here instead, so a triggered request could not
 * express which trigger it was waiting on at all. */

#define TS_OFF RCP_ACF_ABB_HEADER_LEN

static void test_triggered_wire_sub_field_offsets(void)
{
    rcp_triggered_step_t step = {0};
    rcp_bytes_t frame;

    step.trigger_source_ep = 0x11;
    step.trigger_signal_nr = 0x22;
    step.trigger_threshold = 0x33;
    step.exec_delay         = 0x4455;
    step.repeat_count       = 0x6677;

    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, 7, &step, 1, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len >= TS_OFF + 8u);

    TEST_ASSERT_EQUAL_HEX8(0x0E, frame.data[TS_OFF + 0]); /* request_type = triggered */
    TEST_ASSERT_EQUAL_HEX8(0x11, frame.data[TS_OFF + 1]); /* trigger_source_ep */
    TEST_ASSERT_EQUAL_HEX8(0x22, frame.data[TS_OFF + 2]); /* trigger_signal_nr */
    TEST_ASSERT_EQUAL_HEX8(0x33, frame.data[TS_OFF + 3]); /* trigger_threshold */
    TEST_ASSERT_EQUAL_HEX8(0x44, frame.data[TS_OFF + 4]); /* trigger_exec_delay hi */
    TEST_ASSERT_EQUAL_HEX8(0x55, frame.data[TS_OFF + 5]); /* trigger_exec_delay lo */
    TEST_ASSERT_EQUAL_HEX8(0x66, frame.data[TS_OFF + 6]); /* trigger_repetitions hi */
    TEST_ASSERT_EQUAL_HEX8(0x77, frame.data[TS_OFF + 7]); /* trigger_repetitions lo */

    rcp_bytes_free(&frame);
}

static void test_triggered_safety_opcode_is_0x8e(void)
{
    rcp_triggered_step_t step = {0};
    rcp_bytes_t frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED_SAFETY, 1, &step,
                                                       0, NULL, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_HEX8(0x8E, frame.data[TS_OFF + 0]);
    rcp_bytes_free(&frame);
}

static void test_triggered_decode_reads_hand_built_spec_layout(void)
{
    rcp_triggered_step_t step = {0};
    rcp_triggered_step_t got_step = {0};
    rcp_bytes_t frame;
    uint8_t rt = 0;
    rcp_byte_bus_id_t bbid = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, 5, &step, 9, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    frame.data[TS_OFF + 1] = 0x0A; /* trigger_source_ep */
    frame.data[TS_OFF + 2] = 0x0B; /* trigger_signal_nr */
    frame.data[TS_OFF + 3] = 0x0C; /* trigger_threshold */
    frame.data[TS_OFF + 4] = 0x12; /* exec_delay = 0x1234 */
    frame.data[TS_OFF + 5] = 0x34;
    frame.data[TS_OFF + 6] = 0xFF; /* repetitions = infinite */
    frame.data[TS_OFF + 7] = 0xFF;

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_OK,
                           rcp_triggered_decode_request(frame.data, frame.len, &rt, &bbid,
                                                         &got_step, &payload, &payload_len, &txn));

    TEST_ASSERT_EQUAL_UINT8(0x0A, got_step.trigger_source_ep);
    TEST_ASSERT_EQUAL_UINT8(0x0B, got_step.trigger_signal_nr);
    TEST_ASSERT_EQUAL_UINT8(0x0C, got_step.trigger_threshold);
    TEST_ASSERT_EQUAL_UINT16(0x1234, got_step.exec_delay);
    TEST_ASSERT_EQUAL_UINT16(RCP_TRIGGERED_REPEAT_INFINITE, got_step.repeat_count);

    rcp_bytes_free(&frame);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_is_triggered);
    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_triggered_request_round_trip);
    RUN_TEST(test_safety_request_round_trip);
    RUN_TEST(test_encode_rejects_unrecognized_request_type);
    RUN_TEST(test_decode_rejects_short_frame);
    RUN_TEST(test_decode_rejects_bad_msg_type);
    RUN_TEST(test_decode_rejects_non_repurposed);
    RUN_TEST(test_decode_rejects_unknown_request_type);

    RUN_TEST(test_enter_started_resets_counter);
    RUN_TEST(test_record_occurrence_requires_started);
    RUN_TEST(test_record_occurrence_only_counts_the_selected_trigger);
    RUN_TEST(test_threshold_counts_occurrences_before_execution);
    RUN_TEST(test_exec_delay_elapsed);
    RUN_TEST(test_tick_requires_started_and_threshold);
    RUN_TEST(test_tick_fires_only_when_idle);
    RUN_TEST(test_tick_blocked_until_exec_delay_elapses);
    RUN_TEST(test_counter_free_runs_independent_of_idle);

    RUN_TEST(test_triggered_wire_sub_field_offsets);
    RUN_TEST(test_triggered_safety_opcode_is_0x8e);
    RUN_TEST(test_triggered_decode_reads_hand_built_spec_layout);

    return UNITY_END();
}
