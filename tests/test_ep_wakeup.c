/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-WAKEUP-001
//cfusa:test REQ-WAKEUP-002
//cfusa:test REQ-WAKEUP-003
//cfusa:test REQ-WAKEUP-004
//cfusa:test REQ-WAKEUP-005
//cfusa:test REQ-WAKEUP-006
//cfusa:test REQ-WAKEUP-007
//cfusa:test REQ-WAKEUP-008
//cfusa:test REQ-WAKEUP-009
//cfusa:test REQ-WAKEUP-010
//cfusa:test REQ-WAKEUP-011
//cfusa:test REQ-WAKEUP-012
//cfusa:test REQ-WAKEUP-013
//cfusa:test REQ-WAKEUP-014
//cfusa:test REQ-WAKEUP-015
//cfusa:test REQ-WAKEUP-016
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_wakeup.h>
#include <rcp/lifecycle.h>
#include <rcp/power.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define BUS_ID ((rcp_byte_bus_id_t)7u)

/* ── functional cfg / wake-source monitoring ─────────────────────────────────── */

static void test_functional_cfg_init_zeroes_everything(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    size_t                         i;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_wakeup_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);

    for (i = 0; i < RCP_EP_WAKEUP_MAX_SOURCES; i++) {
        TEST_ASSERT_FALSE(cfg.sources[i].enabled);
        TEST_ASSERT_FALSE(cfg.sources[i].active_high);
    }
}

static void test_functional_cfg_writable_matches_lifecycle_functional_w(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_EQUAL(
        rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer),
        rcp_ep_wakeup_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, writer));

    TEST_ASSERT_FALSE(rcp_ep_wakeup_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_source_asserted_requires_enabled_and_matching_polarity(void)
{
    rcp_ep_wakeup_source_cfg_t disabled = { false, true };
    rcp_ep_wakeup_source_cfg_t active_high = { true, true };
    rcp_ep_wakeup_source_cfg_t active_low  = { true, false };

    TEST_ASSERT_FALSE(rcp_ep_wakeup_source_asserted(disabled, true));

    TEST_ASSERT_TRUE(rcp_ep_wakeup_source_asserted(active_high, true));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_source_asserted(active_high, false));

    TEST_ASSERT_TRUE(rcp_ep_wakeup_source_asserted(active_low, false));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_source_asserted(active_low, true));
}

static void test_any_source_asserted_true_when_one_matches(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    bool                           levels[3] = { false, true, false };

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    cfg.sources[0].enabled     = true;
    cfg.sources[0].active_high = true; /* levels[0] == false -> not asserted */
    cfg.sources[1].enabled     = true;
    cfg.sources[1].active_high = true; /* levels[1] == true -> asserted */

    TEST_ASSERT_TRUE(rcp_ep_wakeup_any_source_asserted(&cfg, levels, 3));
}

static void test_any_source_asserted_false_when_none_match(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    bool                           levels[2] = { false, false };

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    cfg.sources[0].enabled     = true;
    cfg.sources[0].active_high = true;

    TEST_ASSERT_FALSE(rcp_ep_wakeup_any_source_asserted(&cfg, levels, 2));
}

static void test_any_source_asserted_null_safe(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    bool                           levels[1] = { true };

    rcp_ep_wakeup_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_wakeup_any_source_asserted(NULL, levels, 1));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_any_source_asserted(&cfg, NULL, 1));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_any_source_asserted(&cfg, NULL, 0));
}

/* ── wup_status latch ─────────────────────────────────────────────────────────── */

static void test_wup_status_init_is_clear(void)
{
    rcp_ep_wakeup_wup_status_t s;

    rcp_ep_wakeup_wup_status_init(&s);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&s));
}

static void test_wup_status_latch_then_clear(void)
{
    rcp_ep_wakeup_wup_status_t s;

    rcp_ep_wakeup_wup_status_init(&s);
    rcp_ep_wakeup_wup_status_latch(&s);
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&s));

    rcp_ep_wakeup_wup_status_clear(&s);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&s));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_nonnull_for_every_code(void)
{
    TEST_ASSERT_NOT_NULL(rcp_ep_wakeup_strerror(RCP_EP_WAKEUP_OK));
    TEST_ASSERT_NOT_NULL(rcp_ep_wakeup_strerror(RCP_EP_WAKEUP_ERR_SHORT_FRAME));
    TEST_ASSERT_NOT_NULL(rcp_ep_wakeup_strerror(RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE));
    TEST_ASSERT_NOT_NULL(rcp_ep_wakeup_strerror(RCP_EP_WAKEUP_ERR_WRONG_BUS));
    TEST_ASSERT_NOT_NULL(rcp_ep_wakeup_strerror(RCP_EP_WAKEUP_ERR_BAD_OPCODE));
    /* RCP_EP_WAKEUP_ERR_BAD_TARGET_MODE retired 2026-08-10 (c-RCP-AUDIT-06,
     * issue #256 Group E) -- see include/rcp/ep_wakeup.h. */
    TEST_ASSERT_NOT_NULL(rcp_ep_wakeup_strerror((rcp_ep_wakeup_errc_t)99));
}

