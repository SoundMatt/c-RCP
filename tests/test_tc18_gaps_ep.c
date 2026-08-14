/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ACF-017
//cfusa:test REQ-ACF-018
//cfusa:test REQ-ACF-019
//cfusa:test REQ-ACF-020
//cfusa:test REQ-ACF-021
//cfusa:test REQ-GPIO-033
//cfusa:test REQ-GPIO-034
//cfusa:test REQ-GPIO-035
//cfusa:test REQ-GPIO-036
//cfusa:test REQ-SPI-033
//cfusa:test REQ-SPI-034
//cfusa:test REQ-SPI-035
//cfusa:test REQ-SPI-036
//cfusa:test REQ-SPI-037
//cfusa:test REQ-I2C-019
//cfusa:test REQ-I2C-020
//cfusa:test REQ-PWM-055
//cfusa:test REQ-PWM-056
//cfusa:test REQ-PWM-057
//cfusa:test REQ-PWM-058
//cfusa:test REQ-WAKEUP-017
//cfusa:test REQ-WAKEUP-018
//cfusa:test REQ-WAKEUP-019
//cfusa:test REQ-WAKEUP-020
//cfusa:test REQ-WAKEUP-021
//cfusa:test REQ-WAKEUP-022
//cfusa:test REQ-E2E-028
//cfusa:test REQ-E2E-029
//cfusa:test REQ-E2E-030
//cfusa:test REQ-E2E-045
//cfusa:test REQ-E2E-046

/* test_tc18_gaps_ep.c -- a spec-literal conformance-and-deviation suite for
 * the TC18 clauses catalogued by the v0.105.0 requirements-corpus
 * completeness pass that this library's endpoint functional-configuration
 * register blocks and request handling (TC18 13.7.1-13.7.7), the ACF
 * byte_message_info header (11.2), and the E2E safe-point framing (12.7.7)
 * either implement, implement only in part, or do not implement at all.
 *
 * For a requirement catalogued "implemented", the test below asserts the
 * specified behaviour literally, against the constant the cited TC18 clause
 * itself fixes. For a requirement catalogued "partial" or "not-implemented",
 * the test is a DEVIATION PIN: it asserts the current, real, observable
 * behaviour of this code, with a comment naming the TC18 clause that is not
 * met and what a conforming implementation would do instead. Such a test
 * failing means the deviation was closed (or moved) -- the requirement's
 * status and this test must then be updated together. */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/e2e.h>
#include <rcp/ep_gpio.h>
#include <rcp/ep_i2c.h>
#include <rcp/ep_pwm.h>
#include <rcp/ep_spi.h>
#include <rcp/ep_wakeup.h>
#include <rcp/errors.h>
#include <rcp/lifecycle.h>
#include <rcp/mock.h>
#include <rcp/power.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/request_timed.h>
#include <rcp/server.h>

#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Shared mock.c dispatch fixture (issue #338, PR D) ─────────────────────
 *
 * mock.c's own file header states its design deliberately: "This module
 * owns none of the per-endpoint wire semantics itself (it never calls into
 * ep_gpio.c or any sibling directly) -- a caller registers one handler per
 * byte_bus_id, and that handler is free to use whichever ep_*.h... encode/
 * decode functions it is testing." Closing "no real dispatch path calls
 * this primitive yet" (REQ-GPIO-035/036's own text) therefore does NOT mean
 * modifying mock.c itself -- that would contradict its own stated
 * architecture -- it means proving a REAL caller (this test file, acting as
 * exactly the kind of caller that header describes) exercises the
 * primitive through mock.c's existing, unmodified dispatch path. */
static const rcp_lifecycle_plausibility_snapshot_t GAP_EMPTY_SNAP = {NULL, 0, NULL, 0};

/* Any byte_bus_id passes rcp_lifecycle_should_accept() once HW_CONFIGURED
 * -- see lifecycle.h's own rule; a plain-{0} writer is correct for this
 * specific transition (HW_UNCONFIGURED -> HW_CONFIGURED does not consult
 * one), matching test_tc18_gaps_e2e.c's own identical to_hw_configured(). */
static void gap_to_hw_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &GAP_EMPTY_SNAP,
                                                 none, true));
}

/* HW_CONFIGURED only accepts EP0 traffic (TC18 §12.3.1.2) -- a non-EP0
 * byte_bus_id needs RCP_CONFIGURED, matching test_tc18_gaps_e2e.c's own
 * identical to_rcp_configured(). GAP_EMPTY_SNAP's zero endpoint/request-
 * stream counts trivially satisfy both plausibility checks. */
static void gap_to_rcp_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    gap_to_hw_configured(srv);
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &GAP_EMPTY_SNAP,
                                                 root, true));
}

/* ── ACF byte_message_info (TC18 11.2.1 Table 4 / 11.2.2.1 Table 6) ───────── */

/* REQ-ACF-017 (implemented): the two acf_msg_type values TC18 11.2.1 Table 4
 * and 11.2.2.1 Table 6 assign to ACF_ABB and ACF_GBB.
 * REQ-ACF-019 (implemented): the 3-state application enum maps onto the
 * single op wire bit at octet 6 bit 7 -- 0b for a read (a response with data
 * is expected), 1b otherwise; a decode never reproduces RCP_ACF_OP_NONE. */
static void test_acf_msg_type_constants_and_op_wire_bit(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t              *payload     = NULL;
    size_t                      payload_len = 0;
    rcp_bytes_t                 frame;

    TEST_ASSERT_EQUAL_HEX8(0x0Eu, RCP_ACF_MSG_TYPE_ABB);
    TEST_ASSERT_EQUAL_HEX8(0x0Du, RCP_ACF_MSG_TYPE_GBB);

    /* op = RCP_ACF_OP_NONE is encode-only: it must land on the wire as the
     * write sense (bit set) and decode back as RCP_ACF_OP_WRITE. */
    hdr.op = RCP_ACF_OP_NONE;
    frame  = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_HEX8(0x80u, (uint8_t)(frame.data[6] & 0x80u));
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_WRITE, out.op);
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, out.acf_msg_type);
    rcp_bytes_free(&frame);

    hdr.op = RCP_ACF_OP_READ;
    frame  = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(frame.data[6] & 0x80u));
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_READ, out.op);
    rcp_bytes_free(&frame);
}

/* REQ-ACF-018 (partial) DEVIATION PIN: TC18 11.2.1 Table 4 / 11.2.2.1 Table 6
 * make the 12-bit read_size_or_segment_num field mean read_size when op == 0
 * (read) and segment_num otherwise. A conforming API would report which of
 * the two a caller is looking at; c-RCP exposes one uninterpreted slot whose
 * decoded value is identical under either op sense. (This test used to also
 * carry REQ-ACF-020's own byte_bus_id-width deviation pin -- split out into
 * test_acf_bus_id_is_now_eleven_bits_wide below once that gap closed, so
 * this test's own remaining scope is exactly its own single requirement.) */
static void test_acf_read_size_slot_is_ambiguous(void)
{
    rcp_acf_byte_message_info_t hdr    = {0};
    rcp_acf_byte_message_info_t out    = {0};
    uint8_t                     packed[8];

    hdr.read_size_or_segment_num = 0x1321u; /* 12-bit field: 0x321 survives */

    hdr.op = RCP_ACF_OP_READ;
    rcp_acf_pack_header(packed, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(packed, &out));
    TEST_ASSERT_EQUAL_HEX16(0x321u, out.read_size_or_segment_num);

    hdr.op = RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(packed, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(packed, &out));
    /* Same slot, same value: nothing selects the segment_num reading. */
    TEST_ASSERT_EQUAL_HEX16(0x321u, out.read_size_or_segment_num);
}

/* REQ-ACF-020: byte_bus_id is 11 bits on the wire (Figure 7: [10:8] in
 * octet 2 bits 2:0, [7:0] in octet 3). Fixed (REQ-RMAP-053's own
 * companion requirement): rcp_byte_bus_id_t is now wide enough to hold
 * the full range, so endpoint identifiers up to 2047 are representable,
 * encodable, and decodable -- split out of the combined test above,
 * which used to also pin this as a still-open deviation. */
static void test_acf_bus_id_is_now_eleven_bits_wide(void)
{
    rcp_acf_byte_message_info_t hdr    = {0};
    rcp_acf_byte_message_info_t out    = {0};
    uint8_t                     packed[8];

    hdr.byte_bus_id = 0x1FFu; /* endpoint 511 -- above the old 8-bit ceiling */
    hdr.op          = RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(packed, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL_HEX8(0x01u, (uint8_t)(packed[2] & 0x07u)); /* [10:8] now real */
    TEST_ASSERT_EQUAL_HEX8(0xFFu, packed[3]);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(packed, &out));
    TEST_ASSERT_EQUAL_UINT16(0x1FFu, out.byte_bus_id);
}

/* REQ-ACF-021: TC18 11.2.1 Table 4 / 11.2.2.3 Table 8 require an encoded
 * request to carry rsv = 00b, hs = 0b, err = 0b and rsp = 0b (rsp = 0b is
 * what makes a message a request rather than a response), and a received
 * message with rsp set must not be admitted as a request.
 *
 * rcp_acf_pack_header()/_encode_abb() still round-trip hs, cs, rsp, err and
 * ms verbatim -- that is deliberate and unchanged: those two functions are
 * shared with RESPONSE encoding (rcp_acf_build_error_response() sets
 * rsp=1/err=1 on purpose), so they cannot force these fields to their
 * request-only fixed values unconditionally. rsv (octet 2 bits 4:3 and
 * octet 4 bits 3:2) is the one field genuinely always forced to zero by
 * rcp_acf_pack_header() itself, for either message shape.
 *
 * What closes the gap: rcp_acf_request_header_constraints_valid() is the
 * pure validator a caller building a request can check before encoding,
 * and rcp_server_endpoint_admit() now calls rcp_acf_header_is_request()
 * on every arriving frame and refuses admission (RCP_SERVER_ADMIT_REJECTED,
 * RCP_ERROR_INVALID_PARAMETER) when rsp is set -- TC18 11.2.2.3's own
 * admission rule, not just an encode-time nicety. */
static void test_acf_request_flags_round_trip_but_admission_now_rejects_rsp(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t              *payload     = NULL;
    size_t                      payload_len = 0;
    rcp_bytes_t                 frame;
    rcp_server_endpoint_t       ep;
    uint8_t                     request_type = 0xFFu;
    rcp_wire_error_t            admit_err    = RCP_ERROR_NONE;

    hdr.hs  = 1u;
    hdr.cs  = 1u;
    hdr.rsp = 1u;
    hdr.err = 1u;
    hdr.ms  = 1u;
    hdr.op  = RCP_ACF_OP_READ;

    /* This header fails the request-constraint validator on every one of
     * hs/rsp/err (cs is exempted only when the caller says it carries a
     * meaning of its own -- neither case applies here). */
    TEST_ASSERT_FALSE(rcp_acf_request_header_constraints_valid(&hdr, false));
    TEST_ASSERT_FALSE(rcp_acf_request_header_constraints_valid(&hdr, true)); /* still fails: hs/rsp/err */

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    /* rsv (octet 2 bits 4:3 and octet 4 bits 3:2) IS forced to zero. */
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(frame.data[2] & 0x18u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(frame.data[4] & 0x0Cu));

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(1u, out.hs);  /* round-tripped verbatim, by design -- see above */
    TEST_ASSERT_EQUAL_UINT8(1u, out.rsp);
    TEST_ASSERT_EQUAL_UINT8(1u, out.err);
    TEST_ASSERT_EQUAL_UINT8(1u, out.cs);
    TEST_ASSERT_EQUAL_UINT8(1u, out.ms);
    TEST_ASSERT_FALSE(rcp_acf_header_is_request(&out)); /* rsp=1 -- this is a response shape */

    /* FIXED: the rsp = 1b message is no longer admitted as an ordinary
     * request -- TC18 11.2.2.3's own rule is now enforced at admission. */
    rcp_server_endpoint_init(&ep, true);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_REJECTED,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u, &request_type,
                                                NULL, &admit_err));
    TEST_ASSERT_EQUAL(RCP_ERROR_INVALID_PARAMETER, admit_err);
    rcp_server_endpoint_destroy(&ep);
    rcp_bytes_free(&frame);
}

/* REQ-ACF-018: the 12-bit read_size_or_segment_num field is read_size when
 * op selects the read sense and segment_num otherwise -- a function of op
 * alone, independent of the field's actual numeric value. */
static void test_acf_read_size_or_segment_num_kind_follows_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RSS_READ_SIZE, rcp_acf_read_size_or_segment_num_kind(&hdr));

    hdr.op = RCP_ACF_OP_WRITE;
    TEST_ASSERT_EQUAL(RCP_ACF_RSS_SEGMENT_NUM, rcp_acf_read_size_or_segment_num_kind(&hdr));

    /* RCP_ACF_OP_NONE is encode-only (see rcp_acf_op_t's doc comment) and
     * encodes identically to WRITE on the wire -- classifies the same way. */
    hdr.op = RCP_ACF_OP_NONE;
    TEST_ASSERT_EQUAL(RCP_ACF_RSS_SEGMENT_NUM, rcp_acf_read_size_or_segment_num_kind(&hdr));
}

