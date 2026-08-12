/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ADC-031
//cfusa:test REQ-ADC-032
//cfusa:test REQ-ADC-033
//cfusa:test REQ-ADC-034
//cfusa:test REQ-ADC-035
//cfusa:test REQ-ADC-036
//cfusa:test REQ-ADC-037
//cfusa:test REQ-CANEP-028
//cfusa:test REQ-CANEP-029
//cfusa:test REQ-CANEP-030
//cfusa:test REQ-CANEP-031
//cfusa:test REQ-CANEP-032
//cfusa:test REQ-ISELED-025
//cfusa:test REQ-ISELED-026
//cfusa:test REQ-ISELED-027
//cfusa:test REQ-ISELED-028
//cfusa:test REQ-ISELED-029
//cfusa:test REQ-LINEP-023
//cfusa:test REQ-LINEP-024
//cfusa:test REQ-MDIO-020
//cfusa:test REQ-MDIO-021
//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-023
//cfusa:test REQ-UART-032
//cfusa:test REQ-UART-033
//cfusa:test REQ-UART-034
//cfusa:test REQ-UART-035
//cfusa:test REQ-UART-036
//cfusa:test REQ-UART-037
//cfusa:test REQ-UART-038

/*
 * test_tc18_gaps_ep2.c -- spec-literal conformance-and-deviation suite for
 * the TC18 clauses catalogued in the v0.105.0 requirements-corpus
 * completeness pass covering the UART (§13.7.8), ADC (§13.7.9), LIN
 * (§13.7.10), CAN (§13.7.11), ISELED (§13.7.12) and MDIO (§13.7.13)
 * endpoint types.
 *
 * Two kinds of test live here, and they are deliberately not mixed up:
 *
 *   - For a requirement whose catalogued status is "implemented", the test
 *     asserts the specified behaviour literally, against constants derived
 *     from the cited clause (e.g. the CAN FrameFormat code assignment of
 *     Table 54, the ADC half-the-read_size value-count rule) rather than
 *     round-tripping the implementation against itself.
 *
 *   - For a requirement whose status is "partial" or "not-implemented",
 *     the test PINS THE DEVIATION: it asserts the current, real, observable
 *     behaviour of this codebase, and the comment above each such assertion
 *     names the TC18 clause that is NOT met and what a conforming
 *     implementation would do instead. These tests are expected to be
 *     CHANGED, not merely extended, if and when the gap is closed.
 *
 * A recurring deviation-pinning technique below is the writable-footprint
 * count: every rcp_ep_*_functional_cfg_init() memsets its whole struct, so
 * a byte-for-byte snapshot taken after init and compared after driving
 * every mutator the module offers (with values whose every octet is
 * non-zero) yields exactly the number of octets any client can ever reach
 * through this endpoint's functional-configuration surface. That count is
 * the direct, portable evidence that a register row the cited table fixes
 * -- an *_ep_len, an *_ep_status, a base clock, a divider -- has no
 * representation here at all.
 */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_adc.h>
#include <rcp/ep_can.h>
#include <rcp/ep_iseled.h>
#include <rcp/ep_lin.h>
#include <rcp/ep_mdio.h>
#include <rcp/ep_pwm.h>
#include <rcp/ep_uart.h>
#include <rcp/lifecycle.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>

#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Shared helpers ────────────────────────────────────────────────────────── */

/* Number of octets that differ between two same-length raw views of a
 * functional-config block -- see the file header's writable-footprint note. */
static size_t changed_octets(const uint8_t *before, const uint8_t *after, size_t n)
{
    size_t i;
    size_t changed = 0u;

    for (i = 0u; i < n; i++) {
        if (before[i] != after[i]) changed++;
    }
    return changed;
}

/* An authorized functional-config writer in a state that permits the
 * write. As of REQ-LIFECYCLE-030/036, RCP_LIFECYCLE_HW_CONFIGURED no
 * longer accepts any writer unconditionally -- it requires the same
 * root-client/owning-stream authorization RCP_CONFIGURED already did
 * (lifecycle.h) -- so this helper explicitly grants via_owning_stream to
 * stay authorized; every one of this file's tests uses it purely to get
 * past the writability gate on the way to testing something else
 * entirely (block layout, register width, etc.), not to exercise
 * lifecycle authorization policy itself. */
static rcp_lifecycle_writer_ctx_t any_writer(void)
{
    rcp_lifecycle_writer_ctx_t w = {0};

    w.via_owning_stream = true;
    return w;
}

/* ── UART (§13.7.8) ────────────────────────────────────────────────────────── */

/* FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-UART-038):
 * renamed from `..._has_no_len_or_status_register` -- the block now has
 * both, plus the four previously-missing R/W fields, all reachable via
 * the new rcp_ep_uart_apply_reconfig()/_render_registers() register-block
 * path (REQ-UART-039/040, tests/test_ep_uart.c). This test's own "13
 * octets changed" assertion below still holds and is still meaningful:
 * it confirms the four *legacy*, per-field setters this module already
 * had (set_baud_rate/_frame_format/_rx_buffer_size/_timeout) remain
 * narrowly scoped to exactly the fields they always touched -- the new
 * fields are deliberately NOT wired into those setters (matching every
 * other endpoint type's own precedent: PWM_OUT's/GPIO's/SPI's/I2C's own
 * register-block-only fields have no dedicated named setter either, only
 * the generic §12.7.1 write path). */
