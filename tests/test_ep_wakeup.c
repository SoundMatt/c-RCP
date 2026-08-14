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
//cfusa:test REQ-WAKEUP-021
//cfusa:test REQ-WAKEUP-022
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
        TEST_ASSERT_EQUAL_UINT16(0u, cfg.sources[i].pin_number);
    }

    TEST_ASSERT_EQUAL_UINT16(0u, cfg.ep_status);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&cfg.wup_status));
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
    rcp_ep_wakeup_source_cfg_t disabled = { false, true, 0 };
    rcp_ep_wakeup_source_cfg_t active_high = { true, true, 0 };
    rcp_ep_wakeup_source_cfg_t active_low  = { true, false, 0 };

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
    rcp_ep_wakeup_wup_status_latch_source(&s, 0);
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&s));

    rcp_ep_wakeup_wup_status_clear(&s);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&s));
}

/* REQ-WAKEUP-021 (issue #341 lineage): TC18's own wup_status register is
 * a per-source bitmask, "each bit represents a wake-up source" -- these
 * tests prove that shape directly, distinct from the aggregate
 * is_clear()/clear() whole-mask queries already covered above. */
static void test_wup_status_latch_source_is_independent_per_index(void)
{
    rcp_ep_wakeup_wup_status_t s;

    rcp_ep_wakeup_wup_status_init(&s);
    rcp_ep_wakeup_wup_status_latch_source(&s, 2);

    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_source_is_latched(&s, 2));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_source_is_latched(&s, 0));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_source_is_latched(&s, 1));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&s)); /* something is latched */
}

static void test_wup_status_clear_source_leaves_other_sources_latched(void)
{
    rcp_ep_wakeup_wup_status_t s;

    rcp_ep_wakeup_wup_status_init(&s);
    rcp_ep_wakeup_wup_status_latch_source(&s, 0);
    rcp_ep_wakeup_wup_status_latch_source(&s, 3);

    rcp_ep_wakeup_wup_status_clear_source(&s, 0);

    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_source_is_latched(&s, 0));
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_source_is_latched(&s, 3));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&s)); /* source 3 still latched */
}

static void test_wup_status_out_of_range_source_index_is_a_no_op(void)
{
    rcp_ep_wakeup_wup_status_t s;

    rcp_ep_wakeup_wup_status_init(&s);
    rcp_ep_wakeup_wup_status_latch_source(&s, RCP_EP_WAKEUP_MAX_SOURCES); /* out of range */
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&s));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_source_is_latched(&s, RCP_EP_WAKEUP_MAX_SOURCES));

    rcp_ep_wakeup_wup_status_latch_source(&s, 0);
    rcp_ep_wakeup_wup_status_clear_source(&s, RCP_EP_WAKEUP_MAX_SOURCES); /* out of range, no-op */
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_source_is_latched(&s, 0));
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

/* ── The EP_func register block (evt[2:0] == 111b) ───────────────────────────
 * ADDED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I dedicated
 * investigation, task #95): see ep_wakeup.h's own "register block" file
 * header note for the full address-collision-resolution rationale and
 * the remaining, honestly disclosed simplification (IO_SRC only
 * represents 3 of Table 37's 6 values -- wup_status's own former
 * single-aggregate-bit simplification is RESOLVED as of REQ-WAKEUP-021,
 * issue #341 lineage: it is now a genuine per-source bitmask, see the
 * tests below). */

static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        out[RCP_EP_WAKEUP_EP_FUNC_LEN];

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    cfg.ep_status = 0x1234;
    rcp_ep_wakeup_wup_status_latch_source(&cfg.wup_status, 0);
    cfg.sources[0].enabled     = true;
    cfg.sources[0].active_high = true;
    cfg.sources[0].pin_number  = 0x0041u; /* fits in 11 bits */

    rcp_ep_wakeup_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_WAKEUP_EP_FUNC_LEN, out[RCP_EP_WAKEUP_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_WAKEUP_MAX_SOURCES,
                             out[RCP_EP_WAKEUP_REG_NR_IO_PINS_MAX]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_WAKEUP_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_WAKEUP_REG_EP_STATUS + 1]);
    /* Address collision resolved: wup_status (0x0004-5) and slot 0's own
     * wup_io_scr1 (0x0006-7) render to distinct addresses. */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[RCP_EP_WAKEUP_REG_WUP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[RCP_EP_WAKEUP_REG_WUP_STATUS + 1]);
    /* io_src=HIGH_LEVEL(0x04) << 11 == 0x2000, | pin_number(0x0041). */
    TEST_ASSERT_EQUAL_UINT8(0x20u, out[RCP_EP_WAKEUP_REG_SOURCE_BASE]);
    TEST_ASSERT_EQUAL_UINT8(0x41u, out[RCP_EP_WAKEUP_REG_SOURCE_BASE + 1]);

    TEST_ASSERT_EQUAL_UINT16(0x0016u, RCP_EP_WAKEUP_EP_FUNC_LEN);
}