/* REQ-ACF-021: cs_has_meaning=true is the compound-wait/chained exemption --
 * an otherwise-conforming header (hs=rsp=err=0) with cs=1 is valid only
 * when the caller asserts cs carries a meaning of its own. */
static void test_acf_request_header_constraints_cs_exemption(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    TEST_ASSERT_TRUE(rcp_acf_request_header_constraints_valid(&hdr, false));
    TEST_ASSERT_TRUE(rcp_acf_request_header_constraints_valid(&hdr, true));

    hdr.cs = 1u;
    TEST_ASSERT_FALSE(rcp_acf_request_header_constraints_valid(&hdr, false));
    TEST_ASSERT_TRUE(rcp_acf_request_header_constraints_valid(&hdr, true));
}

/* ── GPIO endpoint (TC18 13.7.4) ──────────────────────────────────────────── */

/* FIXED 2026-08-12 (issue #201, REQ-GPIO-033): TC18 13.7.4.1 fixes the GPIO
 * request payload at exactly four octets, with an endpoint of fewer than
 * 32 pins mapped onto the least-significant bits (this codebase's fixed
 * bit-index n <-> pin IOn encoding satisfies that trivially, regardless of
 * how many pins a real instance physically has -- verified below), and
 * rejects a violation with error code INVALID_PARAMETER. c-RCP always
 * enforced the length; what was missing was a way to produce the correct
 * numbered wire code for that rejection -- rcp_ep_gpio_wire_error() now
 * maps RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN to RCP_ERROR_INVALID_PARAMETER, so
 * a caller building an Error Response frame (acf.h's
 * rcp_acf_build_error_response()) has the TC18-conformant code available. */
static void test_gpio_request_payload_is_four_octets(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    uint8_t                     short_p[3] = {0x01u, 0x02u, 0x03u};
    rcp_bytes_t                 frame;
    uint32_t                    bitmask = 0;
    uint8_t                     evt = 0, tn = 0;

    TEST_ASSERT_EQUAL_UINT(4u, RCP_EP_GPIO_PAYLOAD_LEN);
    TEST_ASSERT_EQUAL_UINT8(32u, RCP_EP_GPIO_MAX_PINS);
    /* Least-significant-bit-first mapping: pin 0 is bit 0 of the 32-bit
     * payload, pin 31 its most significant bit. */
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, rcp_ep_gpio_pin_mask(0u));
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, rcp_ep_gpio_pin_mask(31u));
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_get(0x00000001u, 0u));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_get(0x00000001u, 1u));

    frame = rcp_ep_gpio_encode_write_request(7u, 0x0000000Fu, RCP_EP_GPIO_WRITE_REPLACE, 0x21u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                      rcp_ep_gpio_decode_write_request(frame.data, frame.len, 7u, &bitmask, &evt,
                                                       &tn));
    TEST_ASSERT_EQUAL_HEX32(0x0000000Fu, bitmask);
    rcp_bytes_free(&frame);

    hdr.byte_bus_id = 7u;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, short_p, sizeof(short_p));
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
                      rcp_ep_gpio_decode_write_request(frame.data, frame.len, 7u, &bitmask, &evt,
                                                       &tn));
    /* The numbered wire code TC18 13.7.4.1 names for this case is now
     * reachable via rcp_ep_gpio_wire_error(). */
    {
        /* Held in ints, not compared as their own enum types: MSVC's C5287
         * rejects a direct comparison of two different enumerations. */
        const int wire_code = (int)rcp_ep_gpio_wire_error(RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN);

        TEST_ASSERT_EQUAL_INT(15, wire_code);
        TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_INVALID_PARAMETER, wire_code);
    }
    rcp_bytes_free(&frame);
}

/* Every other rcp_ep_gpio_errc_t value is a local framing/routing outcome
 * with no numbered wire-error-code counterpart -- see
 * rcp_ep_gpio_wire_error()'s own doc comment (ep_gpio.h). */
static void test_gpio_wire_error_is_none_for_local_only_codes(void)
{
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE, (int)rcp_ep_gpio_wire_error(RCP_EP_GPIO_OK));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_gpio_wire_error(RCP_EP_GPIO_ERR_SHORT_FRAME));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_gpio_wire_error(RCP_EP_GPIO_ERR_BAD_MSG_TYPE));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_gpio_wire_error(RCP_EP_GPIO_ERR_WRONG_BUS));
    TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_NONE,
                          (int)rcp_ep_gpio_wire_error(RCP_EP_GPIO_ERR_WRONG_OP));
}

/* FIXED 2026-08-14 (issue #426, REQ-GPIO-012): TC18 §13.5 Table 33's
 * GPIO/PWM_OUT row states a two-part rule for evt[2:0]=100b
 * (RCP_EP_GPIO_WRITE_RESERVED4): "reserved -- request shall be ignored
 * and an err-response with error code = UNSUPPORTED_CMD shall be sent".
 * Both halves are now exercised here: rcp_ep_gpio_apply_write() still
 * ignores the request (returns current unchanged), and
 * rcp_ep_gpio_decode_write_request() now returns the dedicated
 * RCP_EP_GPIO_ERR_RESERVED_EVT for that same evt value, which
 * rcp_ep_gpio_wire_error() maps to RCP_ERROR_UNSUPPORTED_CMD -- closing
 * the previously-missing error-response half of the rule. */
static void test_gpio_reserved_evt_is_ignored_and_reports_unsupported_cmd(void)
{
    rcp_bytes_t frame;
    uint32_t    bitmask = 0xFFFFFFFFu;
    uint8_t     evt = 0xFFu, tn = 0xFFu;

    /* The "ignored" half: apply_write() leaves the register unchanged. */
    TEST_ASSERT_EQUAL_UINT32(0x12345678u,
        rcp_ep_gpio_apply_write(0x12345678u, 0xDEADBEEFu, RCP_EP_GPIO_WRITE_RESERVED4));

    /* The "err-response" half: decode_write_request() now rejects it,
     * without touching any of the output parameters. */
    frame = rcp_ep_gpio_encode_write_request(9u, 0xDEADBEEFu, RCP_EP_GPIO_WRITE_RESERVED4, 0x55u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_RESERVED_EVT,
                      rcp_ep_gpio_decode_write_request(frame.data, frame.len, 9u, &bitmask, &evt,
                                                       &tn));
    /* Outputs are untouched on this error path. */
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, bitmask);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, evt);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, tn);
    rcp_bytes_free(&frame);

    /* And the numbered wire code TC18 Table 33 names for this case is
     * reachable via rcp_ep_gpio_wire_error(). */
    {
        const int wire_code = (int)rcp_ep_gpio_wire_error(RCP_EP_GPIO_ERR_RESERVED_EVT);

        TEST_ASSERT_EQUAL_INT(1, wire_code);
        TEST_ASSERT_EQUAL_INT((int)RCP_ERROR_UNSUPPORTED_CMD, wire_code);
    }
}

/* REQ-GPIO-034 (partial) DEVIATION PIN: the three trigger CONDITIONS exist,
 * but TC18 13.7.4.1 Table 40's trigger signal NUMBERING (0 = execution done;
 * per pin IOn: 3n+1 change, 3n+2 rising, 3n+3 falling, up to 96 for IO31
 * falling) is not represented -- the selector is a 4-value per-pin enum whose
 * whole range is 0..3, so no Table 40 signal number above 3 can be named.
 * REQ-GPIO-035 (partial) DEVIATION PIN: FIXED 2026-08-11 (issue #256 Group
 * G, REQ-GPIO-013) as far as storage goes -- ep_status/clk_divider/
 * debounce[32] now exist and are wire-addressable via the real EP_func
 * mechanism (see ep_gpio.h's own file header). FIXED 2026-08-13 (issue
 * #336): rcp_ep_gpio_debounce_sample() now implements the filtering rule
 * itself -- test_ep_gpio.c's own dedicated tests cover it directly, and
 * test_gpio_dispatch_debounces_writes_before_reporting_settled_value()
 * (below) proves a real mock.c dispatch path exercises it. Still not
 * implemented: gpio_base_clk always renders 0 (this module defines no
 * real GPIO clock source, the same architecture-wide constant every other
 * endpoint type's own base_clk field already establishes). */
static void test_gpio_trigger_numbering_and_functional_cfg_gaps(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;

    TEST_ASSERT_EQUAL_INT(0, (int)RCP_EP_GPIO_TRIGGER_NONE);
    TEST_ASSERT_EQUAL_INT(3, (int)RCP_EP_GPIO_TRIGGER_FALLING);
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, true, false));
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, true, false));
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, false, true));

    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.pins[0].pin_property);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_NONE, cfg.pins[31].trigger);
    TEST_ASSERT_EQUAL_UINT(2u * (size_t)RCP_EP_GPIO_MAX_PINS, sizeof(cfg.pins));

    /* FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group G, REQ-GPIO-013):
     * this test used to pin the struct's exhaustiveness at pins[32] --
     * "no room for a base clock, a divider, a status word or 32 debounce
     * registers" -- as a deviation. The struct now has exactly that room:
     * ep_status, clk_divider, and debounce[32] give the EP_func register
     * block (see ep_gpio.h's own file header) somewhere real to live. */
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_ep_functional_cfg_t),
                           offsetof(rcp_ep_gpio_functional_cfg_t, pins));
    TEST_ASSERT_EQUAL_UINT(0u, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.clk_divider);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.debounce[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.debounce[RCP_EP_GPIO_MAX_PINS - 1]);
    TEST_ASSERT_EQUAL_UINT((size_t)RCP_EP_GPIO_MAX_PINS, sizeof(cfg.debounce));
}

/* REQ-GPIO-034 IMPLEMENTED (issue #336): TC18 Table 40 (RC1)/Table 43
 * (RC5)'s own trigger signal numbering -- signal 3n+1/3n+2/3n+3 for pin
 * n's ANY_CHANGE/RISING/FALLING trigger, up to signal 96 for IO31's own
 * FALLING entry. Signal 0 ("GPIO EP request execution done") is a
 * whole-endpoint trigger this per-pin function deliberately does not
 * model -- see rcp_ep_gpio_trigger_signal_number()'s own doc comment. */
static void test_gpio_trigger_signal_numbering(void)
{
    uint8_t signal;

    /* Pin 0's own three trigger signals: the table's first three
     * non-zero rows, verbatim. */
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_signal_number(0u, RCP_EP_GPIO_TRIGGER_ANY_CHANGE,
                                                        &signal));
    TEST_ASSERT_EQUAL_UINT8(1u, signal);
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_signal_number(0u, RCP_EP_GPIO_TRIGGER_RISING, &signal));
    TEST_ASSERT_EQUAL_UINT8(2u, signal);
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_signal_number(0u, RCP_EP_GPIO_TRIGGER_FALLING, &signal));
    TEST_ASSERT_EQUAL_UINT8(3u, signal);

    /* Pin 1's own first signal (4) -- confirms the table's own "IO1
     * signal change" row, one pin over from pin 0's own three. */
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_signal_number(1u, RCP_EP_GPIO_TRIGGER_ANY_CHANGE,
                                                        &signal));
    TEST_ASSERT_EQUAL_UINT8(4u, signal);

    /* The table's own last, highest-numbered row: IO31 falling edge, 96 --
     * the upper bound this codebase's own RCP_EP_GPIO_MAX_PINS (32) and
     * the table's own explicit "…96 IO31 falling edge" row agree on. */
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_signal_number(31u, RCP_EP_GPIO_TRIGGER_FALLING,
                                                        &signal));
    TEST_ASSERT_EQUAL_UINT8(96u, signal);

    /* Out-of-range pin index and the NONE trigger both correctly report
     * "no such signal" rather than fabricating a number. */
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_signal_number(32u, RCP_EP_GPIO_TRIGGER_ANY_CHANGE,
                                                         &signal));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_signal_number(0u, RCP_EP_GPIO_TRIGGER_NONE, &signal));
}

/* FIXED 2026-08-13 (issue #336, REQ-GPIO-036): rcp_ep_gpio_response_timing()
 * is now the real classifier TC18 13.7.4.3 requires -- a pure read (no
 * byte_msg_payload) is immediate; a payload-bearing read or any write
 * requires waiting the configured debounce time before responding. This
 * test used to pin the deviation ("c-RCP models no debounce and no
 * response delay"); per this file's own established convention, a
 * gap-pinning test that starts failing once the gap closes gets rewritten
 * to the conforming expectation, not deleted. */
static void test_gpio_response_timing_classifier_distinguishes_read_and_write(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RESPONSE_IMMEDIATE,
                      rcp_ep_gpio_response_timing(RCP_ACF_OP_READ, 0u));
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RESPONSE_AFTER_DEBOUNCE,
                      rcp_ep_gpio_response_timing(RCP_ACF_OP_WRITE, RCP_EP_GPIO_PAYLOAD_LEN));
}

