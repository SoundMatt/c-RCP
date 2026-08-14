/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-E2E-031
//cfusa:test REQ-E2E-032
//cfusa:test REQ-E2E-033
//cfusa:test REQ-E2E-034
//cfusa:test REQ-E2E-035
//cfusa:test REQ-E2E-036
//cfusa:test REQ-E2E-037
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
//cfusa:test REQ-E2E-040
//cfusa:test REQ-E2E-041
//cfusa:test REQ-E2E-042
//cfusa:test REQ-WDG-010
//cfusa:test REQ-E2E-021
//cfusa:test REQ-E2E-028
//cfusa:test REQ-E2E-029

/*
 * test_tc18_gaps_e2e.c -- spec-literal conformance-and-deviation suite for
 * the TC18 sec. 13.6 ("End-to-End data protection / safe endpoints")
 * clauses catalogued during the requirements-corpus completeness pass
 * (.fusa-reqs.json REQ-E2E-031..042).
 *
 * Two kinds of test live here, and the difference matters when reading a
 * failure:
 *
 *   - For a requirement catalogued "implemented", the test asserts the
 *     clause literally, against values derived from the clause itself
 *     (the 8-octet stream_id + 4-octet avtp_timestamp big-endian CRC
 *     prefix, the +1-quadlet acf_msg_length adaptation, the 4-octet
 *     trailer) rather than round-tripping this module against itself.
 *
 *   - For a requirement catalogued "partial" or "not-implemented", the
 *     test PINS THE DEVIATION: it asserts the current, real, observable
 *     behaviour of this code, and the comment above it names the sec. 13.6
 *     clause that is not met and what a conforming implementation would
 *     do instead. Such a test failing means the behaviour changed --
 *     most likely because the gap was closed, in which case the test
 *     (and the requirement's status) must be rewritten to the conforming
 *     expectation.
 */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/e2e.h>
#include <rcp/mock.h>
#include <rcp/regmap.h>
#include <rcp/request_timed.h>
#include <rcp/scheduler.h>
#include <rcp/watchdog.h>

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Fixed, deliberately non-palindromic test vectors: byte-order mistakes
 * in either the stream_id or the avtp_timestamp change the CRC. */
#define TEST_SID ((uint64_t)0x0102030405060708ULL)
#define TEST_TS  ((uint32_t)0x11223344u)

/* Encodes one ACF_ABB message through acf.c's own encoder, i.e. exactly
 * the input rcp_e2e_wrap() documents itself as taking. */
static rcp_bytes_t make_abb(uint8_t ms, uint8_t rsp, uint16_t seg,
                            const uint8_t *payload, size_t payload_len)
{
    rcp_acf_byte_message_info_t h;

    memset(&h, 0, sizeof(h));
    h.byte_bus_id              = 0x11;
    h.transaction_num          = 0x22;
    h.op                       = (uint8_t)RCP_ACF_OP_WRITE;
    h.ms                       = ms;
    h.rsp                      = rsp;
    h.read_size_or_segment_num = seg;
    return rcp_acf_encode_abb(&h, payload, payload_len);
}

/* The 9-bit acf_msg_length quadlet count: bit 0 of octet 0 plus octet 1. */
static uint16_t len_field(const uint8_t *frame)
{
    return (uint16_t)(((uint16_t)(frame[0] & 0x01u) << 8) | (uint16_t)frame[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ── mock.c dispatch_e2e()/dispatch_frame_e2e() test fixtures ─────────────── */

static const rcp_lifecycle_plausibility_snapshot_t EMPTY_SNAP = {NULL, 0, NULL, 0};

/* Any byte_bus_id passes rcp_lifecycle_should_accept() once HW_CONFIGURED.
 * HW_UNCONFIGURED -> HW_CONFIGURED does not consult writer, so a plain
 * {0} is correct here, not just a convenience default. As of the
 * REQ-LIFECYCLE-022 fix, this advance also requires all_other_eps_idle
 * -- true here, since this fixture is not itself testing idleness. */
static void to_hw_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t none = {0};

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP, none, true));
}

/* As of the REQ-LIFECYCLE-028 fix, HW_CONFIGURED unconditionally drops
 * TSCF-headed AVTPDUs (TC18 §12.3.1.2) -- a fixture dispatching real,
 * nonzero-timestamp TSCF-framed traffic needs RCP_CONFIGURED instead.
 * EMPTY_SNAP's zero endpoint/request-stream counts trivially satisfy both
 * plausibility checks along the way, the same way to_hw_configured()
 * already relies on for its own single transition. As of the
 * REQ-LIFECYCLE-031 fix, the HW_CONFIGURED -> RCP_CONFIGURED advance
 * also requires an authorized writer (not idle-gated, per Figure 16). */
static void to_rcp_configured(rcp_mock_server_t *srv)
{
    rcp_lifecycle_writer_ctx_t root = {true, false, false, false};

    to_hw_configured(srv);
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP, root, true));
}

static bool g_handler_called;

static void counting_handler(const uint8_t *request, size_t request_len, rcp_bytes_t *out_response,
                              void *user_data)
{
    (void)request;
    (void)out_response;
    (void)user_data;
    g_handler_called = true;
    TEST_ASSERT_TRUE(request_len > 0);
}

/* ── REQ-E2E-031: plain vs. safe command mode ──────────────────────────────── */

/* TC18 sec. 13.6: in safe command mode (ep_req_crc_enable set) a request
 * arriving without a valid CRC32 trailer must not be executed; plain
 * command mode never checks. As of the REQ-E2E-031/041 fix,
 * rcp_mock_server_set_endpoint_req_crc_enable() (mock.c's own in-process
 * stand-in for the register bit -- see that function's own doc comment
 * for why it can't just read rcp_regmap_ep_functional_cfg_t directly) and
 * rcp_mock_server_dispatch_e2e() together implement this: an endpoint left
 * at its default (plain mode) executes an unprotected request exactly as
 * rcp_mock_server_dispatch() would; the same endpoint switched to safe
 * mode does not. */
static void test_dispatch_e2e_plain_mode_executes_unprotected_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));

    /* RCP_CONFIGURED, not HW_CONFIGURED: HW_CONFIGURED admits only EP0 as
     * of the REQ-LIFECYCLE-032 fix -- see to_rcp_configured()'s own
     * comment. */
    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    /* req_crc_enable left at its default (false): plain command mode. */

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    plain.data, plain.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_e2e_safe_mode_executes_a_validly_wrapped_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped =
        rcp_e2e_wrap_framed(TEST_SID, false /* TSCF-framed */, TEST_TS, plain.data, plain.len);

    /* RCP_CONFIGURED, not HW_CONFIGURED: TSCF-framed traffic (real,
     * nonzero avtp_timestamp) is unconditionally dropped in HW_CONFIGURED
     * as of the REQ-LIFECYCLE-028 fix (TC18 §12.3.1.2) -- see
     * to_rcp_configured()'s own comment. */
    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    g_handler_called = false;
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    wrapped.data, wrapped.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* The unprotected request from the plain-mode test above, sent to the
 * SAME endpoint after it is switched into safe mode: rejected, not
 * executed -- exactly the gate the pre-fix version of this test pinned
 * as missing. */
static void test_dispatch_e2e_safe_mode_rejects_an_unprotected_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));

    to_hw_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    plain.data, plain.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-032 / REQ-E2E-040: what safe mode changes, in both directions ─── */

/* Both catalogued "implemented": safe command mode alters nothing but the
 * appended trailer and the length accounting, and one identical scheme
 * serves requests and responses (no direction parameter exists). */