/* REQ-WAKEUP-021 (issue #341 lineage): proves the wire word is a genuine
 * multi-bit mask, not just bit 0 -- two independently-latched sources
 * render as two independently-set bits in the same 16-bit word. */
static void test_render_registers_wup_status_is_a_multi_bit_mask(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        out[RCP_EP_WAKEUP_EP_FUNC_LEN];

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    rcp_ep_wakeup_wup_status_latch_source(&cfg.wup_status, 0);
    rcp_ep_wakeup_wup_status_latch_source(&cfg.wup_status, 3);

    rcp_ep_wakeup_render_registers(&cfg, out);

    /* bit 0 | bit 3 == 0x0009. */
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[RCP_EP_WAKEUP_REG_WUP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x09u, out[RCP_EP_WAKEUP_REG_WUP_STATUS + 1]);
}

static void test_apply_reconfig_writes_ep_status(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[4];

    rcp_ep_wakeup_functional_cfg_init(&cfg);

    payload[0] = 0x00; payload[1] = 0x02; /* address = RCP_EP_WAKEUP_REG_EP_STATUS */
    payload[2] = 0xAB; payload[3] = 0xCD;

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_OK,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.ep_status);
}

static void test_apply_reconfig_wup_status_write_one_clears(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[4];

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    rcp_ep_wakeup_wup_status_latch_source(&cfg.wup_status, 0);
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&cfg.wup_status));

    /* Writing bit 0 == 0 is a no-op -- it must not itself clear or set
     * the latch. */
    payload[0] = 0x00; payload[1] = RCP_EP_WAKEUP_REG_WUP_STATUS & 0xFFu;
    payload[2] = 0x00; payload[3] = 0x00;
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_OK,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&cfg.wup_status));

    /* Writing bit 0 == 1 clears it (write-1-to-clear, TC18 §13.7.2.2). */
    payload[2] = 0x00; payload[3] = 0x01;
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_OK,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&cfg.wup_status));
}

/* REQ-WAKEUP-021: the defining new behavior -- a write naming only SOME
 * bits clears only those sources, leaving every other latched source
 * untouched. The old single-aggregate-bit model could not even express
 * this scenario (there was only ever one bit to clear). */
static void test_apply_reconfig_wup_status_clears_only_the_named_sources(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[4];

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    rcp_ep_wakeup_wup_status_latch_source(&cfg.wup_status, 0);
    rcp_ep_wakeup_wup_status_latch_source(&cfg.wup_status, 3);

    /* Write bit 0 only (0x0001) -- source 3 must remain latched. */
    payload[0] = 0x00; payload[1] = RCP_EP_WAKEUP_REG_WUP_STATUS & 0xFFu;
    payload[2] = 0x00; payload[3] = 0x01;
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_OK,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));

    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_source_is_latched(&cfg.wup_status, 0));
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_source_is_latched(&cfg.wup_status, 3));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&cfg.wup_status));
}

static void test_apply_reconfig_writes_source_slot(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[4];
    uint16_t                       reg;

    rcp_ep_wakeup_functional_cfg_init(&cfg);

    /* Slot 2's own register = RCP_EP_WAKEUP_REG_SOURCE_BASE + 2*2. LOW_LEVEL
     * (0x05) << 11 | pin_number(0x0064). */
    reg = (uint16_t)((RCP_EP_WAKEUP_IO_SRC_LOW_LEVEL << 11) | 0x0064u);
    payload[0] = 0x00;
    payload[1] = (uint8_t)(RCP_EP_WAKEUP_REG_SOURCE_BASE + 2u * RCP_EP_WAKEUP_REG_SOURCE_SPAN);
    payload[2] = (uint8_t)(reg >> 8);
    payload[3] = (uint8_t)(reg & 0xFFu);

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_OK,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_TRUE(cfg.sources[2].enabled);
    TEST_ASSERT_FALSE(cfg.sources[2].active_high);
    TEST_ASSERT_EQUAL_UINT16(0x0064u, cfg.sources[2].pin_number);
    /* Untouched slots. */
    TEST_ASSERT_FALSE(cfg.sources[0].enabled);
    TEST_ASSERT_FALSE(cfg.sources[1].enabled);
}

/* REQ-WAKEUP-022's own remaining, honestly disclosed gap: an edge-
 * triggered or reserved io_src value cannot be represented by this
 * module's own level-only rcp_ep_wakeup_source_asserted() predicate, so
 * a configuration write encoding one leaves enabled/active_high
 * untouched rather than silently misinterpreting it as a level mode --
 * pin_number still updates regardless, since it is always representable. */
