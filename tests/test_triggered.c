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
#include <rcp/sequencer.h>
#include <rcp/triggered.h>

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

    step.sequencer_index      = 2;
    step.start_state           = 1;
    step.next_state            = 5;
    step.trigger_exec_delay_ms = 1234;
    step.repeat_count          = 7;

    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, 9, &step, 42, body,
                                          sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_TRIGGERED_OK,
                           rcp_triggered_decode_request(frame.data, frame.len, &rt, &bbid,
                                                         &got_step, &payload, &payload_len, &txn));

    TEST_ASSERT_EQUAL_UINT8(RCP_REQUEST_TYPE_TRIGGERED, rt);
    TEST_ASSERT_EQUAL_UINT8(9, bbid);
    TEST_ASSERT_EQUAL_UINT8(2, got_step.sequencer_index);
    TEST_ASSERT_EQUAL_UINT8(1, got_step.start_state);
    TEST_ASSERT_EQUAL_UINT8(5, got_step.next_state);
    TEST_ASSERT_EQUAL_UINT16(1234, got_step.trigger_exec_delay_ms);
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

    step.sequencer_index      = 0;
    step.next_state            = 3;
    step.repeat_count          = RCP_TRIGGERED_REPEAT_INFINITE;

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

    TEST_ASSERT_FALSE(rcp_triggered_runtime_record_occurrence(&rt));
    TEST_ASSERT_EQUAL_UINT32(0, rt.occurrence_count);

    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt));
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt));
    TEST_ASSERT_EQUAL_UINT32(2, rt.occurrence_count);
}

static void test_advance_guard(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_triggered_step_t step = {0};

    step.sequencer_index = 1;
    step.start_state       = RCP_SEQUENCER_POWER_ON_STATE;

    TEST_ASSERT_TRUE(rcp_triggered_advance_guard(&table, &step));

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 1, 9));
    TEST_ASSERT_FALSE(rcp_triggered_advance_guard(&table, &step));

    step.sequencer_index = 200; /* out of range for a 4-entry table */
    TEST_ASSERT_FALSE(rcp_triggered_advance_guard(&table, &step));

    rcp_sequencer_table_free(&table);
}

static void test_exec_delay_elapsed(void)
{
    rcp_triggered_step_t step = {0};
    step.trigger_exec_delay_ms = 100;

    TEST_ASSERT_FALSE(rcp_triggered_exec_delay_elapsed(&step, 99));
    TEST_ASSERT_TRUE(rcp_triggered_exec_delay_elapsed(&step, 100));
    TEST_ASSERT_TRUE(rcp_triggered_exec_delay_elapsed(&step, 101));
}

static void test_tick_requires_started_and_occurrence(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_triggered_step_t step = {0};
    rcp_triggered_runtime_t rt = {0};
    uint8_t got = 0;

    step.sequencer_index      = 0;
    step.start_state           = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state            = 9;
    step.trigger_exec_delay_ms = 0;

    /* Never started: never fires. */
    TEST_ASSERT_FALSE(rcp_triggered_tick(&table, &step, &rt, 1000, true));

    rcp_triggered_runtime_enter_started(&rt);
    /* Started but no occurrence recorded yet: never fires. */
    TEST_ASSERT_FALSE(rcp_triggered_tick(&table, &step, &rt, 1000, true));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);

    rcp_sequencer_table_free(&table);
}

static void test_tick_fires_only_when_idle(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_triggered_step_t step = {0};
    rcp_triggered_runtime_t rt = {0};
    uint8_t got = 0;

    step.sequencer_index      = 0;
    step.start_state           = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state            = 9;
    step.trigger_exec_delay_ms = 0;

    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt));

    /* Occurrence recorded, delay elapsed, but endpoint busy: never fires. */
    TEST_ASSERT_FALSE(rcp_triggered_tick(&table, &step, &rt, 1000, false));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(RCP_SEQUENCER_POWER_ON_STATE, got);
    TEST_ASSERT_TRUE(rt.started); /* untouched by a failed tick */

    /* Now idle: fires, and the runtime resets. */
    TEST_ASSERT_TRUE(rcp_triggered_tick(&table, &step, &rt, 1000, true));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(9, got);
    TEST_ASSERT_FALSE(rt.started);
    TEST_ASSERT_EQUAL_UINT32(0, rt.occurrence_count);

    rcp_sequencer_table_free(&table);
}

static void test_tick_counter_free_runs_independent_of_idle(void)
{
    rcp_triggered_runtime_t rt = {0};

    rcp_triggered_runtime_enter_started(&rt);

    /* Recording occurrences never itself checks idle/busy -- the file
     * header's "counts independent of the endpoint's idle/busy status"
     * rule. */
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt));
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt));
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt));
    TEST_ASSERT_EQUAL_UINT32(3, rt.occurrence_count);
}

static void test_tick_guard_blocks_fire(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4);
    rcp_triggered_step_t step = {0};
    rcp_triggered_runtime_t rt = {0};
    uint8_t got = 0;

    step.sequencer_index      = 0;
    step.start_state           = RCP_SEQUENCER_POWER_ON_STATE;
    step.next_state            = 9;

    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0, 3)); /* not start_state */

    rcp_triggered_runtime_enter_started(&rt);
    TEST_ASSERT_TRUE(rcp_triggered_runtime_record_occurrence(&rt));

    TEST_ASSERT_FALSE(rcp_triggered_tick(&table, &step, &rt, 0, true));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0, &got));
    TEST_ASSERT_EQUAL_UINT8(3, got);

    rcp_sequencer_table_free(&table);
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
    RUN_TEST(test_advance_guard);
    RUN_TEST(test_exec_delay_elapsed);
    RUN_TEST(test_tick_requires_started_and_occurrence);
    RUN_TEST(test_tick_fires_only_when_idle);
    RUN_TEST(test_tick_counter_free_runs_independent_of_idle);
    RUN_TEST(test_tick_guard_blocks_fire);

    return UNITY_END();
}
