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
//cfusa:test REQ-ISELED-041
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

#include "../src/mem_bounded.h"

#include <rcp/acf.h>
#include <rcp/ep_adc.h>
#include <rcp/ep_can.h>
#include <rcp/ep_iseled.h>
#include <rcp/ep_lin.h>
#include <rcp/ep_mdio.h>
#include <rcp/ep_pwm.h>
#include <rcp/ep_uart.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>

#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Shared mock.c dispatch fixture (issue #338, PR D continued) -- see
 * test_tc18_gaps_ep.c's own identical fixture for the full mock.c
 * architecture rationale (this module deliberately never calls into
 * ep_adc.c/ep_iseled.c directly; a caller-registered handler is the
 * documented way to wire one in). ─────────────────────────────────────── */
static const rcp_lifecycle_plausibility_snapshot_t GAP_EMPTY_SNAP = {NULL, 0, NULL, 0};

static void gap_to_hw_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &GAP_EMPTY_SNAP,
                                                 none, true));
}

static void gap_to_rcp_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    gap_to_hw_configured(srv);
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &GAP_EMPTY_SNAP,
                                                 root, true));
}

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
    rcp_memcpy_bounded(before, sizeof(before), &cfg, sizeof(before));

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

static void test_uart_rx_fifo_size_bounds_nothing_overflow_flag_left_uninterpreted(void)
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

    /* REQ-UART-032 IMPLEMENTED (2026-08-12, catalog-drift correction): TC18
     * §13.7.8.1 requires an RX FIFO overflow to be flagged in
     * uart_ep_status -- and that 16-bit register has, in fact, existed as a
     * real, freely-settable, round-tripped field (cfg.ep_status) since PR
     * #276 (issue #256, 2026-08-11), the day before this requirement was
     * even filed against a stale reading of the code. .fusa-reqs.json's own
     * "not-implemented" status was simply never updated to match -- this
     * fix is a catalog correction, not new code. TC18 never defines which
     * bit of uart_ep_status carries the overflow flag (the same
     * "_ep_status has no printed bit layout" spec-silence pattern already
     * accepted for CAN/WakeUp/several other endpoint types' own status
     * registers, e.g. REQ-CANEP-028), so this module correctly does not
     * invent a bit position -- it stores and round-trips whatever value a
     * caller or register-map write assigns, the same disposition every
     * other endpoint type's own status register already gets. This test
     * documents what that disposition does NOT do: ep_rx_buffer_size
     * itself bounds nothing at the encode/decode layer (a response four
     * times the configured FIFO size still encodes happily, in exactly one
     * unfragmented frame, with no overflow signal on any surface) --
     * that's expected, not a gap, since flagging overflow is uart_ep_
     * status's job, and setting that flag from a live FIFO-fill condition
     * is an integrator's runtime responsibility, not this wire/register
     * library's (REQ-UART-033's own read-COMPLETION arbitration, covered
     * by the deviation this test used to also pin, is resolved separately
     * below by rcp_ep_uart_read_completion_decision()). */
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

/* REQ-UART-037's own stop_bits half-unit deviation CLOSED 2026-08-14
 * (tc18-gap post-backlog audit) -- moved out of this still-genuinely-
 * diverging test into test_uart_stop_bits_now_representable_at_half_
 * unit_precision() below. The baud_rate/timeout deviations remain real,
 * separate, unaddressed limitations this fix does not touch. */
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

    /* DEVIATION -- Table 48 expresses uart_timeout as an 8-bit count of BIT
     * TIMES measured from the last received stop bit. c-RCP stores a 32-bit
     * millisecond value with no measurement origin, unrelated to the baud
     * rate: changing the baud rate leaves the stored timeout untouched,
     * where a bit-time register's real duration would move with it. */
    TEST_ASSERT_TRUE(rcp_ep_uart_set_timeout(&cfg, 250u, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_TRUE(rcp_ep_uart_set_baud_rate(&cfg, 9600u, RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_EQUAL_UINT32(250u, cfg.uart_timeout_ms);
}

/* CLOSED 2026-08-14 (REQ-UART-037, tc18-gap post-backlog audit): Table
 * 48 counts uart_stop_bits in HALF stop bits, so 1.5 stop bits is the
 * legal wire value 3. rcp_ep_uart_stop_bits_t now has a third member,
 * RCP_EP_UART_STOP_BITS_ONE_HALF (2), so this is exactly representable
 * through the public setter -- no longer an unvalidated raw integer with
 * no named framing behind it. */
static void test_uart_stop_bits_now_representable_at_half_unit_precision(void)
{
    rcp_ep_uart_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t   w = any_writer();

    rcp_ep_uart_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL_INT(0, (int)RCP_EP_UART_STOP_BITS_ONE);
    TEST_ASSERT_EQUAL_INT(1, (int)RCP_EP_UART_STOP_BITS_TWO);
    TEST_ASSERT_EQUAL_INT(2, (int)RCP_EP_UART_STOP_BITS_ONE_HALF);

    TEST_ASSERT_TRUE(rcp_ep_uart_set_frame_format(&cfg, 8u, RCP_EP_UART_PARITY_NONE,
                                                  RCP_EP_UART_STOP_BITS_ONE_HALF,
                                                  RCP_LIFECYCLE_HW_CONFIGURED, w));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_UART_STOP_BITS_ONE_HALF, cfg.stop_bits);
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
    rcp_memcpy_bounded(before, sizeof(before), &cfg, sizeof(before));

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

/* Still true and still worth pinning: rcp_ep_adc_average_interval()
 * itself (layer 1's arithmetic-mean reduction) has no timing awareness
 * of its own and never will -- the two intervals below have completely
 * different sample spacing and are nevertheless indistinguishable at
 * THIS function's own output. That is by design (this function's own
 * job is the mean, not the timing check); REQ-ADC-033's actual spacing
 * validation is a separate, dedicated primitive exercised below. */
static void test_adc_average_interval_itself_has_no_timing_awareness(void)
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

    TEST_ASSERT_EQUAL_UINT16(100u, a.value);
    TEST_ASSERT_EQUAL_UINT16(a.value, b.value);
    TEST_ASSERT_EQUAL_UINT64(2000u, a.timestamp);
    TEST_ASSERT_EQUAL_UINT64(a.timestamp, b.timestamp);
}

/* CLOSED 2026-08-14 (was DEVIATION PIN): REQ-ADC-033's own real spacing
 * check, rcp_ep_adc_validate_sample_spacing() (ep_adc.h/ep_adc.c) --
 * proven against the exact same "even" (uniform, 1000ns cadence) vs.
 * "ragged" (wildly non-uniform) sample sets the old deviation-pin test
 * used, now genuinely distinguished. base_clk_divider=5,
 * sample_interval=200 (both within Table 51's own 8-bit fields),
 * base_clk_hz=1_000_000_000 (a 1GHz caller-real
 * clock) yields an expected_spacing_ns of exactly 1000 -- chosen so the
 * "even" fixture's own real 1000-tick timestamps validate at zero
 * tolerance without any unit-conversion cleverness in the test itself. */
static void test_adc_validate_sample_spacing_distinguishes_even_from_ragged(void)
{
    rcp_ep_adc_sample_t even[3];
    rcp_ep_adc_sample_t ragged[3];
    size_t               i;

    for (i = 0u; i < 3u; i++) {
        even[i].value   = 100u;
        ragged[i].value = 100u;
    }
    even[0].timestamp   = 0u;
    even[1].timestamp   = 1000u;
    even[2].timestamp   = 2000u;
    ragged[0].timestamp = 0u;
    ragged[1].timestamp = 5u;
    ragged[2].timestamp = 2000u;

    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_OK,
                      rcp_ep_adc_validate_sample_spacing(even, 3u, 5u, 200u,
                                                          1000000000u, 0u));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_VIOLATION,
                      rcp_ep_adc_validate_sample_spacing(ragged, 3u, 5u, 200u,
                                                          1000000000u, 0u));
}

