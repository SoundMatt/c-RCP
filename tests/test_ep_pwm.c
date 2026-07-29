//cfusa:test REQ-PWM-001
//cfusa:test REQ-PWM-002
//cfusa:test REQ-PWM-003
//cfusa:test REQ-PWM-004
//cfusa:test REQ-PWM-005
//cfusa:test REQ-PWM-006
//cfusa:test REQ-PWM-007
//cfusa:test REQ-PWM-008
//cfusa:test REQ-PWM-009
//cfusa:test REQ-PWM-010
//cfusa:test REQ-PWM-011
//cfusa:test REQ-PWM-012
//cfusa:test REQ-PWM-013
//cfusa:test REQ-PWM-014
//cfusa:test REQ-PWM-015
//cfusa:test REQ-PWM-016
//cfusa:test REQ-PWM-017
//cfusa:test REQ-PWM-018
//cfusa:test REQ-PWM-019
//cfusa:test REQ-PWM-020
//cfusa:test REQ-PWM-021
//cfusa:test REQ-PWM-022
//cfusa:test REQ-PWM-023
//cfusa:test REQ-PWM-024
//cfusa:test REQ-PWM-025
//cfusa:test REQ-PWM-026
//cfusa:test REQ-PWM-027
//cfusa:test REQ-PWM-028
//cfusa:test REQ-PWM-029
//cfusa:test REQ-PWM-030
//cfusa:test REQ-PWM-031
//cfusa:test REQ-PWM-032
//cfusa:test REQ-PWM-033
//cfusa:test REQ-PWM-034
//cfusa:test REQ-PWM-035
//cfusa:test REQ-PWM-036
//cfusa:test REQ-PWM-037
//cfusa:test REQ-PWM-038
//cfusa:test REQ-PWM-039
//cfusa:test REQ-PWM-040
//cfusa:test REQ-PWM-041
//cfusa:test REQ-PWM-042
//cfusa:test REQ-PWM-043
//cfusa:test REQ-PWM-044
//cfusa:test REQ-PWM-045
//cfusa:test REQ-PWM-046
//cfusa:test REQ-PWM-047
//cfusa:test REQ-PWM-048
//cfusa:test REQ-PWM-049
//cfusa:test REQ-PWM-050
//cfusa:test REQ-PWM-051
//cfusa:test REQ-PWM-052
//cfusa:test REQ-PWM-053
//cfusa:test REQ-PWM-054
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_pwm.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── PWM_OUT: evt[2:0] write semantics ─────────────────────────────────────── */

static void test_out_write_semantics_valid(void)
{
    uint8_t v;

    for (v = 0; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_pwm_out_write_semantics_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_write_semantics_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_write_semantics_valid(255));
}

static void test_out_apply_write_replace(void)
{
    rcp_ep_pwm_value_t current = {100, 50};
    rcp_ep_pwm_value_t request = {200, 75};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_REPLACE);

    TEST_ASSERT_EQUAL_UINT16(200, result.period);
    TEST_ASSERT_EQUAL_UINT16(75, result.active_duration);
}