/* ── GPIO endpoint: real mock.c dispatch path (issue #336, PR D;
 * REQ-GPIO-036's own remaining gap closed 2026-08-14) ──────────────────────
 *
 * gpio_dispatch_state_t/gpio_dispatch_handler() is this test file's own
 * rcp_mock_endpoint_handler_fn -- exactly the kind of caller-supplied
 * handler mock.h's own file header describes -- proving REQ-GPIO-035's
 * debounce filter and REQ-GPIO-036's response-timing classifier are both
 * reachable, and actually consulted, from a real dispatch() call, not
 * merely correct in isolation. Bit 0 of the bitmask is the only pin this
 * fixture debounces, to keep the two tests below minimal.
 *
 * RESOLVED 2026-08-14: this section's own prior text found a genuine
 * remaining gap -- a debounced write's own response was left
 * permanently unfabricated (mock.c had nowhere to hold it once the
 * debounce time genuinely elapsed) and concluded "actually WAITING...
 * would need a genuinely new mechanism this batch does not add". That
 * mechanism now exists: mock.h's rcp_mock_server_stash_deferred_
 * response()/_take_deferred_response(). test_gpio_dispatch_deferred_
 * write_response_is_retrievable_once_debounce_settles() below closes
 * it end-to-end. */
typedef struct {
    rcp_ep_gpio_debounce_state_t debounce;
    uint32_t                     settled_bitmask;
    uint8_t                      debounce_n;
} gpio_dispatch_state_t;

static void gpio_dispatch_handler(const uint8_t *request, size_t request_len,
                                   rcp_bytes_t *out_response, void *user_data)
{
    gpio_dispatch_state_t      *st = (gpio_dispatch_state_t *)user_data;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;

    if (rcp_acf_decode_abb(request, request_len, &hdr, &payload, &payload_len) != RCP_ACF_OK) return;

    if (hdr.op == (uint8_t)RCP_ACF_OP_WRITE) {
        uint32_t bitmask;
        uint8_t  evt;
        uint8_t  tn;

        if (rcp_ep_gpio_decode_write_request(request, request_len, 3u, &bitmask, &evt,
                                              &tn) != RCP_EP_GPIO_OK) {
            return;
        }

        /* REQ-GPIO-035: the raw just-written bit 0 does not become the
         * settled/reported value until it has been sampled n consecutive
         * times -- this handler's own "sample" is simply "a write
         * happened", the same one-sample-per-call cadence
         * rcp_ep_gpio_debounce_sample()'s own doc comment already notes a
         * caller-owned timer (gpio_base_clk/gpio_clk_divider) would
         * normally drive independently of requests. */
        if (rcp_ep_gpio_debounce_sample(&st->debounce, (bitmask & 1u) != 0, st->debounce_n, NULL)) {
            st->settled_bitmask |= 1u;
        } else {
            st->settled_bitmask &= ~1u;
        }

        /* REQ-GPIO-036: a write always requires the debounce wait before
         * responding -- this synchronous test double has no timer of its
         * own to actually wait with, so it honors the classifier's real
         * consequence the same way mock.h's own out_response convention
         * already models an endpoint that hasn't answered yet: leaving
         * *out_response zeroed (a fire-and-forget request, from this
         * call's own point of view). */
        TEST_ASSERT_EQUAL(RCP_EP_GPIO_RESPONSE_AFTER_DEBOUNCE,
                          rcp_ep_gpio_response_timing((rcp_acf_op_t)hdr.op, payload_len));
        return;
    }

    /* A pure read (op == READ, no payload -- the only shape
     * rcp_ep_gpio_encode_read_request() ever produces): immediate,
     * carrying the debounce filter's own current settled value. */
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_RESPONSE_IMMEDIATE,
                      rcp_ep_gpio_response_timing((rcp_acf_op_t)hdr.op, payload_len));
    *out_response = rcp_ep_gpio_encode_response(3u, st->settled_bitmask, hdr.transaction_num,
                                                 false, 0u);
}

/* A pure read dispatched through mock.c gets an IMMEDIATE response --
 * proving rcp_ep_gpio_response_timing() is reached and its IMMEDIATE
 * outcome genuinely produces a response in the same dispatch() call. */
static void test_gpio_dispatch_read_responds_immediately(void)
{
    rcp_mock_server_t     *srv = rcp_mock_server_new();
    gpio_dispatch_state_t  st  = {0};
    rcp_bytes_t             req;
    rcp_bytes_t             resp = {0};
    uint32_t                bitmask;
    bool                    timed;
    uint64_t                ts;
    uint8_t                 tn;

    TEST_ASSERT_NOT_NULL(srv);
    gap_to_rcp_configured(srv);
    rcp_ep_gpio_debounce_state_init(&st.debounce);
    st.debounce_n = 3u;
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                      rcp_mock_server_add_endpoint(srv, 3u, 0u, true, gpio_dispatch_handler, &st));

    req = rcp_ep_gpio_encode_read_request(3u, 0x11u);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch(srv, 3u, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB,
                                                true, 1u, req.data, req.len, &resp));
    TEST_ASSERT_NOT_NULL(resp.data); /* a real response, produced in this same call */
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                      rcp_ep_gpio_decode_response(resp.data, resp.len, 3u, &bitmask, &timed, &ts, &tn));
    TEST_ASSERT_EQUAL_HEX32(0u, bitmask); /* nothing settled yet */
    TEST_ASSERT_EQUAL_UINT8(0x11u, tn);

    rcp_bytes_free(&req);
    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* REQ-GPIO-035/036 together, through a real dispatch path: three writes
 * of the SAME raw value (n=3) don't settle -- and, per REQ-GPIO-036,
 * don't respond synchronously either -- until the third one; a
 * subsequent pure read then reports the newly-settled value, not the
 * raw one any single write carried. */
static void test_gpio_dispatch_debounces_writes_before_reporting_settled_value(void)
{
    rcp_mock_server_t     *srv = rcp_mock_server_new();
    gpio_dispatch_state_t  st  = {0};
    rcp_bytes_t             req;
    rcp_bytes_t             resp = {0};
    uint32_t                bitmask;
    bool                    timed;
    uint64_t                ts;
    uint8_t                 tn;
    int                     i;

    TEST_ASSERT_NOT_NULL(srv);
    gap_to_rcp_configured(srv);
    rcp_ep_gpio_debounce_state_init(&st.debounce);
    st.debounce_n = 3u;
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                      rcp_mock_server_add_endpoint(srv, 3u, 0u, true, gpio_dispatch_handler, &st));

    /* Three writes of bit 0 = 1: none produce a response (REQ-GPIO-036,
     * every write is AFTER_DEBOUNCE), and only the third one settles
     * (REQ-GPIO-035, n=3) -- checked after every single write, not just
     * at the end, so a debounce_n mix-up (e.g. settling after the 1st or
     * 2nd write) is caught, not just "settled by the 3rd or later". */
    for (i = 0; i < 3; i++) {
        req = rcp_ep_gpio_encode_write_request(3u, 1u, RCP_EP_GPIO_WRITE_REPLACE, 0x20u);
        TEST_ASSERT_NOT_NULL(req.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                          rcp_mock_server_dispatch(srv, 3u, RCP_AVTP_SUBTYPE_NTSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, 1u, req.data,
                                                    req.len, &resp));
        TEST_ASSERT_NULL(resp.data); /* no synchronous response for any write */
        rcp_bytes_free(&req);

        if (i < 2) {
            TEST_ASSERT_EQUAL_HEX32(0u, st.settled_bitmask); /* not yet settled */
        } else {
            TEST_ASSERT_EQUAL_HEX32(1u, st.settled_bitmask); /* settled on exactly the 3rd */
        }
    }

    /* A pure read now reports the newly-settled value. */
    req = rcp_ep_gpio_encode_read_request(3u, 0x21u);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch(srv, 3u, RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB,
                                                true, 1u, req.data, req.len, &resp));
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                      rcp_ep_gpio_decode_response(resp.data, resp.len, 3u, &bitmask, &timed, &ts, &tn));
    TEST_ASSERT_EQUAL_HEX32(1u, bitmask);
    rcp_bytes_free(&req);
    rcp_bytes_free(&resp);

    rcp_mock_server_destroy(srv);
}

/* REQ-GPIO-036: closes this requirement's own last-remaining gap --
 * "actually WAITING the configured debounce time and THEN producing a
 * response... would need a genuinely new mechanism this batch does not
 * add" (the prior batch's own text). That mechanism now exists
 * (mock.h's rcp_mock_server_stash_deferred_response()/_take_deferred_
 * response()). This test drives the SAME three-write debounce sequence
 * as the test above, but on the settling (3rd) write -- the moment
 * gpio_dispatch_handler()'s own caller (this test, not the handler
 * itself, which has no srv of its own -- see mock.h's own file header
 * note) knows the real response is now due -- stashes the response
 * that write genuinely owes, and proves it is later retrievable
 * exactly once, carrying that write's own transaction_num (not a
 * subsequent read's), closing the gap the prior test's own "no
 * synchronous response for any write" assertion left permanently
 * open. */
static void test_gpio_dispatch_deferred_write_response_is_retrievable_once_debounce_settles(void)
{
    rcp_mock_server_t     *srv = rcp_mock_server_new();
    gpio_dispatch_state_t  st  = {0};
    rcp_bytes_t             req;
    rcp_bytes_t             resp = {0};
    rcp_bytes_t             deferred = {0};
    uint32_t                bitmask;
    bool                    timed;
    uint64_t                ts;
    uint8_t                 tn;
    int                     i;
    static const uint8_t    write_tn[3] = {0x30u, 0x31u, 0x32u};

    TEST_ASSERT_NOT_NULL(srv);
    gap_to_rcp_configured(srv);
    rcp_ep_gpio_debounce_state_init(&st.debounce);
    st.debounce_n = 3u;
    TEST_ASSERT_EQUAL(RCP_MOCK_OK,
                      rcp_mock_server_add_endpoint(srv, 3u, 0u, true, gpio_dispatch_handler, &st));

    /* Nothing stashed before any write. */
    TEST_ASSERT_FALSE(rcp_mock_server_take_deferred_response(srv, 3u, &deferred));
    TEST_ASSERT_NULL(deferred.data);

    for (i = 0; i < 3; i++) {
        req = rcp_ep_gpio_encode_write_request(3u, 1u, RCP_EP_GPIO_WRITE_REPLACE, write_tn[i]);
        TEST_ASSERT_NOT_NULL(req.data);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                          rcp_mock_server_dispatch(srv, 3u, RCP_AVTP_SUBTYPE_NTSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, 1u, req.data,
                                                    req.len, &resp));
        TEST_ASSERT_NULL(resp.data); /* still no synchronous response */
        rcp_bytes_free(&req);

        if (i == 2) {
            /* The 3rd write is exactly the one that settled
             * (st.settled_bitmask just became 1, per the test above's
             * own identical check) -- this write's own response is
             * genuinely due now. The caller (this test) builds it the
             * same way gpio_dispatch_handler()'s own read path already
             * does, and stashes it. */
            TEST_ASSERT_EQUAL_HEX32(1u, st.settled_bitmask);
            deferred = rcp_ep_gpio_encode_response(3u, st.settled_bitmask, write_tn[i], false, 0u);
            TEST_ASSERT_NOT_NULL(deferred.data);
            TEST_ASSERT_TRUE(rcp_mock_server_stash_deferred_response(srv, 3u, deferred));
        }
    }

    /* Retrievable exactly once, carrying the settling WRITE's own
     * transaction_num -- not fabricated, not a later read's. */
    TEST_ASSERT_TRUE(rcp_mock_server_take_deferred_response(srv, 3u, &resp));
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                      rcp_ep_gpio_decode_response(resp.data, resp.len, 3u, &bitmask, &timed, &ts, &tn));
    TEST_ASSERT_EQUAL_HEX32(1u, bitmask);
    TEST_ASSERT_EQUAL_UINT8(write_tn[2], tn);
    rcp_bytes_free(&resp);

    /* Taken once means gone -- not a queue, not re-deliverable. */
    TEST_ASSERT_FALSE(rcp_mock_server_take_deferred_response(srv, 3u, &resp));
    TEST_ASSERT_NULL(resp.data);

    rcp_mock_server_destroy(srv);
}

/* ── SPI endpoint (TC18 13.7.3) ───────────────────────────────────────────── */

/* FIXED 2026-08-12 (issue #201, REQ-SPI-033): TC18 13.7.3.1/13.7.3.2 give
 * the SPI endpoint six independently pre-configured channels, selected by
 * a request's evt[2:0]. Six channel configurations exist and evt[2:0]
 * selects them; the deviation was only that the catalogue, not the code,
 * was missing that statement -- fixed as a text/status-only correction. */