/* A spacing within tolerance is OK; the same deviation outside tolerance
 * is a violation -- tolerance_ns is a real, exercised parameter, not
 * dead width. */
static void test_adc_validate_sample_spacing_respects_tolerance(void)
{
    rcp_ep_adc_sample_t s[2];

    s[0].value = 100u;
    s[1].value = 100u;
    s[0].timestamp = 0u;
    s[1].timestamp = 1050u; /* 50ns late against an expected 1000ns */

    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_VIOLATION,
                      rcp_ep_adc_validate_sample_spacing(s, 2u, 5u, 200u, 1000000000u, 0u));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_OK,
                      rcp_ep_adc_validate_sample_spacing(s, 2u, 5u, 200u, 1000000000u, 50u));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_VIOLATION,
                      rcp_ep_adc_validate_sample_spacing(s, 2u, 5u, 200u, 1000000000u, 49u));
}

/* No real clock rate/divider to check against (base_clk_hz == 0 or
 * base_clk_divider == 0, matching adc_base_clk's own never-modelled
 * default) or fewer than 2 samples: fails open, not a false violation. */
static void test_adc_validate_sample_spacing_fails_open_without_a_real_clock(void)
{
    rcp_ep_adc_sample_t s[2];

    s[0].value = 100u;
    s[1].value = 100u;
    s[0].timestamp = 0u;
    s[1].timestamp = 999999999u; /* wildly off from any sane 1000ns cadence */

    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_OK,
                      rcp_ep_adc_validate_sample_spacing(s, 2u, 1u, 1u, 0u, 0u));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_OK,
                      rcp_ep_adc_validate_sample_spacing(s, 2u, 0u, 1u, 1000000000u, 0u));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_OK,
                      rcp_ep_adc_validate_sample_spacing(s, 1u, 1u, 1u, 1000000000u, 0u));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_OK,
                      rcp_ep_adc_validate_sample_spacing(NULL, 0u, 1u, 1u, 1000000000u, 0u));
}

/* A non-monotonic timestamp pair (real clock configured) is its own
 * violation, distinct from an out-of-tolerance spacing -- caught before
 * the unsigned subtraction that would otherwise underflow. */
static void test_adc_validate_sample_spacing_rejects_non_monotonic_timestamps(void)
{
    rcp_ep_adc_sample_t s[2];

    s[0].value = 100u;
    s[1].value = 100u;
    s[0].timestamp = 2000u;
    s[1].timestamp = 1000u; /* earlier than s[0] */

    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_VIOLATION,
                      rcp_ep_adc_validate_sample_spacing(s, 2u, 5u, 200u, 1000000000u, 0u));

    /* The monotonicity check must be its own, independent reason to
     * reject -- not an accident of the (i+1)-minus-i subtraction
     * underflowing into some huge unsigned value that happens to also
     * fail the ordinary tolerance window. Proven by choosing a
     * tolerance_ns wide enough (UINT64_MAX - expected_ns) that the
     * window clamps to [0, UINT64_MAX] -- an underflowed value would
     * fall INSIDE that window and read as a false OK if the
     * monotonicity check were ever removed or short-circuited away. */
    TEST_ASSERT_EQUAL(RCP_EP_ADC_SPACING_VIOLATION,
                      rcp_ep_adc_validate_sample_spacing(s, 2u, 5u, 200u, 1000000000u,
                                                          (uint64_t)-1 - 1000u));
}