static void test_out_apply_write_or(void)
{
    rcp_ep_pwm_value_t current = {0x00F0, 0x0F00};
    rcp_ep_pwm_value_t request = {0x000F, 0x00F0};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_OR);

    TEST_ASSERT_EQUAL_UINT16(0x00FF, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

static void test_out_apply_write_and(void)
{
    rcp_ep_pwm_value_t current = {0x00FF, 0x0FF0};
    rcp_ep_pwm_value_t request = {0x000F, 0x0FF0};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_AND);

    TEST_ASSERT_EQUAL_UINT16(0x000F, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

static void test_out_apply_write_xor(void)
{
    rcp_ep_pwm_value_t current = {0x00FF, 0x0F0F};
    rcp_ep_pwm_value_t request = {0x000F, 0x00FF};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_XOR);

    TEST_ASSERT_EQUAL_UINT16(0x00F0, result.period);
    TEST_ASSERT_EQUAL_UINT16(0x0FF0, result.active_duration);
}

static void test_out_apply_write_add_saturates(void)
{
    rcp_ep_pwm_value_t current = {0xFFF0, 100};
    rcp_ep_pwm_value_t request = {0x0020, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_ADD);

    TEST_ASSERT_EQUAL_UINT16(0xFFFF, result.period); /* saturates, not wraps */
    TEST_ASSERT_EQUAL_UINT16(150, result.active_duration);
}

static void test_out_apply_write_sub_saturates(void)
{
    rcp_ep_pwm_value_t current = {10, 200};
    rcp_ep_pwm_value_t request = {50, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request, RCP_EP_PWM_OUT_WRITE_SUB);

    TEST_ASSERT_EQUAL_UINT16(0, result.period); /* saturates, not wraps negative */
    TEST_ASSERT_EQUAL_UINT16(150, result.active_duration);
}

static void test_out_apply_write_reserved4_is_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              RCP_EP_PWM_OUT_WRITE_RESERVED4);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

/* Regression test for issue #104: raw wire evt[2:0] values 4/5/6 must map
 * to Reserved/ADD/SUB respectively (not the previous off-by-one ADD/SUB/
 * Reserved mapping). Exercises rcp_ep_pwm_out_apply_write() with the raw
 * wire-value enum casts a decoder would actually produce, not just the
 * named constants. */
static void test_out_apply_write_wire_value_4_is_reserved_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)4u);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

static void test_out_apply_write_wire_value_5_is_add(void)
{
    rcp_ep_pwm_value_t current = {10, 200};
    rcp_ep_pwm_value_t request = {20, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)5u);

    TEST_ASSERT_EQUAL_UINT16(30, result.period);
    TEST_ASSERT_EQUAL_UINT16(250, result.active_duration);
}

static void test_out_apply_write_wire_value_6_is_sub(void)
{
    rcp_ep_pwm_value_t current = {30, 250};
    rcp_ep_pwm_value_t request = {20, 50};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              (rcp_ep_pwm_out_write_semantics_t)6u);

    TEST_ASSERT_EQUAL_UINT16(10, result.period);
    TEST_ASSERT_EQUAL_UINT16(200, result.active_duration);
}

static void test_out_apply_write_reconfig_misrouted_is_noop(void)
{
    rcp_ep_pwm_value_t current = {123, 456};
    rcp_ep_pwm_value_t request = {999, 999};
    rcp_ep_pwm_value_t result  = rcp_ep_pwm_out_apply_write(current, request,
                                                              RCP_EP_PWM_OUT_WRITE_RECONFIG);

    TEST_ASSERT_EQUAL_UINT16(123, result.period);
    TEST_ASSERT_EQUAL_UINT16(456, result.active_duration);
}

static void test_out_apply_reconfig_toggles_when_bit0_set(void)
{
    bool enabled = false;

    rcp_ep_pwm_out_apply_reconfig(&enabled, 0x00000001u);
    TEST_ASSERT_TRUE(enabled);

    rcp_ep_pwm_out_apply_reconfig(&enabled, 0x00000001u);
    TEST_ASSERT_FALSE(enabled);
}

static void test_out_apply_reconfig_leaves_unchanged_when_bit0_clear(void)
{
    bool enabled = true;

    rcp_ep_pwm_out_apply_reconfig(&enabled, 0xFFFFFFFEu);
    TEST_ASSERT_TRUE(enabled);

    enabled = false;
    rcp_ep_pwm_out_apply_reconfig(&enabled, 0x00000000u);
    TEST_ASSERT_FALSE(enabled);
}

/* ── PWM_OUT: triggers ──────────────────────────────────────────────────────── */

static void test_out_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE, RCP_EP_PWM_OUT_EVENT_DONE));
}

static void test_out_trigger_cycle_start(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_CYCLE_START, RCP_EP_PWM_OUT_EVENT_DONE));
}

static void test_out_trigger_mid_pulse(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE, RCP_EP_PWM_OUT_EVENT_DONE));
}