static void test_safe_mode_changes_only_trailer_and_length(void)
{
    const uint8_t pl[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t   req   = make_abb(0, 0, 0, pl, sizeof(pl)); /* request-shaped  */
    rcp_bytes_t   rsp   = make_abb(0, 1, 0, pl, sizeof(pl)); /* response-shaped */
    rcp_bytes_t   wreq  = rcp_e2e_wrap(TEST_SID, TEST_TS, req.data, req.len);
    rcp_bytes_t   wrsp  = rcp_e2e_wrap(TEST_SID, TEST_TS, rsp.data, rsp.len);

    TEST_ASSERT_NOT_NULL(wreq.data);
    TEST_ASSERT_NOT_NULL(wrsp.data);

    /* Exactly four octets longer, acf_msg_type preserved, and every octet
     * from the byte_message_info's third onward (i.e. all but the two
     * octets holding acf_msg_length) byte-identical to plain mode. */
    TEST_ASSERT_EQUAL_UINT(req.len + RCP_E2E_CRC_LEN, wreq.len);
    TEST_ASSERT_EQUAL_HEX8(req.data[0] & 0xFEu, wreq.data[0] & 0xFEu);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req.data + 2, wreq.data + 2, req.len - 2);
    TEST_ASSERT_EQUAL_UINT16(len_field(req.data) + 1u, len_field(wreq.data));

    /* The response direction differs in no respect at all. */
    TEST_ASSERT_EQUAL_UINT(rsp.len + RCP_E2E_CRC_LEN, wrsp.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rsp.data + 2, wrsp.data + 2, rsp.len - 2);
    TEST_ASSERT_EQUAL_UINT16(len_field(rsp.data) + 1u, len_field(wrsp.data));

    /* Same trailer derivation for both: CRC over the length-adapted frame,
     * big-endian, in the message's final quadlet. */
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SID, TEST_TS, wreq.data, req.len),
                            be32(wreq.data + req.len));
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SID, TEST_TS, wrsp.data, rsp.len),
                            be32(wrsp.data + rsp.len));

    rcp_bytes_free(&wrsp);
    rcp_bytes_free(&wreq);
    rcp_bytes_free(&rsp);
    rcp_bytes_free(&req);
}

/* ── REQ-E2E-034: the CRC coverage prefix ──────────────────────────────────── */

/* Catalogued "implemented": stream_id contributes 8 big-endian octets and
 * avtp_timestamp 4 (IEEE 1722's own field width), in that order, ahead of
 * the ACF frame. Asserted against a hand-built concatenation, plus two
 * negative controls that would pass if the width or the byte order were
 * wrong. */
static void test_crc_coverage_prefix_is_stream_id_8_then_timestamp_4(void)
{
    const uint8_t body[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t       be_ref[8 + 4 + 4];
    uint8_t       le_ts[8 + 4 + 4];
    uint8_t       ts8[8 + 8 + 4];
    uint32_t      actual;
    size_t        i;

    for (i = 0; i < 8; i++) be_ref[i] = (uint8_t)(TEST_SID >> (56u - 8u * i));
    for (i = 0; i < 4; i++) be_ref[8 + i] = (uint8_t)(TEST_TS >> (24u - 8u * i));
    memcpy(be_ref + 12, body, sizeof(body));

    memcpy(le_ts, be_ref, sizeof(le_ts));
    for (i = 0; i < 4; i++) le_ts[8 + i] = (uint8_t)(TEST_TS >> (8u * i));

    memcpy(ts8, be_ref, 8);
    for (i = 0; i < 8; i++) ts8[8 + i] = (uint8_t)((uint64_t)TEST_TS >> (56u - 8u * i));
    memcpy(ts8 + 16, body, sizeof(body));

    actual = rcp_e2e_compute_crc(TEST_SID, TEST_TS, body, sizeof(body));

    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_crc32(be_ref, sizeof(be_ref)), actual);
    /* Not little-endian, and not an 8-octet timestamp. */
    TEST_ASSERT_TRUE(rcp_e2e_crc32(le_ts, sizeof(le_ts)) != actual);
    TEST_ASSERT_TRUE(rcp_e2e_crc32(ts8, sizeof(ts8)) != actual);
}

/* ── REQ-E2E-035: the NTSCF all-zero timestamp stand-in ────────────────────── */

/* TC18 sec. 13.6 requires an NTSCF-framed message (an NTSCF header carries
 * no timestamp of its own) to contribute four all-zero octets as its
 * avtp_timestamp. The raw rcp_e2e_wrap()/_unwrap() primitives stay
 * general-purpose (a bare uint32_t avtp_timestamp, since they serve both
 * TSCF- and NTSCF-framed callers) and still trust the caller to pass 0 for
 * NTSCF traffic -- see test_wrap_unwrap_round_trip_ok()-style direct-primitive
 * tests elsewhere for that documented contract. As of the REQ-E2E-035 fix,
 * rcp_e2e_wrap_framed()/_unwrap_framed() are the framing-safe alternative:
 * a caller that already knows a message's framing passes is_ntscf_framed
 * instead of pre-zeroing avtp_timestamp itself, and the wrapper enforces
 * the zero contribution regardless of what avtp_timestamp it was given. */
static void test_ntscf_framed_wrapper_forces_zero_timestamp(void)
{
    const uint8_t pl[4]   = {0x5A, 0x5B, 0x5C, 0x5D};
    rcp_bytes_t   frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    /* Raw primitive: still trusts the caller (documented, unchanged). */
    rcp_bytes_t   as_spec = rcp_e2e_wrap(TEST_SID, 0u, frame.data, frame.len);
    rcp_bytes_t   as_told = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    /* Framing-aware wrapper: is_ntscf_framed=true forces the zero
     * contribution even though a nonzero avtp_timestamp was passed in. */
    rcp_bytes_t   framed  = rcp_e2e_wrap_framed(TEST_SID, true, TEST_TS,
                                                 frame.data, frame.len);
    rcp_bytes_t   body    = {0};

    /* Raw primitive: both wraps succeed and are the same size -- nothing
     * rejects or corrects the non-zero timestamp on its own. */
    TEST_ASSERT_NOT_NULL(as_spec.data);
    TEST_ASSERT_NOT_NULL(as_told.data);
    TEST_ASSERT_EQUAL_UINT(as_spec.len, as_told.len);
    TEST_ASSERT_TRUE(be32(as_spec.data + frame.len) != be32(as_told.data + frame.len));

    /* Framing-aware wrapper: despite being told TEST_TS, it produces
     * exactly the same trailer as the correctly-zeroed raw call. */
    TEST_ASSERT_NOT_NULL(framed.data);
    TEST_ASSERT_EQUAL_UINT(as_spec.len, framed.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(as_spec.data, framed.data, as_spec.len);

    /* Symmetric on the decode side: unwrap_framed(..., true, TEST_TS, ...)
     * verifies against the zeroed contribution and matches the wrapper's
     * own output, regardless of the nonzero avtp_timestamp passed in. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap_framed(TEST_SID, true, TEST_TS,
                                                 framed.data, framed.len, &body));
    rcp_bytes_free(&body);

    /* The raw primitive's own mismatch-surfaces-late behavior is unchanged
     * (still documented, not a regression this fix touches). */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                          rcp_e2e_unwrap(TEST_SID, 0u, as_told.data, as_told.len, &body));
    rcp_bytes_free(&body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, 0u, as_spec.data, as_spec.len, &body));

    rcp_bytes_free(&body);
    rcp_bytes_free(&framed);
    rcp_bytes_free(&as_told);
    rcp_bytes_free(&as_spec);
    rcp_bytes_free(&frame);
}

/* ── REQ-E2E-036: the +1-quadlet acf_msg_length adaptation ─────────────────── */

/* Catalogued "implemented": +1 quadlet on wrap (before the CRC is
 * computed), -1 on unwrap, fail-safe at both ends of the 9-bit field. */
static void test_acf_msg_length_adaptation_and_reversal(void)
{
    const uint8_t pl[4]      = {0x10, 0x20, 0x30, 0x40};
    rcp_bytes_t   frame      = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t   wrapped;
    rcp_bytes_t   body       = {0};
    /* acf_msg_length already at its 9-bit maximum (0x1FF): +1 overflows.
     * Octet 0 is (RCP_ACF_MSG_TYPE_ABB << 1) | length bit 8. */
    uint8_t       maxed[8]   = {0x1Du, 0xFFu, 0, 0, 0, 0, 0, 0};
    uint8_t       one_octet  = 0x1Cu;
    rcp_bytes_t   too_short;
    rcp_bytes_t   overflowed;

    /* 8-octet header + 4-octet payload = 3 quadlets; the trailer makes 4. */
    TEST_ASSERT_EQUAL_UINT(12u, frame.len);
    TEST_ASSERT_EQUAL_UINT16(3u, len_field(frame.data));

    wrapped = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL_UINT16(4u, len_field(wrapped.data));
    TEST_ASSERT_EQUAL_UINT16(3u, len_field(frame.data)); /* caller's copy untouched */

    /* unwrap() reverses it exactly: byte-identical to what wrap() got. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, wrapped.data, wrapped.len, &body));
    TEST_ASSERT_EQUAL_UINT(frame.len, body.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, body.data, frame.len);

    /* Fail safe: too short to hold the field, and adaptation overflow. */
    too_short = rcp_e2e_wrap(TEST_SID, TEST_TS, &one_octet, 1u);
    TEST_ASSERT_NULL(too_short.data);
    TEST_ASSERT_EQUAL_UINT(0u, too_short.len);
    overflowed = rcp_e2e_wrap(TEST_SID, TEST_TS, maxed, sizeof(maxed));
    TEST_ASSERT_NULL(overflowed.data);
    TEST_ASSERT_EQUAL_UINT(0u, overflowed.len);

    rcp_bytes_free(&body);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&frame);
}