static void test_uart_functional_block_now_has_full_register_coverage(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    uint8_t                      before[sizeof(rcp_ep_uart_functional_cfg_t)];
    rcp_lifecycle_writer_ctx_t   w = any_writer();

    TEST_ASSERT_EQUAL_size_t(1u, sizeof(bool)); /* footprint counting precondition */
    rcp_ep_uart_functional_cfg_init(&cfg);
    memcpy(before, &cfg, sizeof(before));

    TEST_ASSERT_TRUE(rcp_ep_uart_set_baud_rate(&cfg, 0x11223344u,
                                               RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_uart_set_frame_format(&cfg, 7u, RCP_EP_UART_PARITY_EVEN,
                                                  RCP_EP_UART_STOP_BITS_TWO,
                                                  RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_uart_set_rx_buffer_size(&cfg, 0x99AAu,
                                                    RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_uart_set_timeout(&cfg, 0x55667788u,
                                             RCP_LIFECYCLE_HW_CONFIGURED, w));

    /* TC18 §13.7.8.2 Table 48's whole register block -- uart_ep_len
     * (0x0000, R), a reserved octet (0x0001, R), uart_ep_status (0x0004,
     * R/W), uart_rts_enable/uart_cts_enable/uart_half_duplex (0x0009.2-4)
     * and uart_trail (0x000C) included -- is now reachable via
     * rcp_ep_uart_render_registers()/_apply_reconfig() (REQ-UART-039/040,
     * tests/test_ep_uart.c's own dedicated register-block test section).
     * c-RCP's struct is still a plain C struct rather than a wire image
     * (as every other endpoint type's own functional-config struct also
     * is), but that struct now carries every one of Table 48's fields --
     * confirmed positively here rather than by the old test's absence
     * check. */
    TEST_ASSERT_EQUAL_size_t(0u, offsetof(rcp_ep_uart_functional_cfg_t, common));

    /* The four *legacy* setters this test exercises above remain
     * narrowly scoped to exactly the 13 octets they always touched --
     * the fields the new register-block path added (ep_status,
     * baud_rate_kbps, rts_enable, cts_enable, half_duplex,
     * wire_timeout_bit_times, trail) are untouched by them, by design. */
    TEST_ASSERT_EQUAL_size_t(13u, changed_octets(before, (const uint8_t *)&cfg,
                                                 sizeof(before)));

    /* Positive assertion: the new fields exist and are independently
     * writable via the register-block path (exercised end-to-end in
     * tests/test_ep_uart.c; here we just confirm the struct has room). */
    {
        rcp_ep_uart_functional_cfg_t cfg2;

        rcp_ep_uart_functional_cfg_init(&cfg2);
        cfg2.ep_status              = 0x1234u;
        cfg2.baud_rate_kbps         = 0x5678u;
        cfg2.rts_enable             = true;
        cfg2.cts_enable             = true;
        cfg2.half_duplex            = true;
        cfg2.wire_timeout_bit_times = 0x42u;
        cfg2.trail                  = 0x24u;
        TEST_ASSERT_EQUAL_UINT16(0x1234u, cfg2.ep_status);
        TEST_ASSERT_EQUAL_UINT16(0x5678u, cfg2.baud_rate_kbps);
        TEST_ASSERT_TRUE(cfg2.rts_enable);
        TEST_ASSERT_TRUE(cfg2.cts_enable);
        TEST_ASSERT_TRUE(cfg2.half_duplex);
        TEST_ASSERT_EQUAL_UINT8(0x42u, cfg2.wire_timeout_bit_times);
        TEST_ASSERT_EQUAL_UINT8(0x24u, cfg2.trail);
    }
}

static void test_uart_rx_fifo_size_bounds_nothing_at_all(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t   w = any_writer();
    uint8_t                      rx[16];
    const uint8_t               *out_rx = NULL;
    size_t                       out_len = 0u;
    bool                         timed = true;
    uint64_t                     ts = 1u;
    uint8_t                      tn = 0u;
    rcp_bytes_t                  f;

    rcp_ep_uart_functional_cfg_init(&cfg);
    TEST_ASSERT_TRUE(rcp_ep_uart_set_rx_buffer_size(&cfg, 4u, RCP_LIFECYCLE_HW_CONFIGURED, w));
    memset(rx, 0xA5, sizeof(rx));

    /* REQ-UART-032 DEVIATION PIN (still open): TC18 §13.7.8.1 requires an
     * RX FIFO overflow to be flagged in uart_ep_status -- TC18 never
     * defines which bit of that 16-bit register carries the flag (the same
     * "_ep_status has no printed bit layout" spec-silence pattern found
     * across CAN/WakeUp/several other endpoint types' own status
     * registers), so this module has no bit position to wire to without
     * inventing one unilaterally. A response four times the configured
     * FIFO size still encodes happily, in exactly one unfragmented frame,
     * with no overflow signal on any surface -- ep_rx_buffer_size is inert
     * for this purpose (REQ-UART-033's own read-COMPLETION arbitration,
     * covered by the deviation this test used to also pin, is resolved
     * separately below by rcp_ep_uart_read_completion_decision()). */
    f = rcp_ep_uart_encode_read_response(0x11u, rx, sizeof(rx), 3u, false, 0u);
    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL_size_t(1u, rcp_ep_uart_read_response_fragment_count(sizeof(rx), 64u));
    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_OK,
                          rcp_ep_uart_decode_read_response(f.data, f.len, 0x11u, &out_rx,
                                                           &out_len, &timed, &ts, &tn));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), out_len);
    TEST_ASSERT_GREATER_THAN_UINT16(cfg.ep_rx_buffer_size, (uint16_t)out_len);
    rcp_bytes_free(&f);
}

/* REQ-UART-033 IMPLEMENTED (issue #336): TC18 §13.7.8.1's own three
 * read-completion triggers, arbitrated by rcp_ep_uart_read_completion_
 * decision() -- see that function's own doc comment for the exact rule. */
static void test_uart_read_completion_decision(void)
{
    /* read_size satisfied before either the fifo fills to capacity or the
     * timeout expires -- a normal (non-fragmented) response. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_READ_RESPOND_NORMAL,
                          rcp_ep_uart_read_completion_decision(4u, 4u, 0u, 250u, 8u));

    /* Not yet satisfied, timeout not yet expired, and read_size fits
     * within the fifo's own capacity -- still waiting. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_READ_NOT_YET_COMPLETE,
                          rcp_ep_uart_read_completion_decision(2u, 4u, 100u, 250u, 8u));

    /* uart_timeout expires with fewer bytes than requested still in the
     * fifo -- a short read, emitted as a normal (non-fragmented) response
     * carrying only what's actually available. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_READ_RESPOND_NORMAL,
                          rcp_ep_uart_read_completion_decision(2u, 4u, 250u, 250u, 8u));

    /* read_size exceeds the fifo's own capacity, and the fifo has filled
     * to that capacity -- fragmentation is required. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_READ_RESPOND_FRAGMENTED,
                          rcp_ep_uart_read_completion_decision(8u, 20u, 0u, 250u, 8u));

    /* read_size exceeds the fifo's own capacity, but the fifo hasn't
     * filled yet, and the timeout hasn't expired -- still waiting, not
     * yet fragmented. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_READ_NOT_YET_COMPLETE,
                          rcp_ep_uart_read_completion_decision(3u, 20u, 50u, 250u, 8u));

    /* A zero-length timeout completes immediately, matching "as soon
     * as... uart_timeout has expired" read literally for uart_timeout_ms
     * == 0. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_READ_RESPOND_NORMAL,
                          rcp_ep_uart_read_completion_decision(0u, 4u, 0u, 0u, 8u));
}

/* RESOLVED (v0.110.0/v0.111.0) -- this test previously pinned a real gap:
 * c-RCP had no UART compound-wait comparison predicate at all (the only
 * one reachable was ep_lin.h's own, which is itself endpoint-agnostic and
 * knows nothing of a UART fifo's bound). acf.h's rcp_acf_compound_wait_match()
 * now provides that comparison surface universally, per TC18 §13.5.1, for
 * every endpoint type including UART (wired into real dispatch via
 * server.c's rcp_server_tick_ctx_t.current_status). §13.7.8.1's own
 * "compared length bounded above by uart_rx_fifo_size" falls directly out
 * of the shared §13.5.1 length rule once a real fifo's contents (which
 * physically can never exceed uart_rx_fifo_size) are supplied as
 * current_status: an expected byte_msg_payload longer than the fifo could
 * ever hold can never match (status shorter than payload never matches,
 * TC18's own rule) -- exactly the bound §13.7.8.1 describes, with no
 * UART-specific logic required. */
static void test_uart_compound_wait_now_resolved_via_generic_primitive(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t   w = any_writer();
    const uint8_t                expected[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    /* The fifo can never hold more than ep_rx_buffer_size(4) bytes at
     * once -- a real UART fifo, capped by hardware, not by this test. */
    const uint8_t                fifo_contents[4] = {1, 2, 3, 4};

    rcp_ep_uart_functional_cfg_init(&cfg);
    TEST_ASSERT_TRUE(rcp_ep_uart_set_rx_buffer_size(&cfg, 4u, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_EQUAL_UINT16(4u, cfg.ep_rx_buffer_size);

    /* An 8-byte expectation can never be satisfied by a fifo that only
     * ever holds 4 -- correctly bounded, not a silent false match. */
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x0u, expected, sizeof(expected),
                                                   fifo_contents, sizeof(fifo_contents)));

    /* A 4-byte expectation matching the fifo's own current contents
     * exactly does succeed -- the real, working comparison surface. */
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x0u, fifo_contents, sizeof(fifo_contents),
                                                  fifo_contents, sizeof(fifo_contents)));
}