static void test_out_trigger_done(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_DONE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE, RCP_EP_PWM_OUT_EVENT_MID_PULSE));
}

/* ── PWM_OUT: functional config ─────────────────────────────────────────────── */

static void test_out_functional_cfg_init_zeroes(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_TRIGGER_NONE, cfg.trigger);
    TEST_ASSERT_FALSE(cfg.enabled);
}

static void test_out_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_out_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, writer));
}

static void test_out_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t unauth = {0};
    rcp_lifecycle_writer_ctx_t auth   = {0};

    auth.via_root_client_ep0 = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, unauth));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, auth));
}

static void test_out_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_set_trigger(&cfg, RCP_EP_PWM_OUT_TRIGGER_DONE,
                                                  RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_TRIGGER_NONE, cfg.trigger);
}

static void test_out_set_trigger_applies_when_authorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_pwm_out_set_trigger(&cfg, RCP_EP_PWM_OUT_TRIGGER_DONE,
                                                 RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_TRIGGER_DONE, cfg.trigger);
}

static void test_out_set_enabled_rejects_unauthorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_out_set_enabled(&cfg, true, RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_FALSE(cfg.enabled);
}

static void test_out_set_enabled_applies_when_authorized(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t         writer = {0};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_pwm_out_set_enabled(&cfg, true, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_TRUE(cfg.enabled);
}

/* ── PWM_OUT: error codes ──────────────────────────────────────────────────── */

static void test_out_strerror_never_null_and_distinct(void)
{
    rcp_ep_pwm_out_errc_t codes[] = {
        RCP_EP_PWM_OUT_OK,               RCP_EP_PWM_OUT_ERR_SHORT_FRAME,
        RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE, RCP_EP_PWM_OUT_ERR_WRONG_BUS,
        RCP_EP_PWM_OUT_ERR_WRONG_OP,     RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN,
    };
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_pwm_out_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_pwm_out_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_pwm_out_strerror((rcp_ep_pwm_out_errc_t)99));
}

/* ── PWM_OUT: read request ─────────────────────────────────────────────────── */

static void test_out_read_request_round_trip(void)
{
    rcp_bytes_t            frame = rcp_ep_pwm_out_encode_read_request(3, 7);
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t  rc;

    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 3, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(7, out_tn);

    rcp_bytes_free(&frame);
}

static void test_out_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t           frame = rcp_ep_pwm_out_encode_read_request(3, 7);
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 4, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_BUS, rc);
    rcp_bytes_free(&frame);
}

static void test_out_read_request_rejects_wrong_op(void)
{
    rcp_ep_pwm_value_t    value = {1, 2};
    rcp_bytes_t           frame = rcp_ep_pwm_out_encode_write_request(3, value, RCP_EP_PWM_OUT_WRITE_REPLACE, 1);
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(frame.data, frame.len, 3, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_OP, rc);
    rcp_bytes_free(&frame);
}

static void test_out_read_request_rejects_short_frame(void)
{
    uint8_t               out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_read_request(NULL, 0, 3, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_SHORT_FRAME, rc);
}

/* ── PWM_OUT: write request ────────────────────────────────────────────────── */

static void test_out_write_request_round_trip(void)
{
    rcp_ep_pwm_value_t    value = {1000, 500};
    rcp_bytes_t            frame;
    rcp_ep_pwm_value_t    out_value;
    uint8_t                out_evt;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    frame = rcp_ep_pwm_out_encode_write_request(5, value, RCP_EP_PWM_OUT_WRITE_OR, 42);
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 5, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(1000, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(500, out_value.active_duration);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_WRITE_OR, out_evt);
    TEST_ASSERT_EQUAL_UINT8(42, out_tn);

    rcp_bytes_free(&frame);
}