/* ── REQ-E2E-033: per-ACF-message CRC in a multi-ACF AVTPDU ────────────────── */

/* TC18 sec. 13.6 requires a separate CRC32 per E2E-protected ACF message,
 * verified individually -- never one CRC across the whole AVTPDU payload.
 * rcp_e2e_wrap()/_unwrap() are already at that granularity (first block
 * below, unchanged); as of the REQ-E2E-033/041 fix,
 * rcp_mock_server_dispatch_frame_e2e() composes that across a real
 * multi-member frame: each member is routed through
 * rcp_mock_server_dispatch_e2e() independently, so corrupting only the
 * second member's trailer produces one successful execution and one
 * RCP_MOCK_DISPATCH_CRC_ERROR, not an all-or-nothing frame-wide verdict. */
static void test_each_member_of_a_multi_acf_frame_carries_its_own_crc(void)
{
    const uint8_t a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    const uint8_t b[4] = {0xB1, 0xB2, 0xB3, 0xB4};
    rcp_bytes_t   m1   = make_abb(0, 0, 0, a, sizeof(a));
    rcp_bytes_t   m2   = make_abb(0, 0, 0, b, sizeof(b));
    rcp_bytes_t   w1   = rcp_e2e_wrap(TEST_SID, TEST_TS, m1.data, m1.len);
    rcp_bytes_t   w2   = rcp_e2e_wrap(TEST_SID, TEST_TS, m2.data, m2.len);
    rcp_bytes_t   body = {0};
    uint8_t       joined[32];
    size_t        offs[4];

    TEST_ASSERT_EQUAL_UINT(16u, w1.len);
    TEST_ASSERT_EQUAL_UINT(16u, w2.len);
    memcpy(joined, w1.data, w1.len);
    memcpy(joined + w1.len, w2.data, w2.len);

    TEST_ASSERT_EQUAL_UINT(2u, rcp_sched_split_frame_members(joined, sizeof(joined), offs, 4));
    TEST_ASSERT_EQUAL_UINT(0u, offs[0]);
    TEST_ASSERT_EQUAL_UINT(16u, offs[1]);

    /* Each member verifies against its own trailer, not one CRC across
     * the whole payload. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, joined, 16u, &body));
    rcp_bytes_free(&body);

    /* Corrupt only the second member's trailer. */
    joined[31] ^= 0xFFu;
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, joined, 16u, &body));
    rcp_bytes_free(&body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, joined + 16u, 16u, &body));
    /* The frame walker itself, being e2e-blind, still reports two
     * syntactic members regardless -- that's rcp_sched_split_frame_members()'s
     * own, unrelated contract (member boundaries, not trailer validity). */
    TEST_ASSERT_EQUAL_UINT(2u, rcp_sched_split_frame_members(joined, sizeof(joined), offs, 4));

    rcp_bytes_free(&body);
    rcp_bytes_free(&w2);
    rcp_bytes_free(&w1);
    rcp_bytes_free(&m2);
    rcp_bytes_free(&m1);
}

/* The real server-facing composition: both members addressed to the same
 * safe-mode endpoint (make_abb() always targets byte_bus_id 0x11), one
 * with a valid trailer and one corrupted, dispatched together as a single
 * frame via rcp_mock_server_dispatch_frame_e2e(). */
static void test_dispatch_frame_e2e_verifies_each_member_independently(void)
{
    rcp_mock_server_t             *srv = rcp_mock_server_new();
    const uint8_t                   a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    const uint8_t                   b[4] = {0xB1, 0xB2, 0xB3, 0xB4};
    rcp_bytes_t                     m1   = make_abb(0, 0, 0, a, sizeof(a));
    rcp_bytes_t                     m2   = make_abb(0, 0, 0, b, sizeof(b));
    rcp_bytes_t                     w1   = rcp_e2e_wrap(TEST_SID, TEST_TS, m1.data, m1.len);
    rcp_bytes_t                     w2   = rcp_e2e_wrap(TEST_SID, TEST_TS, m2.data, m2.len);
    uint8_t                         joined[32];
    rcp_mock_frame_member_result_t  results[4];
    size_t                          dispatched;

    TEST_ASSERT_EQUAL_UINT(16u, w1.len);
    TEST_ASSERT_EQUAL_UINT(16u, w2.len);
    memcpy(joined, w1.data, w1.len);
    memcpy(joined + w1.len, w2.data, w2.len);
    joined[31] ^= 0xFFu; /* corrupt only the second member's trailer */

    /* RCP_CONFIGURED, not HW_CONFIGURED -- see to_rcp_configured()'s own
     * comment (TSCF-framed traffic is unconditionally dropped in
     * HW_CONFIGURED as of the REQ-LIFECYCLE-028 fix). */
    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    g_handler_called = false;
    dispatched = rcp_mock_server_dispatch_frame_e2e(srv, RCP_AVTP_SUBTYPE_TSCF, true, TEST_SID,
                                                     TEST_TS, 0u, joined, sizeof(joined), results, 4);
    TEST_ASSERT_EQUAL_UINT(2u, dispatched);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR, results[1].result);
    TEST_ASSERT_TRUE(g_handler_called); /* the first, valid member DID execute */
    /* The second member's error response carries the real Table 27 code. */
    TEST_ASSERT_NOT_NULL(results[1].response.data);

    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&results[1].response);
    rcp_bytes_free(&w2);
    rcp_bytes_free(&w1);
    rcp_bytes_free(&m2);
    rcp_bytes_free(&m1);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-037: the AVTPDU data-length adjustment ────────────────────────── */

/* TC18 sec. 13.6 requires the AVTPDU's ntscf_data_length /
 * stream_data_length to be raised by 4 octets for every E2E-protected ACF
 * message it carries. rcp_avtp_encode_ntscf() already satisfies this
 * automatically and correctly -- it recomputes the field from whatever
 * payload it is handed (it ignores hdr.ntscf_data_length entirely, as the
 * bogus value below shows), so the +4-per-protected-member accounting was
 * already right as long as the caller wrapped every protected member
 * first. As of the REQ-E2E-037 fix, that rule now also has its own named,
 * pure, directly-testable expression: rcp_e2e_data_length_for_protected_members(). */
static void test_avtpdu_data_length_grows_four_octets_per_protected_member(void)
{
    const uint8_t             p[4] = {0xC1, 0xC2, 0xC3, 0xC4};
    rcp_bytes_t               m    = make_abb(0, 0, 0, p, sizeof(p));
    rcp_bytes_t               w    = rcp_e2e_wrap(TEST_SID, TEST_TS, m.data, m.len);
    uint8_t                   plain_payload[24];
    uint8_t                   safe_payload[32];
    rcp_avtp_ntscf_header_t   hdr;
    rcp_avtp_ntscf_header_t   got;
    rcp_bytes_t               enc_plain;
    rcp_bytes_t               enc_safe;
    const uint8_t            *pl  = NULL;
    size_t                    len = 0;

    memcpy(plain_payload, m.data, m.len);
    memcpy(plain_payload + m.len, m.data, m.len);
    memcpy(safe_payload, w.data, w.len);
    memcpy(safe_payload + w.len, w.data, w.len);

    memset(&hdr, 0, sizeof(hdr));
    hdr.sv                = 1;
    hdr.stream_id         = rcp_stream_id_from_u64(TEST_SID);
    hdr.ntscf_data_length = 0x7FFu; /* deliberately wrong: encode ignores it */

    enc_plain = rcp_avtp_encode_ntscf(&hdr, plain_payload, sizeof(plain_payload));
    enc_safe  = rcp_avtp_encode_ntscf(&hdr, safe_payload, sizeof(safe_payload));
    TEST_ASSERT_NOT_NULL(enc_plain.data);
    TEST_ASSERT_NOT_NULL(enc_safe.data);

    TEST_ASSERT_EQUAL_INT(RCP_AVTP_OK,
                          rcp_avtp_decode_ntscf(enc_plain.data, enc_plain.len, &got, &pl, &len));
    TEST_ASSERT_EQUAL_UINT16(24u, got.ntscf_data_length);
    TEST_ASSERT_EQUAL_INT(RCP_AVTP_OK,
                          rcp_avtp_decode_ntscf(enc_safe.data, enc_safe.len, &got, &pl, &len));
    /* Two protected members: exactly rcp_e2e_data_length_for_protected_members(2)
     * more than the plain-mode length -- the named rule matches the
     * encoder's own actual behavior. */
    TEST_ASSERT_EQUAL_UINT16(24u + (uint16_t)rcp_e2e_data_length_for_protected_members(2u),
                             got.ntscf_data_length);

    rcp_bytes_free(&enc_safe);
    rcp_bytes_free(&enc_plain);
    rcp_bytes_free(&w);
    rcp_bytes_free(&m);
}