/* FIXED 2026-08-12 (issue #201, REQ-UART-034): TC18 §13.7.8.1 contemplates
 * a read_size larger than the RX FIFO (driving a fragmented response),
 * and acf.h's read_size_or_segment_num is the wire's full 12-bit field
 * (0..4095). c-RCP's UART read codec previously narrowed read_size to
 * one octet, silently truncating a conforming peer's request above 255;
 * *out_read_size is now the same 12-bit-wide uint16_t and round-trips
 * a value above 255 exactly. */
static void test_uart_read_size_above_one_octet_round_trips(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 f;
    uint16_t                    read_size = 0xFFFFu;
    uint8_t                     tn        = 0u;

    hdr.byte_bus_id              = 0x21u;
    hdr.op                       = RCP_ACF_OP_READ;
    hdr.transaction_num          = 9u;
    hdr.read_size_or_segment_num = 1024u; /* well within acf.h's 12-bit field */

    f = rcp_acf_encode_abb(&hdr, NULL, 0u);
    TEST_ASSERT_NOT_NULL(f.data);

    TEST_ASSERT_EQUAL_INT(RCP_EP_UART_OK,
                          rcp_ep_uart_decode_read_request(f.data, f.len, 0x21u,
                                                          &read_size, &tn));
    TEST_ASSERT_EQUAL_UINT16(1024u, read_size);
    TEST_ASSERT_EQUAL_UINT8(9u, tn);
    rcp_bytes_free(&f);
}