static void test_spi_six_channels_selected_by_evt(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_bytes_t                 frame;
    const uint8_t               tx[2] = {0xAAu, 0x55u};
    const uint8_t              *rx    = NULL;
    size_t                      rx_len = 0;
    uint16_t                    read_size = 0;
    uint8_t                     channel = 0xFFu, tn = 0;

    TEST_ASSERT_EQUAL_UINT8(6u, RCP_EP_SPI_MAX_CHANNELS);
    TEST_ASSERT_TRUE(rcp_ep_spi_channel_valid(0u));
    TEST_ASSERT_TRUE(rcp_ep_spi_channel_valid(5u));
    TEST_ASSERT_FALSE(rcp_ep_spi_channel_valid(6u)); /* evt[2:0] = 110b/111b */
    TEST_ASSERT_FALSE(rcp_ep_spi_channel_valid(7u));

    rcp_ep_spi_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT(6u * sizeof(rcp_ep_spi_channel_cfg_t), sizeof(cfg.channels));

    frame = rcp_ep_spi_encode_transfer_request(9u, 5u, tx, sizeof(tx), 0u, 0x31u);
    TEST_ASSERT_NOT_NULL(frame.data);
    /* evt occupies octet 4 bits 7:4; its low three bits carry the channel. */
    TEST_ASSERT_EQUAL_UINT8(5u, (uint8_t)((frame.data[4] >> 4) & 0x07u));
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
                      rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 9u, &channel, &rx,
                                                         &rx_len, &read_size, &tn));
    TEST_ASSERT_EQUAL_UINT8(5u, channel);
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), rx_len);
    rcp_bytes_free(&frame);
}

/* REQ-SPI-034 IMPLEMENTED (issue #336): TC18 13.7.3.1's own Table 41 "spi
 * trigger outputs" (RC5 -- the citation of "Table 38" this deviation pin
 * used to carry was stale; RC5's own Table 38 is the unrelated RC-Server
 * worked example, corrected alongside this fix) numbers fourteen SPI
 * trigger outputs per channel -- 0 execution done (whole-endpoint), 1
 * reserved, and 2+2n/3+2n pairing CSn with an asserted/de-asserted event
 * (0 <= n < 16, narrowed to this module's own 6 channels). c-RCP's
 * per-channel trigger selector stays the deliberately-collapsed 4-value
 * enum described in the file header (that design is unaffected -- see
 * rcp_ep_spi_trigger_signal_number()'s own test, below, for the new,
 * separate numbering computation this fix actually adds).
 * REQ-SPI-035 FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I): Table
 * 39's per-channel register block used to be modelled only in reduced form
 * (no spi_baud_rateN, spi_use_csN, spi_bits_maxN, or spi_clk-cycle lead/
 * trail/pause times, and no wire render/parse path reaching any of it at
 * all). rcp_ep_spi_render_registers()/_apply_reconfig() (evt[2:0] == 111b,
 * TC18 Table 30's own SPI row + §12.7.1) now model the whole block; this
 * test pins that rcp_ep_spi_channel_cfg_t carries every field the wire
 * register block needs, and that CPOL/CPHA still round-trip losslessly
 * through the existing `mode` byte rather than needing two new fields. */
static void test_spi_trigger_numbering_and_channel_cfg_full(void)
{
    rcp_ep_spi_channel_cfg_t ch = {0};

    TEST_ASSERT_EQUAL_INT(0, (int)RCP_EP_SPI_TRIGGER_NONE);
    TEST_ASSERT_EQUAL_INT(3, (int)RCP_EP_SPI_TRIGGER_CS_DEASSERT);
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_ASSERT,
                                              RCP_EP_SPI_EVENT_CS_ASSERT));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_ASSERT,
                                               RCP_EP_SPI_EVENT_CS_DEASSERT));
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_TRANSFER_DONE,
                                              RCP_EP_SPI_EVENT_TRANSFER_DONE));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_NONE,
                                               RCP_EP_SPI_EVENT_TRANSFER_DONE));

    /* CPOL and CPHA are derived from one folded mode byte -- a deliberate,
     * lossless representation choice (rcp_ep_spi_render_registers() derives
     * the two wire bits from it, and parsing recovers the mode exactly),
     * not a missing pair of fields. */
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_1));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_1));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_2));
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_2));

    /* Struct now HAS room for every Table 39 per-channel field the old
     * "reduced form" deviation flagged as missing. */
    ch.baud_rate_kbps  = 12345u;
    ch.use_common_cs   = true;
    ch.cs_clk_leadtime  = 7u;
    ch.clk_cs_trailtime = 8u;
    ch.bits_max         = 9u;
    ch.pause_min        = 10u;
    TEST_ASSERT_EQUAL_UINT16(12345u, ch.baud_rate_kbps);
    TEST_ASSERT_TRUE(ch.use_common_cs);
    TEST_ASSERT_EQUAL_UINT8(7u, ch.cs_clk_leadtime);
    TEST_ASSERT_EQUAL_UINT8(8u, ch.clk_cs_trailtime);
    TEST_ASSERT_EQUAL_UINT8(9u, ch.bits_max);
    TEST_ASSERT_EQUAL_UINT8(10u, ch.pause_min);
}

/* REQ-SPI-034 IMPLEMENTED (issue #336): TC18 Table 41's own 2+2n/3+2n
 * per-channel trigger signal numbering -- see
 * rcp_ep_spi_trigger_signal_number()'s own doc comment. */
static void test_spi_trigger_signal_numbering(void)
{
    uint8_t signal;

    /* Channel 0's own pair: the table's first two non-zero, non-reserved
     * rows, verbatim. */
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_signal_number(0u, RCP_EP_SPI_TRIGGER_CS_ASSERT,
                                                       &signal));
    TEST_ASSERT_EQUAL_UINT8(2u, signal);
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_signal_number(0u, RCP_EP_SPI_TRIGGER_CS_DEASSERT,
                                                       &signal));
    TEST_ASSERT_EQUAL_UINT8(3u, signal);

    /* Channel 1's own pair (4/5) confirms the table's own 2+2n/3+2n
     * pattern one channel over. */
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_signal_number(1u, RCP_EP_SPI_TRIGGER_CS_ASSERT,
                                                       &signal));
    TEST_ASSERT_EQUAL_UINT8(4u, signal);

    /* This module's own highest channel (5, RCP_EP_SPI_MAX_CHANNELS-1):
     * 2+2*5=12 asserted, 3+2*5=13 de-asserted -- exactly the "signals
     * 2..13" range the requirement's own text names for 6 channels. */
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_signal_number(5u, RCP_EP_SPI_TRIGGER_CS_ASSERT,
                                                       &signal));
    TEST_ASSERT_EQUAL_UINT8(12u, signal);
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_signal_number(5u, RCP_EP_SPI_TRIGGER_CS_DEASSERT,
                                                       &signal));
    TEST_ASSERT_EQUAL_UINT8(13u, signal);

    /* Out-of-range channel, TRANSFER_DONE (signal 0 is whole-endpoint,
     * not per-channel), and NONE all correctly report "no such signal"
     * rather than fabricating a number. */
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_signal_number(RCP_EP_SPI_MAX_CHANNELS,
                                                        RCP_EP_SPI_TRIGGER_CS_ASSERT, &signal));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_signal_number(0u, RCP_EP_SPI_TRIGGER_TRANSFER_DONE,
                                                        &signal));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_signal_number(0u, RCP_EP_SPI_TRIGGER_NONE, &signal));
}

/* FIXED 2026-08-12 (issue #201, REQ-SPI-036): TC18 13.7.3.3 derives the
 * transfer length from read_size and the payload -- zero octets are appended
 * when read_size exceeds the byte_msg_payload, and the whole payload still
 * goes out on PICO when read_size is smaller. rcp_ep_spi_{en,de}code_
 * transfer_request() now carry read_size through the ACF header's own
 * read_size_or_segment_num field, and rcp_ep_spi_transfer_length() computes
 * the resulting bus transfer length (see tests/test_ep_spi.c's own
 * dedicated tests for that primitive's own case-by-case verification;
 * this test's own job is just the request-level round trip). */
static void test_spi_read_size_round_trips_through_transfer_request(void)
{
    rcp_acf_byte_message_info_t hdr       = {0};
    const uint8_t                tx[2]     = {0x01u, 0x02u};
    const uint8_t                *rx       = NULL;
    size_t                        rx_len   = 0;
    uint16_t                      read_size = 0;
    rcp_bytes_t                   frame;
    uint8_t                       channel = 0, tn = 0;

    frame = rcp_ep_spi_encode_transfer_request(9u, 1u, tx, sizeof(tx), 0x0Au, 0x44u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(frame.data, &hdr));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_READ, hdr.op); /* read sense: the slot is read_size */
    TEST_ASSERT_EQUAL_HEX16(0x0Au, hdr.read_size_or_segment_num);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
                      rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 9u, &channel, &rx,
                                                         &rx_len, &read_size, &tn));
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), rx_len);
    TEST_ASSERT_EQUAL_UINT16(0x0Au, read_size);
    TEST_ASSERT_EQUAL_UINT(0x0Au, rcp_ep_spi_transfer_length(rx_len, read_size));
    rcp_bytes_free(&frame);
}

/* REQ-SPI-037 (not-implemented) DEVIATION PIN: TC18 13.7.3.3 requires a
 * stopped SPI endpoint to latch an error state with its EP_config enable bit
 * RESET (the client must clear it and re-enable), and a clamped IO pin to set
 * the err flag in every subsequent response. c-RCP's codec is stateless: a
 * rejected request leaves the endpoint's enable bit exactly as it was. */
static void test_spi_no_error_latch(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    const uint8_t                tx[2]  = {0x01u, 0x02u};
    const uint8_t                *rx     = NULL;
    size_t                        rx_len = 0;
    uint16_t                      read_size = 0;
    rcp_bytes_t                   frame;
    uint8_t                       channel = 0, tn = 0;

    frame = rcp_ep_spi_encode_transfer_request(9u, 1u, tx, sizeof(tx), 0u, 0x44u);
    TEST_ASSERT_NOT_NULL(frame.data);

    rcp_ep_spi_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    /* A rejected request (wrong bus) neither latches an error state nor
     * clears the endpoint's enable bit. */
    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_WRONG_BUS,
                      rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 10u, &channel, &rx,
                                                         &rx_len, &read_size, &tn));
    TEST_ASSERT_TRUE(cfg.common.ep_enable);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
                      rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 9u, &channel, &rx,
                                                         &rx_len, &read_size, &tn));
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), rx_len);
    rcp_bytes_free(&frame);
}

/* ── I2C endpoint (TC18 13.7.7) ───────────────────────────────────────────── */

/* REQ-I2C-019 FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group I): TC18
 * §13.7.7.2 Table 46's Ultra-fast (5 Mbit/s) preset at i2c_mode value 4 is
 * now accepted, and the register block (i2c_clock_divider/i2c_trail/
 * i2c_base_clk/i2c_ep_status) is now modelled via
 * rcp_ep_i2c_render_registers()/_apply_reconfig() -- see
 * ep_i2c.h's file header for the Table 46 address-collision resolution
 * this required. */
static void test_i2c_mode_presets_and_register_block(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;

    rcp_ep_i2c_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_STANDARD, cfg.i2c_mode);

    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(0u));
    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(1u));
    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(2u));
    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(3u));
    /* Table 46's Ultra-fast (5 Mbit/s) preset, value 4: now accepted. */
    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(4u));
    TEST_ASSERT_FALSE(rcp_ep_i2c_mode_valid(5u));
    /* The two Table 46 rows both numbered 3 are resolved conservatively to
     * the lower numbering: High-speed sits immediately after Fast mode plus. */
    TEST_ASSERT_EQUAL_INT(2, (int)RCP_EP_I2C_MODE_FAST_PLUS);
    TEST_ASSERT_EQUAL_INT(3, (int)RCP_EP_I2C_MODE_HIGH_SPEED);
    TEST_ASSERT_EQUAL_INT(4, (int)RCP_EP_I2C_MODE_ULTRA_FAST);

    /* Struct now HAS room for ep_status/clock_divider/trail beyond
     * i2c_mode. */
    cfg.ep_status     = 0x1234u;
    cfg.clock_divider = 5u;
    cfg.trail         = 6u;
    TEST_ASSERT_EQUAL_UINT16(0x1234u, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(5u, cfg.clock_divider);
    TEST_ASSERT_EQUAL_UINT8(6u, cfg.trail);
}

/* REQ-I2C-020 (implemented): TC18 13.7.7.3 makes byte_msg_payload the
 * complete I2C payload INCLUDING the target address, which the endpoint must
 * neither interpret, validate nor rewrite -- staying transparent to 7-bit and
 * 10-bit addressing alike. */