/* Direct tests of rcp_e2e_data_length_for_protected_members() itself. */
static void test_data_length_for_protected_members_is_pure_arithmetic(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, rcp_e2e_data_length_for_protected_members(0u));
    TEST_ASSERT_EQUAL_UINT((size_t)RCP_E2E_CRC_LEN,
                           rcp_e2e_data_length_for_protected_members(1u));
    TEST_ASSERT_EQUAL_UINT((size_t)RCP_E2E_CRC_LEN * 5u,
                           rcp_e2e_data_length_for_protected_members(5u));
    /* Saturates at SIZE_MAX on overflow, same discipline as
     * rcp_e2e_length_with_crc(). */
    TEST_ASSERT_EQUAL_UINT((size_t)-1,
                           rcp_e2e_data_length_for_protected_members((size_t)-1));
}

/* ── REQ-E2E-038: the fragmentation coverage rule ──────────────────────────── */

/* PARTIAL (was: not implemented). TC18 sec. 13.6 requires a fragmented
 * message's CRC32 to span the FIRST AVTPDU's stream_id/avtp_timestamp and
 * the FIRST fragment's ACF header, followed by the concatenated
 * byte_msg_payload of EVERY segment in order. As of the REQ-E2E-038 fix,
 * rcp_e2e_compute_fragmented_crc() computes exactly that -- covering
 * segment 0 in a way rcp_e2e_wrap()'s existing final-fragment-only
 * behavior still does not (the last two assertions show the same gap
 * this test always pinned: mutating segment 0's payload moves the
 * conforming CRC but leaves rcp_e2e_wrap()-based verification happy,
 * because c-RCP still has no caller anywhere that wires the new
 * primitive into an actual fragmented encode/decode path -- see the
 * function's own doc comment for why). */
static void test_fragmented_crc_covers_only_the_last_fragment(void)
{
    const uint8_t p0[4] = {0xF0, 0xF1, 0xF2, 0xF3};
    const uint8_t p1[4] = {0xE0, 0xE1, 0xE2, 0xE3};
    const uint8_t p2[4] = {0xD0, 0xD1, 0xD2, 0xD3};
    rcp_bytes_t   f0    = make_abb(1, 0, 0, p0, sizeof(p0)); /* ms=1, segment 0 */
    rcp_bytes_t   f1    = make_abb(1, 0, 1, p1, sizeof(p1)); /* ms=1, segment 1 */
    rcp_bytes_t   f2    = make_abb(0, 0, 2, p2, sizeof(p2)); /* ms=0, final     */
    rcp_bytes_t   w2;
    rcp_bytes_t   body  = {0};
    uint8_t       payload[4 + 4 + 4]; /* every segment's own payload, concatenated */
    uint32_t      conforming;

    TEST_ASSERT_TRUE(rcp_e2e_fragment_carries_crc(true));
    w2 = rcp_e2e_wrap(TEST_SID, TEST_TS, f2.data, f2.len);
    TEST_ASSERT_NOT_NULL(w2.data);

    /* What rcp_e2e_wrap()-based dispatch actually computes: the final
     * fragment's bytes alone. */
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SID, TEST_TS, w2.data, f2.len),
                            be32(w2.data + f2.len));

    memcpy(payload, p0, 4);
    memcpy(payload + 4, p1, 4);
    memcpy(payload + 8, p2, 4);
    conforming = rcp_e2e_compute_fragmented_crc(TEST_SID, TEST_TS, f0.data, 8u,
                                                 payload, sizeof(payload));
    TEST_ASSERT_TRUE(conforming != be32(w2.data + f2.len));

    /* Segment 0 is unprotected: its corruption moves the conforming CRC
     * but leaves this implementation's verification happy. */
    payload[0] ^= 0xFFu;
    TEST_ASSERT_TRUE(rcp_e2e_compute_fragmented_crc(TEST_SID, TEST_TS, f0.data, 8u,
                                                     payload, sizeof(payload)) != conforming);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SID, TEST_TS, w2.data, w2.len, &body));

    rcp_bytes_free(&body);
    rcp_bytes_free(&w2);
    rcp_bytes_free(&f2);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
}

/* Direct tests of rcp_e2e_compute_fragmented_crc() itself: equivalent to
 * concatenating header ++ payload and calling rcp_e2e_compute_crc() once
 * (the technique it uses internally to avoid the allocation), and
 * sensitive to a change anywhere in either region. */
static void test_compute_fragmented_crc_matches_manual_concatenation(void)
{
    const uint8_t hdr[8]     = {0x0Eu, 0x00, 0x11, 0x22, 0x00, 0x00, 0x00, 0x10};
    const uint8_t payload[8] = {0xA0, 0xA1, 0xA2, 0xA3, 0xB0, 0xB1, 0xB2, 0xB3};
    uint8_t       concat[sizeof(hdr) + sizeof(payload)];
    uint32_t      via_helper;
    uint32_t      via_manual_concat;

    memcpy(concat, hdr, sizeof(hdr));
    memcpy(concat + sizeof(hdr), payload, sizeof(payload));

    via_helper = rcp_e2e_compute_fragmented_crc(TEST_SID, TEST_TS, hdr, sizeof(hdr),
                                                 payload, sizeof(payload));
    via_manual_concat = rcp_e2e_compute_crc(TEST_SID, TEST_TS, concat, sizeof(concat));
    TEST_ASSERT_EQUAL_HEX32(via_manual_concat, via_helper);

    /* Sensitive to the header region... */
    {
        uint8_t bad_hdr[8];
        memcpy(bad_hdr, hdr, sizeof(hdr));
        bad_hdr[0] ^= 0xFFu;
        TEST_ASSERT_TRUE(rcp_e2e_compute_fragmented_crc(TEST_SID, TEST_TS, bad_hdr, sizeof(bad_hdr),
                                                         payload, sizeof(payload)) != via_helper);
    }
    /* ...and to the payload region, anywhere in it (not just the tail). */
    {
        uint8_t bad_payload[8];
        memcpy(bad_payload, payload, sizeof(payload));
        bad_payload[0] ^= 0xFFu; /* segment 0's own octet, not segment 1's */
        TEST_ASSERT_TRUE(rcp_e2e_compute_fragmented_crc(TEST_SID, TEST_TS, hdr, sizeof(hdr),
                                                         bad_payload, sizeof(bad_payload)) != via_helper);
    }
}

/* ── REQ-E2E-039: the ms bit / trailer binding ─────────────────────────────── */

/* DEVIATION PIN (partial). TC18 sec. 13.6 binds the trailer to the ACF
 * header's ms bit: ms=0 carries the CRC32 in its last quadlet, ms=1
 * carries none. c-RCP expresses only half of that -- the binding below
 * holds when a caller derives the argument from a decoded ms bit itself,
 * but rcp_e2e_fragment_carries_crc() takes a bare bool, is called nowhere
 * in src/, and rcp_e2e_wrap() never reads ms: it appends a trailer to an
 * ms=1 (non-final) fragment just as readily. A conforming implementation
 * would refuse to protect a fragment whose ms bit is set. */