static void test_uart_register_units_diverge_from_table_48(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t   w = any_writer();

    rcp_ep_uart_functional_cfg_init(&cfg);

    /* DEVIATION -- TC18 §13.7.8.2 Table 48 makes uart_baud_rate a 16-bit
     * R/W register in kbit/s. c-RCP stores a raw uint32_t that holds a
     * value no 16-bit kbit/s register could represent, so the two disagree
     * numerically on this register. */
    TEST_ASSERT_TRUE(rcp_ep_uart_set_baud_rate(&cfg, 115200u, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_EQUAL_UINT32(115200u, cfg.baud_rate);
    TEST_ASSERT_GREATER_THAN_UINT32(65535u, cfg.baud_rate);

    /* DEVIATION -- Table 48 counts uart_stop_bits in HALF stop bits, so 1.5
     * stop bits is the legal value 3. c-RCP's enum has exactly two members,
     * 0 and 1, meaning one and two whole stop bits; 3 passes the setter
     * unvalidated and is stored, but names no framing this module defines,
     * so 1.5-stop-bit framing is inexpressible. */
    TEST_ASSERT_EQUAL_INT(0, (int)RCP_EP_UART_STOP_BITS_ONE);
    TEST_ASSERT_EQUAL_INT(1, (int)RCP_EP_UART_STOP_BITS_TWO);
    TEST_ASSERT_TRUE(rcp_ep_uart_set_frame_format(&cfg, 8u, RCP_EP_UART_PARITY_NONE,
                                                  (rcp_ep_uart_stop_bits_t)3,
                                                  RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_EQUAL_UINT8(3u, cfg.stop_bits);

    /* DEVIATION -- Table 48 expresses uart_timeout as an 8-bit count of BIT
     * TIMES measured from the last received stop bit. c-RCP stores a 32-bit
     * millisecond value with no measurement origin, unrelated to the baud
     * rate: changing the baud rate leaves the stored timeout untouched,
     * where a bit-time register's real duration would move with it. */
    TEST_ASSERT_TRUE(rcp_ep_uart_set_timeout(&cfg, 250u, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_uart_set_baud_rate(&cfg, 9600u, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_EQUAL_UINT32(250u, cfg.uart_timeout_ms);
}

/* ── ADC (§13.7.9) ─────────────────────────────────────────────────────────── */

static void test_adc_value_width_and_named_analog_input_signal(void)
{
    /* IMPLEMENTED half of the clause -- TC18 §13.7.9.1 limits an ADC
     * endpoint to 16-bit resolution, and a response carries as many values
     * as half the request's read_size. */
    TEST_ASSERT_EQUAL_size_t(2u, RCP_EP_ADC_VALUE_LEN);
    TEST_ASSERT_EQUAL_size_t(4u, rcp_ep_adc_response_value_count(8u));
    TEST_ASSERT_EQUAL_size_t(0u, rcp_ep_adc_response_value_count(9u));

    /* REQ-ADC-032 IMPLEMENTED (catalog text corrected, no code change
     * needed): TC18 §13.7.9.1 also requires that each ADC endpoint serve
     * exactly one channel and have an analog input pin selected for it.
     * regmap.h's named-signal index -- the only way any endpoint type
     * binds a signal to a hardware pin -- defines RCP_REGMAP_SIGNAL_
     * ADC_IN (REQ-RMAP-044), with EP_Signal_Nr 0 (asserted below), so an
     * ADC endpoint's analog input binds via an ordinary hw_pin_map entry
     * (hw_ep_nr = the ADC endpoint's own number, hw_ep_pin_nr = 0) --
     * the same generic mechanism every other endpoint type already uses,
     * no ADC-specific code required. The one-channel rule is structurally
     * guaranteed: Table 23 enumerates exactly one ADC-relevant signal, so
     * hw_ep_pin_nr for an ADC endpoint's own binding can only ever be 0 --
     * there is no second channel number the addressing scheme could even
     * express. Whether a real deployment's own hw_pin_map actually
     * populates that row is caller/config-time data, the same as every
     * other endpoint type's own pin binding -- not a decode/encode gap. */
    TEST_ASSERT_EQUAL_STRING("ADC_IN", rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_ADC_IN));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_regmap_named_signal_ep_signal_nr(RCP_REGMAP_SIGNAL_ADC_IN));
}

/* REQ-ADC-035/036 RESOLVED (stale test found and fixed alongside REQ-ADC-032/
 * -033's own catalog corrections, 2026-08-12): this test used to pin exactly
 * the gap REQ-ADC-035/-036's own earlier batch (2026-08-11) already closed --
 * it kept asserting the pre-fix struct footprint (5 octets) and a comment
 * claiming none of Table 51's clock/status/interval registers existed, when
 * rcp_ep_adc_functional_cfg_t has carried ep_status/base_clk_divider/
 * sample_interval/resolution/trigger_min/trigger_max (9 more octets) and a
 * real rcp_ep_adc_render_registers()/_apply_reconfig() round-trip ever since
 * -- already thoroughly covered by test_ep_adc.c's own dedicated register-
 * block tests. This test now positively confirms the fields exist and are
 * distinct from the three original sampling-pipeline fields, rather than
 * re-duplicating test_ep_adc.c's own full round-trip coverage. */
static void test_adc_functional_cfg_has_clock_status_and_interval_fields(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     before[sizeof(rcp_ep_adc_functional_cfg_t)];
    rcp_lifecycle_writer_ctx_t  w = any_writer();

    rcp_ep_adc_functional_cfg_init(&cfg);
    memcpy(before, &cfg, sizeof(before));

    TEST_ASSERT_TRUE(rcp_ep_adc_set_samples_per_avg_interval(&cfg, 0x1122u,
                                                             RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_adc_set_avg_intervals_per_request(&cfg, 0x3344u,
                                                              RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_adc_set_combine_avg_values(&cfg, 0x55u,
                                                       RCP_LIFECYCLE_HW_CONFIGURED, w));
    /* No dedicated setters exist for these (matching every other
     * register-block-only field's own convention, REQ-ADC-036's own text) --
     * set directly, the same way rcp_ep_adc_apply_reconfig() would after
     * parsing a real wire write. */
    cfg.ep_status        = 0x6677u;
    cfg.base_clk_divider = 0x88u;
    cfg.sample_interval  = 0x99u;
    cfg.resolution        = 12u;
    cfg.trigger_min       = 0xAABBu;
    cfg.trigger_max       = 0xCCDDu;

    TEST_ASSERT_EQUAL_size_t(0u, offsetof(rcp_ep_adc_functional_cfg_t, common));
    /* 5 octets from the three original sampling-pipeline fields, plus 9
     * more from the six fields REQ-ADC-035/036 added -- the full 14-octet
     * footprint this struct actually carries today. */
    TEST_ASSERT_EQUAL_size_t(14u, changed_octets(before, (const uint8_t *)&cfg,
                                                 sizeof(before)));
}

static void test_adc_inter_sample_spacing_is_unconstrained(void)
{
    rcp_ep_adc_sample_t    even[3];
    rcp_ep_adc_sample_t    ragged[3];
    rcp_ep_adc_avg_value_t a;
    rcp_ep_adc_avg_value_t b;
    size_t                 i;

    for (i = 0u; i < 3u; i++) {
        even[i].value   = 100u;
        ragged[i].value = 100u;
    }
    even[0].timestamp   = 0u;      /* a uniform 1000-tick cadence */
    even[1].timestamp   = 1000u;
    even[2].timestamp   = 2000u;
    ragged[0].timestamp = 0u;      /* wildly non-uniform spacing */
    ragged[1].timestamp = 5u;
    ragged[2].timestamp = 2000u;

    a = rcp_ep_adc_average_interval(even, 3u);
    b = rcp_ep_adc_average_interval(ragged, 3u);

    /* REQ-ADC-033 DEVIATION PIN (still genuinely open, not a routine gap):
     * TC18 §13.7.9.1 requires successive samples to be exactly one
     * adc_sample_interval apart, that interval being a multiple of
     * adc_base_clk scaled by adc_base_clk_divider, whenever more than one
     * sample is taken. adc_sample_interval and adc_base_clk_divider are now
     * real, wire-reachable config fields (REQ-ADC-035/036, see
     * test_adc_functional_cfg_has_clock_status_and_interval_fields above),
     * but adc_base_clk itself is deliberately never modelled as a real
     * value (always renders 0, the same honest "no real clock source"
     * stance ep_gpio.h's/ep_i2c.h's/ep_lin.h's own base_clk fields commit
     * to) -- so this module has no way to convert a cycle count into real
     * wall-clock spacing, and rcp_ep_adc_average_interval() still consumes
     * caller-supplied samples with no timing validation whatsoever: the two
     * intervals below have completely different sample spacing and are
     * nevertheless indistinguishable at this module's only output.
     * Enforcing this would mean inventing a clock model this codebase
     * deliberately doesn't have for any endpoint type, not a field-wiring
     * fix. */
    TEST_ASSERT_EQUAL_UINT16(100u, a.value);
    TEST_ASSERT_EQUAL_UINT16(a.value, b.value);
    TEST_ASSERT_EQUAL_UINT64(2000u, a.timestamp);
    TEST_ASSERT_EQUAL_UINT64(a.timestamp, b.timestamp);
}

static void test_adc_has_no_trigger_outputs_and_no_retained_average(void)
{
    rcp_ep_adc_sample_t    crossing[2];
    rcp_ep_adc_sample_t    flat[2];
    rcp_ep_adc_avg_value_t crossed;
    rcp_ep_adc_avg_value_t steady;
    rcp_ep_adc_avg_value_t empty;

    crossing[0].value = 100u; /* below a nominal adc_trigger_min of 150 */
    crossing[1].value = 200u; /* and back above it -- two threshold crossings */
    flat[0].value     = 150u; /* never crosses anything */
    flat[1].value     = 150u;
    crossing[0].timestamp = 10u;
    crossing[1].timestamp = 20u;
    flat[0].timestamp     = 10u;
    flat[1].timestamp     = 20u;

    crossed = rcp_ep_adc_average_interval(crossing, 2u);
    steady  = rcp_ep_adc_average_interval(flat, 2u);
    empty   = rcp_ep_adc_average_interval(NULL, 0u);

    /* DEVIATION -- TC18 §13.7.9.1 Table 50 enumerates five ADC trigger
     * outputs: falling below / rising above adc_trigger_min (0/1), falling
     * below / rising above adc_trigger_max (2/3), and measurement-interval
     * completion (4). c-RCP defines no trigger enum, no threshold registers
     * and no evaluation predicate, so a sample stream that crosses a
     * threshold twice is byte-for-byte indistinguishable from one that
     * never moves. Trigger 4's absence is why the spec's own cyclic-ADC
     * pattern -- a trigger request fired by the ADC finishing a measurement
     * -- cannot be built on this implementation. */
    TEST_ASSERT_EQUAL_UINT16(150u, crossed.value);
    TEST_ASSERT_EQUAL_UINT16(crossed.value, steady.value);
    TEST_ASSERT_EQUAL_UINT64(crossed.timestamp, steady.timestamp);

    /* DEVIATION -- §13.7.9.1 also requires sampling only while a request
     * executes, and requires a compound wait to compare against the LAST
     * ACQUIRED average without sampling. This pipeline is stateless: with
     * no samples it reports the no-signal sentinel rather than any retained
     * previous average, so there is nothing for a compound wait to compare
     * against and no request-execution state to gate sampling on. */
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, empty.value);
    TEST_ASSERT_EQUAL_UINT64(0u, rcp_ep_adc_capture_moment_timestamp(NULL, 0u));

    /* DEVIATION PIN (REQ-ADC-037, not implemented): TC18 §13.7.9.2 states
     * three cadence cases comparing adc_combine_avg_values against
     * adc_avg_intervals_per_request (multi-request-to-one-response,
     * one-to-one, one-to-multi-response). rcp_ep_adc_collect_response_values()
     * takes only avg_count/value_count as plain parameters -- neither
     * adc_combine_avg_values nor adc_avg_intervals_per_request is anywhere
     * in its signature -- and simply packs min(avg_count, value_count)
     * values regardless of what either config field says, proving this
     * pure packer has no cadence awareness at all; nothing else in this
     * module does either. */
    {
        rcp_ep_adc_avg_value_t five[5] = {0};
        uint16_t                packed[3];
        size_t                  n;

        n = rcp_ep_adc_collect_response_values(five, 5u, packed, 3u);
        TEST_ASSERT_EQUAL_size_t(3u, n); /* min(5, 3) -- min() only, no cadence logic */
    }
}

/* ── LIN (§13.7.10) ────────────────────────────────────────────────────────── */

/* FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-LINEP-024):
 * renamed from `..._and_block_lacks_registers` -- Table 52's whole
 * register block, including lin_ep_len/lin_base_clk/lin_ep_status, is
 * now reachable via rcp_ep_lin_render_registers()/_apply_reconfig()
 * (REQ-LINEP-028/029, tests/test_ep_lin.c's own dedicated
 * register-block test section). The trailing-time gap below is ALSO
 * FIXED 2026-08-11 (Phase 5e batch 2, issue #201, REQ-LINEP-023) --
 * further renamed accordingly. */
static void test_lin_trigger_now_honours_trailing_time_and_block_has_registers(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    uint8_t                     before[sizeof(rcp_ep_lin_functional_cfg_t)];
    rcp_lifecycle_writer_ctx_t  w = any_writer();

    TEST_ASSERT_EQUAL_size_t(1u, sizeof(bool)); /* footprint counting precondition */
    rcp_ep_lin_functional_cfg_init(&cfg);
    memcpy(before, &cfg, sizeof(before));

    TEST_ASSERT_TRUE(rcp_ep_lin_set_clk_divider(&cfg, 0x11223344u,
                                                RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_lin_set_trigger(&cfg, RCP_EP_LIN_TRIGGER_TX_DONE,
                                            RCP_LIFECYCLE_HW_CONFIGURED, w));

    /* FIXED -- TC18 §13.7.10.1 requires the LIN transmission-done
     * trigger to fire only once BOTH the transmission has been finalized
     * AND the configured trailing time has expired. rcp_ep_lin_trigger_fires()
     * now takes trailing_time_expired as a second, caller-classified boolean
     * input and ANDs it with tx_done_event -- Table 52 defines no dedicated
     * wire register for "the configured trailing time" (like this endpoint
     * type's own trigger concept as a whole, this remains this module's own
     * original, non-wire-serialized design; see ep_lin.h's own file
     * header). */
    TEST_ASSERT_TRUE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, true, true));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, true, false));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, false, false));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_NONE, true, true));

    /* TC18 §13.7.10.2 Table 52's whole register block -- lin_ep_len
     * (0x0000, R), a reserved octet (0x0001, R), lin_base_clk (0x0004,
     * R) and lin_ep_status (0x0006, R/W) alongside lin_clk_divider
     * (0x0008) -- is now reachable via
     * rcp_ep_lin_render_registers()/_apply_reconfig() (REQ-LINEP-028/029,
     * tests/test_ep_lin.c's own dedicated register-block test section).
     * The two *legacy* setters exercised above remain narrowly scoped to
     * exactly the 5 octets they always touched (lin_clk_divider(4) +
     * trigger(1)) -- the register-block's own new fields (ep_status,
     * wire_clk_divider) are untouched by them, by design, matching every
     * other endpoint type's own precedent (only the generic §12.7.1
     * write path reaches them, not a dedicated named setter). */
    TEST_ASSERT_EQUAL_size_t(0u, offsetof(rcp_ep_lin_functional_cfg_t, common));
    TEST_ASSERT_EQUAL_size_t(5u, changed_octets(before, (const uint8_t *)&cfg,
                                                sizeof(before)));

    /* Positive assertion: the new fields exist and are independently
     * writable via the register-block path. */
    {
        rcp_ep_lin_functional_cfg_t cfg2;

        rcp_ep_lin_functional_cfg_init(&cfg2);
        cfg2.ep_status        = 0x1234u;
        cfg2.wire_clk_divider = 0x42u;
        TEST_ASSERT_EQUAL_UINT16(0x1234u, cfg2.ep_status);
        TEST_ASSERT_EQUAL_UINT8(0x42u, cfg2.wire_clk_divider);
    }
}