static void test_apply_reconfig_edge_triggered_io_src_leaves_enabled_unchanged(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[4];
    uint16_t                       reg;

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    cfg.sources[0].enabled     = true;
    cfg.sources[0].active_high = true;

    /* io_src = 0x01 (rising edge) -- not representable. */
    reg = (uint16_t)((0x01u << 11) | 0x0007u);
    payload[0] = 0x00; payload[1] = (uint8_t)RCP_EP_WAKEUP_REG_SOURCE_BASE;
    payload[2] = (uint8_t)(reg >> 8);
    payload[3] = (uint8_t)(reg & 0xFFu);

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_OK,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_TRUE(cfg.sources[0].enabled);      /* unchanged */
    TEST_ASSERT_TRUE(cfg.sources[0].active_high);  /* unchanged */
    TEST_ASSERT_EQUAL_UINT16(0x0007u, cfg.sources[0].pin_number); /* updated */
}

static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[2 + 2];

    rcp_ep_wakeup_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00) and NR_IO_PINS_MAX (0x01) -- both read-only. */
    payload[0] = 0x00; payload[1] = 0x00;
    payload[2] = 0xFF; payload[3] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_OK,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_WAKEUP_EP_FUNC_LEN];

        rcp_ep_wakeup_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_WAKEUP_EP_FUNC_LEN, out[RCP_EP_WAKEUP_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_WAKEUP_MAX_SOURCES,
                                 out[RCP_EP_WAKEUP_REG_NR_IO_PINS_MAX]);
    }
}

static void test_apply_reconfig_rejects_short_payload(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[2]; /* address only, no data */

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    payload[0] = 0x00; payload[1] = 0x00;

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_ERR_SHORT,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));
}

static void test_apply_reconfig_rejects_out_of_range(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    uint8_t                        payload[3];

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    cfg.sources[7].pin_number = 0x42u;

    /* 0x0016 == RCP_EP_WAKEUP_EP_FUNC_LEN itself -- one past the last
     * valid offset, so even a single-octet write there overruns. */
    payload[0] = 0x00; payload[1] = 0x16;
    payload[2] = 0xAA;

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_wakeup_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0x42u, cfg.sources[7].pin_number); /* untouched */
}

static void test_reconfig_strerror_never_null_and_distinct(void)
{
    const char *ok    = rcp_ep_wakeup_reconfig_strerror(RCP_EP_WAKEUP_RECONFIG_OK);
    const char *short_ = rcp_ep_wakeup_reconfig_strerror(RCP_EP_WAKEUP_RECONFIG_ERR_SHORT);
    const char *range  = rcp_ep_wakeup_reconfig_strerror(RCP_EP_WAKEUP_RECONFIG_ERR_OUT_OF_RANGE);
    const char *unknown = rcp_ep_wakeup_reconfig_strerror((rcp_ep_wakeup_reconfig_errc_t)99);

    TEST_ASSERT_NOT_NULL(ok);
    TEST_ASSERT_NOT_NULL(short_);
    TEST_ASSERT_NOT_NULL(range);
    TEST_ASSERT_NOT_NULL(unknown);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(ok, short_));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(short_, range));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(range, unknown));
}

static void test_encode_reconfig_request_round_trip(void)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    const uint8_t                data[2] = {0xDE, 0xAD};
    rcp_bytes_t                  frame;

    frame = rcp_ep_wakeup_encode_reconfig_request(BUS_ID, RCP_EP_WAKEUP_REG_EP_STATUS,
                                                   data, sizeof(data), 3u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
        rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT(4u, payload_len); /* address(2) + data(2) */
    TEST_ASSERT_EQUAL_HEX8(0x00u, payload[0]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_WAKEUP_REG_EP_STATUS, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0xDEu, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0xADu, payload[3]);
    TEST_ASSERT_EQUAL_UINT8(0x7u, hdr.evt); /* evt[2:0] == 111b */
    rcp_bytes_free(&frame);
}

static void test_encode_reconfig_request_rejects_empty_data(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_reconfig_request(BUS_ID, 0u, NULL, 0u, 1u);

    TEST_ASSERT_NULL(frame.data);
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
    RUN_TEST(test_wup_status_latch_source_is_independent_per_index);
    RUN_TEST(test_wup_status_clear_source_leaves_other_sources_latched);
    RUN_TEST(test_wup_status_out_of_range_source_index_is_a_no_op);

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

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_render_registers_wup_status_is_a_multi_bit_mask);
    RUN_TEST(test_apply_reconfig_writes_ep_status);
    RUN_TEST(test_apply_reconfig_wup_status_write_one_clears);
    RUN_TEST(test_apply_reconfig_wup_status_clears_only_the_named_sources);
    RUN_TEST(test_apply_reconfig_writes_source_slot);
    RUN_TEST(test_apply_reconfig_edge_triggered_io_src_leaves_enabled_unchanged);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_rejects_short_payload);
    RUN_TEST(test_apply_reconfig_rejects_out_of_range);
    RUN_TEST(test_reconfig_strerror_never_null_and_distinct);
    RUN_TEST(test_encode_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);

    return UNITY_END();
}