static void test_adc_pipeline_is_stateless_by_design_and_cadence_deviation_pin(void)
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

    /* Not a deviation (RENAMED 2026-08-13, issue #336, REQ-ADC-031 already
     * fixed this): the comment this replaces claimed "c-RCP defines no
     * trigger enum, no threshold registers and no evaluation predicate" for
     * TC18 §13.7.9.1 Table 53's five ADC trigger outputs -- stale since
     * REQ-ADC-031 (issue #201) added rcp_ep_adc_trigger_state_t/
     * rcp_ep_adc_trigger_evaluate(), which decides exactly this from a
     * caller-supplied newly-acquired value plus the tracked previous one
     * (its own dedicated tests live in tests/test_ep_adc.c). What these two
     * assertions actually pin is unrelated and still true:
     * rcp_ep_adc_average_interval() itself is a pure arithmetic-mean
     * function with no threshold awareness of its own -- crossing a
     * threshold mid-interval and staying flat produce the same mean when
     * the two endpoints average to the same value, exactly as ordinary
     * averaging arithmetic predicts, not as a gap. */
    TEST_ASSERT_EQUAL_UINT16(150u, crossed.value);
    TEST_ASSERT_EQUAL_UINT16(crossed.value, steady.value);
    TEST_ASSERT_EQUAL_UINT64(crossed.timestamp, steady.timestamp);

    /* Not a deviation (RENAMED 2026-08-13, issue #336, REQ-ADC-034
     * corrected -- see .fusa-reqs.json): the comment this replaces claimed
     * "there is nothing for a compound wait to compare against" -- stale in
     * the same way REQ-UART-035 already corrected for UART: TC18 §13.5.1's
     * compound-wait comparison is a universal, endpoint-agnostic mechanism
     * (acf.h's rcp_acf_compound_wait_match(), wired into real dispatch via
     * server.c's rcp_server_tick_ctx_t.current_status, tests/test_acf.c's
     * own 45+ dedicated assertions) -- no ADC-specific comparator was ever
     * needed, the same as no UART-specific one was. "Comparing against the
     * last acquired average" is simply whatever the caller supplies as
     * current_status; this pipeline's own statelessness (asserted below)
     * was never actually an obstacle to that. §13.7.9.1's other half --
     * sampling only while a request executes -- is genuinely out of scope
     * by this module's own documented design (ep_adc.h's file header:
     * "This module never itself owns a timer, thread, or background
     * sampling loop"): there is no c-RCP-owned sampling loop for any
     * caller-side gating rule to apply to. rcp_ep_adc_average_interval()
     * itself remains, by design, a pure function of whatever samples its
     * caller passes -- reporting the no-signal sentinel for zero samples,
     * not a retained value, matching every other stage in this module's own
     * "operates on caller-supplied arrays, owns no sample storage"
     * convention (see rcp_ep_adc_cadence_response_ready()'s own doc
     * comment for the same convention stated explicitly). */
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, empty.value);
    TEST_ASSERT_EQUAL_UINT64(0u, rcp_ep_adc_capture_moment_timestamp(NULL, 0u));

    /* FIXED 2026-08-13 (issue #338, tc18-gap backlog PR D continued):
     * TC18 §13.7.9.2 states three cadence cases comparing
     * adc_combine_avg_values against adc_avg_intervals_per_request.
     * rcp_ep_adc_cadence_case()/_response_ready() give a caller the two
     * decision primitives TC18's own rule requires -- tests/test_ep_adc.c's
     * own dedicated cadence section unit-tests both directly, including
     * both boundary conditions and an end-to-end walk of the ACCUMULATE
     * and FAN_OUT cases. DISPATCH-WIRING CLOSED THIS BATCH:
     * test_adc_dispatch_accumulates_across_executions_before_responding()
     * (below) proves a real caller -- adc_dispatch_handler(), an
     * rcp_mock_endpoint_handler_fn registered via the existing, unmodified
     * rcp_mock_server_add_endpoint() -- calls rcp_ep_adc_cadence_response_
     * ready() on every dispatched request and honors its result end-to-end
     * through a real mock.c dispatch() path, the same "mock.c never calls
     * into ep_*.c directly; a caller-registered handler is the documented
     * mechanism" disposition test_tc18_gaps_ep.c's own GPIO fixture
     * already established (see that file's own shared-fixture comment for
     * the full architecture rationale). rcp_ep_adc_collect_response_
     * values() itself, pinned below, still takes only avg_count/
     * value_count as plain parameters -- by design: assembling a ready
     * response's own value array and deciding its transaction_num remain
     * the caller's own bookkeeping (see rcp_ep_adc_cadence_response_
     * ready()'s own doc comment), the same "operates on caller-supplied
     * arrays" scope this whole module already holds to. */
    {
        rcp_ep_adc_avg_value_t five[5] = {0};
        uint16_t                packed[3];
        size_t                  n;

        n = rcp_ep_adc_collect_response_values(five, 5u, packed, 3u);
        TEST_ASSERT_EQUAL_size_t(3u, n); /* min(5, 3) -- min() only, no cadence logic */
    }
}

/* ── ADC endpoint: real mock.c dispatch path (issue #338, PR D continued) ──
 *
 * adc_dispatch_state_t/adc_dispatch_handler() mirrors test_tc18_gaps_ep.c's
 * own gpio_dispatch_handler() shape: this fixture's own "one execution"
 * (one dispatched read request) contributes exactly one already-averaged
 * value to a pending FIFO -- a deliberate simplification of layer 1's own
 * real sample-to-average reduction (rcp_ep_adc_average_interval(), already
 * directly tested in test_ep_adc.c and exercised end-to-end just above),
 * so this fixture can isolate layers 2/3's own cadence decision without
 * re-deriving layer 1. Demonstrates RCP_EP_ADC_CADENCE_ACCUMULATE
 * (combine_avg_values > 1 execution's worth): the response is withheld
 * across executions until enough values have accumulated. */
#define ADC_DISPATCH_PENDING_CAP 8u

typedef struct {
    uint16_t pending[ADC_DISPATCH_PENDING_CAP];
    size_t   pending_count;
    uint8_t  combine_avg_values;
    uint16_t next_value;
} adc_dispatch_state_t;