static void test_i2c_payload_address_carried_verbatim(void)
{
    /* 7-bit form: address 0x53 shifted left with the bus-level R/W bit; and
     * a 10-bit form, whose first octet is the 11110xx0 prefix. */
    const uint8_t    addr7[3]  = {0xA6u, 0x12u, 0x34u};
    const uint8_t    addr10[4] = {0xF2u, 0x34u, 0x56u, 0x78u};
    const uint8_t   *tx        = NULL;
    size_t           tx_len    = 0;
    uint16_t         read_size = 0xFFFFu;
    uint8_t          tn        = 0;
    rcp_ep_i2c_dir_t dir       = RCP_EP_I2C_DIR_READ;
    rcp_bytes_t      frame;

    frame = rcp_ep_i2c_encode_transfer_request(4u, RCP_EP_I2C_DIR_WRITE, addr7, sizeof(addr7), 0u,
                                               0x51u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
                      rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4u, &dir, &tx,
                                                         &tx_len, &read_size, &tn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_WRITE, dir);
    TEST_ASSERT_EQUAL_UINT(sizeof(addr7), tx_len);
    TEST_ASSERT_EQUAL_HEX8(0xA6u, tx[0]); /* address octet unchanged */
    TEST_ASSERT_EQUAL_INT(0, memcmp(addr7, tx, sizeof(addr7)));
    TEST_ASSERT_EQUAL_UINT8(0x51u, tn);
    rcp_bytes_free(&frame);

    frame = rcp_ep_i2c_encode_transfer_request(4u, RCP_EP_I2C_DIR_WRITE, addr10, sizeof(addr10),
                                               0u, 0x52u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
                      rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4u, &dir, &tx,
                                                         &tx_len, &read_size, &tn));
    TEST_ASSERT_EQUAL_UINT(sizeof(addr10), tx_len);
    TEST_ASSERT_EQUAL_HEX8(0xF2u, tx[0]); /* 10-bit prefix octet unchanged */
    TEST_ASSERT_EQUAL_INT(0, memcmp(addr10, tx, sizeof(addr10)));
    rcp_bytes_free(&frame);
}

/* ── PWM endpoints (TC18 13.7.5 / 13.7.6) ─────────────────────────────────── */

/* REQ-PWM-055 CLOSED (issue #338): rcp_ep_pwm_out_trigger_fires() is
 * still a pure selector-vs-event match, unchanged -- but it is no longer
 * this module's ONLY trigger primitive. rcp_ep_pwm_out_trigger_events_at_
 * tick() (below) is the new phase-tracking primitive that derives WHICH
 * event(s) actually occur at a given clock tick, closing both of TC18
 * §13.7.5.1's own rules this deviation used to pin. */
static void test_pwm_out_trigger_fires_is_a_pure_selector(void)
{
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE,
                                                  RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE,
                                                   RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE,
                                                  RCP_EP_PWM_OUT_EVENT_DONE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE,
                                                   RCP_EP_PWM_OUT_EVENT_DONE));
}

/* REQ-PWM-055 rule 1 (TC18 §13.7.5.1: "For trigger signal generation the
 * delayed signal is used"): with skew == 0, the delayed edge coincides
 * with the undelayed one, so CYCLE_START fires at raw_tick == 0 exactly
 * as a naive, skew-unaware implementation would already expect. */
static void test_pwm_out_trigger_events_zero_skew_matches_undelayed_edge(void)
{
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START,
                            rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 0u, 1u));
}

/* REQ-PWM-055 rule 1, the actual skew case this deviation used to pin:
 * with skew == 5, the delayed edge is 5 ticks LATER than the undelayed
 * source edge -- CYCLE_START must NOT fire at raw_tick == 0 (that's still
 * the undelayed edge), only at raw_tick == skew. */
static void test_pwm_out_trigger_events_nonzero_skew_delays_cycle_start(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 5u, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 5u, 4u));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START,
                            rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 5u, 5u));
}

/* Skew wraps modulo period rather than being applied verbatim -- an
 * 8-bit skew register (0-255) can legally exceed a small period. */
static void test_pwm_out_trigger_events_skew_wraps_modulo_period(void)
{
    /* period 20, skew 25 -> skew_mod 5, same delayed edge as skew == 5. */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START,
                            rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 25u, 5u));
}

/* MID_PULSE fires at active_duration/2 ticks past the delayed cycle
 * start, distinct from CYCLE_START for a genuine nonzero active phase. */
static void test_pwm_out_trigger_events_mid_pulse_at_half_active_duration(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 0u, 4u));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_OUT_TRIGGER_EVENT_MID_PULSE,
                            rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 0u, 5u));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 0u, 6u));
}

/* REQ-PWM-055 rule 2 (Table 45: "in the middle of the active pulse (even
 * in case duty cycle is 0%)"): active_duration == 0 does NOT suppress
 * MID_PULSE -- it fires alongside CYCLE_START, both OR'd into the same
 * tick's own return value, not dropped as a degenerate "no active phase"
 * case. */
static void test_pwm_out_trigger_events_mid_pulse_fires_at_zero_duty_cycle(void)
{
    uint8_t events = rcp_ep_pwm_out_trigger_events_at_tick(20u, 0u, 0u, 0u);

    TEST_ASSERT_TRUE((events & RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START) != 0);
    TEST_ASSERT_TRUE((events & RCP_EP_PWM_OUT_TRIGGER_EVENT_MID_PULSE)   != 0);
    TEST_ASSERT_EQUAL_UINT8(
        RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START | RCP_EP_PWM_OUT_TRIGGER_EVENT_MID_PULSE, events);
}

/* period == 0 (RCP_EP_PWM_OUT_GEN_STOPPED) yields no trigger events at
 * all, regardless of raw_tick -- a stopped generator has no cycle to
 * derive a phase within. */
static void test_pwm_out_trigger_events_stopped_generator_yields_nothing(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_ep_pwm_out_trigger_events_at_tick(0u, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_ep_pwm_out_trigger_events_at_tick(0u, 0u, 5u, 100u));
}

/* raw_tick wraps modulo period the same way skew does -- a caller free-
 * running a tick counter across many cycles still gets the same
 * per-cycle answer every time around. */
static void test_pwm_out_trigger_events_raw_tick_wraps_modulo_period(void)
{
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START,
                            rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 0u, 20u));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_OUT_TRIGGER_EVENT_CYCLE_START,
                            rcp_ep_pwm_out_trigger_events_at_tick(20u, 10u, 0u, 40u));
}

/* FIXED 2026-08-12 (issue #201, REQ-PWM-056): TC18 13.7.5.2 Table 43
 * requires a requested active time below pwmo_duty_cycle_min or above
 * pwmo_duty_cycle_max to be CAPPED to that limit, not applied verbatim.
 * rcp_ep_pwm_out_apply_write() now takes duty_cycle_min/duty_cycle_max
 * and clamps the resulting active_duration into that range. */
static void test_pwm_out_duty_cap(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_ep_pwm_value_t              current = {1000u, 400u};
    rcp_ep_pwm_value_t              out;

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    cfg.duty_cycle_min = 100u;
    cfg.duty_cycle_max = 500u;

    /* Above pwmo_duty_cycle_max (500): capped to 500. */
    out = rcp_ep_pwm_out_apply_write(current, (rcp_ep_pwm_value_t){2000u, 900u},
                                     RCP_EP_PWM_OUT_WRITE_REPLACE, cfg.duty_cycle_min,
                                     cfg.duty_cycle_max);
    TEST_ASSERT_EQUAL_UINT16(500u, out.active_duration);
    TEST_ASSERT_EQUAL_UINT16(2000u, out.period); /* period is not capped */

    /* Below pwmo_duty_cycle_min (100): capped to 100. */
    out = rcp_ep_pwm_out_apply_write(current, (rcp_ep_pwm_value_t){2000u, 10u},
                                     RCP_EP_PWM_OUT_WRITE_REPLACE, cfg.duty_cycle_min,
                                     cfg.duty_cycle_max);
    TEST_ASSERT_EQUAL_UINT16(100u, out.active_duration);

    /* Within the window: passes through unchanged. */
    out = rcp_ep_pwm_out_apply_write(current, (rcp_ep_pwm_value_t){2000u, 300u},
                                     RCP_EP_PWM_OUT_WRITE_REPLACE, cfg.duty_cycle_min,
                                     cfg.duty_cycle_max);
    TEST_ASSERT_EQUAL_UINT16(300u, out.active_duration);

    TEST_ASSERT_EQUAL_UINT16(500u, cfg.duty_cycle_max);
    TEST_ASSERT_EQUAL_UINT16(100u, cfg.duty_cycle_min);
}

/* REQ-PWM-057 IMPLEMENTED (issue #336, 2 of TC18 13.7.5.3's own 4 request
 * rules): rcp_ep_pwm_out_generation_state() classifies the endpoint's
 * signal-generation state purely from its own {period, active_duration}
 * pair -- period == 0 stops generation; active_duration == 0 with period
 * != 0 keeps the endpoint running with the output disabled (triggers still
 * fire); otherwise ordinary running generation. This is a pure classifier
 * over caller-supplied values, not a change to how rcp_ep_pwm_out_apply_write()
 * itself stores the two fields (still opaque 16-bit setpoints, as before --
 * see the module's own doc comment on rcp_ep_pwm_out_generation_state()). */
static void test_pwm_out_generation_state(void)
{
    /* period == 0: stopped, regardless of active_duration. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_GEN_STOPPED,
                          rcp_ep_pwm_out_generation_state((rcp_ep_pwm_value_t){0u, 400u}));
    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_GEN_STOPPED,
                          rcp_ep_pwm_out_generation_state((rcp_ep_pwm_value_t){0u, 0u}));

    /* active_duration == 0, period != 0: running with output disabled. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_GEN_OUTPUT_DISABLED,
                          rcp_ep_pwm_out_generation_state((rcp_ep_pwm_value_t){800u, 0u}));

    /* Both nonzero: ordinary running generation. */
    TEST_ASSERT_EQUAL_INT(RCP_EP_PWM_OUT_GEN_RUNNING,
                          rcp_ep_pwm_out_generation_state((rcp_ep_pwm_value_t){1000u, 400u}));
}

/* REQ-PWM-057 DEVIATION PIN (the other 2 of TC18 13.7.5.3's own 4 request
 * rules -- genuinely deferred, not routine): a trigger configuration
 * request reads its first two payload octets as a PHASE SHIFT rather than
 * a period, and the output pin is read back during generation with an
 * error signalled if it does not toggle. Neither is tractable here: the
 * phase-shift rule depends on the conditional-request layer's own
 * request-kind classification (request_compound.h/_triggered.h/_chained.h),
 * which this endpoint's decode path has no connection to today, AND the
 * TC18 spec-defects report's own items 11-12 document a live, currently-
 * unresolved request_type code collision in exactly that harmonization
 * effort -- "which request even counts as a trigger configuration" is
 * itself an open spec question. The pin-readback rule needs real physical
 * IO this protocol-codec library has never modelled for any endpoint
 * type. The first two payload octets are always decoded as the period. */
static void test_pwm_out_request_semantics_are_verbatim_setpoints(void)
{
    rcp_ep_pwm_value_t decoded = {0u, 0u};
    rcp_bytes_t        frame;
    uint8_t            evt = 0xFFu, tn = 0;

    /* The first two payload octets are always the period, never a phase
     * shift, whatever the request is meant to configure. */
    frame = rcp_ep_pwm_out_encode_write_request(6u, (rcp_ep_pwm_value_t){0x1234u, 0x5678u},
                                                RCP_EP_PWM_OUT_WRITE_REPLACE, 0x61u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_PWM_PAYLOAD_LEN, 4u);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK,
                      rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 6u, &decoded,
                                                          &evt, &tn));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, decoded.period);
    TEST_ASSERT_EQUAL_HEX16(0x5678u, decoded.active_duration);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_WRITE_REPLACE, evt);
    rcp_bytes_free(&frame);
}

/* REQ-PWM-058 (implemented, FIXED 2026-08-11, issue #256 Group I): TC18
 * §13.7.6.2 Table 48's PWM_IN registers -- pwmi_polarity,
 * pwmi_err_on_max_period, pwmi_continuous_mode, pwmi_max_period,
 * pwmi_base_clk, pwmi_clk_divider and pwmi_ep_status -- now all exist,
 * reachable via the evt[2:0]=111b register-block mechanism, same as every
 * other endpoint type. `trigger` remains a non-wire, module-own field (see
 * ep_pwm.h's file header); the measurement-timeout sentinel is unrelated to
 * the register block and untouched. (CORRECTED 2026-08-14, issue #428:
 * this comment previously cited "Table 45" -- PWM_OUT's own "pwmo trigger
 * outputs" table -- rather than Table 48, PWM_IN's real functional-
 * configuration table.) */