static void test_out_write_request_rejects_bad_payload_len(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                      bad_payload[3] = {1, 2, 3};
    rcp_bytes_t                  frame;
    rcp_ep_pwm_value_t          out_value;
    uint8_t                      out_evt;
    uint8_t                      out_tn;
    rcp_ep_pwm_out_errc_t       rc;

    hdr.byte_bus_id = 5;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, bad_payload, sizeof(bad_payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 5, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN, rc);

    rcp_bytes_free(&frame);
}

static void test_out_write_request_rejects_wrong_bus(void)
{
    rcp_ep_pwm_value_t    value = {1, 2};
    rcp_bytes_t            frame = rcp_ep_pwm_out_encode_write_request(5, value, RCP_EP_PWM_OUT_WRITE_REPLACE, 1);
    rcp_ep_pwm_value_t    out_value;
    uint8_t                out_evt;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    rc = rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 6, &out_value, &out_evt, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_WRONG_BUS, rc);

    rcp_bytes_free(&frame);
}

/* ── PWM_OUT: response ─────────────────────────────────────────────────────── */

static void test_out_response_round_trip_untimed(void)
{
    rcp_ep_pwm_value_t    value = {2000, 1000};
    rcp_bytes_t            frame;
    rcp_ep_pwm_value_t    out_value;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    frame = rcp_ep_pwm_out_encode_response(9, value, 11, false, 0);
    rc = rcp_ep_pwm_out_decode_response(frame.data, frame.len, 9, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(2000, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(1000, out_value.active_duration);
    TEST_ASSERT_FALSE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(0, out_ts);
    TEST_ASSERT_EQUAL_UINT8(11, out_tn);

    rcp_bytes_free(&frame);
}

static void test_out_response_round_trip_timed(void)
{
    rcp_ep_pwm_value_t    value = {3000, 1500};
    rcp_bytes_t            frame;
    rcp_ep_pwm_value_t    out_value;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc;

    frame = rcp_ep_pwm_out_encode_response(9, value, 12, true, 123456789ULL);
    rc = rcp_ep_pwm_out_decode_response(frame.data, frame.len, 9, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK, rc);
    TEST_ASSERT_TRUE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(123456789ULL, out_ts);

    rcp_bytes_free(&frame);
}

static void test_out_response_decode_rejects_short_frame(void)
{
    rcp_ep_pwm_value_t    out_value;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;
    rcp_ep_pwm_out_errc_t rc = rcp_ep_pwm_out_decode_response(NULL, 0, 9, &out_value, &out_timed,
                                                                &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_ERR_SHORT_FRAME, rc);
}

/* ── PWM_IN: triggers ───────────────────────────────────────────────────────── */

static void test_in_trigger_none_never_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_NONE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_NONE, true, false));
}

static void test_in_trigger_rising(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, true));
}

static void test_in_trigger_falling(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, true, false));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, false, false));
}

/* ── PWM_IN: functional config ─────────────────────────────────────────────── */

static void test_in_functional_cfg_init_zeroes(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
}

static void test_in_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_in_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, writer));
}

static void test_in_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t unauth = {0};
    rcp_lifecycle_writer_ctx_t auth   = {0};

    auth.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, unauth));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, auth));
}

static void test_in_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_set_trigger(&cfg, RCP_EP_PWM_IN_TRIGGER_RISING,
                                                 RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
}

static void test_in_set_trigger_applies_when_authorized(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t        writer = {0};

    rcp_ep_pwm_in_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_set_trigger(&cfg, RCP_EP_PWM_IN_TRIGGER_FALLING,
                                                RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_FALLING, cfg.trigger);
}

/* ── PWM_IN: error codes ───────────────────────────────────────────────────── */

static void test_in_strerror_never_null_and_distinct(void)
{
    rcp_ep_pwm_in_errc_t codes[] = {
        RCP_EP_PWM_IN_OK,               RCP_EP_PWM_IN_ERR_SHORT_FRAME,
        RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE, RCP_EP_PWM_IN_ERR_WRONG_BUS,
        RCP_EP_PWM_IN_ERR_WRONG_OP,     RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN,
    };
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_pwm_in_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_pwm_in_strerror(codes[j])));
        }
    }
}