static void test_ms_bit_to_carries_crc_binding_is_not_enforced(void)
{
    const uint8_t               pl[4] = {0x71, 0x72, 0x73, 0x74};
    rcp_bytes_t                 last  = make_abb(0, 0, 3, pl, sizeof(pl));
    rcp_bytes_t                 mid   = make_abb(1, 0, 1, pl, sizeof(pl));
    rcp_acf_byte_message_info_t h_last;
    rcp_acf_byte_message_info_t h_mid;
    const uint8_t              *out_pl  = NULL;
    size_t                      out_len = 0;
    rcp_bytes_t                 wmid;

    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(last.data, last.len, &h_last, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(mid.data, mid.len, &h_mid, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(0u, h_last.ms);
    TEST_ASSERT_EQUAL_UINT8(1u, h_mid.ms);

    /* The binding, driven off a real decoded ms bit. */
    TEST_ASSERT_TRUE(rcp_e2e_fragment_carries_crc(!h_last.ms));
    TEST_ASSERT_FALSE(rcp_e2e_fragment_carries_crc(!h_mid.ms));

    /* Unenforced: wrap() protects the ms=1 fragment anyway, leaving its ms
     * bit (octet 6, bit 4) set alongside a trailer sec. 13.6 forbids. */
    wmid = rcp_e2e_wrap(TEST_SID, TEST_TS, mid.data, mid.len);
    TEST_ASSERT_NOT_NULL(wmid.data);
    TEST_ASSERT_EQUAL_UINT(mid.len + RCP_E2E_CRC_LEN, wmid.len);
    TEST_ASSERT_EQUAL_HEX8(0x10u, wmid.data[6] & 0x10u);

    rcp_bytes_free(&wmid);
    rcp_bytes_free(&mid);
    rcp_bytes_free(&last);
}

/* ── REQ-E2E-041: CRC_ERROR handling ───────────────────────────────────────── */

/* TC18 sec. 13.6 requires a CRC mismatch on a protected stream both to
 * skip execution AND to generate an error response carrying the
 * CRC-mismatch code (POCI_FAILURE, 12, per sec. 12.9.6 Table 27). The
 * raw primitives (rcp_e2e_unwrap()/rcp_e2e_wire_error()) already report
 * the mismatch and map it to code 12 -- first block below, unchanged.
 * As of the REQ-E2E-041 fix, rcp_mock_server_dispatch_e2e() is the real
 * caller and error-response emitter that were missing: on a mismatch it
 * returns RCP_MOCK_DISPATCH_CRC_ERROR without ever calling
 * rcp_server_endpoint_admit() (not admitted, not just not-executed), and
 * *out_response is a genuine TC18 sec. 12.9.6 Error Response -- err=1,
 * rsp=1, payload = RCP_ERROR_POCI_FAILURE -- built via
 * rcp_acf_build_error_response(), not a diagnostic echo of the request. */
static void test_crc_mismatch_skips_execution_without_error_response(void)
{
    const uint8_t               pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                 frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                 w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                 body  = {0};
    rcp_e2e_errc_t              rc;

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    rc = rcp_e2e_unwrap(TEST_SID, TEST_TS, w.data, w.len, &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH, rc); /* execution skipped */
    TEST_ASSERT_EQUAL_INT(12, (int)RCP_ERROR_POCI_FAILURE);
    TEST_ASSERT_EQUAL_INT(RCP_ERROR_POCI_FAILURE, rcp_e2e_wire_error(rc));

    rcp_bytes_free(&body);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
}

/* The real server-facing half: the same corrupted-trailer frame, through
 * rcp_mock_server_dispatch_e2e() against a registered, safe-mode
 * endpoint. */
static void test_dispatch_e2e_crc_mismatch_yields_real_error_response(void)
{
    rcp_mock_server_t          *srv  = rcp_mock_server_new();
    const uint8_t                pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                  frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                  w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                  resp  = {0};
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t               *out_pl  = NULL;
    size_t                       out_len = 0;

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    to_hw_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    w.data, w.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called); /* never admitted, let alone executed */

    /* A genuine Error Response, not an echo of the request. */
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(resp.data, resp.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.rsp);
    TEST_ASSERT_EQUAL_UINT(1u, out_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_POCI_FAILURE, out_pl[0]);
    /* byte_bus_id/transaction_num carried through from the request, per
     * TC18 sec. 12.9.6. */
    TEST_ASSERT_EQUAL_UINT8(0x11u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(0x22u, hdr.transaction_num); /* make_abb()'s fixed value */

    rcp_bytes_free(&resp);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-045 (issue #335): CRC-error safe-state broadcast ──────────────
 *
 * TC18 §12.7.7 Table 24's own rx_enforce_e2e/rx_enforce_crc (0x000D.0)
 * names TWO consequences at the SAME single bit -- "stream is blocked
 * until released" (the fault-tracker latch the test above's own sibling
 * tests, e.g. test_crc_error_latches_the_whole_stream_faulted in this
 * file's own REQ-E2E-021 section, already cover) AND "Safe state will be
 * entered", stream-wide. This test proves that SECOND consequence
 * end-to-end: a CRC mismatch on endpoint 0x11 purges a non-safety-tagged
 * request queued on endpoint 0x12, a sibling bound to the same request
 * stream via EP_ID_config (rcp_mock_server_set_ep_id_map(), issue #335)
 * -- 0x12 was never the endpoint whose own CRC failed, and never even had
 * req_crc_enable set on it. */
static void test_crc_error_on_one_endpoint_broadcasts_safe_state_to_stream_siblings(void)
{
    rcp_mock_server_t              *srv     = rcp_mock_server_new();
    const uint8_t                    pl[4]   = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                      frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                      w       = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                      resp    = {0};
    rcp_bytes_t                      timed;
    rcp_regmap_ep_id_map_entry_t     ep_map[2] = {
        {1, 0x11, 1}, /* ep 1, bbid 0x11, stream 1 -- the one whose CRC fails */
        {2, 0x12, 1}, /* ep 2, bbid 0x12, stream 1 -- the sibling */
    };
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    /* RCP_CONFIGURED, not merely HW_CONFIGURED: the sibling's own pending
     * request below is admitted via the PLAIN rcp_mock_server_dispatch()
     * (not dispatch_e2e()'s own CRC-mismatch branch, which returns before
     * ever reaching a lifecycle check), and rcp_lifecycle_should_accept()
     * only admits a non-EP0 byte_bus_id once fully operational. */
    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 0x12, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, ep_map, 2));

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id = TEST_SID;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    /* Give 0x12 one non-safety-tagged (RCP_REQUEST_TYPE_TIMED, MSB clear)
     * stored request to later confirm gets purged. */
    timed = rcp_timed_encode_request(0x12, 0x1000u, 7u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(timed.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch(srv, 0x12, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, TEST_SID, timed.data,
                                                timed.len, &resp));
    rcp_bytes_free(&resp);
    rcp_bytes_free(&timed);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 0x12));

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    w.data, w.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called); /* never admitted, let alone executed */

    /* The actual proof: 0x12's own stored request was purged by the
     * broadcast, though 0x12 itself was never addressed by the failing
     * request. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 0x12));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Negative control: identical setup, EXCEPT srv's own EP_ID_config table
 * is left empty (no rcp_mock_server_set_ep_id_map() call) -- confirms the
 * broadcast above is a genuine consequence of EP_ID_config's own content,
 * not something rcp_mock_server_dispatch_e2e() would have done anyway
 * (e.g. as a byproduct of the fault-tracker latch, or unconditionally for
 * every registered endpoint). Without a bound-endpoint table, 0x12's own
 * pending request survives. */
static void test_crc_error_does_not_broadcast_without_an_ep_id_map(void)
{
    rcp_mock_server_t              *srv     = rcp_mock_server_new();
    const uint8_t                    pl[4]   = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                      frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                      w       = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                      resp    = {0};
    rcp_bytes_t                      timed;
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 0x12, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));
    /* Deliberately no rcp_mock_server_set_ep_id_map() call. */

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id = TEST_SID;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    timed = rcp_timed_encode_request(0x12, 0x1000u, 7u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(timed.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch(srv, 0x12, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, TEST_SID, timed.data,
                                                timed.len, &resp));
    rcp_bytes_free(&resp);
    rcp_bytes_free(&timed);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    w.data, w.len, &resp));

    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 0x12));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-028/029 (issue #338): per-frame sequence_num gate ─────────────
 *
 * TC18 §12.7.7 Table 24's own rx_enforce_seq (0x000D bit 1) and
 * rx_seq_safestate_enable (0x000D bit 2) previously had a complete pure
 * primitive (rcp_e2e_seq_evaluate(), e2e.h) but no caller anywhere in this
 * codebase -- rcp_mock_server_dispatch_frame()/_dispatch_frame_e2e() now
 * evaluate it once per frame, before any member is processed, using
 * srv's own new per-request-stream rcp_e2e_seq_tracker_t. */

static void set_up_seq_stream(rcp_mock_server_t *srv, bool rx_enforce_seq,
                               bool rx_seq_safestate_enable)
{
    rcp_regmap_request_stream_cfg_t stream_cfg[1];

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id             = TEST_SID;
    stream_cfg[0].rx_enforce_seq           = rx_enforce_seq;
    stream_cfg[0].rx_seq_safestate_enable  = rx_seq_safestate_enable;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));
}

/* The first call ever against a fresh tracker always accepts (nothing to
 * compare against yet, rcp_e2e_seq_evaluate()'s own has_prev contract) --
 * a REPLAY of that same sequence_num on the very next frame is then
 * correctly rejected: every member of the second frame comes back
 * RCP_MOCK_DISPATCH_SEQ_ERROR, byte_bus_id 0, response zeroed. */