static void test_pwm_in_functional_cfg_has_full_register_coverage(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;
    uint8_t                        block[RCP_EP_PWM_IN_EP_FUNC_LEN];

    rcp_ep_pwm_in_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
    TEST_ASSERT_FALSE(cfg.common.ep_enable);

    /* No pwmi_polarity: the capture-edge selector is the only polarity-like
     * control, and it is a two-edge enum, not an active-phase level. */
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, false));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, true, false));

    /* The timeout is still reported only via this sentinel measurement
     * value, unrelated to pwmi_max_period's own register. */
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, RCP_EP_PWM_IN_NO_SIGNAL);

    /* Now positively confirm Table 48's registers round-trip. */
    cfg.ep_status   = 0x1234u;
    cfg.clk_divider = 7u;
    cfg.flags       = RCP_EP_PWM_IN_FLAG_POLARITY | RCP_EP_PWM_IN_FLAG_CONTINUOUS_MODE;
    cfg.max_period  = 0xBEEFu;

    rcp_ep_pwm_in_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_EP_FUNC_LEN, block[RCP_EP_PWM_IN_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, (uint16_t)((block[RCP_EP_PWM_IN_REG_EP_STATUS] << 8) |
                                                block[RCP_EP_PWM_IN_REG_EP_STATUS + 1]));
    TEST_ASSERT_EQUAL_UINT8(7u, block[RCP_EP_PWM_IN_REG_CLK_DIVIDER]);
    TEST_ASSERT_EQUAL_UINT8(cfg.flags, block[RCP_EP_PWM_IN_REG_FLAGS]);
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, (uint16_t)((block[RCP_EP_PWM_IN_REG_MAX_PERIOD] << 8) |
                                                block[RCP_EP_PWM_IN_REG_MAX_PERIOD + 1]));
}

/* ── WakeUp endpoint (TC18 12.4 / 12.5 / 13.7.2) ──────────────────────────── */

/* REQ-WAKEUP-017's own remaining scope, as of 2026-08-12 (issue #201
 * batch 8): the PLAIN rcp_ep_wakeup_encode_wakeup_message()/
 * _decode_wakeup_message() pair deliberately keeps its own original
 * 1-byte-opcode-only shape unchanged -- this test still pins that fact,
 * not as a deviation any longer but as the documented, intentional
 * behavior of the NARROWER function. TC18 §12.4.1's own requirement is
 * now met by the NEW, additive
 * rcp_ep_wakeup_encode_wakeup_message_with_source()/
 * _decode_wakeup_message_with_source() pair, tested separately below --
 * see that test's own comment for the fix. */
static void test_wakeup_plain_message_has_no_source_field(void)
{
    rcp_acf_byte_message_info_t    hdr         = {0};
    const uint8_t                 *payload     = NULL;
    size_t                         payload_len = 0;
    rcp_bytes_t                    frame;
    uint8_t                        tn = 0;

    frame = rcp_ep_wakeup_encode_wakeup_message(2u, 0x77u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    /* One octet, the opcode, and no wake-source field of any width. */
    TEST_ASSERT_EQUAL_UINT(1u, payload_len);
    TEST_ASSERT_EQUAL_HEX8(RCP_EP_WAKEUP_WAKEUP_OPCODE, payload[0]);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, 2u, &tn));
    TEST_ASSERT_EQUAL_UINT8(0x77u, tn);
    rcp_bytes_free(&frame);
}

/* REQ-WAKEUP-017, FIXED 2026-08-12 (issue #201 batch 8): TC18 §12.4.1
 * requires the repetitive wake response to convey both a WakeUp message
 * AND the WakeUp source that caused the wake.
 * rcp_ep_wakeup_encode_wakeup_message_with_source()/
 * _decode_wakeup_message_with_source() now provide that, as a 3-byte
 * ACF_ABB payload (opcode + source + source_index) alongside the
 * pre-existing, unchanged 1-byte plain pair. */
static void test_wakeup_message_with_source_round_trips_all_source_kinds(void)
{
    rcp_bytes_t             frame;
    uint8_t                 tn;
    rcp_ep_wakeup_source_t  src;
    uint8_t                 idx;

    /* A configured IO source, with its own table index. */
    frame = rcp_ep_wakeup_encode_wakeup_message_with_source(
        3u, 0x11u, RCP_EP_WAKEUP_SOURCE_IO, 5u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message_with_source(frame.data, frame.len, 3u,
                                                                       &tn, &src, &idx));
    TEST_ASSERT_EQUAL_UINT8(0x11u, tn);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_SOURCE_IO, src);
    TEST_ASSERT_EQUAL_UINT8(5u, idx);
    /* The plain decoder still tolerates this longer message -- a
     * strictly additive wire extension, not a breaking change. */
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, 3u, &tn));
    TEST_ASSERT_EQUAL_UINT8(0x11u, tn);
    rcp_bytes_free(&frame);

    /* The dedicated wakepin -- source_index is the "not applicable" sentinel. */
    frame = rcp_ep_wakeup_encode_wakeup_message_with_source(
        3u, 0x12u, RCP_EP_WAKEUP_SOURCE_WAKEPIN, RCP_EP_WAKEUP_SOURCE_INDEX_NA);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message_with_source(frame.data, frame.len, 3u,
                                                                       &tn, &src, &idx));
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_SOURCE_WAKEPIN, src);
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_WAKEUP_SOURCE_INDEX_NA, idx);
    rcp_bytes_free(&frame);

    /* A TC14/TC10 network wake-up request. */
    frame = rcp_ep_wakeup_encode_wakeup_message_with_source(
        3u, 0x13u, RCP_EP_WAKEUP_SOURCE_NETWORK, RCP_EP_WAKEUP_SOURCE_INDEX_NA);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message_with_source(frame.data, frame.len, 3u,
                                                                       &tn, &src, &idx));
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_SOURCE_NETWORK, src);
    rcp_bytes_free(&frame);

    /* Unknown/no information -- the default a caller with nothing better
     * to report can still send. */
    frame = rcp_ep_wakeup_encode_wakeup_message_with_source(
        3u, 0x14u, RCP_EP_WAKEUP_SOURCE_UNKNOWN, RCP_EP_WAKEUP_SOURCE_INDEX_NA);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message_with_source(frame.data, frame.len, 3u,
                                                                       &tn, &src, &idx));
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_SOURCE_UNKNOWN, src);
    rcp_bytes_free(&frame);
}

/* An out-of-range source byte is a decode failure, not silently
 * reinterpreted as RCP_EP_WAKEUP_SOURCE_UNKNOWN. */
static void test_wakeup_message_with_source_rejects_an_unknown_source_byte(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_wakeup_message_with_source(
        3u, 0x15u, RCP_EP_WAKEUP_SOURCE_NETWORK, RCP_EP_WAKEUP_SOURCE_INDEX_NA);
    uint8_t                tn;
    rcp_ep_wakeup_source_t  src;
    uint8_t                 idx;

    TEST_ASSERT_NOT_NULL(frame.data);
    frame.data[frame.len - 3u] = 0x7Fu; /* payload[1], the source byte -- an invalid value */

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_BAD_OPCODE,
                      rcp_ep_wakeup_decode_wakeup_message_with_source(frame.data, frame.len, 3u,
                                                                       &tn, &src, &idx));
    rcp_bytes_free(&frame);
}

/* The plain 1-byte message shape is rejected by the _with_source
 * decoder -- its own contract is specifically the 3-byte shape, not
 * "the 3-byte shape or shorter". */
static void test_wakeup_message_with_source_decoder_rejects_the_plain_shape(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_wakeup_message(3u, 0x16u);
    uint8_t                tn;
    rcp_ep_wakeup_source_t  src;
    uint8_t                 idx;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_SHORT_FRAME,
                      rcp_ep_wakeup_decode_wakeup_message_with_source(frame.data, frame.len, 3u,
                                                                       &tn, &src, &idx));
    rcp_bytes_free(&frame);
}

/* REQ-WAKEUP-018, PARTIAL as of 2026-08-12 (issue #201 batch 7): TC18
 * §12.4.1 makes the WakeUp repetition time part of the endpoint's
 * functional configuration. rcp_ep_wakeup_functional_cfg_t now carries a
 * repetition_time_us field -- discoverable and settable over this
 * module's own in-memory API, zero-init default 0 -- closing the
 * specific "not discoverable, not settable" complaint this requirement's
 * own text raised. Remains PARTIAL, not fully implemented: TC18 §13.7.2.2
 * Table 36 (this endpoint's own functional-config register block,
 * already fully mapped by REQ-WAKEUP-021) defines no field for it at
 * all, so this value has no wire-register address -- see the field's own
 * doc comment (ep_wakeup.h) for the full citation trail, including why
 * reusing response_queue_cfg's own unrelated flush_time_us field was
 * deliberately not done unilaterally. */
static void test_wakeup_repetition_time_is_configurable_but_not_wire_reachable(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(0u, cfg.repetition_time_us);

    /* Discoverable and settable, over this module's own in-memory API. */
    cfg.repetition_time_us = 25000u; /* 25 ms, an arbitrary but plausible value */
    TEST_ASSERT_EQUAL_UINT32(25000u, cfg.repetition_time_us);

    TEST_ASSERT_EQUAL_UINT(8u, RCP_EP_WAKEUP_MAX_SOURCES);
    /* ADDED 2026-08-11: sources no longer immediately follows common with
     * zero padding -- rcp_ep_wakeup_source_cfg_t now contains a uint16_t
     * (pin_number), which forces 2-byte alignment, so the compiler pads
     * common (5 bytes) up to the next even offset before sources starts.
     * This is ordinary C struct layout, not a bug: the offset is now
     * >= sizeof(common), not necessarily ==. */
    TEST_ASSERT_TRUE(offsetof(rcp_ep_wakeup_functional_cfg_t, sources) >=
                     sizeof(rcp_regmap_ep_functional_cfg_t));
    /* ep_status/wup_status/repetition_time_us now follow sources -- see
     * REQ-WAKEUP-021's own updated text and the register-block work
     * below. sources is no longer the struct's own last field;
     * repetition_time_us is (as of this batch), modulo ordinary C
     * trailing padding (the struct's own alignment requirements can round
     * total size up by more than offsetof(repetition_time_us) +
     * sizeof(repetition_time_us)) alone -- hence ">=", not "=="). */
    TEST_ASSERT_TRUE(sizeof(cfg) >= offsetof(rcp_ep_wakeup_functional_cfg_t, repetition_time_us)
                                        + sizeof(cfg.repetition_time_us));
}

/* REQ-WAKEUP-019, FIXED 2026-08-12 (issue #201 batch 6): TC18 §12.5
 * requires a refused sleep/standby request to be answered with an ERROR
 * response carrying error code REQUEST_CANCELED. As of this fix,
 * rcp_ep_wakeup_encode_sleepcmd_response(..., RCP_PWRMODE_ENTRY_REFUSED,
 * ...) returns a genuine ACF Error Response (err set, classifies as
 * RCP_ACF_RESP_ERROR, payload = the numbered wire code) instead of the
 * old positive-form SleepCMD-shaped response this test used to pin as
 * the deviation -- a conforming RC Client watching for an error response
 * now sees the refusal, and the numbered wire code TC18 §12.5 calls for
 * is genuinely carried on the wire. */
static void test_wakeup_refusal_is_a_genuine_error_response(void)
{
    rcp_acf_byte_message_info_t hdr    = {0};
    rcp_pwrmode_entry_result_t  result = RCP_PWRMODE_ENTRY_OK;
    rcp_bytes_t                 frame;
    uint8_t                     tn = 0;
    const uint8_t               *payload  = NULL;
    size_t                       payload_len = 0;

    frame = rcp_ep_wakeup_encode_sleepcmd_response(1u, RCP_PWRMODE_ENTRY_REFUSED, 0x33u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));

    /* A genuine error response: err is set and the message classifies
     * accordingly, not as an ordinary write response. */
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_NOT_EQUAL(RCP_ACF_RESP_WRITE, rcp_acf_classify_response(&hdr));

    /* The numbered wire code TC18 §12.5 calls for IS carried, as the
     * response's own single payload octet. */
    TEST_ASSERT_EQUAL_UINT(1u, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_REQUEST_CANCELED, payload[0]);

    /* The dedicated decoder still round-trips this exchange correctly,
     * recognizing the error response as the refused-entry case. */
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_sleepcmd_response(frame.data, frame.len, 1u, &result,
                                                             &tn));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, result);
    TEST_ASSERT_EQUAL_UINT8(0x33u, tn);

    rcp_bytes_free(&frame);
}

/* A response an unrelated err code (never built by this module's own
 * encode side) is NOT reinterpreted as a refusal -- REQ-WAKEUP-019's own
 * decode-side fix is specific to RCP_ERROR_REQUEST_CANCELED, not "any
 * error response at all". */
static void test_wakeup_decode_rejects_an_unrelated_error_code(void)
{
    rcp_bytes_t                 frame = rcp_acf_build_error_response(1u, 0x44u,
                                                                       RCP_ERROR_EP_NOT_FOUND);
    rcp_pwrmode_entry_result_t  result = RCP_PWRMODE_ENTRY_OK;
    uint8_t                     tn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_BAD_OPCODE,
                      rcp_ep_wakeup_decode_sleepcmd_response(frame.data, frame.len, 1u, &result,
                                                             &tn));

    rcp_bytes_free(&frame);
}

/* REQ-WAKEUP-020 (not-implemented) DEVIATION PIN: TC18 13.7.2.1 fixes the
 * WakeUp endpoint at endpoint number 1 -- the only endpoint that stays active
 * in Sleep mode. Every c-RCP entry point takes a caller-supplied
 * byte_bus_id and the library defines no fixed WakeUp endpoint index, so a
 * WakeUp message addressed to an arbitrary endpoint is perfectly valid. */