/* ── SleepCMD request round trip ──────────────────────────────────────────────── */

/* TC18 §13.7.2.3 Figure 22: SleepCMD is a 1-byte opcode + padding, with
 * no target-mode field at all -- corrected 2026-08-10 (c-RCP-AUDIT-06,
 * issue #256 Group E). This request unconditionally means Sleep; the
 * former test_sleepcmd_request_round_trip_standby() and
 * test_sleepcmd_request_encode_rejects_normal_and_unpowered() no longer
 * apply, since there is no mode parameter left to be Standby, Normal, or
 * Unpowered. */
static void test_sleepcmd_request_round_trip(void)
{
    rcp_bytes_t frame;
    uint8_t     out_tn;

    frame = rcp_ep_wakeup_encode_sleepcmd_request(BUS_ID, 9);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_SLEEPCMD_OPCODE, frame.data[8]); /* payload starts right after ABB header */

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                       rcp_ep_wakeup_decode_sleepcmd_request(frame.data, frame.len, BUS_ID, &out_tn));
    TEST_ASSERT_EQUAL_UINT8(9, out_tn);

    rcp_bytes_free(&frame);
}

static void test_sleepcmd_request_decode_wrong_bus(void)
{
    rcp_bytes_t frame;
    uint8_t     out_tn;

    frame = rcp_ep_wakeup_encode_sleepcmd_request(BUS_ID, 1);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_WRONG_BUS,
                       rcp_ep_wakeup_decode_sleepcmd_request(frame.data, frame.len, BUS_ID + 1, &out_tn));
    rcp_bytes_free(&frame);
}

static void test_sleepcmd_request_decode_short_frame(void)
{
    uint8_t out_tn;
    uint8_t tiny[3] = {0};

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_SHORT_FRAME,
                       rcp_ep_wakeup_decode_sleepcmd_request(tiny, sizeof(tiny), BUS_ID, &out_tn));
}

static void test_sleepcmd_request_decode_bad_opcode(void)
{
    rcp_bytes_t frame;
    uint8_t     out_tn;

    frame = rcp_ep_wakeup_encode_sleepcmd_request(BUS_ID, 1);
    frame.data[8] = 0x00; /* corrupt the fixed opcode byte */

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_BAD_OPCODE,
                       rcp_ep_wakeup_decode_sleepcmd_request(frame.data, frame.len, BUS_ID, &out_tn));
    rcp_bytes_free(&frame);
}

/* Figure 22's own padding region carries no meaning at all -- decoding
 * must succeed regardless of its content, confirming this fix actually
 * removed the byte-9 validation the old (wrong) target-mode check
 * performed. Replaces the retired test_sleepcmd_request_decode_bad_target_mode(). */
static void test_sleepcmd_request_decode_ignores_padding_content(void)
{
    rcp_bytes_t frame;
    uint8_t     out_tn;

    frame = rcp_ep_wakeup_encode_sleepcmd_request(BUS_ID, 1);
    frame.data[9] = 0xFFu; /* arbitrary padding content -- must not affect the outcome */

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                       rcp_ep_wakeup_decode_sleepcmd_request(frame.data, frame.len, BUS_ID, &out_tn));
    rcp_bytes_free(&frame);
}

static void test_sleepcmd_request_decode_wrong_msg_type(void)
{
    uint8_t out_tn;
    uint8_t gbb[16 + 2] = {0};

    gbb[0] = RCP_ACF_MSG_TYPE_GBB;
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_BAD_MSG_TYPE,
                       rcp_ep_wakeup_decode_sleepcmd_request(gbb, sizeof(gbb), BUS_ID, &out_tn));
}

/* ── SleepCMD response round trip ─────────────────────────────────────────────── */

static void test_sleepcmd_response_round_trip_ok(void)
{
    rcp_bytes_t                frame;
    rcp_pwrmode_entry_result_t out_result;
    uint8_t                    out_tn;

    frame = rcp_ep_wakeup_encode_sleepcmd_response(BUS_ID, RCP_PWRMODE_ENTRY_OK, 3);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK, rcp_ep_wakeup_decode_sleepcmd_response(frame.data, frame.len, BUS_ID,
                                                                                &out_result, &out_tn));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_OK, out_result);
    TEST_ASSERT_EQUAL_UINT8(3, out_tn);

    rcp_bytes_free(&frame);
}

static void test_sleepcmd_response_round_trip_refused(void)
{
    rcp_bytes_t                frame;
    rcp_pwrmode_entry_result_t out_result;
    uint8_t                    out_tn;

    frame = rcp_ep_wakeup_encode_sleepcmd_response(BUS_ID, RCP_PWRMODE_ENTRY_REFUSED, 4);

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK, rcp_ep_wakeup_decode_sleepcmd_response(frame.data, frame.len, BUS_ID,
                                                                                &out_result, &out_tn));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, out_result);

    rcp_bytes_free(&frame);
}