static void test_dispatch_frame_rejects_replayed_sequence_num(void)
{
    rcp_mock_server_t             *srv   = rcp_mock_server_new();
    const uint8_t                   pl[4] = {1, 2, 3, 4};
    rcp_bytes_t                     frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_mock_frame_member_result_t  results[4];
    size_t                           n;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    set_up_seq_stream(srv, true, false);

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 5u,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    rcp_bytes_free(&results[0].response);

    /* Same seq again -- a replay, not a fresh request. */
    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 5u,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_SEQ_ERROR, results[0].result);
    TEST_ASSERT_EQUAL_UINT16(0, results[0].byte_bus_id);
    TEST_ASSERT_NULL(results[0].response.data);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* RFC 1982 wraparound: 0xFF -> 0x00 is a valid single increment, not a
 * replay -- see rcp_e2e_seq_evaluate()'s own doc comment (e2e.h). */
static void test_dispatch_frame_accepts_wrapped_sequence_num(void)
{
    rcp_mock_server_t             *srv   = rcp_mock_server_new();
    const uint8_t                   pl[4] = {1, 2, 3, 4};
    rcp_bytes_t                     frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_mock_frame_member_result_t  results[4];
    size_t                           n;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    set_up_seq_stream(srv, true, false);

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 0xFFu,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    rcp_bytes_free(&results[0].response);

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 0x00u,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result); /* NOT rejected as a replay */
    rcp_bytes_free(&results[0].response);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* The correctness pitfall this design deliberately avoids: sequence_num
 * is a property of the whole AVTPDU, not of any one ACF member packed
 * inside it. Two members in the SAME frame, same seq, must both be
 * admitted -- a per-member evaluate() would spuriously reject the second
 * one (its own prev_seq already advanced by the first member's own
 * call). */
static void test_dispatch_frame_seq_gate_evaluates_once_not_per_member(void)
{
    rcp_mock_server_t             *srv    = rcp_mock_server_new();
    const uint8_t                   pl[4]  = {1, 2, 3, 4};
    rcp_bytes_t                     one    = make_abb(0, 0, 0, pl, sizeof(pl));
    uint8_t                        *joined;
    size_t                           joined_len = one.len * 2u;
    rcp_mock_frame_member_result_t  results[4];
    size_t                           n;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    set_up_seq_stream(srv, true, false);

    joined = malloc(joined_len);
    TEST_ASSERT_NOT_NULL(joined);
    memcpy(joined, one.data, one.len);
    memcpy(joined + one.len, one.data, one.len);

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 9u,
                                        joined, joined_len, results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[1].result); /* NOT SEQ_ERROR */
    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&results[1].response);

    free(joined);
    rcp_bytes_free(&one);
    rcp_mock_server_destroy(srv);
}

/* rx_seq_safestate_enable's own discontinuity check is independent of
 * rx_enforce_seq's own accept/reject decision -- a forward jump of more
 * than one (still "ahead", so accept stays true) still broadcasts safe
 * state to every endpoint bound to the stream, the same
 * rcp_mock_server_broadcast_safe_state() escalation already proven for
 * REQ-E2E-030 (overflow) and REQ-E2E-045 (CRC error, above), reached
 * through a materially different trigger this time. */
static void test_dispatch_frame_discontinuity_broadcasts_safe_state_without_rejecting(void)
{
    rcp_mock_server_t             *srv       = rcp_mock_server_new();
    const uint8_t                   pl[4]     = {1, 2, 3, 4};
    rcp_bytes_t                     frame     = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                     resp      = {0};
    rcp_bytes_t                     timed;
    rcp_mock_frame_member_result_t  results[4];
    rcp_regmap_ep_id_map_entry_t    ep_map[2] = {
        {1, 0x11, 1},
        {2, 0x12, 1},
    };
    size_t                           n;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 0x12, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, ep_map, 2));
    set_up_seq_stream(srv, true, true);

    /* Give 0x12 one non-safety-tagged stored request to confirm gets
     * purged by the broadcast. */
    timed = rcp_timed_encode_request(0x12, 0x1000u, 7u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(timed.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch(srv, 0x12, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, TEST_SID, timed.data,
                                                timed.len, &resp));
    rcp_bytes_free(&resp);
    rcp_bytes_free(&timed);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 0x12));

    /* Establishes prev_seq = 5 (first call, always accepts). */
    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 5u,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    rcp_bytes_free(&results[0].response);

    /* Jumps to 10 -- still forward (accept stays true, this frame's own
     * member is NOT rejected), but a genuine gap (not seq == 6). */
    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 10u,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    rcp_bytes_free(&results[0].response);

    /* The actual proof: 0x12's own stored request was purged, though 0x12
     * was never addressed by either dispatch_frame() call above. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 0x12));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* No rcp_mock_server_set_request_stream_cfg() call resolving TEST_SID at
 * all -- resolve_index() returns 0, the gate is skipped entirely (fail-
 * toward-no-action), and dispatch proceeds regardless of any sequence_num
 * pattern -- the exact same seq value twice in a row, unrejected. */
static void test_dispatch_frame_seq_gate_skipped_for_unresolvable_stream(void)
{
    rcp_mock_server_t             *srv   = rcp_mock_server_new();
    const uint8_t                   pl[4] = {1, 2, 3, 4};
    rcp_bytes_t                     frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_mock_frame_member_result_t  results[4];
    size_t                           n;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    /* Deliberately no rcp_mock_server_set_request_stream_cfg() call. */

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 5u,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    rcp_bytes_free(&results[0].response);

    n = rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 5u,
                                        frame.data, frame.len, results, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result); /* NOT SEQ_ERROR */
    rcp_bytes_free(&results[0].response);

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Confirms rcp_mock_server_dispatch_frame_e2e()'s own identical gate,
 * end-to-end: a replayed sequence_num rejects the frame before any
 * member's own CRC handling is even attempted, on an endpoint with
 * req_crc_enable set (so the E2E-specific code path is genuinely
 * exercised, not just the plain per-member dispatch it delegates to when
 * req_crc_enable is unset). */
static void test_dispatch_frame_e2e_rejects_replayed_sequence_num(void)
{
    rcp_mock_server_t             *srv   = rcp_mock_server_new();
    const uint8_t                   pl[4] = {1, 2, 3, 4};
    rcp_bytes_t                     frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                     w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_mock_frame_member_result_t  results[4];
    size_t                           n;

    TEST_ASSERT_NOT_NULL(w.data);

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    set_up_seq_stream(srv, true, false);

    n = rcp_mock_server_dispatch_frame_e2e(srv, RCP_AVTP_SUBTYPE_TSCF, true, TEST_SID, TEST_TS,
                                            5u, w.data, w.len, results, 4);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    rcp_bytes_free(&results[0].response);

    n = rcp_mock_server_dispatch_frame_e2e(srv, RCP_AVTP_SUBTYPE_TSCF, true, TEST_SID, TEST_TS,
                                            5u, w.data, w.len, results, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_SEQ_ERROR, results[0].result);
    TEST_ASSERT_NULL(results[0].response.data);

    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-WDG-010: rcp_mock_server_dispatch_e2e() kicks the per-stream
 * watchdog ──────────────────────────────────────────────────────────────── */

static void wdg_busy_wait_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();

    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait: no sleep primitive is exported by rcp/clock.h, same
         * as every other timing-based test in this codebase (see
         * test_watchdog.c's own test_sleep_ms() / test_tc18_gaps_server.c's
         * own busy_wait_ms()). */
    }
}

/* As of the REQ-WDG-010 fix, rcp_mock_server_dispatch_e2e() calls
 * rcp_watchdog_keeper_kick() on every request it receives on the
 * associated stream, so a client dispatching well inside its configured
 * timeout never overflows -- the exact scenario
 * test_tc18_gaps_server.c's own test_watchdog_overflows_despite_
 * continuous_requests() pins as broken for the lower-level
 * rcp_server_endpoint_submit() path (still true; that fix is deliberately
 * out of scope here, see rcp_mock_server_set_watchdog_keeper()'s own doc
 * comment). Mirrors test_watchdog.c's own
 * test_kick_resets_timer_prevents_overflow(): a 40 ms timeout, dispatched
 * every 10 ms for 100 ms total -- far longer than 40 ms would survive
 * without kicking. */