/* ── CAN (§13.7.11) ────────────────────────────────────────────────────────── */

static void test_can_frame_format_values_match_table_54(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    /* Leading quadlet's top 3 bits = 110b (6), Table 54's first reserved
     * code -- the low 29 bits (arbitration_id) are irrelevant here. */
    const uint8_t                body[4] = {0xC0u, 0, 0, 0};
    rcp_bytes_t                 f;
    rcp_ep_can_frame_format_t   fmt = RCP_EP_CAN_FRAME_CBFF;
    uint32_t                    id  = 0u;
    rcp_ep_can_xl_header_t      xl  = {0};
    const uint8_t              *data = NULL;
    size_t                      data_len = 0u;
    uint8_t                     tn = 0u;

    /* IMPLEMENTED -- TC18 §13.7.11.3 Table 54 assigns the FrameFormat
     * selector codes 0..5 in this exact order, leaving 6 and 7 reserved. */
    TEST_ASSERT_EQUAL_INT(0, (int)RCP_EP_CAN_FRAME_CBFF);
    TEST_ASSERT_EQUAL_INT(1, (int)RCP_EP_CAN_FRAME_CEFF);
    TEST_ASSERT_EQUAL_INT(2, (int)RCP_EP_CAN_FRAME_FBFF);
    TEST_ASSERT_EQUAL_INT(3, (int)RCP_EP_CAN_FRAME_FEFF);
    TEST_ASSERT_EQUAL_INT(4, (int)RCP_EP_CAN_FRAME_XL_CLASSICAL_PL);
    TEST_ASSERT_EQUAL_INT(5, (int)RCP_EP_CAN_FRAME_XL_NEW_PL);
    TEST_ASSERT_TRUE(rcp_ep_can_frame_format_valid(5u));
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_valid(6u));
    TEST_ASSERT_FALSE(rcp_ep_can_frame_format_valid(7u));

    /* FIXED (v0.109.0) -- the selector rides the payload's own leading
     * quadlet (TC18 §13.7.11.3 Figure 39), not evt[2:0] (an earlier
     * revision's own design choice, not TC18's); evt is left at its
     * ordinary Table 30 Row-2 "plain request" value (0), and a reserved
     * FrameFormat code in the payload is rejected by the frame decoder. */
    hdr.byte_bus_id     = 0x31u;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0u;
    hdr.transaction_num = 2u;
    f = rcp_acf_encode_abb(&hdr, body, sizeof(body));
    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL_INT(RCP_EP_CAN_ERR_BAD_FRAME_FORMAT,
                          rcp_ep_can_decode_frame_request(f.data, f.len, 0x31u, &fmt, &id,
                                                          &xl, &data, &data_len, &tn));
    rcp_bytes_free(&f);
}