static void test_wakeup_codec_accepts_any_bus_id(void)
{
    rcp_bytes_t frame;
    uint8_t     tn = 0;

    frame = rcp_ep_wakeup_encode_wakeup_message(0xB7u, 0x0Au);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, 0xB7u, &tn));
    TEST_ASSERT_EQUAL_UINT8(0x0Au, tn);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_is_wakeup_echo(frame.data, frame.len, 0xB7u, 0x0Au));
    /* The fixed endpoint number the clause names is simply a different bus
     * id, rejected like any other mismatch. */
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_WRONG_BUS,
                      rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, 1u, &tn));
    rcp_bytes_free(&frame);

    /* ep_type is this endpoint TYPE's regmap value, not an endpoint NUMBER
     * -- nothing pins the endpoint index. */
    TEST_ASSERT_EQUAL_HEX8(0x01u, RCP_EP_WAKEUP_EP_TYPE);
}

/* REQ-WAKEUP-021 (partial) / REQ-WAKEUP-022 (partial): FIXED 2026-08-11
 * (c-RCP-AUDIT-06, issue #256 Group I dedicated investigation, task #95).
 * TC18 §13.7.2.2 Table 36 has a genuine, literal address collision between
 * wup_status and the wake-source array's own first entry (wup_io_scr1),
 * both printed at the same relative address -- confirmed via the rendered
 * page image on both the 0.5.1_RC baseline and the 0.5.1_RC5 revision
 * (identical on both, not an extraction artifact, not independently
 * corrected by the spec committee the way MDIO's own Table 56/59 collision
 * was). Resolved via this session's own established cross-table pattern:
 * wup_status keeps its own printed address, the source array shifts to
 * start immediately after it. rcp_ep_wakeup_render_registers()/
 * _apply_reconfig() now implement the whole collision-free block --
 * wup_ep_len/wup_nr_io_pins_max/wup_ep_status/wup_status/wup_io_scrN are
 * all reachable via the generic evt[2:0]==111b configuration mechanism.
 *
 * Both former simplifications here are now RESOLVED (issue #341
 * lineage; see ep_wakeup.h's own "register block" file-header note for
 * the full history): wup_status is a genuine per-source bitmask
 * (REQ-WAKEUP-021, 2026-08-14 -- see test_ep_wakeup.c for the dedicated
 * per-source tests), and each wup_io_scrN register now renders/parses
 * all 6 of Table 40's own defined IO_SRC values, including the 3 edge
 * modes (REQ-WAKEUP-022, 2026-08-14 -- via the new, separate, stateful
 * rcp_ep_wakeup_source_edge_asserted()/_any_source_edge_asserted()
 * predicate pair; the pre-existing level-only
 * rcp_ep_wakeup_source_asserted() itself is unchanged, see
 * test_ep_wakeup.c for the dedicated edge-detection tests). Only the
 * genuinely reserved IO_SRC range (0x06-0x1F) remains unrepresentable,
 * correctly so -- a configuration write encoding one leaves that slot's
 * own enabled/active_high/trigger_on_*_edge untouched (only pin_number
 * always updates) rather than silently misinterpreting it. */
static void test_wakeup_register_block_has_collision_free_layout(void)
{
    rcp_ep_wakeup_wup_status_t     status;
    rcp_ep_wakeup_source_cfg_t     high = {true, true, 0};
    rcp_ep_wakeup_source_cfg_t     low  = {true, false, 0};
    rcp_ep_wakeup_functional_cfg_t cfg;
    bool                           levels[2] = {false, true};
    uint8_t                        out[RCP_EP_WAKEUP_EP_FUNC_LEN];

    rcp_ep_wakeup_wup_status_init(&status);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&status));
    rcp_ep_wakeup_wup_status_latch_source(&status, 0);
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&status));
    /* One wholesale clear, not a per-source write-1-to-clear bit -- the
     * standalone rcp_ep_wakeup_wup_status_t API's own _clear() is a
     * whole-mask clear; only the register block built on top of it
     * (below) additionally gained per-BIT write-1-to-clear wire
     * semantics, via _clear_source(). */
    rcp_ep_wakeup_wup_status_clear(&status);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&status));
    TEST_ASSERT_EQUAL_UINT(sizeof(uint16_t), sizeof(status));

    /* Level modes only: the predicate takes the current level and nothing
     * else, so rising/falling/both-edges have no representation -- still
     * true, unchanged by the register-block addition. */
    TEST_ASSERT_TRUE(rcp_ep_wakeup_source_asserted(high, true));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_source_asserted(high, false));
    TEST_ASSERT_TRUE(rcp_ep_wakeup_source_asserted(low, false));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_source_asserted(low, true));

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    TEST_ASSERT_FALSE(rcp_ep_wakeup_any_source_asserted(&cfg, levels, 2u));
    cfg.sources[1] = high;
    TEST_ASSERT_TRUE(rcp_ep_wakeup_any_source_asserted(&cfg, levels, 2u));

    /* The collision is now resolved: wup_status (0x0004-5) and
     * wup_io_scr1 (0x0006-7, no longer 0x0004) render to distinct,
     * non-overlapping addresses. */
    TEST_ASSERT_EQUAL_UINT16(0x0004u, RCP_EP_WAKEUP_REG_WUP_STATUS);
    TEST_ASSERT_EQUAL_UINT16(0x0006u, RCP_EP_WAKEUP_REG_SOURCE_BASE);
    TEST_ASSERT_EQUAL_UINT16(0x0016u, RCP_EP_WAKEUP_EP_FUNC_LEN);

    cfg.sources[1].pin_number = 5u;
    rcp_ep_wakeup_wup_status_latch_source(&cfg.wup_status, 0);
    rcp_ep_wakeup_render_registers(&cfg, out);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_WAKEUP_EP_FUNC_LEN, out[RCP_EP_WAKEUP_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_WAKEUP_MAX_SOURCES,
                            out[RCP_EP_WAKEUP_REG_NR_IO_PINS_MAX]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[RCP_EP_WAKEUP_REG_WUP_STATUS]);
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[RCP_EP_WAKEUP_REG_WUP_STATUS + 1]); /* bit 0 latched */
    /* Slot 1's own register: io_src=HIGH_LEVEL(0x04) in bits [15:11],
     * pin_number=5 in bits [10:0] -> (0x04 << 11) | 5 == 0x2005. */
    TEST_ASSERT_EQUAL_HEX8(0x20u, out[RCP_EP_WAKEUP_REG_SOURCE_BASE + 2]);
    TEST_ASSERT_EQUAL_HEX8(0x05u, out[RCP_EP_WAKEUP_REG_SOURCE_BASE + 3]);
}

/* ── E2E request-stream configuration (TC18 12.7.7 Table 22) ──────────────── */

/* REQ-E2E-028/029 (partial) DEVIATION PIN: TC18 12.7.7 Table 22 relative
 * address 0x000D bits 1/2 (rx_enforce_seq/rx_seq_safestate_enable) require
 * rejecting a request whose AVTPDU sequence number has not increased, and
 * escalating every endpoint on the stream to its configured safe state on a
 * discontinuity. rcp_e2e_seq_evaluate() (e2e.h) now implements both
 * decisions as a pure, directly-testable primitive -- see the tests below --
 * but rcp_server_endpoint_admit() itself still has no sequence_num input at
 * all (it operates on the ACF payload only; the AVTPDU header, including
 * sequence_num, is decoded and discarded one layer above it, in whatever
 * caller demultiplexes an AVTP frame before handing admit() the ACF
 * message). Wiring rcp_e2e_seq_evaluate() into the actual admission path
 * therefore needs sequence_num threaded through that caller -- e.g.
 * mock.c's rcp_mock_server_dispatch_frame()/rcp_mock_server_dispatch() --
 * which is a larger, separate integration change, not done here. The very
 * same frame replayed byte-for-byte is still admitted and stored a second
 * time by admit() itself. */
static void test_e2e_replayed_request_is_admitted_again(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame;
    const uint8_t         payload[2]   = {0xC0u, 0xDEu};
    uint8_t               request_type = 0;
    size_t                index        = 0;

    frame = rcp_timed_encode_request(3u, 0x1000u, 0x40u, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rcp_server_endpoint_init(&ep, true);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u, &request_type,
                                                &index, NULL));
    /* Identical AVTPDU content, no sequence advance: filed again anyway --
     * admit() itself still has no sequence_num input to gate on. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u, &request_type,
                                                &index, NULL));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_server_endpoint_pending_count(&ep));

    rcp_server_endpoint_destroy(&ep);
    rcp_bytes_free(&frame);
}

/* Companion to the deviation pin above: the watchdog remains the only
 * safe-state entry path admit()'s own call chain can reach; rcp_e2e_wd_evaluate()
 * still takes no sequence number, and with the watchdog disabled no elapsed
 * time whatsoever produces enter_safe_state. rcp_e2e_seq_evaluate()'s own
 * enter_safe_state field (tested separately below) is the sequence-driven
 * counterpart, available as a primitive but not yet wired into any
 * admission call chain -- see the deviation pin above. */
static void test_e2e_safe_state_only_reachable_via_watchdog(void)
{
    rcp_e2e_wd_result_t r;

    /* Watchdog off: no input to this module can drive the safe state. */
    r = rcp_e2e_wd_evaluate(false, 10u, true, true, 1000000u);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);

    /* Watchdog on and expired: this is the ONLY route to enter_safe_state. */
    r = rcp_e2e_wd_evaluate(true, 10u, true, false, 10u);
    TEST_ASSERT_TRUE(r.overflowed);
    TEST_ASSERT_TRUE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);

    r = rcp_e2e_wd_evaluate(true, 10u, false, true, 11u);
    TEST_ASSERT_TRUE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_TRUE(r.notify);

    /* Not yet expired: still nothing. */
    r = rcp_e2e_wd_evaluate(true, 10u, true, true, 9u);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
}

/* REQ-E2E-028: the first call on a fresh tracker has nothing to compare
 * against, so it always accepts and is never a discontinuity, regardless
 * of what seq it's given or how rx_enforce_seq/rx_seq_safestate_enable are
 * set. */
static void test_e2e_seq_evaluate_first_call_always_accepts(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    TEST_ASSERT_FALSE(t.has_prev);

    r = rcp_e2e_seq_evaluate(&t, true, true, 200u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_TRUE(t.has_prev);
    TEST_ASSERT_EQUAL_UINT8(200u, t.prev_seq);
}

/* REQ-E2E-028: a single-increment sequence always accepts and is never a
 * discontinuity, with rx_enforce_seq on. */
static void test_e2e_seq_evaluate_single_increment_is_clean(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 10u);

    r = rcp_e2e_seq_evaluate(&t, true, true, 11u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(11u, t.prev_seq);
}

/* REQ-E2E-028/029: a gap (increase by more than one) still accepts -- order
 * was preserved, just not every seq in between -- but IS a discontinuity,
 * so enter_safe_state tracks rx_seq_safestate_enable independently of
 * accept. */
static void test_e2e_seq_evaluate_gap_accepts_but_is_a_discontinuity(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 10u);

    r = rcp_e2e_seq_evaluate(&t, true, true, 13u); /* skipped 11, 12 */
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_TRUE(r.discontinuity);
    TEST_ASSERT_TRUE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(13u, t.prev_seq);

    /* Same gap, but rx_seq_safestate_enable off: still a discontinuity,
     * just no escalation. */
    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, false, 10u);
    r = rcp_e2e_seq_evaluate(&t, true, false, 13u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_TRUE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
}

/* REQ-E2E-028: a stale/replayed or reordered-backward seq is rejected when
 * rx_enforce_seq is on, and the tracker's prev_seq does NOT move -- a
 * rejected replay must not become the new reference point (see this
 * primitive's own doc comment for why). */
static void test_e2e_seq_evaluate_replay_is_rejected_and_does_not_move_tracker(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 50u);

    /* Exact replay of the same seq. */
    r = rcp_e2e_seq_evaluate(&t, true, true, 50u);
    TEST_ASSERT_FALSE(r.accept);
    TEST_ASSERT_EQUAL_UINT8(50u, t.prev_seq); /* unmoved */

    /* An older seq (reordered-backward). */
    r = rcp_e2e_seq_evaluate(&t, true, true, 40u);
    TEST_ASSERT_FALSE(r.accept);
    TEST_ASSERT_EQUAL_UINT8(50u, t.prev_seq); /* still unmoved */

    /* The genuine next seq is still correctly accepted afterward -- the
     * rejected replays never displaced the real reference point. */
    r = rcp_e2e_seq_evaluate(&t, true, true, 51u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
}

/* REQ-E2E-028: rx_enforce_seq off means every seq accepts regardless of
 * direction (a real replay included), while discontinuity/enter_safe_state
 * are still computed and reported -- rx_enforce_seq and
 * rx_seq_safestate_enable are independent bits, per this primitive's own
 * doc comment. */
static void test_e2e_seq_evaluate_enforce_off_always_accepts(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, false, true, 50u);

    r = rcp_e2e_seq_evaluate(&t, false, true, 10u); /* a real backward jump */
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_TRUE(r.discontinuity);
    TEST_ASSERT_TRUE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(10u, t.prev_seq); /* advances: this call accepted */
}