static void test_dispatch_e2e_kicks_the_watchdog_on_every_admitted_request(void)
{
    rcp_mock_server_t         *srv = rcp_mock_server_new();
    rcp_watchdog_stream_cfg_t  stream = {TEST_SID, true, 40u, true, true};
    rcp_watchdog_config_t      cfg    = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t     *k;
    const uint8_t               pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t                 plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                 wrapped;
    int                          i;

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, &stream, 1u);
    TEST_ASSERT_NOT_NULL(k);

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    rcp_mock_server_set_watchdog_keeper(srv, k);

    for (i = 0; i < 10; i++) {
        rcp_bytes_t resp = {0};

        wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_TS, plain.data, plain.len);
        TEST_ASSERT_NOT_NULL(wrapped.data);

        wdg_busy_wait_ms(10u);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                          rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                        RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                        TEST_TS, wrapped.data, wrapped.len, &resp));
        TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, TEST_SID).overflowed);

        rcp_bytes_free(&resp);
        rcp_bytes_free(&wrapped);
    }

    rcp_watchdog_keeper_destroy(k);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* The "receipt not validation" half of the design: a request that
 * dispatch_e2e() goes on to REJECT (a CRC mismatch, here -- the same
 * fixture as test_dispatch_e2e_crc_mismatch_yields_real_error_response())
 * still means the RC Client is alive and talking on this stream, so it
 * must still kick. A single dispatch call right after construction can't
 * tell the two orderings apart -- rcp_watchdog_keeper_new() itself sets
 * last_kick_ms at construction time, so "overflowed" would read false
 * either way with almost no elapsed time. Instead this test spends most
 * of the 60 ms timeout BEFORE dispatching the rejected request, then
 * spends most of it again AFTER: only a kick actually caused by the
 * rejected dispatch call (not the constructor's own initial kick) can
 * keep the stream from overflowing across both waits. If the kick were
 * placed after CRC validation instead of before it (a plausible but
 * wrong ordering), this test fails -- that ordering is exactly what
 * mutation-testing this fix caught. */
static void test_dispatch_e2e_kicks_the_watchdog_even_when_the_request_is_rejected(void)
{
    rcp_mock_server_t         *srv = rcp_mock_server_new();
    rcp_watchdog_stream_cfg_t  stream = {TEST_SID, true, 60u, true, true};
    rcp_watchdog_config_t      cfg    = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t     *k;
    const uint8_t               pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                 frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                 w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                 resp  = {0};

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    cfg.poll_interval_ms = 5;
    k = rcp_watchdog_keeper_new(cfg, &stream, 1u);
    TEST_ASSERT_NOT_NULL(k);

    to_hw_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    rcp_mock_server_set_watchdog_keeper(srv, k);

    wdg_busy_wait_ms(40u); /* consume most of the 60 ms budget from construction */
    TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, TEST_SID).overflowed);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    w.data, w.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called); /* rejected, not executed ... */

    wdg_busy_wait_ms(40u); /* would overflow (80 ms > 60 ms) without a kick just now */
    TEST_ASSERT_FALSE(rcp_watchdog_keeper_status(k, TEST_SID).overflowed); /* ... but still kicked */

    rcp_watchdog_keeper_destroy(k);
    rcp_bytes_free(&resp);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* No keeper set (rcp_mock_server_new()'s own default, unchanged by every
 * test above this one in this file) -- dispatch_e2e() must not crash or
 * otherwise misbehave; it just has nothing to kick. */
static void test_dispatch_e2e_with_no_watchdog_keeper_set_dispatches_normally(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    /* rcp_mock_server_set_watchdog_keeper() deliberately never called. */

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    plain.data, plain.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-021: a CRC error on an rx_enforce_e2e stream blocks the whole
 * stream until released ─────────────────────────────────────────────────── */

/* A corrupted-trailer request on an rx_enforce_e2e endpoint latches the
 * stream; a SUBSEQUENT, perfectly valid request on that SAME stream_id
 * is rejected outright -- RCP_MOCK_DISPATCH_STREAM_FAULTED, not even
 * reaching CRC validation -- until the tracker is reset. This is the
 * exact gap REQ-E2E-021 used to pin: before this fix, the second request
 * below would have been admitted and executed normally. */
static void test_dispatch_e2e_crc_error_with_rx_enforce_e2e_blocks_the_whole_stream(void)
{
    rcp_mock_server_t         *srv = rcp_mock_server_new();
    rcp_e2e_stream_fault_tracker_t tracker;
    const uint8_t                pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                  frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                  w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                  resp1 = {0};
    rcp_bytes_t                  resp2 = {0};
    const uint8_t                good_pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t                  good_frame = make_abb(0, 0, 1, good_pl, sizeof(good_pl));
    rcp_bytes_t                  good_wrapped =
        rcp_e2e_wrap_framed(TEST_SID, false, TEST_TS, good_frame.data, good_frame.len);
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *out_pl  = NULL;
    size_t                        out_len = 0;

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    rcp_e2e_stream_fault_tracker_init(&tracker);
    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));
    rcp_mock_server_set_stream_fault_tracker(srv, &tracker);

    /* First request: CRC mismatch, latches the stream (rx_enforce_e2e). */
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    w.data, w.len, &resp1));
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&tracker, TEST_SID));

    /* Second request: perfectly valid, but the stream is now blocked --
     * never even reaches CRC validation, let alone admission. */
    g_handler_called = false;
    TEST_ASSERT_NOT_NULL(good_wrapped.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_STREAM_FAULTED,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    good_wrapped.data, good_wrapped.len, &resp2));
    TEST_ASSERT_FALSE(g_handler_called);
    /* A genuine error response, matching CRC_ERROR's own convention. */
    TEST_ASSERT_NOT_NULL(resp2.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(resp2.data, resp2.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT(1u, out_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_POCI_FAILURE, out_pl[0]);
    rcp_bytes_free(&resp2); /* free the STREAM_FAULTED response before resp2 is reused below */

    /* Released: the same valid request now succeeds normally. */
    rcp_e2e_stream_fault_tracker_reset(&tracker, TEST_SID);
    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    good_wrapped.data, good_wrapped.len, &resp2));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp2);
    rcp_bytes_free(&resp1);
    rcp_bytes_free(&good_wrapped);
    rcp_bytes_free(&good_frame);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Without rx_enforce_e2e (the default), a CRC error still skips that one
 * request (RCP_MOCK_DISPATCH_CRC_ERROR, matching every pre-existing CRC-
 * mismatch test in this file) but does NOT latch the stream -- a
 * subsequent valid request on the same stream succeeds normally. Matches
 * rcp_e2e_crc_error_action()'s own RCP_E2E_CRC_ACTION_DROP_REQUEST. */