static void test_sleepcmd_response_decode_unrecognized_byte_is_refused(void)
{
    rcp_bytes_t                frame;
    rcp_pwrmode_entry_result_t out_result;
    uint8_t                    out_tn;

    frame = rcp_ep_wakeup_encode_sleepcmd_response(BUS_ID, RCP_PWRMODE_ENTRY_OK, 1);
    frame.data[9] = 0xFF; /* neither OK's nor REFUSED's own raw value */

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK, rcp_ep_wakeup_decode_sleepcmd_response(frame.data, frame.len, BUS_ID,
                                                                                &out_result, &out_tn));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, out_result); /* fail-safe */

    rcp_bytes_free(&frame);
}

/* ── WakeUp message ────────────────────────────────────────────────────────────── */

static void test_wakeup_message_round_trip(void)
{
    rcp_bytes_t frame;
    uint8_t     out_tn;

    frame = rcp_ep_wakeup_encode_wakeup_message(BUS_ID, 42);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_WAKEUP_OPCODE, frame.data[8]);

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK, rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, BUS_ID, &out_tn));
    TEST_ASSERT_EQUAL_UINT8(42, out_tn);

    rcp_bytes_free(&frame);
}

static void test_wakeup_message_opcode_distinct_from_sleepcmd(void)
{
    TEST_ASSERT_NOT_EQUAL(RCP_EP_WAKEUP_SLEEPCMD_OPCODE, RCP_EP_WAKEUP_WAKEUP_OPCODE);
}

static void test_wakeup_message_decode_wrong_bus(void)
{
    rcp_bytes_t frame;
    uint8_t     out_tn;

    frame = rcp_ep_wakeup_encode_wakeup_message(BUS_ID, 1);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_WRONG_BUS,
                       rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, BUS_ID + 1, &out_tn));
    rcp_bytes_free(&frame);
}

static void test_wakeup_message_decode_short_frame(void)
{
    uint8_t out_tn;
    uint8_t tiny[3] = {0};

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_SHORT_FRAME,
                       rcp_ep_wakeup_decode_wakeup_message(tiny, sizeof(tiny), BUS_ID, &out_tn));
}

/* ── is_wakeup_echo ────────────────────────────────────────────────────────────── */

static void test_is_wakeup_echo_true_when_matching(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_wakeup_message(BUS_ID, 17);

    TEST_ASSERT_TRUE(rcp_ep_wakeup_is_wakeup_echo(frame.data, frame.len, BUS_ID, 17));
    rcp_bytes_free(&frame);
}

static void test_is_wakeup_echo_false_when_transaction_num_differs(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_wakeup_message(BUS_ID, 17);

    TEST_ASSERT_FALSE(rcp_ep_wakeup_is_wakeup_echo(frame.data, frame.len, BUS_ID, 18));
    rcp_bytes_free(&frame);
}

static void test_is_wakeup_echo_false_on_decode_failure(void)
{
    uint8_t tiny[3] = {0};

    TEST_ASSERT_FALSE(rcp_ep_wakeup_is_wakeup_echo(tiny, sizeof(tiny), BUS_ID, 0));
}

static void test_is_wakeup_echo_false_for_sleepcmd_frame(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_sleepcmd_request(BUS_ID, 17);

    TEST_ASSERT_FALSE(rcp_ep_wakeup_is_wakeup_echo(frame.data, frame.len, BUS_ID, 17));
    rcp_bytes_free(&frame);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_functional_cfg_init_zeroes_everything);
    RUN_TEST(test_functional_cfg_writable_matches_lifecycle_functional_w);
    RUN_TEST(test_source_asserted_requires_enabled_and_matching_polarity);
    RUN_TEST(test_any_source_asserted_true_when_one_matches);
    RUN_TEST(test_any_source_asserted_false_when_none_match);
    RUN_TEST(test_any_source_asserted_null_safe);

    RUN_TEST(test_wup_status_init_is_clear);
    RUN_TEST(test_wup_status_latch_then_clear);

    RUN_TEST(test_strerror_nonnull_for_every_code);

    RUN_TEST(test_sleepcmd_request_round_trip);
    RUN_TEST(test_sleepcmd_request_decode_wrong_bus);
    RUN_TEST(test_sleepcmd_request_decode_short_frame);
    RUN_TEST(test_sleepcmd_request_decode_bad_opcode);
    RUN_TEST(test_sleepcmd_request_decode_ignores_padding_content);
    RUN_TEST(test_sleepcmd_request_decode_wrong_msg_type);

    RUN_TEST(test_sleepcmd_response_round_trip_ok);
    RUN_TEST(test_sleepcmd_response_round_trip_refused);
    RUN_TEST(test_sleepcmd_response_decode_unrecognized_byte_is_refused);

    RUN_TEST(test_wakeup_message_round_trip);
    RUN_TEST(test_wakeup_message_opcode_distinct_from_sleepcmd);
    RUN_TEST(test_wakeup_message_decode_wrong_bus);
    RUN_TEST(test_wakeup_message_decode_short_frame);

    RUN_TEST(test_is_wakeup_echo_true_when_matching);
    RUN_TEST(test_is_wakeup_echo_false_when_transaction_num_differs);
    RUN_TEST(test_is_wakeup_echo_false_on_decode_failure);
    RUN_TEST(test_is_wakeup_echo_false_for_sleepcmd_frame);

    return UNITY_END();
}