static void adc_dispatch_handler(const uint8_t *request, size_t request_len,
                                  rcp_bytes_t *out_response, void *user_data)
{
    adc_dispatch_state_t *st = (adc_dispatch_state_t *)user_data;
    uint16_t               read_size;
    uint8_t                 tn;

    if (rcp_ep_adc_decode_read_request(request, request_len, 4u, &read_size, &tn) !=
        RCP_EP_ADC_OK) {
        return;
    }

    /* This fixture's own stand-in "execution": one newly-averaged value,
     * appended to the pending FIFO -- see this section's own header
     * comment for why layer 1's real averaging is deliberately not
     * re-derived here. */
    TEST_ASSERT_TRUE(st->pending_count < ADC_DISPATCH_PENDING_CAP);
    st->pending[st->pending_count++] = st->next_value++;

    /* REQ-ADC-037: the one comparison underlying all three cadence cases
     * -- withhold the response until enough values have accumulated. */
    if (!rcp_ep_adc_cadence_response_ready(st->pending_count, st->combine_avg_values)) {
        return; /* out_response left zeroed -- not yet ready */
    }

    *out_response = rcp_ep_adc_encode_response(4u, st->pending, st->combine_avg_values, tn, false,
                                                0u);

    /* FIFO: shift the consumed prefix out, matching
     * rcp_ep_adc_collect_response_values()'s own "packs the FIRST
     * value_count... in capture order" convention. */
    rcp_memmove_bounded(st->pending, sizeof(st->pending), &st->pending[st->combine_avg_values],
            (st->pending_count - st->combine_avg_values) * sizeof(st->pending[0]));
    st->pending_count -= st->combine_avg_values;
}

static void test_adc_dispatch_accumulates_across_executions_before_responding(void)
{
    rcp_mock_server_t    *srv = rcp_mock_server_new();
    adc_dispatch_state_t   st  = {0};
    rcp_bytes_t             req;
    rcp_bytes_t             resp = {0};
    uint16_t                out_values[3];
    size_t                  out_count;
    bool                    out_timed;
    uint64_t                out_ts;
    uint8_t                 out_tn;
    int                     i;

    TEST_ASSERT_NOT_NULL(srv);
    gap_to_rcp_configured(srv);
    st.combine_avg_values = 3u;
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                      rcp_mock_server_add_endpoint(srv, 4u, 0u, true, adc_dispatch_handler, &st));

    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_ACCUMULATE, rcp_ep_adc_cadence_case(1u, 3u));

    /* Two executions: neither alone has 3 accumulated values yet. */
    for (i = 0; i < 2; i++) {
        req = rcp_ep_adc_encode_read_request(4u, 6u, (uint8_t)(0x30 + i));
        TEST_ASSERT_NOT_NULL(req.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                          rcp_mock_server_dispatch(srv, 4u, RCP_AVTP_SUBTYPE_NTSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, 1u, req.data,
                                                    req.len, &resp));
        TEST_ASSERT_NULL(resp.data); /* not enough accumulated yet */
        rcp_bytes_free(&req);
    }
    TEST_ASSERT_EQUAL_size_t(2u, st.pending_count);

    /* The third execution reaches combine_avg_values (3): a real response,
     * in this same call, carrying all 3 accumulated values in order. */
    req = rcp_ep_adc_encode_read_request(4u, 6u, 0x32u);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch(srv, 4u, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB,
                                                true, 1u, req.data, req.len, &resp));
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK,
                      rcp_ep_adc_decode_response(resp.data, resp.len, 4u, out_values, 3u,
                                                  &out_count, &out_timed, &out_ts, &out_tn));
    TEST_ASSERT_EQUAL_size_t(3u, out_count);
    TEST_ASSERT_EQUAL_UINT16(0u, out_values[0]);
    TEST_ASSERT_EQUAL_UINT16(1u, out_values[1]);
    TEST_ASSERT_EQUAL_UINT16(2u, out_values[2]);
    TEST_ASSERT_EQUAL_UINT8(0x32u, out_tn);
    TEST_ASSERT_EQUAL_size_t(0u, st.pending_count); /* fully drained */

    rcp_bytes_free(&req);
    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
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
    rcp_memcpy_bounded(before, sizeof(before), &cfg, sizeof(before));

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

/* REQ-CANEP-028 IMPLEMENTED (issue #201, 2026-08-12): TC18 §13.7.11.2
 * Table 56 (RC1's own Table 53 -- renumbered, issue #341 lineage) fixes
 * can_ep_len at 0x0000 (8 bit, R), a reserved octet at 0x0001,
 * can_base_clk at 0x0004 (16 bit, R), can_ep_status at 0x0006 (16 bit,
 * R/W), a 32-bit CAN EP status at 0x001C and a 32-bit FIFO status at
 * 0x0020 -- rcp_ep_can_render_registers()/_apply_reconfig()
 * (ep_can.h/ep_can.c) now serialize/parse exactly that span, reachable
 * via the generic §12.7.1 evt[2:0]==111b mechanism, so bus-off,
 * error-passive and FIFO-overflow conditions (carried in the new
 * status/fifo_status fields) are now observable and settable. Scoped to
 * end at 0x0024, immediately before REQ-CANEP-029's own already-
 * documented address collision in the acceptance-filter region --
 * closing this register block did not require resolving that collision,
 * since it lies entirely outside this span. */