static void test_dispatch_e2e_crc_error_without_rx_enforce_e2e_does_not_block_the_stream(void)
{
    rcp_mock_server_t         *srv = rcp_mock_server_new();
    rcp_e2e_stream_fault_tracker_t tracker;
    const uint8_t                pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                  frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                  w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                  resp1 = {0};
    rcp_bytes_t                  resp2 = {0};
    const uint8_t                good_pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t                  good_frame = make_abb(0, 0, 1, good_pl, sizeof(good_pl));
    rcp_bytes_t                  good_wrapped =
        rcp_e2e_wrap_framed(TEST_SID, false, TEST_TS, good_frame.data, good_frame.len);

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu;

    rcp_e2e_stream_fault_tracker_init(&tracker);
    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    /* rcp_mock_server_set_endpoint_rx_enforce_e2e() deliberately never called (defaults false). */
    rcp_mock_server_set_stream_fault_tracker(srv, &tracker);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    w.data, w.len, &resp1));
    TEST_ASSERT_FALSE(rcp_e2e_stream_fault_tracker_is_faulted(&tracker, TEST_SID));

    g_handler_called = false;
    TEST_ASSERT_NOT_NULL(good_wrapped.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    good_wrapped.data, good_wrapped.len, &resp2));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp2);
    rcp_bytes_free(&resp1);
    rcp_bytes_free(&good_wrapped);
    rcp_bytes_free(&good_frame);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* No stream fault tracker set (rcp_mock_server_new()'s own default) --
 * dispatch_e2e() must not crash or otherwise misbehave; it just has
 * nothing to check or record against. */
static void test_dispatch_e2e_with_no_stream_fault_tracker_set_dispatches_normally(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    /* rcp_mock_server_set_stream_fault_tracker() deliberately never called. */

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    plain.data, plain.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-046 (issue #336): rx_stream_status live wiring ─────────────────
 *
 * srv's own stream_status[] array (mock.c) is now latched as a byproduct
 * of the SAME live dispatch paths that already existed for
 * stream_fault_tracker (CRC) and the frame-level sequence gate (seq) --
 * proven here via rcp_mock_server_stream_status_rx_blocked(), not by
 * calling rcp_e2e_stream_status_*() directly (that would only prove the
 * primitive works, already established by e2e.h's own unit tests; this
 * proves the WIRING). */

/* A CRC mismatch on a request-stream-cfg-resolvable stream latches
 * stream_status[]'s own CRC cause -- independent of whether
 * rx_enforce_e2e/stream_fault_tracker are configured at all (this is
 * the readable Table 24 bit, not the blocking mechanism). */
static void test_dispatch_e2e_crc_error_latches_stream_status(void)
{
    rcp_mock_server_t              *srv   = rcp_mock_server_new();
    const uint8_t                    pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                      frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                      w     = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                      resp  = {0};
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    /* rx_enforce_e2e must be set for a CRC error to actually latch --
     * rcp_e2e_stream_fault_on_crc_error()'s own gate, the same one
     * stream_fault_tracker's own identical call already relies on. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id = TEST_SID;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID)); /* not yet */

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, TEST_TS,
                                                    w.data, w.len, &resp));

    TEST_ASSERT_TRUE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* A replayed sequence_num on an rx_enforce_seq-configured stream latches
 * stream_status[]'s own sequence cause. */
static void test_dispatch_frame_seq_error_latches_stream_status(void)
{
    rcp_mock_server_t             *srv   = rcp_mock_server_new();
    const uint8_t                   pl[4] = {1, 2, 3, 4};
    rcp_bytes_t                     frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_mock_frame_member_result_t  results[4];

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    /* rx_seq_safestate_enable must be set for a discontinuity to set
     * result.enter_safe_state -- stream_status[]'s own seq latch is
     * gated on that flag, the same one the broadcast-safe-state
     * actuator right below it (mock.c) already gates on. A plain
     * rejection (result.accept == false) alone does not imply
     * enter_safe_state == true. */
    set_up_seq_stream(srv, true, true);

    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));

    rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 5u, frame.data,
                                    frame.len, results, 4);
    rcp_bytes_free(&results[0].response);
    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID)); /* first-ever seq
                                                                                    always accepts */

    /* Replay -- rejected, and now latched. */
    rcp_mock_server_dispatch_frame(srv, RCP_AVTP_SUBTYPE_NTSCF, true, TEST_SID, 5u, frame.data,
                                    frame.len, results, 4);
    TEST_ASSERT_NULL(results[0].response.data);
    TEST_ASSERT_TRUE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));

    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* An unresolvable stream_id (no rcp_mock_server_set_request_stream_cfg()
 * call for it) reads as not-blocked -- the same fail-toward-not-blocked
 * disposition every other unresolvable-stream case in this module
 * already uses; not a crash or an error. */
static void test_stream_status_rx_blocked_false_for_unresolvable_stream(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));

    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-042: pad coverage and quadlet alignment ───────────────────────── */

/* TC18 sec. 13.6 Figures 19/20 compute the CRC over whole quadlets --
 * header quadlets plus byte_msg_payload INCLUDING the 0x00 pad octets --
 * so that the CRC itself occupies the message's final whole quadlet. The
 * pad octets are covered (mutating one changes the CRC), and as of the
 * REQ-E2E-042 fix, alignment is enforced too: rcp_e2e_wrap() rejects any
 * acf_frame_len that is not a whole quadlet (data=NULL, len=0) rather than
 * appending a trailer that straddles a quadlet boundary. */
static void test_crc_covers_pad_octets_and_alignment_is_enforced(void)
{
    const uint8_t               pl[5] = {0x01, 0x02, 0x03, 0x04, 0x05};
    rcp_bytes_t                 frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *out_pl  = NULL;
    size_t                      out_len = 0;
    uint32_t                    with_zero_pad;
    uint8_t                     misaligned[6] = {0x1Cu, 0x02u, 0, 0, 0, 0};
    rcp_bytes_t                 mis;
    rcp_bytes_t                 aligned_ok;

    /* 8 + 5 = 13 octets, so 3 pad octets bring it to 4 whole quadlets. */
    TEST_ASSERT_EQUAL_UINT(16u, frame.len);
    TEST_ASSERT_EQUAL_UINT8(3u, rcp_acf_pad_len(8u + sizeof(pl)));
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(3u, hdr.pad);
    TEST_ASSERT_EQUAL_UINT8(0u, frame.data[15]);

    /* The pad octets are inside the coverage span. */
    with_zero_pad = rcp_e2e_compute_crc(TEST_SID, TEST_TS, frame.data, frame.len);
    frame.data[15] = 0xFFu;
    TEST_ASSERT_TRUE(rcp_e2e_compute_crc(TEST_SID, TEST_TS, frame.data, frame.len) !=
                     with_zero_pad);

    /* Enforced: 6 octets is not a whole quadlet, so wrap() now fails safe
     * instead of appending a trailer that straddles a quadlet boundary. */
    mis = rcp_e2e_wrap(TEST_SID, TEST_TS, misaligned, sizeof(misaligned));
    TEST_ASSERT_NULL(mis.data);
    TEST_ASSERT_EQUAL_UINT(0u, mis.len);

    /* A properly quadlet-aligned, already-padded frame still wraps fine. */
    frame.data[15] = 0u;
    aligned_ok = rcp_e2e_wrap(TEST_SID, TEST_TS, frame.data, frame.len);
    TEST_ASSERT_NOT_NULL(aligned_ok.data);
    TEST_ASSERT_EQUAL_UINT(frame.len + RCP_E2E_CRC_LEN, aligned_ok.len);

    rcp_bytes_free(&aligned_ok);
    rcp_bytes_free(&mis);
    rcp_bytes_free(&frame);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dispatch_e2e_plain_mode_executes_unprotected_request);
    RUN_TEST(test_dispatch_e2e_safe_mode_executes_a_validly_wrapped_request);
    RUN_TEST(test_dispatch_e2e_safe_mode_rejects_an_unprotected_request);
    RUN_TEST(test_safe_mode_changes_only_trailer_and_length);
    RUN_TEST(test_crc_coverage_prefix_is_stream_id_8_then_timestamp_4);
    RUN_TEST(test_ntscf_framed_wrapper_forces_zero_timestamp);
    RUN_TEST(test_acf_msg_length_adaptation_and_reversal);
    RUN_TEST(test_each_member_of_a_multi_acf_frame_carries_its_own_crc);
    RUN_TEST(test_dispatch_frame_e2e_verifies_each_member_independently);
    RUN_TEST(test_avtpdu_data_length_grows_four_octets_per_protected_member);
    RUN_TEST(test_data_length_for_protected_members_is_pure_arithmetic);
    RUN_TEST(test_fragmented_crc_covers_only_the_last_fragment);
    RUN_TEST(test_compute_fragmented_crc_matches_manual_concatenation);
    RUN_TEST(test_ms_bit_to_carries_crc_binding_is_not_enforced);
    RUN_TEST(test_crc_mismatch_skips_execution_without_error_response);
    RUN_TEST(test_dispatch_e2e_crc_mismatch_yields_real_error_response);
    RUN_TEST(test_crc_error_on_one_endpoint_broadcasts_safe_state_to_stream_siblings);
    RUN_TEST(test_crc_error_does_not_broadcast_without_an_ep_id_map);

    RUN_TEST(test_dispatch_frame_rejects_replayed_sequence_num);
    RUN_TEST(test_dispatch_frame_accepts_wrapped_sequence_num);
    RUN_TEST(test_dispatch_frame_seq_gate_evaluates_once_not_per_member);
    RUN_TEST(test_dispatch_frame_discontinuity_broadcasts_safe_state_without_rejecting);
    RUN_TEST(test_dispatch_frame_seq_gate_skipped_for_unresolvable_stream);
    RUN_TEST(test_dispatch_frame_e2e_rejects_replayed_sequence_num);
    RUN_TEST(test_dispatch_e2e_kicks_the_watchdog_on_every_admitted_request);
    RUN_TEST(test_dispatch_e2e_kicks_the_watchdog_even_when_the_request_is_rejected);
    RUN_TEST(test_dispatch_e2e_with_no_watchdog_keeper_set_dispatches_normally);
    RUN_TEST(test_dispatch_e2e_crc_error_with_rx_enforce_e2e_blocks_the_whole_stream);
    RUN_TEST(test_dispatch_e2e_crc_error_without_rx_enforce_e2e_does_not_block_the_stream);
    RUN_TEST(test_dispatch_e2e_with_no_stream_fault_tracker_set_dispatches_normally);

    RUN_TEST(test_dispatch_e2e_crc_error_latches_stream_status);
    RUN_TEST(test_dispatch_frame_seq_error_latches_stream_status);
    RUN_TEST(test_stream_status_rx_blocked_false_for_unresolvable_stream);
    RUN_TEST(test_crc_covers_pad_octets_and_alignment_is_enforced);
    return UNITY_END();
}