static void test_can_base_identifier_is_right_aligned_and_data_only(void)
{
    const uint8_t               payload_in[2] = {0xDEu, 0xADu};
    rcp_bytes_t                 f;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload  = NULL;
    size_t                      pay_len  = 0u;

    /* IMPLEMENTED -- TC18 §13.7.11.3: an 11-bit CAN identifier is
     * right-aligned in the CAN ID field. The identifier prefix is a fixed
     * big-endian 4-octet field, so the widest base-format identifier
     * (0x7FF) occupies only the low 11 bits and the top 21 bits read
     * zero. */
    f = rcp_ep_can_encode_frame_request(0x31u, RCP_EP_CAN_FRAME_CBFF, 0x7FFu, NULL,
                                        payload_in, sizeof(payload_in), 4u);
    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK, rcp_acf_decode_abb(f.data, f.len, &hdr, &payload,
                                                         &pay_len));
    TEST_ASSERT_EQUAL_size_t(4u + sizeof(payload_in), pay_len);
    TEST_ASSERT_EQUAL_HEX8(0x00u, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x07u, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, payload[3]);
    rcp_bytes_free(&f);

    /* No bit can escape that alignment: a base-format identifier above
     * 0x7FF is refused outright, and only data frames exist -- there is no
     * remote-frame flag, encoder or decode outcome anywhere in this
     * module, so every frame it produces is a data frame. */
    TEST_ASSERT_FALSE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_CBFF, 0x800u));
    TEST_ASSERT_TRUE(rcp_ep_can_arbitration_id_valid(RCP_EP_CAN_FRAME_CEFF, 0x1FFFFFFFu));
}

static void test_can_block_lacks_registers_and_receive_filter_table(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t  w = any_writer();
    rcp_ep_can_xl_filter_t      filt;

    rcp_ep_can_functional_cfg_init(&cfg);
    filt.id     = 0x123u;
    filt.mask   = 0x7FFu;
    filt.enable = true;
    TEST_ASSERT_TRUE(rcp_ep_can_set_xl_filter(&cfg, 0u, filt, RCP_LIFECYCLE_HW_CONFIGURED, w));

    /* DEVIATION -- TC18 §13.7.11.2 Table 53 fixes can_ep_len at 0x0000
     * (8 bit, R), a reserved octet at 0x0001, can_base_clk at 0x0004
     * (16 bit, R), can_ep_status at 0x0006 (16 bit, R/W), a 32-bit CAN EP
     * status at 0x001C and a 32-bit FIFO status at 0x0020. c-RCP's block
     * begins at relative address 0x0000 with the shared common flags and
     * models none of those rows, so bus-off, error-passive and
     * FIFO-overflow conditions are unobservable. */
    TEST_ASSERT_EQUAL_size_t(0u, offsetof(rcp_ep_can_functional_cfg_t, common));

    /* DEVIATION -- Table 53 defines TWO distinct 4-entry filter tables:
     * acceptance filters 1..4 at 0x0024..0x0030 (CAN XL) and receive ID
     * filters 1..4 at 0x0030..0x003C, the latter valid for ALL CAN
     * variants. c-RCP has exactly one table, scoped to CAN XL, and it is
     * the final member of the block -- nothing follows it -- so Classical
     * CAN, CAN FD and CAN FD light traffic cannot be ID-filtered on
     * reception at all. A conforming implementation would carry a second,
     * independently writable receive-filter table. */
    TEST_ASSERT_EQUAL_UINT8(4u, RCP_EP_CAN_XL_MAX_FILTERS);
    TEST_ASSERT_TRUE(rcp_ep_can_xl_filter_index_valid(3u));
    TEST_ASSERT_FALSE(rcp_ep_can_xl_filter_index_valid(4u));
    TEST_ASSERT_EQUAL_UINT32(0x123u, cfg.xl_filters[0].id);
    TEST_ASSERT_EQUAL_size_t(sizeof(cfg),
                             offsetof(rcp_ep_can_functional_cfg_t, xl_filters)
                                 + sizeof(cfg.xl_filters));
}