/* ── PWM_IN: read request ──────────────────────────────────────────────────── */

static void test_in_read_request_round_trip(void)
{
    rcp_bytes_t           frame = rcp_ep_pwm_in_encode_read_request(2, 5);
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t  rc;

    rc = rcp_ep_pwm_in_decode_read_request(frame.data, frame.len, 2, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(5, out_tn);

    rcp_bytes_free(&frame);
}

static void test_in_read_request_rejects_short_frame(void)
{
    uint8_t              out_tn;
    rcp_ep_pwm_in_errc_t rc = rcp_ep_pwm_in_decode_read_request(NULL, 0, 2, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_ERR_SHORT_FRAME, rc);
}

/* ── PWM_IN: response ──────────────────────────────────────────────────────── */

static void test_in_response_round_trip_untimed(void)
{
    rcp_ep_pwm_value_t   value = {4000, 2000};
    rcp_bytes_t           frame;
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    frame = rcp_ep_pwm_in_encode_response(1, value, 3, false, 0);
    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 1, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(4000, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(2000, out_value.active_duration);
    TEST_ASSERT_FALSE(out_timed);

    rcp_bytes_free(&frame);
}

static void test_in_response_round_trip_timed(void)
{
    rcp_ep_pwm_value_t   value = {5000, 2500};
    rcp_bytes_t           frame;
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    frame = rcp_ep_pwm_in_encode_response(1, value, 4, true, 555ULL);
    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 1, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_TRUE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(555ULL, out_ts);

    rcp_bytes_free(&frame);
}

static void test_in_response_decode_rejects_wrong_bus(void)
{
    rcp_ep_pwm_value_t   value = {1, 1};
    rcp_bytes_t           frame = rcp_ep_pwm_in_encode_response(1, value, 1, false, 0);
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 2, &out_value, &out_timed, &out_ts, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_ERR_WRONG_BUS, rc);

    rcp_bytes_free(&frame);
}

static void test_in_response_no_signal_sentinel_round_trips(void)
{
    rcp_ep_pwm_value_t   value = {RCP_EP_PWM_IN_NO_SIGNAL, RCP_EP_PWM_IN_NO_SIGNAL};
    rcp_bytes_t           frame;
    rcp_ep_pwm_value_t   out_value;
    bool                  out_timed;
    uint64_t              out_ts;
    uint8_t               out_tn;
    rcp_ep_pwm_in_errc_t rc;

    frame = rcp_ep_pwm_in_encode_response(1, value, 9, false, 0);
    rc = rcp_ep_pwm_in_decode_response(frame.data, frame.len, 1, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_PWM_IN_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, out_value.active_duration);

    rcp_bytes_free(&frame);
}

/* ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ────────────── */

static void test_compound_wait_mode_valid_accepts_exactly_4_to_7(void)
{
    uint8_t v;

    for (v = 0; v <= 3; v++) {
        TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_mode_valid(v));
    }
    for (v = 4; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_mode_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_mode_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_mode_valid(255));
}

static void test_compound_wait_period_ge(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 1000));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 999));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 1001));
}

static void test_compound_wait_period_le(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 1000));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 1001));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 999));
}

static void test_compound_wait_duty_ge(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 500));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 501));
}

static void test_compound_wait_duty_le(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_TRUE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 500));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 499));
}

static void test_compound_wait_invalid_mode_returns_false(void)
{
    rcp_ep_pwm_value_t captured = {1000, 500};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, (rcp_ep_pwm_in_compound_wait_mode_t)0, 0));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, (rcp_ep_pwm_in_compound_wait_mode_t)8, 0));
}