static void test_can_register_block_round_trips_ep_status_and_status_fields(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    uint8_t                     block[RCP_EP_CAN_EP_FUNC_LEN];
    rcp_ep_can_functional_cfg_t roundtrip;

    rcp_ep_can_functional_cfg_init(&cfg);
    cfg.ep_status   = 0x1234u;
    cfg.status      = 0xDEADBEEFu;
    cfg.fifo_status = 0xCAFEF00Du;

    rcp_ep_can_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_CAN_EP_FUNC_LEN, block[0x0000]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, block[0x0001]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, block[0x0004]); /* can_base_clk: no real
                                                        clock modelled */
    TEST_ASSERT_EQUAL_UINT8(0x12u, block[0x0006]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, block[0x0007]);
    TEST_ASSERT_EQUAL_UINT8(0xDEu, block[0x001C]);
    TEST_ASSERT_EQUAL_UINT8(0xEFu, block[0x001F]);
    TEST_ASSERT_EQUAL_UINT8(0xCAu, block[0x0020]);
    TEST_ASSERT_EQUAL_UINT8(0x0Du, block[0x0023]);

    rcp_ep_can_functional_cfg_init(&roundtrip);

    /* A payload carrying only the address prefix, no data octet after
     * it, is rejected -- matching every other endpoint type's own
     * identical RECONFIG_ADDR_LEN + 1 minimum. */
    TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_ERR_SHORT,
                      rcp_ep_can_apply_reconfig(&roundtrip, (const uint8_t[]){0x00, 0x00},
                                                RCP_EP_CAN_RECONFIG_ADDR_LEN));

    /* A real addressed write covering can_ep_status and the 32-bit status
     * registers round-trips through parse_can_registers(). */
    {
        uint8_t payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + (RCP_EP_CAN_EP_FUNC_LEN - 0x0006u)];

        payload[0] = 0x00u;
        payload[1] = 0x06u; /* start_address = 0x0006 */
        rcp_memcpy_bounded(&payload[RCP_EP_CAN_RECONFIG_ADDR_LEN], sizeof(payload) - RCP_EP_CAN_RECONFIG_ADDR_LEN, &block[0x0006],
               RCP_EP_CAN_EP_FUNC_LEN - 0x0006u);
        TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_OK,
                          rcp_ep_can_apply_reconfig(&roundtrip, payload, sizeof(payload)));
    }
    TEST_ASSERT_EQUAL_UINT16(0x1234u, roundtrip.ep_status);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, roundtrip.status);
    TEST_ASSERT_EQUAL_UINT32(0xCAFEF00Du, roundtrip.fifo_status);

    /* A write covering EP_LEN/the reserved octet/base_clk (all read-only)
     * is silently ignored for those octets specifically -- the whole
     * write is not rejected, per §12.7.1's own "written data on read
     * only registers has no effect" rule, matching every other endpoint
     * type's own read-only-octet handling. */
    {
        uint8_t payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + 6] = {0x00, 0x00, 0xFF, 0xFF,
                                                              0xFF, 0xFF, 0xFF, 0xFF};

        TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_OK,
                          rcp_ep_can_apply_reconfig(&roundtrip, payload, sizeof(payload)));
    }
    TEST_ASSERT_EQUAL_UINT16(0x1234u, roundtrip.ep_status); /* unaffected --
                                                                 payload's tail
                                                                 (offsets
                                                                 0x0002-0x0005)
                                                                 only reached
                                                                 common flags
                                                                 and base_clk */

    /* The not-yet-decomposed 0x0008-0x001B span (clk_divider, both
     * reserved regions, the three CAN bit time registers, and TDCC) is
     * also read-only for now -- a write covering it leaves render()'s
     * own output for that whole span at 0, not the written value.
     * Literal offsets (not RCP_EP_CAN_REG_* names, which are private to
     * ep_can.c) matching this file's own convention elsewhere above. */
    {
        uint8_t  payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + 0x14];
        uint8_t  after[RCP_EP_CAN_EP_FUNC_LEN];
        uint16_t i;

        payload[0] = 0x00u;
        payload[1] = 0x08u; /* start_address = 0x0008 */
        memset(&payload[RCP_EP_CAN_RECONFIG_ADDR_LEN], 0xFF, 0x14);

        TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_OK,
                          rcp_ep_can_apply_reconfig(&roundtrip, payload, sizeof(payload)));
        rcp_ep_can_render_registers(&roundtrip, after);
        for (i = 0x08u; i < 0x1Cu; i++) {
            TEST_ASSERT_EQUAL_UINT8(0x00u, after[i]);
        }
    }
}

/* REQ-CANEP-028 wire-format regression (issue #470, 2026-08-14):
 * CAN_ENABLE_CLR_BIT_CLEAR was defined at bit 1 instead of TC18 Table 35's
 * bit 4 for ep_clear_req_storage -- self-consistent (render and parse both
 * used the same wrong bit), so the round-trip test above never caught it.
 * This test asserts the exact rendered byte value instead of round-
 * tripping through this module's own (at the time, wrong) encode/decode
 * pair, the only way to catch this class of bug: it would have failed
 * before the fix (can_ep_enable&clr would have rendered 0x02, not 0x10). */
static void test_can_ep_enable_clr_clear_bit_is_wire_bit_4(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    uint8_t                     block[RCP_EP_CAN_EP_FUNC_LEN];
    rcp_ep_can_functional_cfg_t roundtrip;

    /* ep_clear_req_storage alone (ep_enable left false) must set exactly
     * bit 4 (0x10) of can_ep_enable&clr (0x0002) -- not bit 1 (0x02),
     * TC18 Table 35's reserved position this endpoint used to (wrongly)
     * react to. */
    rcp_ep_can_functional_cfg_init(&cfg);
    cfg.common.ep_clear_req_storage = true;
    rcp_ep_can_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_HEX8(0x10u, block[0x0002]);
    TEST_ASSERT_BITS(0x02u, 0x00u, block[0x0002]); /* the old wrong bit
                                                        stays clear */

    /* ep_enable + ep_clear_req_storage together set exactly bits 0 and 4
     * (0x11), TC18 Table 35's own two defined bits of this octet -- every
     * other bit (including the old wrong bit 1) stays clear. */
    rcp_ep_can_functional_cfg_init(&cfg);
    cfg.common.ep_enable            = true;
    cfg.common.ep_clear_req_storage = true;
    rcp_ep_can_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_HEX8(0x11u, block[0x0002]);

    /* A raw register write setting only bit 4 (0x10) -- the real TC18
     * wire bit -- is parsed back as ep_clear_req_storage == true, and a
     * write setting only the old wrong bit 1 (0x02) is NOT. */
    rcp_ep_can_functional_cfg_init(&roundtrip);
    {
        uint8_t payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + 1] = {0x00, 0x02, 0x10};

        TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_OK,
                          rcp_ep_can_apply_reconfig(&roundtrip, payload, sizeof(payload)));
    }
    TEST_ASSERT_TRUE(roundtrip.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(roundtrip.common.ep_enable);

    rcp_ep_can_functional_cfg_init(&roundtrip);
    {
        uint8_t payload[RCP_EP_CAN_RECONFIG_ADDR_LEN + 1] = {0x00, 0x02, 0x02};

        TEST_ASSERT_EQUAL(RCP_EP_CAN_RECONFIG_OK,
                          rcp_ep_can_apply_reconfig(&roundtrip, payload, sizeof(payload)));
    }
    TEST_ASSERT_FALSE(roundtrip.common.ep_clear_req_storage);
}