/* REQ-E2E-028: sequence_num is an 8-bit free-running counter (avtp.h) that
 * wraps 0xFF -> 0x00 over any long-lived stream; that wrap is a single
 * increment, not a discontinuity -- see this primitive's own doc comment
 * for the RFC 1982 comparison technique this relies on. */
static void test_e2e_seq_evaluate_wraparound_is_a_clean_single_increment(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 0xFFu);

    r = rcp_e2e_seq_evaluate(&t, true, true, 0x00u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(0x00u, t.prev_seq);
}

/* REQ-E2E-030 (issue #335, UPDATED 2026-08-13): TC18 §12.7.7 Table 24
 * relative address 0x000D bit 5 (rx_ovrflw_safestate_enable) brings every
 * endpoint bound to the request stream into its configured safe state
 * when any one endpoint's request storage overflows.
 * rcp_server_endpoint_admit() reports the per-request half conformantly
 * -- *out_error is RCP_ERROR_REQUEST_STORAGE_OVERFLOW, letting a caller
 * build a real Table 27 response (see mock.c's finish_admission() for a
 * worked example: as of issue #430/REQ-ACF-033, a RCP_SERVER_ADMIT_
 * REJECTED outcome like this one -- the request was never filed into
 * storage at all -- is answered with TC18 §11.3.1's Acknowledge-shaped
 * rejection, rcp_acf_build_acknowledge_rejected_response(), not the
 * §11.3.4 Error Response rcp_acf_build_error_response() builds for a
 * request already filed whose later execution fails) -- but this
 * function itself, tested here in isolation via a bare rcp_server_endpoint_t
 * with no request-stream context, still correctly does NOT perform the
 * stream-wide safe-state escalation on its own: server.h has no
 * request-stream/EP_ID_config dependency of its own, matching the
 * established "mechanism lives below, context lives here" layering
 * (REQ-SEQ-013's own sequencer-ownership check makes the identical call at
 * the identical layer boundary). The escalation itself IS now performed,
 * one layer up: rcp_mock_server_broadcast_safe_state() (mock.h) is the
 * actual orchestrator, driven from dispatch_plain()'s own overflow check
 * (mock.c) via rcp_regmap_ep_id_map_byte_bus_ids_for_stream() (regmap.h,
 * issue #335) -- see test_conditional_dispatch.c's own
 * test_overflow_on_one_endpoint_broadcasts_safe_state_to_stream_siblings()
 * for the end-to-end proof. rcp_e2e_overflow_should_enter_safe_state() is
 * the pure per-cause decision both this function's own non-escalating
 * behavior and mock.c's own real orchestrator ultimately consult. */
static void test_e2e_request_store_overflow_reports_error_code_but_not_escalation(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame;
    uint8_t               request_type = 0;
    rcp_wire_error_t      err          = RCP_ERROR_NONE;
    size_t                i;

    frame = rcp_timed_encode_request(3u, 0x2000u, 0x41u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);

    rcp_server_endpoint_init(&ep, true);
    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                          rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u, &request_type,
                                                    NULL, &err));
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    }
    TEST_ASSERT_EQUAL_UINT(RCP_SERVER_MAX_PENDING, rcp_server_endpoint_pending_count(&ep));

    /* Overflow: rejected and dropped, but now with a real Table 27 code a
     * caller can turn into a conformant error response. */
    err = RCP_ERROR_NONE;
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_REJECTED,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, false, 0u, 0u, &request_type,
                                                NULL, &err));
    TEST_ASSERT_EQUAL(RCP_ERROR_REQUEST_STORAGE_OVERFLOW, err);
    TEST_ASSERT_EQUAL_UINT(RCP_SERVER_MAX_PENDING, rcp_server_endpoint_pending_count(&ep));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
    rcp_bytes_free(&frame);
}

/* REQ-E2E-030: the pure escalation-decision primitive a stream-wide
 * orchestrator (not yet part of this codebase -- see the deviation pin
 * above) would consult once request-storage overflow has already been
 * detected. Trivially rx_ovrflw_safestate_enable, gated on nothing else,
 * since the overflow condition is a given by the time this is called. */
static void test_e2e_overflow_should_enter_safe_state_is_gated_only_on_the_config_bit(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_overflow_should_enter_safe_state(true));
    TEST_ASSERT_FALSE(rcp_e2e_overflow_should_enter_safe_state(false));
}

/* REQ-E2E-045 (partial) DEVIATION PIN: TC18 §12.7.7 Table 22's own
 * 0x000D.0 rx_enforce_e2e description names two consequences for its 1b
 * value in the same sentence -- "stream is blocked until released, when
 * CRC check at EP fails" (the rcp_e2e_stream_fault_t latch, REQ-E2E-021 --
 * itself also only a pure primitive nothing in the dispatch path calls
 * yet) AND "Safe state will be entered". This library's data model has
 * the same "no type for every other endpoint bound to this stream" gap
 * REQ-E2E-030's own deviation pin (above) documents for
 * rx_ovrflw_safestate_enable -- rcp_e2e_crc_error_should_enter_safe_state()
 * is the pure decision a caller-owned orchestrator would consult to
 * actually perform that escalation once such a caller exists. Unlike
 * rx_ovrflw_safestate_enable, rx_enforce_e2e carries no separate
 * safestate-enable bit of its own: the one bit gates both consequences,
 * so this decision is simply rx_enforce_e2e's own value. */
static void test_e2e_crc_error_should_enter_safe_state_is_gated_only_on_rx_enforce_e2e(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_crc_error_should_enter_safe_state(true));
    TEST_ASSERT_FALSE(rcp_e2e_crc_error_should_enter_safe_state(false));
}

/* REQ-E2E-046 (not-implemented) DEVIATION PIN: TC18 0.5.1_RC5's own Table
 * 24 (§12.7.7, renumbered from the 0.5.1_RC baseline's Table 22) adds a
 * new rx_stream_status read bit with no counterpart at all in the
 * baseline this codebase was built against -- a passive, client-polled
 * aggregate "is this stream currently blocked" status covering all four
 * fault classes (CRC/sequence/watchdog/overflow) uniformly. This module
 * has no equivalent aggregate primitive: it has a genuine asymmetry
 * between its own per-cause mechanisms that this test pins directly.
 * rcp_e2e_stream_fault_t IS a persisted latch for the CRC case
 * specifically -- once rcp_e2e_stream_fault_on_crc_error() latches it,
 * rcp_e2e_stream_fault_is_faulted() can be queried again later,
 * independent of any new CRC error, to learn "is this stream still
 * blocked from a past fault". No equivalent persisted type exists for
 * sequence discontinuity, watchdog overflow, or request-storage overflow
 * -- rcp_e2e_seq_evaluate()/_wd_evaluate()/_overflow_should_enter_safe_state()
 * each report only a per-call decision at the moment of the triggering
 * event, with nothing a caller can query afterward to learn whether the
 * stream remains blocked from one of those three causes. Implementing
 * rx_stream_status correctly needs a new, cross-cutting per-stream
 * aggregate-latch primitive spanning all four causes uniformly -- see
 * regmap.h's own "TC18 0.5.1_RC5 terminology drift" file-header note
 * (task #97) for the full investigation. */
static void test_e2e_has_no_aggregate_stream_blocked_status_across_all_four_fault_causes(void)
{
    rcp_e2e_stream_fault_t f;

    rcp_e2e_stream_fault_init(&f);
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_is_faulted(&f));

    /* CRC: persisted, queryable after the fact. */
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_on_crc_error(&f, true));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_is_faulted(&f));

    /* Sequence/watchdog/overflow: each only a per-call decision, nothing
     * persisted to query later -- rcp_e2e_seq_result_t/rcp_e2e_wd_result_t
     * are both caller-transient return values, not stateful latches like
     * rcp_e2e_stream_fault_t above, and
     * rcp_e2e_overflow_should_enter_safe_state() returns a plain bool
     * with no state at all. There is no function anywhere in this module
     * that, given only a stream identity, answers "is this stream
     * currently blocked" the way rcp_e2e_stream_fault_is_faulted() does
     * for the CRC cause alone. */
    TEST_ASSERT_TRUE(rcp_e2e_overflow_should_enter_safe_state(true)); /* per-call only */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_acf_msg_type_constants_and_op_wire_bit);
    RUN_TEST(test_acf_read_size_slot_is_ambiguous);
    RUN_TEST(test_acf_bus_id_is_now_eleven_bits_wide);
    RUN_TEST(test_acf_request_flags_round_trip_but_admission_now_rejects_rsp);
    RUN_TEST(test_acf_read_size_or_segment_num_kind_follows_op);
    RUN_TEST(test_acf_request_header_constraints_cs_exemption);

    RUN_TEST(test_gpio_request_payload_is_four_octets);
    RUN_TEST(test_gpio_wire_error_is_none_for_local_only_codes);
    RUN_TEST(test_gpio_reserved_evt_is_ignored_and_reports_unsupported_cmd);
    RUN_TEST(test_gpio_trigger_numbering_and_functional_cfg_gaps);
    RUN_TEST(test_gpio_trigger_signal_numbering);
    RUN_TEST(test_gpio_response_timing_classifier_distinguishes_read_and_write);
    RUN_TEST(test_gpio_dispatch_read_responds_immediately);
    RUN_TEST(test_gpio_dispatch_debounces_writes_before_reporting_settled_value);
    RUN_TEST(test_gpio_dispatch_deferred_write_response_is_retrievable_once_debounce_settles);

    RUN_TEST(test_spi_six_channels_selected_by_evt);
    RUN_TEST(test_spi_trigger_numbering_and_channel_cfg_full);
    RUN_TEST(test_spi_trigger_signal_numbering);
    RUN_TEST(test_spi_read_size_round_trips_through_transfer_request);
    RUN_TEST(test_spi_no_error_latch);

    RUN_TEST(test_i2c_mode_presets_and_register_block);
    RUN_TEST(test_i2c_payload_address_carried_verbatim);

    RUN_TEST(test_pwm_out_trigger_fires_is_a_pure_selector);
    RUN_TEST(test_pwm_out_trigger_events_zero_skew_matches_undelayed_edge);
    RUN_TEST(test_pwm_out_trigger_events_nonzero_skew_delays_cycle_start);
    RUN_TEST(test_pwm_out_trigger_events_skew_wraps_modulo_period);
    RUN_TEST(test_pwm_out_trigger_events_mid_pulse_at_half_active_duration);
    RUN_TEST(test_pwm_out_trigger_events_mid_pulse_fires_at_zero_duty_cycle);
    RUN_TEST(test_pwm_out_trigger_events_stopped_generator_yields_nothing);
    RUN_TEST(test_pwm_out_trigger_events_raw_tick_wraps_modulo_period);
    RUN_TEST(test_pwm_out_duty_cap);
    RUN_TEST(test_pwm_out_generation_state);
    RUN_TEST(test_pwm_out_request_semantics_are_verbatim_setpoints);
    RUN_TEST(test_pwm_in_functional_cfg_has_full_register_coverage);

    RUN_TEST(test_wakeup_plain_message_has_no_source_field);
    RUN_TEST(test_wakeup_message_with_source_round_trips_all_source_kinds);
    RUN_TEST(test_wakeup_message_with_source_rejects_an_unknown_source_byte);
    RUN_TEST(test_wakeup_message_with_source_decoder_rejects_the_plain_shape);
    RUN_TEST(test_wakeup_repetition_time_is_configurable_but_not_wire_reachable);
    RUN_TEST(test_wakeup_refusal_is_a_genuine_error_response);
    RUN_TEST(test_wakeup_decode_rejects_an_unrelated_error_code);
    RUN_TEST(test_wakeup_codec_accepts_any_bus_id);
    RUN_TEST(test_wakeup_register_block_has_collision_free_layout);

    RUN_TEST(test_e2e_replayed_request_is_admitted_again);
    RUN_TEST(test_e2e_safe_state_only_reachable_via_watchdog);
    RUN_TEST(test_e2e_seq_evaluate_first_call_always_accepts);
    RUN_TEST(test_e2e_seq_evaluate_single_increment_is_clean);
    RUN_TEST(test_e2e_seq_evaluate_gap_accepts_but_is_a_discontinuity);
    RUN_TEST(test_e2e_seq_evaluate_replay_is_rejected_and_does_not_move_tracker);
    RUN_TEST(test_e2e_seq_evaluate_enforce_off_always_accepts);
    RUN_TEST(test_e2e_seq_evaluate_wraparound_is_a_clean_single_increment);
    RUN_TEST(test_e2e_request_store_overflow_reports_error_code_but_not_escalation);
    RUN_TEST(test_e2e_overflow_should_enter_safe_state_is_gated_only_on_the_config_bit);
    RUN_TEST(test_e2e_crc_error_should_enter_safe_state_is_gated_only_on_rx_enforce_e2e);
    RUN_TEST(test_e2e_has_no_aggregate_stream_blocked_status_across_all_four_fault_causes);

    return UNITY_END();
}