static void test_can_new_physical_layer_is_selected_per_frame(void)
{
    const uint8_t          data[2] = {0x5Au, 0xA5u};
    rcp_ep_can_xl_header_t xl;
    rcp_bytes_t            classical;
    rcp_bytes_t            new_pl;

    xl.sdt  = 0x11u;
    xl.vcid = 0x22u;
    xl.af   = 0x33445566u;

    classical = rcp_ep_can_encode_frame_request(0x31u, RCP_EP_CAN_FRAME_XL_CLASSICAL_PL,
                                                0x100u, &xl, data, sizeof(data), 7u);
    new_pl    = rcp_ep_can_encode_frame_request(0x31u, RCP_EP_CAN_FRAME_XL_NEW_PL,
                                                0x100u, &xl, data, sizeof(data), 7u);
    TEST_ASSERT_NOT_NULL(classical.data);
    TEST_ASSERT_NOT_NULL(new_pl.data);

    /* DEVIATION -- TC18 §13.7.11.2 lists "usage of new PL (YES|NO) for CAN
     * XL" among the CAN endpoint's FUNCTIONAL-CONFIGURATION settings, i.e.
     * a per-endpoint, runtime-writable, readable-back choice. c-RCP makes
     * it a per-FRAME choice instead: two otherwise identical requests
     * differ in exactly one octet, the one carrying the frame-format
     * selector, and no functional-config flag exists to constrain or read
     * back which physical layer the endpoint is provisioned for -- so
     * nothing can reject a frame whose XL variant contradicts the
     * endpoint's actual physical layer. */
    TEST_ASSERT_TRUE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_XL_CLASSICAL_PL));
    TEST_ASSERT_TRUE(rcp_ep_can_frame_format_is_xl(RCP_EP_CAN_FRAME_XL_NEW_PL));
    TEST_ASSERT_EQUAL_size_t(classical.len, new_pl.len);
    TEST_ASSERT_EQUAL_size_t(1u, changed_octets(classical.data, new_pl.data, classical.len));

    rcp_bytes_free(&classical);
    rcp_bytes_free(&new_pl);
}

/* ── ISELED (§13.7.12) ─────────────────────────────────────────────────────── */

static void test_iseled_response_has_no_read_size_ceiling(void)
{
    uint8_t                     rx[40];
    rcp_bytes_t                 f;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload = NULL;
    size_t                      pay_len = 0u;
    const uint8_t              *out_rx  = NULL;
    size_t                      out_len = 0u;
    bool                        timed   = true;
    uint64_t                    ts      = 1u;
    uint8_t                     tn      = 0u;

    memset(rx, 0x3C, sizeof(rx));
    f = rcp_ep_iseled_encode_response(0x41u, rx, sizeof(rx), 6u, false, 0u);
    TEST_ASSERT_NOT_NULL(f.data);

    /* DEVIATION -- TC18 §13.7.12.1 requires responses received from the
     * ISELED network to be 5/4-bit decoded and aggregated into one OR MORE
     * ACF messages, bounded by the read_size the originating read request
     * carried. c-RCP's encoder takes an already-assembled buffer, never
     * consults a read_size, enforces no ceiling and has no multi-message
     * emission path: 40 octets go in, one ACF message comes out carrying
     * all 40, and the encoded header's read_size field stays 0. A
     * conforming implementation would clamp at read_size and split the
     * remainder across further ACF messages. */
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(f.data, f.len, &hdr, &payload, &pay_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), pay_len);
    TEST_ASSERT_EQUAL_UINT16(0u, hdr.read_size_or_segment_num);
    TEST_ASSERT_EQUAL_INT(RCP_EP_ISELED_OK,
                          rcp_ep_iseled_decode_response(f.data, f.len, 0x41u, &out_rx,
                                                        &out_len, &timed, &ts, &tn));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), out_len);
    rcp_bytes_free(&f);
}

/* FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-ISELED-026/027/
 * 029): TC18 §13.7.12.2 Table 55's iseled_collect_resp, iseled_nr_leds and
 * iseled_rcv_timeout registers are now all reachable via the generic
 * evt[2:0]=111b register-block mechanism, same as every other endpoint
 * type. iseled_crc_enable stays deliberately outside the block (this
 * module's own original, second, independent CRC-8 layer -- see
 * ep_iseled.h's file header); iseled_bit_clk_divider stays a distinct,
 * non-wire field from the new wire_clk_divider. */
static void test_iseled_block_now_has_collect_resp_nr_leds_and_rcv_timeout(void)
{
    rcp_ep_iseled_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     w = any_writer();
    uint8_t                        block[RCP_EP_ISELED_EP_FUNC_LEN];

    rcp_ep_iseled_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_iseled_set_bit_clk_divider(&cfg, 0x11223344u,
                                                       RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_iseled_set_use_rcv_clk(&cfg, true, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_iseled_set_crc_enable(&cfg, true, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_iseled_set_trigger(&cfg, RCP_EP_ISELED_TRIGGER_TX_COMPLETE,
                                               RCP_LIFECYCLE_HW_CONFIGURED, w));

    /* Now positively confirm the register block itself round-trips the
     * three previously-missing fields, reachable only via
     * apply_reconfig() -- like every other endpoint type's own
     * register-block fields, these have no individual set_X() convenience
     * function. */
    cfg.collect_resp = true;
    cfg.nr_leds       = 0x1234u;
    cfg.rcv_timeout    = 0x5678u;

    rcp_ep_iseled_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ISELED_EP_FUNC_LEN, block[RCP_EP_ISELED_REG_EP_LEN]);
    TEST_ASSERT_TRUE((block[RCP_EP_ISELED_REG_FLAGS] & RCP_EP_ISELED_FLAG_COLLECT_RESP) != 0u);
    TEST_ASSERT_TRUE((block[RCP_EP_ISELED_REG_FLAGS] & RCP_EP_ISELED_FLAG_USE_RCV_CLK) != 0u);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, (uint16_t)((block[RCP_EP_ISELED_REG_NR_LEDS] << 8) |
                                                block[RCP_EP_ISELED_REG_NR_LEDS + 1]));
    TEST_ASSERT_EQUAL_HEX16(0x5678u, (uint16_t)((block[RCP_EP_ISELED_REG_RCV_TIMEOUT] << 8) |
                                                block[RCP_EP_ISELED_REG_RCV_TIMEOUT + 1]));

    /* iseled_crc_enable is NOT rendered onto the wire -- see the file
     * header -- so the block's own length is unaffected by it. */
    TEST_ASSERT_EQUAL_UINT16(0x000Eu, RCP_EP_ISELED_EP_FUNC_LEN);
}

/* ── MDIO (§13.7.13) ───────────────────────────────────────────────────────── */

/* FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I, REQ-MDIO-020/023):
 * "no configurable parameters" (TC18 §13.7.13.2's own prose) describes
 * what a *write* can change -- it never said the block isn't *readable*.
 * Table 56's mdio_ep_len/reserved/mdio_ep_status rows are now all
 * reachable via the generic evt[2:0]=111b register-block mechanism, same
 * as every other endpoint type -- the fifth address-collision editorial
 * defect this audit has found (mdio_ep_status was printed at the same
 * address as mdio_ep_enable&clr) resolved the same way as the prior four. */
static void test_mdio_block_now_exposes_ep_status_via_reconfig(void)
{
    rcp_ep_mdio_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t   w = any_writer();
    uint8_t                      block[RCP_EP_MDIO_EP_FUNC_LEN];

    rcp_ep_mdio_functional_cfg_init(&cfg);

    /* Still true: no *type-specific configurable* field exists -- the
     * struct's own "nothing more to add" design is unchanged, and the
     * common prefix is still the struct's own leading (now not sole)
     * member. */
    TEST_ASSERT_EQUAL_size_t(0u, offsetof(rcp_ep_mdio_functional_cfg_t, common));
    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_TRUE(rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, w));

    /* Now positively confirm the register block round-trips ep_status --
     * no base_clk row exists in this table, unlike every other endpoint
     * type's own common prefix, so the block is one register width
     * narrower. */
    cfg.ep_status = 0xBEEFu;
    rcp_ep_mdio_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_MDIO_EP_FUNC_LEN, block[RCP_EP_MDIO_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, block[RCP_EP_MDIO_REG_RESERVED_01]);
    TEST_ASSERT_EQUAL_UINT8(0xBEu, block[RCP_EP_MDIO_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0xEFu, block[RCP_EP_MDIO_REG_EP_STATUS + 1]);
    TEST_ASSERT_EQUAL_UINT16(0x0006u, RCP_EP_MDIO_EP_FUNC_LEN);
}

static void test_mdio_request_prefix_carries_no_two_bit_mode_field(void)
{
    rcp_ep_mdio_addr_t          addr;
    rcp_bytes_t                 f;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload = NULL;
    size_t                      pay_len = 0u;

    addr.clause = RCP_EP_MDIO_CLAUSE_45;
    addr.prtad  = 0x1Fu;
    addr.devad  = 0x0Au;
    addr.regad  = 0xBEEFu;

    f = rcp_ep_mdio_encode_read_request(0x51u, addr, 3u, 8u);
    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(f.data, f.len, &hdr, &payload, &pay_len));

    /* DEVIATION -- TC18 §13.7.13.3 Figure 42 lays a request payload out as
     * reserved bits, a 2-BIT mdio_mode field, then mdio_address and
     * mdio_payload, where Table 57 gives mdio_mode four meanings (MMD
     * single-word, MMD multiple-byte, MMS single-word 10b, MMS multiple
     * (double) word 11b). c-RCP's payload is instead a 7-octet prefix: a
     * whole-octet clause selector, prtad, devad, big-endian regad, then a
     * big-endian word_count. There is no mdio_mode field and no
     * single-access/multiple-access distinction -- burst behaviour rides
     * word_count instead -- so these requests are not wire-compatible with
     * a conforming peer. */
    TEST_ASSERT_EQUAL_size_t(7u, pay_len);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_MDIO_CLAUSE_45, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x1Fu, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBEu, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, payload[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, payload[5]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, payload[6]);
    rcp_bytes_free(&f);
}

static void test_mdio_data_fields_are_unconditionally_sixteen_bit(void)
{
    /* One 32-bit MMS0 register value, big-endian, as a conforming peer
     * would place it in an MMS0 data field. */
    const uint8_t mms0_word[4] = {0x12u, 0x34u, 0x56u, 0x78u};
    uint8_t       out[2];
    size_t        word_count = 0u;

    rcp_ep_mdio_word_encode(0xBEEFu, out);
    TEST_ASSERT_EQUAL_HEX8(0xBEu, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, out[1]);

    /* DEVIATION -- TC18 §13.7.13.3 Table 57 requires 16-bit data fields for
     * MMD accesses and for MMS other than 0 and 1, but 32-BIT data fields
     * for MMS0 and MMS1. c-RCP's word codec is unconditionally 16-bit and
     * consults no access mode: a single 32-bit MMS0 value is counted as two
     * words and misparsed as two unrelated 16-bit halves, and the pack
     * length is word_count * 2 where an MMS0/MMS1 access needs
     * word_count * 4. */
    TEST_ASSERT_TRUE(rcp_ep_mdio_word_count_of(sizeof(mms0_word), &word_count));
    TEST_ASSERT_EQUAL_size_t(2u, word_count);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, rcp_ep_mdio_unpack_word_at(mms0_word, 0u));
    TEST_ASSERT_EQUAL_HEX16(0x5678u, rcp_ep_mdio_unpack_word_at(mms0_word, 1u));
    TEST_ASSERT_EQUAL_size_t(6u, rcp_ep_mdio_pack_len(3u));
    TEST_ASSERT_NOT_EQUAL_size_t(12u, rcp_ep_mdio_pack_len(3u));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_uart_functional_block_now_has_full_register_coverage);
    RUN_TEST(test_uart_rx_fifo_size_bounds_nothing_at_all);
    RUN_TEST(test_uart_read_completion_decision);
    RUN_TEST(test_uart_compound_wait_now_resolved_via_generic_primitive);
    RUN_TEST(test_uart_read_size_above_one_octet_round_trips);
    RUN_TEST(test_uart_register_units_diverge_from_table_48);

    RUN_TEST(test_adc_value_width_and_named_analog_input_signal);
    RUN_TEST(test_adc_functional_cfg_has_clock_status_and_interval_fields);
    RUN_TEST(test_adc_inter_sample_spacing_is_unconstrained);
    RUN_TEST(test_adc_has_no_trigger_outputs_and_no_retained_average);

    RUN_TEST(test_lin_trigger_now_honours_trailing_time_and_block_has_registers);

    RUN_TEST(test_can_frame_format_values_match_table_54);
    RUN_TEST(test_can_base_identifier_is_right_aligned_and_data_only);
    RUN_TEST(test_can_block_lacks_registers_and_receive_filter_table);
    RUN_TEST(test_can_new_physical_layer_is_selected_per_frame);

    RUN_TEST(test_iseled_response_has_no_read_size_ceiling);
    RUN_TEST(test_iseled_block_now_has_collect_resp_nr_leds_and_rcv_timeout);

    RUN_TEST(test_mdio_block_now_exposes_ep_status_via_reconfig);
    RUN_TEST(test_mdio_request_prefix_carries_no_two_bit_mode_field);
    RUN_TEST(test_mdio_data_fields_are_unconditionally_sixteen_bit);

    return UNITY_END();
}