static void test_can_block_lacks_receive_filter_table(void)
{
    rcp_ep_can_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t  w = any_writer();
    rcp_ep_can_xl_filter_t      filt;

    rcp_ep_can_functional_cfg_init(&cfg);
    filt.id     = 0x123u;
    filt.mask   = 0x7FFu;
    filt.enable = true;
    TEST_ASSERT_TRUE(rcp_ep_can_set_xl_filter(&cfg, 0u, filt, RCP_LIFECYCLE_HW_CONFIGURED, w));

    /* DEVIATION, still genuinely open -- Table 56 defines TWO distinct
     * 4-entry filter tables: acceptance filters 1..4 (CAN XL, at
     * addresses REQ-CANEP-029 already documents as internally
     * inconsistent -- filters 3 and 4 both print at 0x002C) and receive
     * ID filters 1..4 at 0x0030..0x003C, the latter valid for ALL CAN
     * variants. c-RCP has exactly one table, scoped to CAN XL only -- so
     * Classical CAN, CAN FD and CAN FD light traffic cannot be
     * ID-filtered on reception at all. A conforming implementation would
     * carry a second, independently writable receive-filter table; that
     * table's own wire exposure is additionally blocked by
     * REQ-CANEP-029's own unresolved collision, since the receive-filter
     * region's own addressing depends on how the acceptance-filter
     * region ahead of it is finally laid out. */
    TEST_ASSERT_EQUAL_UINT8(4u, RCP_EP_CAN_XL_MAX_FILTERS);
    TEST_ASSERT_TRUE(rcp_ep_can_xl_filter_index_valid(3u));
    TEST_ASSERT_FALSE(rcp_ep_can_xl_filter_index_valid(4u));
    TEST_ASSERT_EQUAL_UINT32(0x123u, cfg.xl_filters[0].id);
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
//cfusa:test REQ-ISELED-026
//cfusa:test REQ-ISELED-027
//cfusa:test REQ-ISELED-041
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

/* ── ISELED endpoint: real mock.c dispatch path (issue #338, PR D
 * concluded; multi-fragment case closed 2026-08-14) ────────────────────────
 *
 * iseled_dispatch_state_t/iseled_dispatch_handler() mirrors the GPIO/ADC
 * fixtures' own shape for the single-fragment case (chip_data sized to
 * fit in one fragment, asserting rcp_ep_iseled_response_fragment_count()
 * itself returns exactly 1) -- proving the fragmentation entry points are
 * genuinely reachable and correct through a real, plain dispatch() call.
 *
 * RESOLVED 2026-08-14: this section's own prior text found a genuine
 * architectural limit -- rcp_mock_endpoint_handler_fn (mock.h) produces
 * exactly ONE *out_response per dispatched request, while
 * rcp_ep_iseled_encode_response_fragmented() can genuinely need to
 * produce SEVERAL response frames for one request whenever the
 * (read_size-capped) response data exceeds one fragment's own
 * max_fragment_payload -- and concluded closing it would need "a new
 * mock.h entry point returning more than one frame per dispatched
 * request". That entry point now exists:
 * rcp_mock_server_dispatch_multi_response()/rcp_mock_server_add_endpoint_
 * multi_response() (mock.h), a new rcp_mock_endpoint_multi_response_
 * handler_fn kind alongside the plain one, not a breaking change to it.
 * iseled_dispatch_multi_handler()/test_iseled_dispatch_multi_fragment_
 * response_round_trips() below demonstrate the genuinely-multi-fragment
 * case through that real entry point end-to-end. */
typedef struct {
    uint8_t chip_data[8];
    size_t  chip_data_len;
    /* Only consulted by iseled_dispatch_multi_handler() below --
     * iseled_dispatch_handler()'s own single-fragment case still
     * hardcodes RCP_ACF_MAX_PAYLOAD, unchanged. */
    size_t  max_fragment_payload;
} iseled_dispatch_state_t;

static void iseled_dispatch_handler(const uint8_t *request, size_t request_len,
                                     rcp_bytes_t *out_response, void *user_data)
{
    iseled_dispatch_state_t *st = (iseled_dispatch_state_t *)user_data;
    const uint8_t             *tx_data;
    size_t                      tx_len;
    uint8_t                     tn;
    size_t                      n;
    rcp_bytes_t                 frames[1];

    if (rcp_ep_iseled_decode_command_request(request, request_len, 5u, &tx_data, &tx_len,
                                              &tn) != RCP_EP_ISELED_OK) {
        return;
    }

    /* REQ-ISELED-025: read_size == chip_data_len and a generous
     * max_fragment_payload together guarantee exactly one fragment for
     * this fixture's own small chip_data -- see this section's own header
     * comment for why a genuinely multi-fragment response cannot be
     * demonstrated through this handler shape at all. */
    n = rcp_ep_iseled_response_fragment_count(st->chip_data_len, (uint16_t)st->chip_data_len,
                                               RCP_ACF_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL_size_t(1u, n);
    if (n != 1u) return;

    n = rcp_ep_iseled_encode_response_fragmented(5u, st->chip_data, st->chip_data_len,
                                                  (uint16_t)st->chip_data_len, tn, false, 0u,
                                                  RCP_ACF_MAX_PAYLOAD, frames);
    if (n != 1u) return;
    *out_response = frames[0];
}

static void test_iseled_dispatch_single_fragment_response_round_trips(void)
{
    rcp_mock_server_t        *srv = rcp_mock_server_new();
    iseled_dispatch_state_t   st  = {{0x11, 0x22, 0x33, 0x44}, 4u, 0u};
    rcp_bytes_t                req;
    rcp_bytes_t                resp = {0};
    const uint8_t              *out_rx_data;
    size_t                      out_rx_len;
    bool                        out_timed;
    uint64_t                    out_ts;
    uint8_t                     out_tn;

    TEST_ASSERT_NOT_NULL(srv);
    gap_to_rcp_configured(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                      rcp_mock_server_add_endpoint(srv, 5u, 0u, true, iseled_dispatch_handler, &st));

    req = rcp_ep_iseled_encode_command_request(5u, (const uint8_t *)"\x01\x02", 2u, 0x44u);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch(srv, 5u, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB,
                                                true, 1u, req.data, req.len, &resp));
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
                      rcp_ep_iseled_decode_response(resp.data, resp.len, 5u, &out_rx_data,
                                                    &out_rx_len, &out_timed, &out_ts, &out_tn));
    TEST_ASSERT_EQUAL_size_t(4u, out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(st.chip_data, out_rx_data, 4u);
    TEST_ASSERT_EQUAL_UINT8(0x44u, out_tn);

    rcp_bytes_free(&req);
    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* REQ-ISELED-025: the genuinely-multi-fragment counterpart to
 * iseled_dispatch_handler() above -- a rcp_mock_endpoint_multi_response_
 * handler_fn (mock.h), registered via rcp_mock_server_add_endpoint_
 * multi_response() instead of the plain rcp_mock_server_add_endpoint().
 * Otherwise decodes/re-encodes identically, but writes directly into
 * out_responses[] (rcp_ep_iseled_encode_response_fragmented()'s own
 * out_frames parameter) rather than a single *out_response. */
static void iseled_dispatch_multi_handler(const uint8_t *request, size_t request_len,
                                           rcp_bytes_t *out_responses, size_t out_cap,
                                           size_t *out_count, void *user_data)
{
    iseled_dispatch_state_t *st = (iseled_dispatch_state_t *)user_data;
    const uint8_t             *tx_data;
    size_t                      tx_len;
    uint8_t                     tn;
    size_t                      n;

    *out_count = 0;
    if (rcp_ep_iseled_decode_command_request(request, request_len, 5u, &tx_data, &tx_len,
                                              &tn) != RCP_EP_ISELED_OK) {
        return;
    }

    n = rcp_ep_iseled_response_fragment_count(st->chip_data_len, (uint16_t)st->chip_data_len,
                                               st->max_fragment_payload);
    if (n == 0u || n > out_cap) return;

    *out_count = rcp_ep_iseled_encode_response_fragmented(
        5u, st->chip_data, st->chip_data_len, (uint16_t)st->chip_data_len, tn, false, 0u,
        st->max_fragment_payload, out_responses);
}

/* REQ-ISELED-025: closes this requirement's own last-remaining gap --
 * chip_data (8 octets) capped by a deliberately small
 * max_fragment_payload (3 octets) forces ceil(8/3) = 3 fragments, real
 * multi-fragment aggregation TC18 §13.7.12.1 describes, delivered
 * through a real rcp_mock_server_dispatch_multi_response() call, not
 * just the fragmentation primitives tested in isolation
 * (test_ep_iseled.c) or the artificially-single-fragment case above. */
static void test_iseled_dispatch_multi_fragment_response_round_trips(void)
{
    rcp_mock_server_t      *srv = rcp_mock_server_new();
    iseled_dispatch_state_t st  = {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}, 8u, 3u};
    rcp_bytes_t              req;
    rcp_bytes_t              responses[8] = {{0}};
    size_t                   response_count = 0;
    uint8_t                  reassembled[16];
    size_t                   reassembled_len = 0;
    size_t                   i;

    TEST_ASSERT_NOT_NULL(srv);
    gap_to_rcp_configured(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_mock_server_add_endpoint_multi_response(
                                        srv, 5u, 0u, true, iseled_dispatch_multi_handler, &st));

    req = rcp_ep_iseled_encode_command_request(5u, (const uint8_t *)"\x01\x02", 2u, 0x44u);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_multi_response(
                          srv, 5u, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, true, 1u, req.data,
                          req.len, responses, 8u, &response_count));

    /* The point of this test: genuinely MORE than one response frame,
     * not the single-fragment case iseled_dispatch_handler() already
     * covers. */
    TEST_ASSERT_EQUAL_size_t(3u, response_count);

    for (i = 0; i < response_count; i++) {
        const uint8_t *out_rx_data;
        size_t          out_rx_len;
        bool            out_timed;
        uint64_t        out_ts;
        uint8_t         out_tn;

        TEST_ASSERT_NOT_NULL(responses[i].data);
        TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
                          rcp_ep_iseled_decode_response(responses[i].data, responses[i].len, 5u,
                                                        &out_rx_data, &out_rx_len, &out_timed,
                                                        &out_ts, &out_tn));
        TEST_ASSERT_EQUAL_UINT8(0x44u, out_tn);
        TEST_ASSERT_TRUE(reassembled_len + out_rx_len <= sizeof(reassembled));
        rcp_memcpy_bounded(&reassembled[reassembled_len], sizeof(reassembled) - reassembled_len, out_rx_data, out_rx_len);
        reassembled_len += out_rx_len;
    }

    /* Every fragment's own payload reassembles back to the exact
     * original chip_data, in order -- proving this is real, correct
     * response-aggregation, not just three frames of arbitrary content. */
    TEST_ASSERT_EQUAL_size_t(st.chip_data_len, reassembled_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(st.chip_data, reassembled, st.chip_data_len);

    rcp_bytes_free(&req);
    for (i = 0; i < response_count; i++) rcp_bytes_free(&responses[i]);
    rcp_mock_server_destroy(srv);
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

/* REQ-MDIO-021 PARTIAL (2026-08-12, dedicated investigation, user-approved
 * documented assumptions -- see the file header cross-reference below and
 * TC18_spec_defects_report.md items 25/26/55, canonical path
 * /Users/matt/Documents/Coding/SoundMatt/, NOT in this repo): a request's
 * payload now begins with a real, wire-encoded mdio_mode octet
 * (ep_mdio.h's own "mdio_mode" section documents the full investigation),
 * closing this test's own original "no mdio_mode field at all" DEVIATION.
 * UPDATED 2026-08-13: TC18's own MMS addressing family is now fully
 * encodable and interpretable too, via the new rcp_ep_mdio_mms_*()
 * function family (REQ-MDIO-022/024, ep_mdio.h's own "MMS addressing"
 * section, test_ep_mdio.c's own dedicated coverage) -- this was the
 * last still-open item this file's own original DEVIATION comment
 * flagged. See test_mdio_decode_rejects_mms_mode_fails_closed() below
 * for what's still (permanently, by design) true: the MMD-family
 * decoder itself still refuses an MMS-mode frame. */
static void test_mdio_request_prefix_now_carries_a_two_bit_mode_field(void)
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

    /* mdio_mode(1) + clause/prtad/devad/regad(5) + word_count(2) = 8. */
    TEST_ASSERT_EQUAL_size_t(8u, pay_len);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_MDIO_MODE_MMD_MULTI, payload[0]); /* word_count=3 */
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_MDIO_CLAUSE_45, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x1Fu, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0xBEu, payload[4]);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, payload[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, payload[6]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, payload[7]);
    rcp_bytes_free(&f);
}