static void test_compound_wait_no_signal_never_matches(void)
{
    rcp_ep_pwm_value_t captured = {RCP_EP_PWM_IN_NO_SIGNAL, RCP_EP_PWM_IN_NO_SIGNAL};

    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_GE, 0));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_PERIOD_LE, 0xFFFFu));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_GE, 0));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_compound_wait_compare(captured, RCP_EP_PWM_IN_CMP_DUTY_LE, 0xFFFFu));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_out_write_semantics_valid);
    RUN_TEST(test_out_apply_write_replace);
    RUN_TEST(test_out_apply_write_or);
    RUN_TEST(test_out_apply_write_and);
    RUN_TEST(test_out_apply_write_xor);
    RUN_TEST(test_out_apply_write_add_saturates);
    RUN_TEST(test_out_apply_write_sub_saturates);
    RUN_TEST(test_out_apply_write_reserved4_is_noop);
    RUN_TEST(test_out_apply_write_wire_value_4_is_reserved_noop);
    RUN_TEST(test_out_apply_write_wire_value_5_is_add);
    RUN_TEST(test_out_apply_write_wire_value_6_is_sub);
    RUN_TEST(test_out_apply_write_reconfig_misrouted_is_noop);
    RUN_TEST(test_out_apply_reconfig_toggles_when_bit0_set);
    RUN_TEST(test_out_apply_reconfig_leaves_unchanged_when_bit0_clear);

    RUN_TEST(test_out_trigger_none_never_fires);
    RUN_TEST(test_out_trigger_cycle_start);
    RUN_TEST(test_out_trigger_mid_pulse);
    RUN_TEST(test_out_trigger_done);

    RUN_TEST(test_out_functional_cfg_init_zeroes);
    RUN_TEST(test_out_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_out_functional_cfg_writable_true_hw_configured_any_writer);
    RUN_TEST(test_out_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_out_set_trigger_rejects_unauthorized);
    RUN_TEST(test_out_set_trigger_applies_when_authorized);
    RUN_TEST(test_out_set_enabled_rejects_unauthorized);
    RUN_TEST(test_out_set_enabled_applies_when_authorized);

    RUN_TEST(test_out_strerror_never_null_and_distinct);

    RUN_TEST(test_out_read_request_round_trip);
    RUN_TEST(test_out_read_request_rejects_wrong_bus);
    RUN_TEST(test_out_read_request_rejects_wrong_op);
    RUN_TEST(test_out_read_request_rejects_short_frame);

    RUN_TEST(test_out_write_request_round_trip);
    RUN_TEST(test_out_write_request_rejects_bad_payload_len);
    RUN_TEST(test_out_write_request_rejects_wrong_bus);

    RUN_TEST(test_out_response_round_trip_untimed);
    RUN_TEST(test_out_response_round_trip_timed);
    RUN_TEST(test_out_response_decode_rejects_short_frame);

    RUN_TEST(test_in_trigger_none_never_fires);
    RUN_TEST(test_in_trigger_rising);
    RUN_TEST(test_in_trigger_falling);

    RUN_TEST(test_in_functional_cfg_init_zeroes);
    RUN_TEST(test_in_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_in_functional_cfg_writable_true_hw_configured_any_writer);
    RUN_TEST(test_in_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_in_set_trigger_rejects_unauthorized);
    RUN_TEST(test_in_set_trigger_applies_when_authorized);

    RUN_TEST(test_in_strerror_never_null_and_distinct);

    RUN_TEST(test_in_read_request_round_trip);
    RUN_TEST(test_in_read_request_rejects_short_frame);

    RUN_TEST(test_in_response_round_trip_untimed);
    RUN_TEST(test_in_response_round_trip_timed);
    RUN_TEST(test_in_response_decode_rejects_wrong_bus);
    RUN_TEST(test_in_response_no_signal_sentinel_round_trips);

    RUN_TEST(test_compound_wait_mode_valid_accepts_exactly_4_to_7);
    RUN_TEST(test_compound_wait_period_ge);
    RUN_TEST(test_compound_wait_period_le);
    RUN_TEST(test_compound_wait_duty_ge);
    RUN_TEST(test_compound_wait_duty_le);
    RUN_TEST(test_compound_wait_invalid_mode_returns_false);
    RUN_TEST(test_compound_wait_no_signal_never_matches);

    return UNITY_END();
}