/* UPDATED 2026-08-13 (REQ-MDIO-021 now fully IMPLEMENTED, REQ-MDIO-022/024):
 * MMS addressing is no longer unsupported by this module as a whole --
 * see ep_mdio.h's own "MMS addressing" section and the new
 * rcp_ep_mdio_decode_mms_read_request()/_write_request() family
 * (test_ep_mdio.c has the dedicated coverage). What THIS test still
 * correctly pins is narrower and permanent: the MMD-FAMILY decoder
 * specifically still refuses an MMS-mode frame (use the *_mms_* family
 * for those instead) -- RCP_EP_MDIO_ERR_UNSUPPORTED_MMS keeps its name
 * for source compatibility even though MMS itself is no longer
 * unsupported; see ep_mdio.h's own updated doc comment on that error
 * value. No longer a DEVIATION at all -- this is now this module's own
 * intentional family-routing behavior, mirrored by
 * test_mms_read_request_decode_rejects_mmd_mode() (test_ep_mdio.c) in
 * the other direction. */
static void test_mdio_decode_rejects_mms_mode_fails_closed(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[8] = {0};

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMS_SINGLE;
    payload[7] = 1; /* word_count -- otherwise a well-formed request */

    hdr.byte_bus_id = 0x51u;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_UNSUPPORTED_MMS, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 0x51u, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
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

    /* UPDATED 2026-08-13: REQ-MDIO-022 is now IMPLEMENTED via the new
     * MMS-specific rcp_ep_mdio_mms_*() family (ep_mdio.h's own "MMS
     * addressing" section; test_ep_mdio.c has the dedicated 32-vs-16-bit
     * coverage, e.g. test_mms_pack_len_32bit_for_mms0()). What this test
     * still correctly demonstrates is narrower and still true: THIS
     * (MMD-family) word codec -- rcp_ep_mdio_word_encode()/
     * _pack_len()/_word_count_of()/_unpack_word_at() -- is
     * unconditionally 16-bit, by design, and always will be: it is the
     * MMD family's own codec, and MMD data fields are always 16 bits per
     * TC18 Table 60 regardless of this fix. A 32-bit MMS0 value handed
     * to THIS (wrong) codec is still misparsed as two 16-bit halves --
     * that remains correct, expected behavior demonstrating why a caller
     * must route to the right family by mms/mode, not a residual gap. */
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
    RUN_TEST(test_uart_rx_fifo_size_bounds_nothing_overflow_flag_left_uninterpreted);
    RUN_TEST(test_uart_read_completion_decision);
    RUN_TEST(test_uart_compound_wait_now_resolved_via_generic_primitive);
    RUN_TEST(test_uart_read_size_above_one_octet_round_trips);
    RUN_TEST(test_uart_register_units_diverge_from_table_48);
    RUN_TEST(test_uart_stop_bits_now_representable_at_half_unit_precision);

    RUN_TEST(test_adc_value_width_and_named_analog_input_signal);
    RUN_TEST(test_adc_functional_cfg_has_clock_status_and_interval_fields);
    RUN_TEST(test_adc_average_interval_itself_has_no_timing_awareness);
    RUN_TEST(test_adc_validate_sample_spacing_distinguishes_even_from_ragged);
    RUN_TEST(test_adc_validate_sample_spacing_respects_tolerance);
    RUN_TEST(test_adc_validate_sample_spacing_fails_open_without_a_real_clock);
    RUN_TEST(test_adc_validate_sample_spacing_rejects_non_monotonic_timestamps);
    RUN_TEST(test_adc_pipeline_is_stateless_by_design_and_cadence_deviation_pin);
    RUN_TEST(test_adc_dispatch_accumulates_across_executions_before_responding);

    RUN_TEST(test_lin_trigger_now_honours_trailing_time_and_block_has_registers);

    RUN_TEST(test_can_frame_format_values_match_table_54);
    RUN_TEST(test_can_base_identifier_is_right_aligned_and_data_only);
    RUN_TEST(test_can_register_block_round_trips_ep_status_and_status_fields);
    RUN_TEST(test_can_ep_enable_clr_clear_bit_is_wire_bit_4);
    RUN_TEST(test_can_block_lacks_receive_filter_table);
    RUN_TEST(test_can_new_physical_layer_is_selected_per_frame);

    RUN_TEST(test_iseled_response_has_no_read_size_ceiling);
    RUN_TEST(test_iseled_block_now_has_collect_resp_nr_leds_and_rcv_timeout);
    RUN_TEST(test_iseled_dispatch_single_fragment_response_round_trips);
    RUN_TEST(test_iseled_dispatch_multi_fragment_response_round_trips);

    RUN_TEST(test_mdio_block_now_exposes_ep_status_via_reconfig);
    RUN_TEST(test_mdio_request_prefix_now_carries_a_two_bit_mode_field);
    RUN_TEST(test_mdio_decode_rejects_mms_mode_fails_closed);
    RUN_TEST(test_mdio_data_fields_are_unconditionally_sixteen_bit);

    return UNITY_END();
}
