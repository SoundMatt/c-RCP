/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-TIMED-012
//cfusa:test REQ-TIMED-013

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

#include "../src/mem_bounded.h"

#include <rcp/acf.h>
#include <rcp/alloc.h>
#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/e2e.h>
#include <rcp/ep_can.h>
#include <rcp/mock.h>
#include <rcp/regmap.h>
#include <rcp/request.h>
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

/* issue #465: avtp_subtype/header_octet1/tu -- the Figure 20/21
 * header-CRC bytes rcp_e2e_compute_crc()/_wrap()/_unwrap() now require.
 * TEST_OCTET1/TEST_TU are deliberately equal to src/mock.c's own
 * documented RCP_MOCK_E2E_HEADER_OCTET1_PLACEHOLDER/_TU_PLACEHOLDER
 * (0x00/false) -- most tests in this file build a request with the raw
 * wrap()/wrap_framed() primitives and then feed it into
 * rcp_mock_server_dispatch_e2e()/_fragment(), whose own internal
 * rcp_e2e_unwrap_framed()/_compute_fragmented_crc() calls always use
 * that same placeholder (mock.c has no per-message header_octet1/tu of
 * its own to pass -- see mock.c's own file-top comment); using anything
 * else here would make every one of those round-trips spuriously CRC-
 * mismatch. Coverage for "header_octet1/tu are actually fed into the
 * CRC" (flipping just one of them changes the result) lives in
 * test_e2e.c's own dedicated tests instead, which call the raw
 * primitives directly and are not constrained by mock.c's placeholder. */
#define TEST_SUBTYPE ((uint8_t)0x05u) /* TSCF */
#define TEST_OCTET1  ((uint8_t)0x00u)
#define TEST_TU      (false)

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

/* Encodes one ACF_GBB message through acf.c's own encoder -- the
 * fragment/reassembly dispatchers' OTHER acf_msg_type branch, sibling to
 * make_abb() above. mtv left at RCP_ACF_MTV_UNTIMED (message_timestamp
 * irrelevant to the fragment tests using this helper). */
static rcp_bytes_t make_gbb(uint8_t ms, uint8_t rsp, uint16_t seg,
                            const uint8_t *payload, size_t payload_len)
{
    rcp_acf_gbb_header_t h;

    memset(&h, 0, sizeof(h));
    h.info.byte_bus_id              = 0x11;
    h.info.transaction_num          = 0x22;
    h.info.op                       = (uint8_t)RCP_ACF_OP_WRITE;
    h.info.ms                       = ms;
    h.info.rsp                      = rsp;
    h.info.read_size_or_segment_num = seg;
    return rcp_acf_encode_gbb(&h, payload, payload_len);
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
 * also requires an authorized writer (not idle-gated, per Figure 17). */
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
//cfusa:test REQ-E2E-031
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

//cfusa:test REQ-E2E-031
static void test_dispatch_e2e_safe_mode_executes_a_validly_wrapped_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped =
        rcp_e2e_wrap_framed(TEST_SID, false /* TSCF-framed */, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);

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
//cfusa:test REQ-E2E-031
//cfusa:test REQ-E2E-041
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
//cfusa:test REQ-E2E-032
//cfusa:test REQ-E2E-040
static void test_safe_mode_changes_only_trailer_and_length(void)
{
    const uint8_t pl[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t   req   = make_abb(0, 0, 0, pl, sizeof(pl)); /* request-shaped  */
    rcp_bytes_t   rsp   = make_abb(0, 1, 0, pl, sizeof(pl)); /* response-shaped */
    rcp_bytes_t   wreq  = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, req.data, req.len);
    rcp_bytes_t   wrsp  = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, rsp.data, rsp.len);

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
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, wreq.data, req.len),
                            be32(wreq.data + req.len));
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, wrsp.data, rsp.len),
                            be32(wrsp.data + rsp.len));

    rcp_bytes_free(&wrsp);
    rcp_bytes_free(&wreq);
    rcp_bytes_free(&rsp);
    rcp_bytes_free(&req);
}

/* ── REQ-E2E-034: the CRC coverage prefix ──────────────────────────────────── */

/* Catalogued "implemented": avtp_subtype (1 octet) + header_octet1 (1
 * octet) + a tu byte (1 octet) -- Figure 20/21's own orange header-CRC
 * bytes, issue #465 -- then stream_id (8 big-endian octets) and
 * avtp_timestamp (4, IEEE 1722's own field width), in that order, ahead
 * of the ACF frame. Asserted against a hand-built concatenation, plus
 * two negative controls that would pass if the width or the byte order
 * were wrong. */
//cfusa:test REQ-E2E-034
static void test_crc_coverage_prefix_is_stream_id_8_then_timestamp_4(void)
{
    const uint8_t body[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t       be_ref[3 + 8 + 4 + 4];
    uint8_t       le_ts[3 + 8 + 4 + 4];
    uint8_t       ts8[3 + 8 + 8 + 4];
    uint32_t      actual;
    size_t        i;

    be_ref[0] = TEST_SUBTYPE;
    be_ref[1] = TEST_OCTET1;
    be_ref[2] = TEST_TU ? 0x01u : 0x00u;
    for (i = 0; i < 8; i++) be_ref[3 + i] = (uint8_t)(TEST_SID >> (56u - 8u * i));
    for (i = 0; i < 4; i++) be_ref[11 + i] = (uint8_t)(TEST_TS >> (24u - 8u * i));
    rcp_memcpy_bounded(be_ref + 15, sizeof(be_ref) - 15, body, sizeof(body));

    rcp_memcpy_bounded(le_ts, sizeof(le_ts), be_ref, sizeof(le_ts));
    for (i = 0; i < 4; i++) le_ts[11 + i] = (uint8_t)(TEST_TS >> (8u * i));

    rcp_memcpy_bounded(ts8, sizeof(ts8), be_ref, 11);
    for (i = 0; i < 8; i++) ts8[11 + i] = (uint8_t)((uint64_t)TEST_TS >> (56u - 8u * i));
    rcp_memcpy_bounded(ts8 + 19, sizeof(ts8) - 19, body, sizeof(body));

    actual = rcp_e2e_compute_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, body, sizeof(body));

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
//cfusa:test REQ-E2E-035
static void test_ntscf_framed_wrapper_forces_zero_timestamp(void)
{
    const uint8_t pl[4]   = {0x5A, 0x5B, 0x5C, 0x5D};
    rcp_bytes_t   frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    /* Raw primitive: still trusts the caller (documented, unchanged).
     * as_spec models exactly what rcp_e2e_wrap_framed(..., true, ...)
     * derives/forces internally (issue #465): avtp_subtype =
     * RCP_AVTP_SUBTYPE_NTSCF (not TEST_SUBTYPE, a TSCF value), tu =
     * false (not TEST_TU), avtp_timestamp = 0 -- header_octet1 passes
     * through unchanged, exactly like the framed wrapper's own contract. */
    rcp_bytes_t   as_spec = rcp_e2e_wrap(RCP_AVTP_SUBTYPE_NTSCF, TEST_OCTET1, false, TEST_SID, 0u,
                                          frame.data, frame.len);
    rcp_bytes_t   as_told = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
    /* Framing-aware wrapper: is_ntscf_framed=true forces the zero
     * contribution even though a nonzero avtp_timestamp was passed in. */
    rcp_bytes_t   framed  = rcp_e2e_wrap_framed(TEST_SID, true, TEST_OCTET1, TEST_TU, TEST_TS,
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
                          rcp_e2e_unwrap_framed(TEST_SID, true, TEST_OCTET1, TEST_TU, TEST_TS,
                                                 framed.data, framed.len, &body));
    rcp_bytes_free(&body);

    /* The raw primitive's own mismatch-surfaces-late behavior is unchanged
     * (still documented, not a regression this fix touches). */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                          rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, 0u,
                                         as_told.data, as_told.len, &body));
    rcp_bytes_free(&body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(RCP_AVTP_SUBTYPE_NTSCF, TEST_OCTET1, false, TEST_SID, 0u,
                                         as_spec.data, as_spec.len, &body));

    rcp_bytes_free(&body);
    rcp_bytes_free(&framed);
    rcp_bytes_free(&as_told);
    rcp_bytes_free(&as_spec);
    rcp_bytes_free(&frame);
}

/* ── REQ-E2E-036: the +1-quadlet acf_msg_length adaptation ─────────────────── */

/* Catalogued "implemented": +1 quadlet on wrap (before the CRC is
 * computed), -1 on unwrap, fail-safe at both ends of the 9-bit field. */
//cfusa:test REQ-E2E-036
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

    wrapped = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL_UINT16(4u, len_field(wrapped.data));
    TEST_ASSERT_EQUAL_UINT16(3u, len_field(frame.data)); /* caller's copy untouched */

    /* unwrap() reverses it exactly: byte-identical to what wrap() got. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, wrapped.data, wrapped.len, &body));
    TEST_ASSERT_EQUAL_UINT(frame.len, body.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, body.data, frame.len);

    /* Fail safe: too short to hold the field, and adaptation overflow. */
    too_short = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, &one_octet, 1u);
    TEST_ASSERT_NULL(too_short.data);
    TEST_ASSERT_EQUAL_UINT(0u, too_short.len);
    overflowed = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, maxed, sizeof(maxed));
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
//cfusa:test REQ-E2E-033
static void test_each_member_of_a_multi_acf_frame_carries_its_own_crc(void)
{
    const uint8_t a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    const uint8_t b[4] = {0xB1, 0xB2, 0xB3, 0xB4};
    rcp_bytes_t   m1   = make_abb(0, 0, 0, a, sizeof(a));
    rcp_bytes_t   m2   = make_abb(0, 0, 0, b, sizeof(b));
    rcp_bytes_t   w1   = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m1.data, m1.len);
    rcp_bytes_t   w2   = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m2.data, m2.len);
    rcp_bytes_t   body = {0};
    uint8_t       joined[32];
    size_t        offs[4];

    TEST_ASSERT_EQUAL_UINT(16u, w1.len);
    TEST_ASSERT_EQUAL_UINT(16u, w2.len);
    rcp_memcpy_bounded(joined, sizeof(joined), w1.data, w1.len);
    rcp_memcpy_bounded(joined + w1.len, sizeof(joined) - w1.len, w2.data, w2.len);

    TEST_ASSERT_EQUAL_UINT(2u, rcp_sched_split_frame_members(joined, sizeof(joined), offs, 4));
    TEST_ASSERT_EQUAL_UINT(0u, offs[0]);
    TEST_ASSERT_EQUAL_UINT(16u, offs[1]);

    /* Each member verifies against its own trailer, not one CRC across
     * the whole payload. */
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, joined, 16u, &body));
    rcp_bytes_free(&body);

    /* Corrupt only the second member's trailer. */
    joined[31] ^= 0xFFu;
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, joined, 16u, &body));
    rcp_bytes_free(&body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_ERR_CRC_MISMATCH,
                          rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, joined + 16u, 16u, &body));
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
//cfusa:test REQ-E2E-033
//cfusa:test REQ-E2E-041
static void test_dispatch_frame_e2e_verifies_each_member_independently(void)
{
    rcp_mock_server_t             *srv = rcp_mock_server_new();
    const uint8_t                   a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    const uint8_t                   b[4] = {0xB1, 0xB2, 0xB3, 0xB4};
    rcp_bytes_t                     m1   = make_abb(0, 0, 0, a, sizeof(a));
    rcp_bytes_t                     m2   = make_abb(0, 0, 0, b, sizeof(b));
    rcp_bytes_t                     w1   = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m1.data, m1.len);
    rcp_bytes_t                     w2   = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m2.data, m2.len);
    uint8_t                         joined[32];
    rcp_mock_frame_member_result_t  results[4];
    size_t                          dispatched;

    TEST_ASSERT_EQUAL_UINT(16u, w1.len);
    TEST_ASSERT_EQUAL_UINT(16u, w2.len);
    rcp_memcpy_bounded(joined, sizeof(joined), w1.data, w1.len);
    rcp_memcpy_bounded(joined + w1.len, sizeof(joined) - w1.len, w2.data, w2.len);
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
//cfusa:test REQ-E2E-037
static void test_avtpdu_data_length_grows_four_octets_per_protected_member(void)
{
    const uint8_t             p[4] = {0xC1, 0xC2, 0xC3, 0xC4};
    rcp_bytes_t               m    = make_abb(0, 0, 0, p, sizeof(p));
    rcp_bytes_t               w    = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m.data, m.len);
    uint8_t                   plain_payload[24];
    uint8_t                   safe_payload[32];
    rcp_avtp_ntscf_header_t   hdr;
    rcp_avtp_ntscf_header_t   got;
    rcp_bytes_t               enc_plain;
    rcp_bytes_t               enc_safe;
    const uint8_t            *pl  = NULL;
    size_t                    len = 0;

    rcp_memcpy_bounded(plain_payload, sizeof(plain_payload), m.data, m.len);
    rcp_memcpy_bounded(plain_payload + m.len, sizeof(plain_payload) - m.len, m.data, m.len);
    rcp_memcpy_bounded(safe_payload, sizeof(safe_payload), w.data, w.len);
    rcp_memcpy_bounded(safe_payload + w.len, sizeof(safe_payload) - w.len, w.data, w.len);

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
//cfusa:test REQ-E2E-037
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
//cfusa:test REQ-E2E-038
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
    w2 = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f2.data, f2.len);
    TEST_ASSERT_NOT_NULL(w2.data);

    /* What rcp_e2e_wrap()-based dispatch actually computes: the final
     * fragment's bytes alone. */
    TEST_ASSERT_EQUAL_HEX32(rcp_e2e_compute_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, w2.data, f2.len),
                            be32(w2.data + f2.len));

    rcp_memcpy_bounded(payload, sizeof(payload), p0, 4);
    rcp_memcpy_bounded(payload + 4, sizeof(payload) - 4, p1, 4);
    rcp_memcpy_bounded(payload + 8, sizeof(payload) - 8, p2, 4);
    conforming = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data, 8u,
                                                 payload, sizeof(payload));
    TEST_ASSERT_TRUE(conforming != be32(w2.data + f2.len));

    /* Segment 0 is unprotected: its corruption moves the conforming CRC
     * but leaves this implementation's verification happy. */
    payload[0] ^= 0xFFu;
    TEST_ASSERT_TRUE(rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data, 8u,
                                                     payload, sizeof(payload)) != conforming);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK,
                          rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, w2.data, w2.len, &body));

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
//cfusa:test REQ-E2E-038
static void test_compute_fragmented_crc_matches_manual_concatenation(void)
{
    const uint8_t hdr[8]     = {0x0Eu, 0x00, 0x11, 0x22, 0x00, 0x00, 0x00, 0x10};
    const uint8_t payload[8] = {0xA0, 0xA1, 0xA2, 0xA3, 0xB0, 0xB1, 0xB2, 0xB3};
    uint8_t       concat[sizeof(hdr) + sizeof(payload)];
    uint32_t      via_helper;
    uint32_t      via_manual_concat;

    rcp_memcpy_bounded(concat, sizeof(concat), hdr, sizeof(hdr));
    rcp_memcpy_bounded(concat + sizeof(hdr), sizeof(concat) - sizeof(hdr), payload, sizeof(payload));

    via_helper = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, hdr, sizeof(hdr),
                                                 payload, sizeof(payload));
    via_manual_concat = rcp_e2e_compute_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, concat, sizeof(concat));
    TEST_ASSERT_EQUAL_HEX32(via_manual_concat, via_helper);

    /* Sensitive to the header region... */
    {
        uint8_t bad_hdr[8];
        rcp_memcpy_bounded(bad_hdr, sizeof(bad_hdr), hdr, sizeof(hdr));
        bad_hdr[0] ^= 0xFFu;
        TEST_ASSERT_TRUE(rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, bad_hdr, sizeof(bad_hdr),
                                                         payload, sizeof(payload)) != via_helper);
    }
    /* ...and to the payload region, anywhere in it (not just the tail). */
    {
        uint8_t bad_payload[8];
        rcp_memcpy_bounded(bad_payload, sizeof(bad_payload), payload, sizeof(payload));
        bad_payload[0] ^= 0xFFu; /* segment 0's own octet, not segment 1's */
        TEST_ASSERT_TRUE(rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, hdr, sizeof(hdr),
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
//cfusa:test REQ-E2E-039
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
    wmid = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, mid.data, mid.len);
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
//cfusa:test REQ-E2E-041
static void test_crc_mismatch_skips_execution_without_error_response(void)
{
    const uint8_t               pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                 frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                 w     = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                 body  = {0};
    rcp_e2e_errc_t              rc;

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    rc = rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, w.data, w.len, &body);
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
//cfusa:test REQ-E2E-041
static void test_dispatch_e2e_crc_mismatch_yields_real_error_response(void)
{
    rcp_mock_server_t          *srv  = rcp_mock_server_new();
    const uint8_t                pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                  frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                  w     = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
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
//cfusa:test REQ-E2E-045
static void test_crc_error_on_one_endpoint_broadcasts_safe_state_to_stream_siblings(void)
{
    rcp_mock_server_t              *srv     = rcp_mock_server_new();
    const uint8_t                    pl[4]   = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                      frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                      w       = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
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
//cfusa:test REQ-E2E-045
static void test_crc_error_does_not_broadcast_without_an_ep_id_map(void)
{
    rcp_mock_server_t              *srv     = rcp_mock_server_new();
    const uint8_t                    pl[4]   = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                      frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                      w       = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
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
//cfusa:test REQ-E2E-028
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
//cfusa:test REQ-E2E-028
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
//cfusa:test REQ-E2E-028
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
    rcp_memcpy_bounded(joined, joined_len, one.data, one.len);
    rcp_memcpy_bounded(joined + one.len, joined_len - one.len, one.data, one.len);

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
//cfusa:test REQ-E2E-029
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

/* REQ-E2E-046's own last remaining cause (issue #341 lineage): a
 * watchdog overflow on request stream 1 broadcasts safe state to every
 * endpoint bound to that stream via EP_ID_config, the same
 * rcp_mock_server_broadcast_safe_state() escalation already proven above
 * for REQ-E2E-030 (overflow), REQ-E2E-045 (CRC error), and the sequence-
 * discontinuity case -- reached this time through rcp_mock_server_check_
 * watchdog() directly, since a watchdog is this codebase's one TIME-based
 * cause (see that function's own doc comment, mock.h) rather than a
 * per-frame content check reachable through dispatch_frame() itself. */
//cfusa:test REQ-E2E-046
static void test_watchdog_overflow_broadcasts_safe_state_to_stream_siblings(void)
{
    rcp_mock_server_t              *srv       = rcp_mock_server_new();
    rcp_bytes_t                      resp      = {0};
    rcp_bytes_t                      timed;
    rcp_regmap_ep_id_map_entry_t     ep_map[2] = {
        {1, 0x11, 1},
        {2, 0x12, 1},
    };
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];
    rcp_e2e_wd_result_t              result;

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 0x12, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, ep_map, 2));

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id           = TEST_SID;
    stream_cfg[0].rx_wd_enable           = true;
    stream_cfg[0].rx_wd_timeout_ms       = 1000u;
    stream_cfg[0].rx_wd_safestate_enable = true;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    /* Give 0x12 one non-safety-tagged stored request to confirm gets
     * purged by the broadcast, though 0x12 was never itself the
     * endpoint the watchdog is scoped to -- a watchdog is a per-
     * request-STREAM concern, not per-endpoint. */
    timed = rcp_timed_encode_request(0x12, 0x1000u, 7u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(timed.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch(srv, 0x12, RCP_AVTP_SUBTYPE_NTSCF,
                                                RCP_ACF_MSG_TYPE_GBB, true, TEST_SID, timed.data,
                                                timed.len, &resp));
    rcp_bytes_free(&resp);
    rcp_bytes_free(&timed);
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 0x12));

    TEST_ASSERT_TRUE(rcp_mock_server_check_watchdog(srv, 1u, 1000u, &result));
    TEST_ASSERT_TRUE(result.enter_safe_state);

    /* The actual proof: 0x12's own stored request was purged by the
     * broadcast, though 0x12 itself was never addressed by the
     * watchdog check. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 0x12));

    rcp_mock_server_destroy(srv);
}

/* No rcp_mock_server_set_request_stream_cfg() call resolving TEST_SID at
 * all -- resolve_index() returns 0, the gate is skipped entirely (fail-
 * toward-no-action), and dispatch proceeds regardless of any sequence_num
 * pattern -- the exact same seq value twice in a row, unrejected. */
//cfusa:test REQ-E2E-028
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
//cfusa:test REQ-E2E-028
static void test_dispatch_frame_e2e_rejects_replayed_sequence_num(void)
{
    rcp_mock_server_t             *srv   = rcp_mock_server_new();
    const uint8_t                   pl[4] = {1, 2, 3, 4};
    rcp_bytes_t                     frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                     w     = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
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
//cfusa:test REQ-WDG-010
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

        wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
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
    rcp_bytes_t                 w     = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
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
//cfusa:test REQ-E2E-021
static void test_dispatch_e2e_crc_error_with_rx_enforce_e2e_blocks_the_whole_stream(void)
{
    rcp_mock_server_t         *srv = rcp_mock_server_new();
    rcp_e2e_stream_fault_tracker_t tracker;
    const uint8_t                pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                  frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                  w     = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                  resp1 = {0};
    rcp_bytes_t                  resp2 = {0};
    const uint8_t                good_pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t                  good_frame = make_abb(0, 0, 1, good_pl, sizeof(good_pl));
    rcp_bytes_t                  good_wrapped =
        rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, good_frame.data, good_frame.len);
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
//cfusa:test REQ-E2E-021
//cfusa:test REQ-E2E-022
static void test_dispatch_e2e_crc_error_without_rx_enforce_e2e_does_not_block_the_stream(void)
{
    rcp_mock_server_t         *srv = rcp_mock_server_new();
    rcp_e2e_stream_fault_tracker_t tracker;
    const uint8_t                pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                  frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                  w     = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                  resp1 = {0};
    rcp_bytes_t                  resp2 = {0};
    const uint8_t                good_pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t                  good_frame = make_abb(0, 0, 1, good_pl, sizeof(good_pl));
    rcp_bytes_t                  good_wrapped =
        rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, good_frame.data, good_frame.len);

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
//cfusa:test REQ-E2E-021
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
//cfusa:test REQ-E2E-046
static void test_dispatch_e2e_crc_error_latches_stream_status(void)
{
    rcp_mock_server_t              *srv   = rcp_mock_server_new();
    const uint8_t                    pl[4] = {0x9A, 0x9B, 0x9C, 0x9D};
    rcp_bytes_t                      frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                      w     = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
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
//cfusa:test REQ-E2E-046
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
//cfusa:test REQ-E2E-046
static void test_stream_status_rx_blocked_false_for_unresolvable_stream(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));

    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-038/039 (issue #336): real fragmented-message dispatch ─────────
 *
 * rcp_mock_server_dispatch_e2e_fragment() (mock.c) is the fragmentation-
 * aware counterpart to rcp_mock_server_dispatch_e2e() -- these tests
 * exercise the real dispatch wiring end to end, not fragment.h's/e2e.h's
 * own already-tested primitives in isolation (those are covered above,
 * §REQ-E2E-038/039). */

static uint8_t g_captured_payload[64];
static size_t  g_captured_payload_len;

static void capturing_handler(const uint8_t *request, size_t request_len, rcp_bytes_t *out_response,
                               void *user_data)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                        payload_len;

    (void)out_response;
    (void)user_data;
    g_handler_called = true;
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(request, request_len, &hdr, &payload, &payload_len));
    TEST_ASSERT_TRUE(payload_len <= sizeof(g_captured_payload));
    rcp_memcpy_bounded(g_captured_payload, sizeof(g_captured_payload), payload, payload_len);
    g_captured_payload_len = payload_len;
}

static void set_up_frag_stream(rcp_mock_server_t *srv, rcp_mock_endpoint_handler_fn handler)
{
    rcp_regmap_request_stream_cfg_t stream_cfg[1];

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id = TEST_SID;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));
}

/* A single, never-actually-fragmented message dispatched through the new
 * entry point behaves byte-identically to calling
 * rcp_mock_server_dispatch_e2e() directly -- the doc comment's own
 * documented fallback for an ms=0 fragment arriving while the
 * reassembler is not collecting. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_single_fragment_matches_dispatch_e2e(void)
{
    rcp_mock_server_t *srv     = rcp_mock_server_new();
    const uint8_t       pl[4]  = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped;
    rcp_bytes_t          resp = {0};

    set_up_frag_stream(srv, counting_handler);
    wrapped = rcp_e2e_wrap_framed(TEST_SID, false /* TSCF-framed */, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, wrapped.data, wrapped.len,
                                                             &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* A genuine 3-fragment message, CRC-protected via the real
 * rcp_e2e_compute_fragmented_crc() formula (segment 0's header +
 * concatenated segment 0/1/2 payload), reassembles and dispatches
 * exactly once -- the concatenated payload, not just the final
 * fragment's own slice, reaches the handler. TSCF framing (real,
 * nonzero avtp_timestamp) deliberately, so this test cannot pass by
 * accident of the NTSCF-forces-zero rule the dedicated test below pins
 * separately. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_three_fragment_round_trip_succeeds(void)
{
    rcp_mock_server_t *srv        = rcp_mock_server_new();
    const uint8_t       p0[4]     = {0xF0, 0xF1, 0xF2, 0xF3};
    const uint8_t       p1[4]     = {0xE0, 0xE1, 0xE2, 0xE3};
    const uint8_t       p2[4]     = {0xD0, 0xD1, 0xD2, 0xD3};
    rcp_bytes_t          f0       = make_abb(1, 0, 0, p0, sizeof(p0)); /* ms=1, segment 0 */
    rcp_bytes_t          f1       = make_abb(1, 0, 1, p1, sizeof(p1)); /* ms=1, segment 1 */
    rcp_bytes_t          f2       = make_abb(0, 0, 2, p2, sizeof(p2)); /* ms=0, final     */
    rcp_bytes_t          final_wire;
    uint8_t               concatenated[12];
    uint32_t              want;
    rcp_bytes_t           resp = {0};

    set_up_frag_stream(srv, capturing_handler);

    /* rcp_e2e_wrap() is used here purely to obtain a correctly
     * length-adapted final fragment with a trailer SLOT -- its own
     * trailer VALUE is the wrong (single-frame) formula and is
     * overwritten below with the real fragmented CRC. */
    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f2.data, f2.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    rcp_memcpy_bounded(concatenated + 8, sizeof(concatenated) - 8, p2, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data, 8u, concatenated,
                                           sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    rcp_bytes_free(&resp);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f1.data, f1.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    rcp_bytes_free(&resp);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data,
                                                             final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL_UINT(sizeof(concatenated), g_captured_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(concatenated, g_captured_payload, sizeof(concatenated));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f2);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* issue #445 (c-RCP-AUDIT-24) regression test: same 2-fragment shape as
 * the round-trip test above, but the final fragment's own REAL (unpadded)
 * payload is 3 octets, not a multiple of 4 -- rcp_acf_encode_abb() pads
 * it with 1 trailing 0x00 octet to reach quadlet alignment, so the wire
 * frame's own pad field (peek_hdr.pad, byte_message_info octet 2 bits
 * 7:6) reads 1. Before the fix, rcp_mock_server_dispatch_e2e_fragment()
 * unconditionally read the CRC32 trailer from the frame's literal last
 * RCP_E2E_CRC_LEN octets -- correct only when pad_octets == 0. Here that
 * reads 1 pad octet (0x00) plus 3 real trailer octets instead of the
 * real 4-octet trailer sitting one octet earlier (TC18 Figures 20/21:
 * [header][real payload][CRC32][pad], never [...][CRC32 tail clipped by
 * pad]), so the pre-fix code computed the wrong "got" and spuriously
 * rejected this legitimately-CRC'd request with RCP_MOCK_DISPATCH_CRC_
 * ERROR instead of RCP_MOCK_DISPATCH_OK. The CRC trailer itself is
 * placed at the correct pad-aware offset (real_len = f1.len -
 * pad_octets), mirroring exactly where rcp_e2e_wrap() itself would put
 * it -- this test does not depend on the bug under test to construct its
 * own wire frame. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_final_fragment_non_aligned_payload_ok(void)
{
    rcp_mock_server_t *srv        = rcp_mock_server_new();
    const uint8_t       p0[4]     = {0xA0, 0xA1, 0xA2, 0xA3};
    const uint8_t       p1[3]     = {0xB0, 0xB1, 0xB2}; /* NOT a multiple of 4 */
    rcp_bytes_t          f0       = make_abb(1, 0, 0, p0, sizeof(p0)); /* ms=1, segment 0 */
    rcp_bytes_t          f1       = make_abb(0, 0, 1, p1, sizeof(p1)); /* ms=0, final, pad=1 */
    rcp_bytes_t          final_wire;
    uint8_t               concatenated[7];
    uint32_t              want;
    size_t                pad_octets;
    size_t                crc_offset;
    rcp_bytes_t           resp = {0};

    set_up_frag_stream(srv, capturing_handler);

    /* f1.len already includes acf.c's own quadlet-alignment pad octet
     * (rcp_acf_encode_abb() always pads); rcp_e2e_wrap() requires exactly
     * that quadlet-aligned input and itself re-seats the pad after the
     * trailer it appends, so final_wire's own trailer SLOT already sits
     * at the correct pad-aware offset -- only its VALUE (wrap()'s own
     * wrong, single-frame formula) needs overwriting below with the real
     * fragmented CRC. */
    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 3);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data, 8u, concatenated,
                                           sizeof(concatenated));

    pad_octets = rcp_acf_pad_len(sizeof(p1));
    TEST_ASSERT_EQUAL_UINT(1u, pad_octets);
    crc_offset = f1.len - pad_octets;
    final_wire.data[crc_offset + 0u] = (uint8_t)(want >> 24);
    final_wire.data[crc_offset + 1u] = (uint8_t)(want >> 16);
    final_wire.data[crc_offset + 2u] = (uint8_t)(want >> 8);
    final_wire.data[crc_offset + 3u] = (uint8_t)want;

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    rcp_bytes_free(&resp);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data,
                                                             final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL_UINT(sizeof(concatenated), g_captured_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(concatenated, g_captured_payload, sizeof(concatenated));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* Same 3-fragment message as above, but the final fragment's trailer is
 * left at rcp_e2e_wrap()'s own (wrong-formula, but still a real 4-octet
 * value) trailer instead of the real fragmented CRC -- a genuine
 * mismatch, not merely an absent one. Rejected, not executed, and
 * latches the same three consequences rcp_mock_server_dispatch_e2e()'s
 * own CRC-mismatch branch already does. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
//cfusa:test REQ-E2E-041
static void test_dispatch_e2e_fragment_crc_mismatch_is_rejected_and_latches(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x10, 0x11, 0x12, 0x13};
    const uint8_t       p1[4] = {0x20, 0x21, 0x22, 0x23};
    rcp_bytes_t          f0   = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f1   = make_abb(0, 0, 1, p1, sizeof(p1)); /* ms=0, final */
    rcp_bytes_t          final_wire;
    rcp_bytes_t           resp = {0};

    set_up_frag_stream(srv, counting_handler);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));

    /* wrap()'s own trailer is the SINGLE-fragment formula over f1 alone
     * -- already wrong for this 2-fragment message, with no overwrite
     * needed to force a mismatch. */
    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    TEST_ASSERT_FALSE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data,
                                                             final_wire.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_TRUE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* An NTSCF-framed fragmented message's CRC contribution from
 * avtp_timestamp is forced to 0 -- the same rule
 * rcp_e2e_wrap_framed()/_unwrap_framed() already apply, now also
 * honored by rcp_e2e_compute_fragmented_crc()'s own caller in mock.c.
 * Genuinely 2 fragments (not 1): a single-fragment ms=0 message would
 * take the "never actually fragmented" fallback straight to
 * rcp_mock_server_dispatch_e2e() (already covered by the single-fragment
 * test above) and never reach this function's OWN fragmented-CRC
 * computation at all -- exercising the collecting path here is what
 * actually proves this fix, not merely a differently-labeled repeat of
 * that other test. "want" is computed with avtp_timestamp forced to 0,
 * as an NTSCF-framed message's real contribution must be; if this
 * function's own effective_ts forcing were missing, it would instead
 * verify against rcp_e2e_compute_fragmented_crc(..., TEST_TS, ...) and
 * this call would come back RCP_MOCK_DISPATCH_CRC_ERROR. */
//cfusa:test REQ-E2E-035
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_ntscf_forces_zero_timestamp_in_crc(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x30, 0x31, 0x32, 0x33};
    const uint8_t       p1[4] = {0x34, 0x35, 0x36, 0x37};
    rcp_bytes_t          f0   = make_abb(1, 0, 0, p0, sizeof(p0)); /* ms=1, segment 0 */
    rcp_bytes_t          f1   = make_abb(0, 0, 1, p1, sizeof(p1)); /* ms=0, final     */
    rcp_bytes_t           final_wire;
    uint8_t                concatenated[8];
    uint32_t               want;
    rcp_bytes_t            resp = {0};

    set_up_frag_stream(srv, counting_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len); /* trailer slot only, see above */
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    /* issue #465: mock.c's own dispatch_e2e_fragment() passes the REAL
     * avtp_subtype it was given (RCP_AVTP_SUBTYPE_NTSCF here, matching
     * the dispatch calls below), but has no per-message header_octet1/tu
     * bits available -- it feeds its own documented placeholder
     * (0x00/false, mirrored literally here; see src/mock.c's
     * RCP_MOCK_E2E_HEADER_OCTET1_PLACEHOLDER/_TU_PLACEHOLDER) for both,
     * uniformly regardless of framing. */
    want = rcp_e2e_compute_fragmented_crc(RCP_AVTP_SUBTYPE_NTSCF, 0x00u, false, TEST_SID,
                                           0u /* forced, NTSCF has no timestamp field */,
                                           f0.data, 8u, concatenated, sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_NTSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data,
                                                             final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* An out-of-order intermediate fragment (segment_num 5 following a fresh
 * sequence's own segment 0) is rejected and abandons the in-progress
 * reassembly -- a fresh, correctly-ordered sequence afterward succeeds,
 * proving the reassembler was actually reset rather than left wedged. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_out_of_order_segment_is_rejected_and_resets(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x40, 0x41, 0x42, 0x43};
    rcp_bytes_t          f0   = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f_bad = make_abb(1, 0, 5, p0, sizeof(p0)); /* segment 5, not 1 */
    rcp_bytes_t          plain = make_abb(0, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          wrapped;
    rcp_bytes_t           resp = {0};

    set_up_frag_stream(srv, counting_handler);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f_bad.data, f_bad.len, &resp));
    rcp_bytes_free(&resp);

    /* A fresh, correctly-ordered single-fragment message now succeeds --
     * proving the reassembler was reset, not left collecting the
     * abandoned sequence. */
    wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);
    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, wrapped.data, wrapped.len,
                                                             &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_bytes_free(&f_bad);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* A reassembly that would exceed the stream's own configured
 * max_total_len is rejected outright -- rcp_mock_server_fragment_
 * reassembler()'s own documented escape hatch for a caller wanting a
 * tighter bound than RCP_MOCK_FRAG_REASM_DEFAULT_MAX_TOTAL_LEN,
 * exercised here to keep the test cheap (no 64KiB payload needed). */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_too_large_reassembly_is_rejected(void)
{
    rcp_mock_server_t          *srv    = rcp_mock_server_new();
    const uint8_t                p0[4] = {0x50, 0x51, 0x52, 0x53};
    rcp_bytes_t                  f0    = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_fragment_reassembler_t *reasm;
    rcp_bytes_t                  resp = {0};

    set_up_frag_stream(srv, counting_handler);

    reasm = rcp_mock_server_fragment_reassembler(srv, TEST_SID);
    TEST_ASSERT_NOT_NULL(reasm);
    rcp_fragment_reassembler_init(reasm, 2u); /* smaller than p0's own 4 octets */

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(reasm));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* ── CAN endpoint: a client-constructed, genuinely multi-fragment CAN XL
 * write request fragmented via rcp_ep_can_encode_frame_request_fragmented()
 * (issue #611) round-trips through this SAME generic reassembly mechanism ──
 *
 * Issue #611's own correction comment: the receive/reassembly side above
 * (rcp_mock_server_dispatch_e2e_fragment()/srv's own frag_reasm[]) is
 * already generic and endpoint-type-agnostic -- nothing CAN-specific was
 * missing there. What WAS missing was the encode-side convenience
 * function a client uses to slice a large CAN XL write request into
 * correctly ms/segment_num-tagged ACF_ABB fragments in the first place --
 * rcp_ep_can_frame_request_fragment_count()/rcp_ep_can_encode_frame_
 * request_fragmented() (ep_can.h, REQ-CANEP-041/042). This test proves
 * those two new functions genuinely compose with the pre-existing generic
 * reassembly path above.
 *
 * IMPORTANT SIZE NOTE (found while building this test -- see this PR's own
 * description for the full write-up): the LITERAL worst case (2058-octet
 * combined payload, RCP_EP_CAN_XL_MAX_ENCODED_LEN, from a full
 * RCP_EP_CAN_XL_MAX_DATA_LEN 2048-octet CAN XL data field) cannot reach a
 * registered per-endpoint handler through THIS SPECIFIC entry point
 * (rcp_mock_server_dispatch_e2e_fragment()), independent of anything this
 * PR adds: once reassembly completes, mock.c's own dispatch_e2e_fragment()
 * re-encodes the FULL reassembled payload into a single ACF_ABB frame
 * (rcp_acf_encode_abb()) before handing it to dispatch_plain()'s
 * one-ACF-message-in handler contract -- the same contract counting_
 * handler()/capturing_handler() above already rely on decoding via
 * rcp_acf_decode_abb(). RCP_ACF_ABB_MAX_PAYLOAD (2036 octets) is therefore
 * a hard ceiling on any REASSEMBLED request this specific dispatch entry
 * point can actually deliver -- 22 octets short of the CAN XL worst case's
 * own 2058. This is a pre-existing characteristic of mock.c's own
 * dispatch-plumbing (not a defect in rcp_ep_can_encode_frame_request_
 * fragmented(), which correctly PRODUCES the worst-case fragments -- see
 * test_ep_can.c's own test_fragment_worst_case_can_xl_request_round_trip(),
 * which proves the full 2048/2058-octet case at the pure encode/reassemble
 * level, independent of mock.c), and is out of THIS issue's own scope (the
 * issue is scoped to the missing encode-side function, not to mock.c's
 * dispatch_plain() handler contract). This test therefore uses the
 * largest combined payload that still fits under that ceiling (2036
 * octets, i.e. RCP_ACF_ABB_MAX_PAYLOAD itself) split into 2 real fragments
 * -- genuinely exercising multi-fragment composition through the existing
 * mechanism, which is this test's actual purpose; the RC5 reject-when-
 * oversized test below deliberately DOES use the literal 2058-octet worst
 * case, since rejection happens at the reassembler stage, before mock.c's
 * re-encode step, so it is unaffected by this ceiling.
 *
 * can_capturing_handler() mirrors capturing_handler() above exactly, sized
 * generously for a CAN XL-scale payload instead of capturing_handler()'s
 * own 64-octet ceiling. */
static uint8_t g_can_captured_payload[RCP_EP_CAN_XL_MAX_ENCODED_LEN];
static size_t  g_can_captured_payload_len;

static void can_capturing_handler(const uint8_t *request, size_t request_len,
                                   rcp_bytes_t *out_response, void *user_data)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                        payload_len;

    (void)out_response;
    (void)user_data;
    g_handler_called = true;
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(request, request_len, &hdr, &payload, &payload_len));
    TEST_ASSERT_TRUE(payload_len <= sizeof(g_can_captured_payload));
    rcp_memcpy_bounded(g_can_captured_payload, sizeof(g_can_captured_payload), payload, payload_len);
    g_can_captured_payload_len = payload_len;
}

//cfusa:test REQ-CANEP-041
//cfusa:test REQ-CANEP-042
/* The largest combined CAN XL write-request payload
 * (4-octet leading quadlet + 6-octet SDT/VCID/AF prefix + data) that still
 * fits within RCP_ACF_ABB_MAX_PAYLOAD (2036) once reassembled -- see the
 * comment above for why this dispatch entry point (not ep_can.c's new
 * encode function) is what draws that line. */
#define CAN_REQUEST_DISPATCHABLE_DATA_LEN ((size_t)(RCP_ACF_ABB_MAX_PAYLOAD - 4u - 6u))

static void test_dispatch_e2e_fragment_can_xl_write_request_round_trips(void)
{
    rcp_mock_server_t     *srv = rcp_mock_server_new();
    rcp_ep_can_xl_header_t  xl  = {0};
    uint8_t                 tx_data[CAN_REQUEST_DISPATCHABLE_DATA_LEN];
    rcp_bytes_t              frames[2] = {{0}, {0}};
    /* Smaller than the fragment size itself, forcing 2 genuine fragments
     * (ceil(2036/1024) = 2), not a toy split. */
    const size_t              max_fragment_payload = 1024u;
    size_t                   count;
    size_t                   i;
    uint8_t                  combined[RCP_EP_CAN_XL_MAX_ENCODED_LEN];
    size_t                   combined_len = 0;
    uint8_t                  first_hdr[RCP_ACF_ABB_HEADER_LEN];
    size_t                    final_payload_len = 0;
    rcp_bytes_t               final_wire;
    uint32_t                  want;
    size_t                    pad_octets;
    size_t                    crc_offset;
    rcp_bytes_t                resp = {0};

    xl.sdt = 0x5u;
    xl.vcid = 0x9u;
    xl.af   = 0xCAFEBABEu;
    for (i = 0; i < sizeof(tx_data); i++) tx_data[i] = (uint8_t)(i * 7u + 3u);

    set_up_frag_stream(srv, can_capturing_handler);

    count = rcp_ep_can_frame_request_fragment_count(RCP_EP_CAN_FRAME_XL_NEW_PL, 0x321u, &xl,
                                                      sizeof(tx_data), max_fragment_payload);
    TEST_ASSERT_EQUAL_size_t(2u, count);

    TEST_ASSERT_EQUAL_size_t(
        count, rcp_ep_can_encode_frame_request_fragmented(0x11u, RCP_EP_CAN_FRAME_XL_NEW_PL, 0x321u,
                                                           &xl, tx_data, sizeof(tx_data), 0x77u,
                                                           max_fragment_payload, frames));
    TEST_ASSERT_NOT_NULL(frames[0].data);
    TEST_ASSERT_NOT_NULL(frames[1].data);

    /* Decode each fragment via acf.c itself (not by trusting ep_can.c's
     * own internal layout) to recover its own header fields and raw
     * payload slice, reassembling the combined payload independently for
     * the fragmented-CRC computation and the later byte-for-byte check. */
    {
        rcp_acf_byte_message_info_t h0;
        rcp_acf_byte_message_info_t h1;
        const uint8_t               *p0;
        const uint8_t               *p1;
        size_t                        p0len;
        size_t                        p1len;

        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                          rcp_acf_decode_abb(frames[0].data, frames[0].len, &h0, &p0, &p0len));
        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                          rcp_acf_decode_abb(frames[1].data, frames[1].len, &h1, &p1, &p1len));

        TEST_ASSERT_EQUAL_UINT8(0x11u, h0.byte_bus_id);
        TEST_ASSERT_EQUAL(RCP_ACF_OP_WRITE, (rcp_acf_op_t)h0.op);
        TEST_ASSERT_EQUAL_UINT8(0x77u, h0.transaction_num);
        TEST_ASSERT_EQUAL_UINT8(0x77u, h1.transaction_num);
        TEST_ASSERT_TRUE(h0.ms != 0u);
        TEST_ASSERT_EQUAL_UINT16(0u, h0.read_size_or_segment_num);
        TEST_ASSERT_TRUE(h1.ms == 0u);

        rcp_memcpy_bounded(combined, sizeof(combined), p0, p0len);
        rcp_memcpy_bounded(combined + p0len, sizeof(combined) - p0len, p1, p1len);
        combined_len = p0len + p1len;
        final_payload_len = p1len;

        rcp_memcpy_bounded(first_hdr, sizeof(first_hdr), frames[0].data, RCP_ACF_ABB_HEADER_LEN);
    }
    TEST_ASSERT_EQUAL_size_t(4u + 6u + sizeof(tx_data), combined_len);

    /* Same technique test_dispatch_e2e_fragment_three_fragment_round_trip_
     * succeeds() above already uses: rcp_e2e_wrap() supplies a correctly
     * length-adapted/pad-aware trailer SLOT for the final fragment; its
     * own single-frame CRC VALUE is wrong for a fragmented message and is
     * overwritten with the real rcp_e2e_compute_fragmented_crc() answer.
     * issue #445's own pad-aware CRC placement applies here exactly as it
     * does for test_dispatch_e2e_fragment_final_fragment_non_aligned_
     * payload_ok() above: this fragment's own real (unpadded) payload is
     * 22 octets, not a multiple of 4, so the trailer sits BEFORE the
     * pad octets acf.c's own encoder appended, not at final_wire's
     * literal last 4 bytes. */
    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frames[1].data,
                               frames[1].len);
    TEST_ASSERT_NOT_NULL(final_wire.data);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS,
                                           first_hdr, RCP_ACF_ABB_HEADER_LEN, combined, combined_len);
    pad_octets = rcp_acf_pad_len(final_payload_len);
    crc_offset = frames[1].len - pad_octets;
    final_wire.data[crc_offset + 0u] = (uint8_t)(want >> 24);
    final_wire.data[crc_offset + 1u] = (uint8_t)(want >> 16);
    final_wire.data[crc_offset + 2u] = (uint8_t)(want >> 8);
    final_wire.data[crc_offset + 3u] = (uint8_t)want;

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, frames[0].data, frames[0].len,
                                                             &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    rcp_bytes_free(&resp);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data, final_wire.len,
                                                             &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    /* The handler received the exact reassembled combined payload the
     * encoder produced -- real reassembly, not two frames of arbitrary
     * content. */
    TEST_ASSERT_EQUAL_UINT(combined_len, g_can_captured_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(combined, g_can_captured_payload, combined_len);

    /* And that reassembled payload decodes back to the exact original
     * frame_format/arbitration_id/xl_header/tx_data, byte-for-byte --
     * reusing rcp_ep_can_decode_reassembled_frame_response() verbatim for
     * the request direction, exactly as ep_can.h's own new doc comment
     * says a caller should (it inspects only the reassembled bytes
     * themselves, never op/rsp). */
    {
        rcp_ep_can_frame_format_t out_format;
        uint32_t                   out_id  = 0u;
        rcp_ep_can_xl_header_t     out_xl;
        const uint8_t              *out_data = NULL;
        size_t                       out_len  = 0u;

        TEST_ASSERT_EQUAL(RCP_EP_CAN_OK,
                          rcp_ep_can_decode_reassembled_frame_response(
                              g_can_captured_payload, g_can_captured_payload_len, &out_format,
                              &out_id, &out_xl, &out_data, &out_len));
        TEST_ASSERT_EQUAL(RCP_EP_CAN_FRAME_XL_NEW_PL, out_format);
        TEST_ASSERT_EQUAL_UINT32(0x321u, out_id);
        TEST_ASSERT_EQUAL_UINT8(xl.sdt, out_xl.sdt);
        TEST_ASSERT_EQUAL_UINT8(xl.vcid, out_xl.vcid);
        TEST_ASSERT_EQUAL_UINT32(xl.af, out_xl.af);
        TEST_ASSERT_EQUAL_size_t(sizeof(tx_data), out_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(tx_data, out_data, sizeof(tx_data));
    }

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&frames[0]);
    rcp_bytes_free(&frames[1]);
    rcp_mock_server_destroy(srv);
}

/* ── RC5 Table 24 rx_stream_max_request_size: "longer requests will be
 * rejected" -- the reject-when-oversized half issue #611 explicitly names
 * (TC18.txt L3231-3232) ────────────────────────────────────────────────
 *
 * Same worst-case CAN XL write request and the same generic reassembly
 * mechanism as the round-trip test above, but with the stream's own
 * reassembler ceiling (rcp_mock_server_fragment_reassembler()'s escape
 * hatch, matching test_dispatch_e2e_fragment_too_large_reassembly_is_
 * rejected() above) deliberately set to admit the first fragment alone
 * (frag0_len octets) but be exceeded once the second fragment's own
 * octets are added -- proving this is a real REASSEMBLED-TOTAL check
 * (rx_stream_max_request_size bounds the whole request, not any one
 * fragment), not merely "one fragment already too big to append". */
//cfusa:test REQ-CANEP-041
//cfusa:test REQ-CANEP-042
static void test_dispatch_e2e_fragment_can_xl_write_request_rejected_when_reassembled_total_exceeds_ceiling(void)
{
    rcp_mock_server_t          *srv = rcp_mock_server_new();
    rcp_ep_can_xl_header_t       xl  = {0};
    uint8_t                       tx_data[RCP_EP_CAN_XL_MAX_DATA_LEN];
    rcp_bytes_t                   frames[2] = {{0}, {0}};
    size_t                         count;
    size_t                         i;
    size_t                         frag0_len = 0u;
    rcp_fragment_reassembler_t   *reasm;
    rcp_bytes_t                    final_wire;
    rcp_bytes_t                     resp = {0};

    xl.sdt = 0x5u;
    xl.vcid = 0x9u;
    xl.af   = 0xCAFEBABEu;
    for (i = 0; i < sizeof(tx_data); i++) tx_data[i] = (uint8_t)(i * 7u + 3u);

    set_up_frag_stream(srv, counting_handler);

    count = rcp_ep_can_frame_request_fragment_count(RCP_EP_CAN_FRAME_XL_NEW_PL, 0x321u, &xl,
                                                      sizeof(tx_data), RCP_ACF_ABB_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL_size_t(2u, count);
    TEST_ASSERT_EQUAL_size_t(
        count, rcp_ep_can_encode_frame_request_fragmented(0x11u, RCP_EP_CAN_FRAME_XL_NEW_PL, 0x321u,
                                                           &xl, tx_data, sizeof(tx_data), 0x88u,
                                                           RCP_ACF_ABB_MAX_PAYLOAD, frames));
    TEST_ASSERT_NOT_NULL(frames[0].data);
    TEST_ASSERT_NOT_NULL(frames[1].data);

    {
        rcp_acf_byte_message_info_t h0;
        const uint8_t               *p0;
        size_t                        p0len;

        TEST_ASSERT_EQUAL(RCP_ACF_OK,
                          rcp_acf_decode_abb(frames[0].data, frames[0].len, &h0, &p0, &p0len));
        frag0_len = p0len;
    }

    reasm = rcp_mock_server_fragment_reassembler(srv, TEST_SID);
    TEST_ASSERT_NOT_NULL(reasm);
    /* Admits fragment 0 (frag0_len octets) alone; the combined total
     * (RCP_EP_CAN_XL_MAX_ENCODED_LEN, 2058) exceeds it once fragment 1's
     * remaining octets are added. */
    rcp_fragment_reassembler_init(reasm, frag0_len + 4u);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, frames[0].data, frames[0].len,
                                                             &resp));
    TEST_ASSERT_TRUE(rcp_fragment_reassembler_is_collecting(reasm));
    rcp_bytes_free(&resp);

    /* The final fragment's own trailer VALUE does not matter here:
     * rcp_mock_server_dispatch_e2e_fragment()'s own TOO_LARGE check runs
     * (and rejects) strictly before its CRC comparison -- see mock.c's
     * own dispatch_e2e_fragment() ordering -- so rcp_e2e_wrap()'s default
     * (wrong-formula, single-frame) trailer is sufficient to reach that
     * check structurally intact. */
    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frames[1].data,
                               frames[1].len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data, final_wire.len,
                                                             &resp));
    TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(reasm));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&frames[0]);
    rcp_bytes_free(&frames[1]);
    rcp_mock_server_destroy(srv);
}

/* An unresolvable stream_id (no rcp_mock_server_set_request_stream_cfg()
 * call for it) has no reassembler slot to use -- the documented
 * fallback delegates to rcp_mock_server_dispatch_e2e() unchanged rather
 * than rejecting outright, exactly as if the fragment-aware entry point
 * had never been called at all. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_unresolvable_stream_falls_back_to_dispatch_e2e(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       pl[4] = {0x60, 0x61, 0x62, 0x63};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped;
    rcp_bytes_t           resp = {0};

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    /* Deliberately no rcp_mock_server_set_request_stream_cfg() call. */

    wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, wrapped.data, wrapped.len,
                                                             &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* ── mock.c category-2 fault-injection gaps (issue #520 / c-RCP-19):
 * rcp_mock_server_dispatch_e2e_fragment()'s OWN copy of the
 * stream-fault-tracker / CRC-mismatch-with-safe-state-broadcast /
 * malformed-input logic that rcp_mock_server_dispatch_e2e() already has
 * dedicated tests for above -- duplicated per-function by design (each
 * function's own doc comment says so), so each copy needs its own
 * fault-injection coverage rather than inheriting the sibling's. ────────── */

/* Mirrors test_dispatch_e2e_crc_error_with_rx_enforce_e2e_blocks_the_
 * whole_stream()'s own "second request" half: once stream_fault_tracker
 * already has TEST_SID latched (by any means -- a prior CRC error is the
 * only real-world path, but the tracker is pre-faulted directly here to
 * isolate this function's OWN is_faulted branch from dispatch_e2e_
 * fragment()'s multi-step reassembly logic), a fragment arriving on that
 * stream is rejected outright as RCP_MOCK_DISPATCH_STREAM_FAULTED with a
 * genuine Error Response -- never reaching find_slot_on_stream(), let
 * alone reassembly. */
//cfusa:test REQ-E2E-021
static void test_dispatch_e2e_fragment_already_faulted_stream_is_rejected(void)
{
    rcp_mock_server_t             *srv = rcp_mock_server_new();
    rcp_e2e_stream_fault_tracker_t tracker;
    const uint8_t                   pl[4] = {0x70, 0x71, 0x72, 0x73};
    rcp_bytes_t                     plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                     wrapped;
    rcp_bytes_t                     resp = {0};
    rcp_acf_byte_message_info_t     hdr;
    const uint8_t                  *out_pl  = NULL;
    size_t                          out_len = 0;

    rcp_e2e_stream_fault_tracker_init(&tracker);
    set_up_frag_stream(srv, counting_handler);
    rcp_mock_server_set_stream_fault_tracker(srv, &tracker);
    (void)rcp_e2e_stream_fault_tracker_on_crc_error(&tracker, TEST_SID, true);
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&tracker, TEST_SID));

    wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_STREAM_FAULTED,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, wrapped.data, wrapped.len,
                                                             &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(resp.data, resp.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* A fragment addressed to a slot that never had req_crc_enable set falls
 * back to dispatch_plain() unchanged -- the documented "plain command
 * mode" delegation this function's own doc comment shares with
 * rcp_mock_server_dispatch_e2e()'s identical branch, but this function's
 * OWN copy of the check (find_slot_on_stream()+!req_crc_enable, ahead of
 * any fragment-reassembly bookkeeping) had never been exercised. */
//cfusa:test REQ-E2E-031
static void test_dispatch_e2e_fragment_plain_command_mode_falls_back_to_dispatch_plain(void)
{
    rcp_mock_server_t               *srv = rcp_mock_server_new();
    rcp_regmap_request_stream_cfg_t   stream_cfg[1];
    const uint8_t                     pl[4] = {0x74, 0x75, 0x76, 0x77};
    rcp_bytes_t                       plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                       resp  = {0};

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    /* Deliberately no rcp_mock_server_set_endpoint_req_crc_enable() call:
     * plain command mode. */
    rcp_regmap_request_stream_cfg_init(&stream_cfg[0]);
    stream_cfg[0].rx_stream_id = TEST_SID;
    TEST_ASSERT_TRUE(rcp_mock_server_set_request_stream_cfg(srv, stream_cfg, 1));

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, plain.data, plain.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* A fragment shorter than ACF's own fixed 8-octet header (or otherwise
 * failing rcp_acf_unpack_header()) is rejected outright, before any
 * reassembler state is touched. */
static void test_dispatch_e2e_fragment_too_short_header_is_rejected(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    const uint8_t       tiny[4] = {0x00, 0x00, 0x00, 0x00}; /* < 8 octets */
    rcp_bytes_t          resp = {0};

    set_up_frag_stream(srv, counting_handler);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, tiny, sizeof(tiny), &resp));
    TEST_ASSERT_FALSE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_mock_server_destroy(srv);
}

/* A genuine 2-fragment ACF_GBB message (not ABB -- the OTHER acf_msg_type
 * branch of this function's own final-completion logic, previously
 * entirely unexercised by any test in this file) reassembles and
 * dispatches exactly once through the real rcp_acf_decode_gbb()/
 * rcp_acf_encode_gbb() pair, with the real fragmented CRC over the
 * concatenated payload. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_gbb_two_fragment_round_trip_succeeds(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x80, 0x81, 0x82, 0x83};
    const uint8_t       p1[4] = {0x90, 0x91, 0x92, 0x93};
    rcp_bytes_t          f0   = make_gbb(1, 0, 0, p0, sizeof(p0)); /* ms=1, segment 0 */
    rcp_bytes_t          f1   = make_gbb(0, 0, 1, p1, sizeof(p1)); /* ms=0, final     */
    rcp_bytes_t           final_wire;
    uint8_t                concatenated[8];
    uint32_t               want;
    rcp_bytes_t             resp = {0};

    set_up_frag_stream(srv, counting_handler);

    /* rcp_e2e_wrap()'s own trailer VALUE is the wrong (single-frame,
     * ABB-shaped) formula -- only its trailer SLOT (correctly sized for
     * f1's own GBB header length) is used here, same as the ABB
     * round-trip tests above. */
    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_GBB_HEADER_LEN, concatenated, sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, final_wire.data,
                                                             final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* Same 2-fragment ACF_GBB shape as the round-trip test above, but the
 * final fragment's trailer is left at rcp_e2e_wrap()'s own wrong
 * (single-frame) formula -- a genuine fragmented-CRC mismatch on the GBB
 * branch specifically, previously unexercised. Rejected, not executed,
 * and (rx_enforce_e2e set) broadcasts safe state to a stream sibling --
 * the same three consequences the ABB CRC-mismatch test above already
 * pins for the ABB branch, now proven for GBB's own separate code path
 * too. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
//cfusa:test REQ-E2E-041
//cfusa:test REQ-E2E-045
static void test_dispatch_e2e_fragment_gbb_crc_mismatch_broadcasts_safe_state(void)
{
    rcp_mock_server_t              *srv     = rcp_mock_server_new();
    const uint8_t                    p0[4]   = {0xA0, 0xA1, 0xA2, 0xA3};
    const uint8_t                    p1[4]   = {0xB0, 0xB1, 0xB2, 0xB3};
    rcp_bytes_t                      f0      = make_gbb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t                      f1      = make_gbb(0, 0, 1, p1, sizeof(p1));
    rcp_bytes_t                      final_wire;
    rcp_bytes_t                      resp    = {0};
    rcp_bytes_t                      timed;
    rcp_regmap_ep_id_map_entry_t     ep_map[2] = {
        {1, 0x11, 1},
        {2, 0x12, 1},
    };
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 0x12, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, ep_map, 2));

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
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 0x12));

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, final_wire.data,
                                                             final_wire.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);
    /* The actual proof: 0x12's own stored request was purged by the
     * safe-state broadcast, though it was never addressed by the failing
     * request. */
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 0x12));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* ── issue #462: TSCF presentation-time gate wired into every E2E entry
 * point (REQ-TIMED-012/013) ────────────────────────────────────────────── */

/* Companion to test_dispatch_tscf_with_tv_true_postpones_a_standard_
 * request() (test_mock.c) -- rcp_mock_server_dispatch_e2e_tscf() (issue
 * #462) closes exactly the gap that pair's own comment describes: tv=true
 * on a PLAIN-command-mode request (req_crc_enable left at its default)
 * now postpones a standard request via the request store
 * (RCP_SERVER_ADMIT_PENDING/RCP_MOCK_DISPATCH_PENDING), instead of the
 * request's own presentation time being silently discarded the way it
 * still is through rcp_mock_server_dispatch_e2e() itself (see the
 * regression guard below). */
static void test_dispatch_e2e_tscf_plain_mode_with_tv_true_postpones_a_standard_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    /* req_crc_enable left at its default (false): plain command mode. */

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch_e2e_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                         RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                         true /* tv */, 1000000u, 0u, plain.data,
                                                         plain.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NULL(resp.data); /* nothing ran -- no response yet */

    rcp_bytes_free(&resp);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* Same tv=true postponement, but now past a real, valid CRC32 unwrap
 * (safe command mode) -- proves the gate applies to the CRC-validated
 * dispatch_plain() call site too, not just the plain-command-mode
 * delegation branch above. */
static void test_dispatch_e2e_tscf_safe_mode_with_tv_true_postpones_a_standard_request(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped =
        rcp_e2e_wrap_framed(TEST_SID, false /* TSCF-framed */, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    g_handler_called = false;
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch_e2e_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                         RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                         true /* tv */, TEST_TS, 0u, wrapped.data,
                                                         wrapped.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* Mirrors test_crc_error_on_one_endpoint_broadcasts_safe_state_to_stream_
 * siblings() (dispatch_e2e()'s own CRC-mismatch/safe-state test) for
 * rcp_mock_server_dispatch_e2e_tscf() -- issue #462 added this function
 * as a full, separate copy of dispatch_e2e()'s body (own doc comment,
 * above) rather than a shared refactor, so its own CRC-mismatch/
 * fault-tracker-latch/safe-state-broadcast block needs its own
 * fault-injection coverage; tv=false here deliberately, to isolate this
 * CRC path from the tv=true postponement gate the two tests above
 * already cover. */
//cfusa:test REQ-E2E-041
//cfusa:test REQ-E2E-045
static void test_dispatch_e2e_tscf_crc_mismatch_broadcasts_safe_state(void)
{
    rcp_mock_server_t              *srv     = rcp_mock_server_new();
    const uint8_t                    pl[4]   = {0xC0, 0xC1, 0xC2, 0xC3};
    rcp_bytes_t                      frame   = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                      w       = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
    rcp_bytes_t                      resp    = {0};
    rcp_bytes_t                      timed;
    rcp_regmap_ep_id_map_entry_t     ep_map[2] = {
        {1, 0x11, 1},
        {2, 0x12, 1},
    };
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];

    TEST_ASSERT_NOT_NULL(w.data);
    w.data[w.len - 1u] ^= 0xFFu; /* corrupt the trailer's last octet */

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 0x12, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, ep_map, 2));

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
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 0x12));

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                         RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                         false /* tv */, TEST_TS, 0u, w.data,
                                                         w.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 0x12));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&w);
    rcp_bytes_free(&frame);
    rcp_mock_server_destroy(srv);
}

/* Fragment-aware entry point: same tv=true postponement, threaded
 * through the "never actually fragmented" fallback (an ms=0 fragment
 * arriving while nothing is collecting) -- which, as of issue #462,
 * reaches rcp_mock_server_dispatch_e2e_tscf() rather than the plain
 * rcp_mock_server_dispatch_e2e(), so tv/avtp_timestamp/gptp_reference_now
 * survive that delegation instead of being silently dropped one layer
 * down. */
static void test_dispatch_e2e_fragment_tscf_with_tv_true_postpones_a_standard_request(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped;
    rcp_bytes_t          resp = {0};

    set_up_frag_stream(srv, counting_handler);
    wrapped = rcp_e2e_wrap_framed(TEST_SID, false /* TSCF-framed */, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  true /* tv */, TEST_TS, 0u,
                                                                  wrapped.data, wrapped.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* ── mock.c category-2 fault-injection gaps, TSCF-fragment sibling
 * (issue #520 / c-RCP-19): rcp_mock_server_dispatch_e2e_fragment_tscf()'s
 * own doc comment describes it as "a full, separate copy" of
 * rcp_mock_server_dispatch_e2e_fragment()'s body -- before this batch,
 * the ONLY test exercising it at all was the tv=true postponement test
 * immediately above, which returns from the presentation-time gate
 * before reaching any of this function's own reassembly/CRC logic.
 * These mirror dispatch_e2e_fragment()'s own equivalent tests above,
 * tv=false throughout to isolate this function's non-presentation-time
 * behavior (already proven identical to its NTSCF sibling's own pad/
 * timestamp edge cases by construction -- both call the same fragment.h/
 * e2e.h primitives -- so those two edge cases are not separately
 * re-pinned here). ──────────────────────────────────────────────────── */

/* A single, never-actually-fragmented message behaves byte-identically
 * to calling rcp_mock_server_dispatch_e2e_tscf() directly. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_tscf_single_fragment_matches_dispatch_e2e_tscf(void)
{
    rcp_mock_server_t *srv     = rcp_mock_server_new();
    const uint8_t       pl[4]  = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped;
    rcp_bytes_t          resp = {0};

    set_up_frag_stream(srv, counting_handler);
    wrapped = rcp_e2e_wrap_framed(TEST_SID, false /* TSCF-framed */, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false /* tv */, TEST_TS, 0u,
                                                                  wrapped.data, wrapped.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* A genuine 3-fragment ABB message, CRC-protected via the real
 * fragmented-CRC formula, reassembles and dispatches exactly once. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_tscf_three_fragment_round_trip_succeeds(void)
{
    rcp_mock_server_t *srv        = rcp_mock_server_new();
    const uint8_t       p0[4]     = {0xF0, 0xF1, 0xF2, 0xF3};
    const uint8_t       p1[4]     = {0xE0, 0xE1, 0xE2, 0xE3};
    const uint8_t       p2[4]     = {0xD0, 0xD1, 0xD2, 0xD3};
    rcp_bytes_t          f0       = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f1       = make_abb(1, 0, 1, p1, sizeof(p1));
    rcp_bytes_t          f2       = make_abb(0, 0, 2, p2, sizeof(p2));
    rcp_bytes_t          final_wire;
    uint8_t               concatenated[12];
    uint32_t              want;
    rcp_bytes_t           resp = {0};

    set_up_frag_stream(srv, capturing_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f2.data, f2.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    rcp_memcpy_bounded(concatenated + 8, sizeof(concatenated) - 8, p2, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data, 8u, concatenated,
                                           sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f1.data, f1.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, final_wire.data,
                                                                  final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);
    TEST_ASSERT_EQUAL_UINT(sizeof(concatenated), g_captured_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(concatenated, g_captured_payload, sizeof(concatenated));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f2);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* A genuine 2-fragment ACF_GBB message -- the OTHER acf_msg_type branch,
 * previously entirely unexercised for this function. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_tscf_gbb_two_fragment_round_trip_succeeds(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x84, 0x85, 0x86, 0x87};
    const uint8_t       p1[4] = {0x94, 0x95, 0x96, 0x97};
    rcp_bytes_t          f0   = make_gbb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f1   = make_gbb(0, 0, 1, p1, sizeof(p1));
    rcp_bytes_t           final_wire;
    uint8_t                concatenated[8];
    uint32_t               want;
    rcp_bytes_t             resp = {0};

    set_up_frag_stream(srv, counting_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_GBB_HEADER_LEN, concatenated, sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, final_wire.data,
                                                                  final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* A genuine fragmented-CRC mismatch on the final fragment: rejected, not
 * executed, latches the stream, and (rx_enforce_e2e set) broadcasts safe
 * state to a stream sibling -- same three consequences as
 * dispatch_e2e_fragment()'s own equivalent test. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
//cfusa:test REQ-E2E-041
//cfusa:test REQ-E2E-045
static void test_dispatch_e2e_fragment_tscf_crc_mismatch_broadcasts_safe_state(void)
{
    rcp_mock_server_t              *srv     = rcp_mock_server_new();
    const uint8_t                    p0[4]   = {0x10, 0x11, 0x12, 0x13};
    const uint8_t                    p1[4]   = {0x20, 0x21, 0x22, 0x23};
    rcp_bytes_t                      f0      = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t                      f1      = make_abb(0, 0, 1, p1, sizeof(p1));
    rcp_bytes_t                      final_wire;
    rcp_bytes_t                      resp    = {0};
    rcp_bytes_t                      timed;
    rcp_regmap_ep_id_map_entry_t     ep_map[2] = {
        {1, 0x11, 1},
        {2, 0x12, 1},
    };
    rcp_regmap_request_stream_cfg_t  stream_cfg[1];

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    rcp_mock_server_add_endpoint(srv, 0x12, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_rx_enforce_e2e(srv, 0x11, true));
    TEST_ASSERT_TRUE(rcp_mock_server_set_ep_id_map(srv, ep_map, 2));

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
    TEST_ASSERT_EQUAL_size_t(1, rcp_mock_server_pending_count(srv, 0x12));

    /* wrap()'s own trailer is the wrong (single-fragment) formula --
     * already a genuine mismatch for this 2-fragment message. */
    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, final_wire.data,
                                                                  final_wire.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_TRUE(rcp_mock_server_stream_status_rx_blocked(srv, TEST_SID));
    TEST_ASSERT_EQUAL_size_t(0, rcp_mock_server_pending_count(srv, 0x12));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* An out-of-order intermediate fragment is rejected and abandons the
 * in-progress reassembly; a fresh, correctly-ordered sequence afterward
 * succeeds. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_tscf_out_of_order_segment_is_rejected_and_resets(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x40, 0x41, 0x42, 0x43};
    rcp_bytes_t          f0    = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f_bad = make_abb(1, 0, 5, p0, sizeof(p0));
    rcp_bytes_t          plain = make_abb(0, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          wrapped;
    rcp_bytes_t           resp = {0};

    set_up_frag_stream(srv, counting_handler);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f_bad.data,
                                                                  f_bad.len, &resp));
    rcp_bytes_free(&resp);

    wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);
    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, wrapped.data,
                                                                  wrapped.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_bytes_free(&f_bad);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* A reassembly that would exceed the stream's own configured
 * max_total_len is rejected outright. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_tscf_too_large_reassembly_is_rejected(void)
{
    rcp_mock_server_t          *srv    = rcp_mock_server_new();
    const uint8_t                p0[4] = {0x50, 0x51, 0x52, 0x53};
    rcp_bytes_t                  f0    = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_fragment_reassembler_t *reasm;
    rcp_bytes_t                  resp = {0};

    set_up_frag_stream(srv, counting_handler);

    reasm = rcp_mock_server_fragment_reassembler(srv, TEST_SID);
    TEST_ASSERT_NOT_NULL(reasm);
    rcp_fragment_reassembler_init(reasm, 2u);

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(reasm));

    rcp_bytes_free(&resp);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* An unresolvable stream_id falls back to rcp_mock_server_dispatch_e2e_
 * tscf() unchanged. */
//cfusa:test REQ-E2E-038
//cfusa:test REQ-E2E-039
static void test_dispatch_e2e_fragment_tscf_unresolvable_stream_falls_back_to_dispatch_e2e_tscf(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       pl[4] = {0x60, 0x61, 0x62, 0x63};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped;
    rcp_bytes_t           resp = {0};

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));
    /* Deliberately no rcp_mock_server_set_request_stream_cfg() call. */

    wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, wrapped.data,
                                                                  wrapped.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* A stream already latched faulted (stream_fault_tracker) rejects a
 * fragment outright as RCP_MOCK_DISPATCH_STREAM_FAULTED. */
//cfusa:test REQ-E2E-021
static void test_dispatch_e2e_fragment_tscf_already_faulted_stream_is_rejected(void)
{
    rcp_mock_server_t             *srv = rcp_mock_server_new();
    rcp_e2e_stream_fault_tracker_t tracker;
    const uint8_t                   pl[4] = {0x78, 0x79, 0x7A, 0x7B};
    rcp_bytes_t                     plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t                     wrapped;
    rcp_bytes_t                     resp = {0};

    rcp_e2e_stream_fault_tracker_init(&tracker);
    set_up_frag_stream(srv, counting_handler);
    rcp_mock_server_set_stream_fault_tracker(srv, &tracker);
    (void)rcp_e2e_stream_fault_tracker_on_crc_error(&tracker, TEST_SID, true);
    TEST_ASSERT_TRUE(rcp_e2e_stream_fault_tracker_is_faulted(&tracker, TEST_SID));

    wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_STREAM_FAULTED,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, wrapped.data,
                                                                  wrapped.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NOT_NULL(resp.data);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* Multi-member frame entry point: a single, validly-CRC'd member
 * dispatched via rcp_mock_server_dispatch_frame_e2e_tscf() with tv=true
 * is postponed (RCP_MOCK_DISPATCH_PENDING) instead of executing
 * immediately, proving tv/avtp_timestamp/gptp_reference_now reach each
 * member's own admission through the per-member rcp_mock_server_
 * dispatch_e2e_tscf() delegation (issue #462), not just a single-request
 * entry point. */
static void test_dispatch_frame_e2e_tscf_with_tv_true_postpones_a_standard_request(void)
{
    rcp_mock_server_t             *srv = rcp_mock_server_new();
    const uint8_t                   a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    rcp_bytes_t                     m1   = make_abb(0, 0, 0, a, sizeof(a));
    rcp_bytes_t                     w1   = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m1.data, m1.len);
    rcp_mock_frame_member_result_t  results[4];
    size_t                          dispatched;

    TEST_ASSERT_EQUAL_UINT(16u, w1.len);

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    g_handler_called = false;
    dispatched = rcp_mock_server_dispatch_frame_e2e_tscf(srv, RCP_AVTP_SUBTYPE_TSCF, true, TEST_SID,
                                                          true /* tv */, TEST_TS, 0u /* gptp_reference_now */,
                                                          0u /* sequence_num */, w1.data, w1.len, results,
                                                          4);
    TEST_ASSERT_EQUAL_UINT(1u, dispatched);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_PENDING, results[0].result);
    TEST_ASSERT_FALSE(g_handler_called);

    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&w1);
    rcp_bytes_free(&m1);
    rcp_mock_server_destroy(srv);
}

/* ── issue #462 regression guard: the pre-existing entry points are
 * unaffected ────────────────────────────────────────────────────────────── */

/* rcp_mock_server_dispatch_e2e() itself takes no tv parameter at all, by
 * construction -- there is no way for a caller to reach the
 * presentation-time gate through it, whatever avtp_timestamp it is
 * given. The exact request/config pair that
 * test_dispatch_e2e_tscf_plain_mode_with_tv_true_postpones_a_standard_
 * request() above proves IS postponed through the new _tscf sibling
 * still executes immediately (RCP_MOCK_DISPATCH_OK) through this
 * pre-existing entry point -- issue #462 added a new function, it did
 * not change this one. */
static void test_dispatch_e2e_still_ignores_presentation_time_after_462(void)
{
    rcp_mock_server_t *srv = rcp_mock_server_new();
    rcp_bytes_t         resp = {0};
    const uint8_t        pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                    RCP_ACF_MSG_TYPE_ABB, true, TEST_SID, 1000000u,
                                                    plain.data, plain.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* Same regression guard for rcp_mock_server_dispatch_e2e_fragment() --
 * still executes immediately, unaffected by issue #462's new sibling. */
static void test_dispatch_e2e_fragment_still_ignores_presentation_time_after_462(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       pl[4] = {0x01, 0x02, 0x03, 0x04};
    rcp_bytes_t          plain = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_bytes_t          wrapped;
    rcp_bytes_t          resp = {0};

    set_up_frag_stream(srv, counting_handler);
    wrapped = rcp_e2e_wrap_framed(TEST_SID, false, TEST_OCTET1, TEST_TU, TEST_TS, plain.data, plain.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, wrapped.data, wrapped.len,
                                                             &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&resp);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&plain);
    rcp_mock_server_destroy(srv);
}

/* Same regression guard for rcp_mock_server_dispatch_frame_e2e() --
 * still executes immediately, unaffected by issue #462's new sibling. */
static void test_dispatch_frame_e2e_still_ignores_presentation_time_after_462(void)
{
    rcp_mock_server_t             *srv = rcp_mock_server_new();
    const uint8_t                   a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
    rcp_bytes_t                     m1   = make_abb(0, 0, 0, a, sizeof(a));
    rcp_bytes_t                     w1   = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m1.data, m1.len);
    rcp_mock_frame_member_result_t  results[4];
    size_t                          dispatched;

    TEST_ASSERT_EQUAL_UINT(16u, w1.len);

    to_rcp_configured(srv);
    rcp_mock_server_add_endpoint(srv, 0x11, 1, true, counting_handler, NULL);
    TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv, 0x11, true));

    g_handler_called = false;
    dispatched = rcp_mock_server_dispatch_frame_e2e(srv, RCP_AVTP_SUBTYPE_TSCF, true, TEST_SID,
                                                     TEST_TS, 0u, w1.data, w1.len, results, 4);
    TEST_ASSERT_EQUAL_UINT(1u, dispatched);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&w1);
    rcp_bytes_free(&m1);
    rcp_mock_server_destroy(srv);
}

/* ── REQ-E2E-042: pad EXCLUSION, trailer placement, and quadlet alignment ───── */

/* TC18 sec. 13.6 Figures 20/21's own worked ACF_ABB example: an 8-octet
 * byte_message_info header plus a 6-octet real payload (PL_Byte1..
 * PL_Byte6) -- 14 octets unpadded, needing 2 pad octets to reach the next
 * quadlet boundary (16 octets = 4 quadlets). The wire order the figures
 * show is [header][PL_Byte1..6][CRC32][0x00 pad][0x00 pad] -- the CRC32
 * immediately after the real payload, with the pad octets strictly AFTER
 * the complete trailer -- never [header][payload][pad][CRC32]. The CRC32
 * itself excludes the pad octets ("a CRC32 is calculated ... across ...
 * the entire payload (except padding)"). issue #420: previously
 * rcp_e2e_wrap() got both of these backwards -- it required acf.c's
 * already-padded output, computed the CRC over the whole thing (covering
 * the pad octets it should have excluded), and appended the trailer
 * directly after that, producing [header][payload][pad][CRC32]. This test
 * pins the corrected wire layout, the corrected (pad-excluding) CRC value,
 * that perturbing only the pad octets' VALUES never changes the CRC, and
 * that rcp_e2e_unwrap() still reassembles byte-identically to what
 * acf.c's own encoder produced despite the trailer no longer sitting at
 * the very end of the wire frame. The quadlet-alignment rejection this
 * test used to cover is unaffected by the fix and is still asserted
 * below. */
//cfusa:test REQ-E2E-042
static void test_crc_omits_pad_octets_wire_order_header_payload_crc_then_pad(void)
{
    const uint8_t                pl[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    rcp_bytes_t                  frame = make_abb(0, 0, 0, pl, sizeof(pl));
    rcp_acf_byte_message_info_t  hdr;
    const uint8_t                *out_pl  = NULL;
    size_t                        out_len = 0;
    rcp_bytes_t                  wrapped;
    rcp_bytes_t                  wrapped_perturbed_pad;
    uint8_t                      perturbed[16];
    uint8_t                      adapted_prefix[14];
    uint32_t                     expected_crc;
    rcp_e2e_errc_t                rc;
    rcp_bytes_t                  body = {0};
    uint8_t                      misaligned[6] = {0x1Cu, 0x02u, 0, 0, 0, 0};
    rcp_bytes_t                  mis;

    /* 8 + 6 = 14 octets, so 2 pad octets bring it to 4 whole quadlets. */
    TEST_ASSERT_EQUAL_UINT(16u, frame.len);
    TEST_ASSERT_EQUAL_UINT8(2u, rcp_acf_pad_len(8u + sizeof(pl)));
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &out_pl, &out_len));
    TEST_ASSERT_EQUAL_UINT8(2u, hdr.pad);
    TEST_ASSERT_EQUAL_UINT8(0u, frame.data[14]);
    TEST_ASSERT_EQUAL_UINT8(0u, frame.data[15]);

    wrapped = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, frame.data, frame.len);
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL_UINT(frame.len + RCP_E2E_CRC_LEN, wrapped.len); /* 20 */

    /* Wire order: [0..14) real header+payload (acf_msg_length adapted),
     * [14..18) CRC32, [18..20) the original 2 pad octets -- CRC32
     * immediately after the real payload, pad octets strictly after it. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pl, wrapped.data + 8, sizeof(pl)); /* real payload, unmoved */
    TEST_ASSERT_EQUAL_UINT8(0u, wrapped.data[18]); /* pad, now AFTER the CRC32 */
    TEST_ASSERT_EQUAL_UINT8(0u, wrapped.data[19]);

    /* The trailer at [14..18) is the real CRC32: computed independently
     * over the length-adapted 14-byte real prefix only, matching
     * rcp_e2e_compute_crc()'s documented coverage span -- NOT over all 16
     * bytes of frame.data (which would include the 2 pad octets). */
    rcp_memcpy_bounded(adapted_prefix, sizeof(adapted_prefix), frame.data, sizeof(adapted_prefix));
    adapted_prefix[1] = (uint8_t)(adapted_prefix[1] + 1u); /* +1 quadlet: 4 -> 5, no MSB carry here */
    expected_crc = rcp_e2e_compute_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, adapted_prefix, sizeof(adapted_prefix));
    TEST_ASSERT_EQUAL_HEX32(expected_crc, be32(wrapped.data + 14));

    /* Perturbing only the pad octets' VALUES (not their count, still 2
     * per the header's own "pad" field) must not change the CRC32 --
     * TC18's "except padding" exclusion means this module must not care
     * what the pad bytes contain. Build a second frame identical except
     * its trailing pad octets hold different byte values and confirm the
     * two wrapped CRC32 trailers are byte-identical, while the two
     * wrapped pad regions differ (proving the pad bytes are still
     * faithfully carried through, just excluded from the CRC). */
    rcp_memcpy_bounded(perturbed, sizeof(perturbed), frame.data, sizeof(perturbed));
    perturbed[14] = 0xAAu;
    perturbed[15] = 0xBBu;
    wrapped_perturbed_pad = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, perturbed, sizeof(perturbed));
    TEST_ASSERT_NOT_NULL(wrapped_perturbed_pad.data);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wrapped.data + 14, wrapped_perturbed_pad.data + 14,
                                   RCP_E2E_CRC_LEN);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, wrapped_perturbed_pad.data[18]);
    TEST_ASSERT_EQUAL_HEX8(0xBBu, wrapped_perturbed_pad.data[19]);
    TEST_ASSERT_TRUE(memcmp(wrapped.data + 18, wrapped_perturbed_pad.data + 18, 2) != 0);

    /* rcp_e2e_unwrap() reverses both the CRC-in-the-middle placement and
     * the length adaptation, reassembling the exact original acf.c-
     * produced frame (header+payload+pad, un-adapted acf_msg_length) even
     * though the CRC32 trailer sat between the real payload and the pad
     * octets on the wire, not after them. */
    rc = rcp_e2e_unwrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, wrapped.data, wrapped.len, &body);
    TEST_ASSERT_EQUAL_INT(RCP_E2E_OK, rc);
    TEST_ASSERT_EQUAL_UINT(frame.len, body.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, body.data, body.len);

    /* Unaffected by this fix: 6 octets is not a whole quadlet -- not
     * acf.c's own quadlet-aligned output shape -- so wrap() still fails
     * safe instead of appending a trailer to it. */
    mis = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, misaligned, sizeof(misaligned));
    TEST_ASSERT_NULL(mis.data);
    TEST_ASSERT_EQUAL_UINT(0u, mis.len);

    rcp_bytes_free(&body);
    rcp_bytes_free(&wrapped_perturbed_pad);
    rcp_bytes_free(&wrapped);
    rcp_bytes_free(&mis);
    rcp_bytes_free(&frame);
}

/* ══════════════════════════════════════════════════════════════════════
 * MC/DC gap-closing tests: the final-fragment re-encode guards
 * (`!encoded.data && reassembled_len != 0u`) in dispatch_e2e_fragment()
 * (mock.c L2405 GBB / L2464 ABB) and dispatch_e2e_fragment_tscf()
 * (L2733 GBB / L2792 ABB). Every existing fragment round-trip test's
 * own rcp_acf_encode_gbb()/_abb() call succeeds (the ordinary allocator
 * is never made to fail), so `!encoded.data`'s own TRUE side -- and, in
 * turn, `reassembled_len != 0u`'s own independent effect once that's
 * true -- was never exercised at any of the four call sites. Fault-
 * injection idiom per this project's own tests/test_fragment.c
 * precedent (rcp_alloc_hooks_t), with one refinement this call path
 * specifically needs: rcp_e2e_unwrap_framed() (e2e.c) ALSO calls
 * rcp_malloc() internally, for its own body-copy buffer, one call
 * BEFORE mock.c ever reaches the target rcp_acf_encode_gbb()/_abb()
 * line -- a blanket "every malloc call fails" hook makes that earlier
 * call fail first instead, which mock.c's own decode_gbb(NULL, 0, ...)
 * then rejects for an entirely different, unrelated reason
 * (RCP_ACF_ERR_SHORT_FRAME) before the target line is ever reached at
 * all. (Confirmed empirically, not assumed: a blanket-fail hook here
 * produces the SAME RCP_MOCK_DISPATCH_REJECTED result via that
 * unrelated path, silently exercising nothing this decision cares
 * about -- exactly the "test passes, but not for the reason you
 * think" trap this project's own methodology warns against.) A
 * counting allocator -- real malloc() for the first call (the unwrap
 * step's own, letting it succeed normally), NULL for every call after
 * -- isolates the failure to the one call this decision is actually
 * about. */

static int g_alloc_pass_count;

static void *fails_after_n_mallocs(size_t size)
{
    if (g_alloc_pass_count > 0) {
        g_alloc_pass_count--;
        return malloc(size);
    }
    return NULL;
}

/* counting_handler() (above) asserts request_len > 0 -- exactly what a
 * genuinely-empty reassembled payload's own fallback dispatch_plain()
 * call produces (a real, deliberate 0-length request, not a bug), so
 * the empty-payload closure test below needs its own tolerant handler
 * instead. */
static void empty_tolerant_handler(const uint8_t *request, size_t request_len,
                                    rcp_bytes_t *out_response, void *user_data)
{
    (void)request;
    (void)request_len;
    (void)out_response;
    (void)user_data;
    g_handler_called = true;
}

/* Nonzero reassembled_len, encode fails: closes `!encoded.data`'s own
 * TRUE side (and, paired against any ordinary successful round trip,
 * `reassembled_len != 0u`'s TRUE side). */
static void test_dispatch_e2e_fragment_gbb_encode_failure_with_payload_is_rejected(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t       p1[4] = {0x05, 0x06, 0x07, 0x08};
    rcp_bytes_t          f0   = make_gbb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f1   = make_gbb(0, 0, 1, p1, sizeof(p1));
    rcp_bytes_t           final_wire;
    uint8_t                concatenated[8];
    uint32_t               want;
    rcp_bytes_t             resp = {0};
    rcp_alloc_hooks_t       hooks = {0};

    set_up_frag_stream(srv, counting_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_GBB_HEADER_LEN, concatenated, sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    /* Only the final rcp_acf_encode_gbb() call (inside mock.c, after
     * reassembly completes) must fail -- everything up to here already
     * ran with the real allocator. */
    g_alloc_pass_count = 1; /* let unwrap_framed()'s own internal malloc through */
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, final_wire.data, final_wire.len,
                                                             &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_e2e_fragment_abb_encode_failure_with_payload_is_rejected(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x11, 0x12, 0x13, 0x14};
    const uint8_t       p1[4] = {0x15, 0x16, 0x17, 0x18};
    rcp_bytes_t          f0   = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f1   = make_abb(0, 0, 1, p1, sizeof(p1));
    rcp_bytes_t           final_wire;
    uint8_t                concatenated[8];
    uint32_t               want;
    rcp_bytes_t             resp = {0};
    rcp_alloc_hooks_t       hooks = {0};

    set_up_frag_stream(srv, counting_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_ABB_HEADER_LEN, concatenated, sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    g_alloc_pass_count = 1; /* let unwrap_framed()'s own internal malloc through */
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data, final_wire.len,
                                                             &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* Zero reassembled_len (both fragments carry an EMPTY payload), encode
 * still fails (rcp_acf_encode_gbb()/_abb() always allocates a real
 * header-sized buffer, regardless of payload length): closes
 * `reassembled_len != 0u`'s own FALSE side, paired against the
 * TRUE-side tests just above (both share `!encoded.data` == true, only
 * reassembled_len differs). Not REJECTED this time -- dispatch_plain()
 * is called with a NULL/0-length "encoded" frame instead, the same
 * fail-toward-a-harmless-empty-request shape a genuinely empty (but
 * successfully encoded) payload would take. */
static void test_dispatch_e2e_fragment_gbb_encode_failure_empty_payload_falls_through(void)
{
    rcp_mock_server_t *srv  = rcp_mock_server_new();
    rcp_bytes_t         f0  = make_gbb(1, 0, 0, NULL, 0);
    rcp_bytes_t         f1  = make_gbb(0, 0, 1, NULL, 0);
    rcp_bytes_t          final_wire;
    uint32_t              want;
    rcp_bytes_t            resp = {0};
    rcp_alloc_hooks_t      hooks = {0};

    set_up_frag_stream(srv, empty_tolerant_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_GBB_HEADER_LEN, NULL, 0);
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    g_alloc_pass_count = 1; /* let unwrap_framed()'s own internal malloc through */
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                             TEST_TS, final_wire.data, final_wire.len,
                                                             &resp));
    /* Encode genuinely failed (reassembled_len == 0, malloc still
     * failed) -- dispatch_plain() ran with a NULL/0-length request
     * instead, and the enabled endpoint's own handler executed
     * immediately on it. */
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* L2464's own ABB encode-fail guard needs this SAME empty-payload
 * closure independently -- L2405 (GBB) and L2464 (ABB) are two
 * DIFFERENT source lines in mock.c's own if/else msg-type branches, so
 * the GBB-only empty-payload test just above does not carry over to
 * this one. */
static void test_dispatch_e2e_fragment_abb_encode_failure_empty_payload_falls_through(void)
{
    rcp_mock_server_t *srv  = rcp_mock_server_new();
    rcp_bytes_t         f0  = make_abb(1, 0, 0, NULL, 0);
    rcp_bytes_t         f1  = make_abb(0, 0, 1, NULL, 0);
    rcp_bytes_t          final_wire;
    uint32_t              want;
    rcp_bytes_t            resp = {0};
    rcp_alloc_hooks_t      hooks = {0};

    set_up_frag_stream(srv, empty_tolerant_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_ABB_HEADER_LEN, NULL, 0);
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, f0.data, f0.len, &resp));
    rcp_bytes_free(&resp);

    g_alloc_pass_count = 1;
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                             RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                             TEST_TS, final_wire.data, final_wire.len,
                                                             &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* Same two closures (nonzero-payload TRUE side, empty-payload FALSE
 * side) for dispatch_e2e_fragment_tscf()'s own identical guards. */
static void test_dispatch_e2e_fragment_tscf_gbb_encode_failure_with_payload_is_rejected(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x21, 0x22, 0x23, 0x24};
    const uint8_t       p1[4] = {0x25, 0x26, 0x27, 0x28};
    rcp_bytes_t          f0   = make_gbb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f1   = make_gbb(0, 0, 1, p1, sizeof(p1));
    rcp_bytes_t           final_wire;
    uint8_t                concatenated[8];
    uint32_t               want;
    rcp_bytes_t             resp = {0};
    rcp_alloc_hooks_t       hooks = {0};

    set_up_frag_stream(srv, counting_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_GBB_HEADER_LEN, concatenated, sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    g_alloc_pass_count = 1; /* let unwrap_framed()'s own internal malloc through */
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, final_wire.data,
                                                                  final_wire.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_e2e_fragment_tscf_abb_encode_failure_with_payload_is_rejected(void)
{
    rcp_mock_server_t *srv    = rcp_mock_server_new();
    const uint8_t       p0[4] = {0x31, 0x32, 0x33, 0x34};
    const uint8_t       p1[4] = {0x35, 0x36, 0x37, 0x38};
    rcp_bytes_t          f0   = make_abb(1, 0, 0, p0, sizeof(p0));
    rcp_bytes_t          f1   = make_abb(0, 0, 1, p1, sizeof(p1));
    rcp_bytes_t           final_wire;
    uint8_t                concatenated[8];
    uint32_t               want;
    rcp_bytes_t             resp = {0};
    rcp_alloc_hooks_t       hooks = {0};

    set_up_frag_stream(srv, counting_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    rcp_memcpy_bounded(concatenated, sizeof(concatenated), p0, 4);
    rcp_memcpy_bounded(concatenated + 4, sizeof(concatenated) - 4, p1, 4);
    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_ABB_HEADER_LEN, concatenated, sizeof(concatenated));
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    g_alloc_pass_count = 1; /* let unwrap_framed()'s own internal malloc through */
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, final_wire.data,
                                                                  final_wire.len, &resp));
    TEST_ASSERT_FALSE(g_handler_called);
    TEST_ASSERT_NULL(resp.data);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* L2733's own GBB encode-fail guard needs this SAME empty-payload
 * closure independently -- L2733 (GBB) and L2792 (ABB) are two
 * DIFFERENT source lines. */
static void test_dispatch_e2e_fragment_tscf_gbb_encode_failure_empty_payload_falls_through(void)
{
    rcp_mock_server_t *srv  = rcp_mock_server_new();
    rcp_bytes_t         f0  = make_gbb(1, 0, 0, NULL, 0);
    rcp_bytes_t         f1  = make_gbb(0, 0, 1, NULL, 0);
    rcp_bytes_t          final_wire;
    uint32_t              want;
    rcp_bytes_t            resp = {0};
    rcp_alloc_hooks_t      hooks = {0};

    set_up_frag_stream(srv, empty_tolerant_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_GBB_HEADER_LEN, NULL, 0);
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    g_alloc_pass_count = 1;
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_GBB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, final_wire.data,
                                                                  final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

static void test_dispatch_e2e_fragment_tscf_abb_encode_failure_empty_payload_falls_through(void)
{
    rcp_mock_server_t *srv  = rcp_mock_server_new();
    rcp_bytes_t         f0  = make_abb(1, 0, 0, NULL, 0);
    rcp_bytes_t         f1  = make_abb(0, 0, 1, NULL, 0);
    rcp_bytes_t          final_wire;
    uint32_t              want;
    rcp_bytes_t            resp = {0};
    rcp_alloc_hooks_t      hooks = {0};

    set_up_frag_stream(srv, empty_tolerant_handler);

    final_wire = rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f1.data, f1.len);
    TEST_ASSERT_NOT_NULL(final_wire.data);

    want = rcp_e2e_compute_fragmented_crc(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, f0.data,
                                           RCP_ACF_ABB_HEADER_LEN, NULL, 0);
    final_wire.data[final_wire.len - 4u] = (uint8_t)(want >> 24);
    final_wire.data[final_wire.len - 3u] = (uint8_t)(want >> 16);
    final_wire.data[final_wire.len - 2u] = (uint8_t)(want >> 8);
    final_wire.data[final_wire.len - 1u] = (uint8_t)want;

    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_FRAGMENT_PENDING,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, f0.data, f0.len,
                                                                  &resp));
    rcp_bytes_free(&resp);

    g_alloc_pass_count = 1; /* let unwrap_framed()'s own internal malloc through */
    hooks.malloc_fn = fails_after_n_mallocs;
    rcp_alloc_set_hooks(&hooks);

    g_handler_called = false;
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK,
                      rcp_mock_server_dispatch_e2e_fragment_tscf(srv, 0x11, RCP_AVTP_SUBTYPE_TSCF,
                                                                  RCP_ACF_MSG_TYPE_ABB, true, TEST_SID,
                                                                  false, TEST_TS, 0u, final_wire.data,
                                                                  final_wire.len, &resp));
    TEST_ASSERT_TRUE(g_handler_called);

    rcp_alloc_reset_hooks();
    rcp_bytes_free(&final_wire);
    rcp_bytes_free(&f1);
    rcp_bytes_free(&f0);
    rcp_mock_server_destroy(srv);
}

/* ── L3307: rcp_mock_server_dispatch_frame_e2e()'s own per-member
 * chaining-error classifier -- the same shape as mock.c's plain
 * dispatch_frame() (test_conditional_dispatch.c's own MC/DC test, this
 * batch), but this decision is a SEPARATE source line reached through
 * rcp_mock_server_dispatch_e2e() per member instead. CRC_ERROR (the
 * decision's own 4th condition) is already independently demonstrated
 * here (test_dispatch_frame_e2e_verifies_each_member_independently(),
 * above); DROPPED/REJECTED/ERR_UNKNOWN_BUS are not. */
static void test_dispatch_frame_e2e_prev_errored_classifier_mcdc(void)
{
    rcp_mock_server_t              *srv = rcp_mock_server_new(); /* still HW_UNCONFIGURED */
    rcp_bytes_t                      dropped_member, rejected_member;
    rcp_acf_byte_message_info_t      abb_hdr = {0};
    rcp_acf_gbb_header_t              gbb_hdr;
    uint8_t                          frame[64];
    size_t                           frame_len;
    rcp_mock_frame_member_result_t   results[4];
    size_t                           n;

    /* Member 0: DROPPED (HW_UNCONFIGURED, NTSCF, non-discovery bus). */
    abb_hdr.byte_bus_id     = 9;
    abb_hdr.transaction_num = 1;
    dropped_member = rcp_acf_encode_abb(&abb_hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(dropped_member.data);

    /* Member 1: REJECTED (HW_UNCONFIGURED, NTSCF, discovery bus, GBB --
     * see test_dispatch_frame_prev_errored_classifier_mcdc()'s own
     * identical rationale, test_conditional_dispatch.c). */
    memset(&gbb_hdr, 0, sizeof(gbb_hdr));
    gbb_hdr.info.byte_bus_id     = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    gbb_hdr.info.transaction_num = 2;
    rejected_member = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(rejected_member.data);

    TEST_ASSERT_TRUE(dropped_member.len + rejected_member.len <= sizeof(frame));
    rcp_memcpy_bounded(frame, sizeof(frame), dropped_member.data, dropped_member.len);
    rcp_memcpy_bounded(frame + dropped_member.len, sizeof(frame) - dropped_member.len,
                        rejected_member.data, rejected_member.len);
    frame_len = dropped_member.len + rejected_member.len;

    n = rcp_mock_server_dispatch_frame_e2e(srv, RCP_AVTP_SUBTYPE_NTSCF, false, 1u, 0u, 0u, frame,
                                            frame_len, results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_DROPPED, results[0].result);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, results[1].result);
    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&results[1].response);
    rcp_bytes_free(&dropped_member);
    rcp_bytes_free(&rejected_member);
    rcp_mock_server_destroy(srv);

    /* ERR_UNKNOWN_BUS -- a real RCP_CONFIGURED server with no endpoint
     * registered at the addressed bus at all. */
    {
        rcp_mock_server_t         *srv2 = rcp_mock_server_new();
        rcp_lifecycle_writer_ctx_t root = {true, false, false, false};
        rcp_bytes_t                 solo;
        rcp_acf_byte_message_info_t h2 = {0};

        TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
            rcp_mock_server_transition(srv2, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP,
                                        (rcp_lifecycle_writer_ctx_t){0}, true));
        TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
            rcp_mock_server_transition(srv2, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP, root, true));

        h2.byte_bus_id     = 77;
        h2.transaction_num = 3;
        solo = rcp_acf_encode_abb(&h2, NULL, 0);
        TEST_ASSERT_NOT_NULL(solo.data);

        n = rcp_mock_server_dispatch_frame_e2e(srv2, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, 0u, 0u,
                                                solo.data, solo.len, results, 4);
        TEST_ASSERT_EQUAL_size_t(1, n);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS, results[0].result);
        rcp_bytes_free(&results[0].response);
        rcp_bytes_free(&solo);
        rcp_mock_server_destroy(srv2);
    }
}

/* ── L3472: rcp_mock_server_dispatch_frame_e2e_tscf()'s own copy of the
 * identical classifier -- ALL FOUR conditions missing here (unlike
 * L3307's sibling, no existing test drives ANY of DROPPED/REJECTED/
 * ERR_UNKNOWN_BUS/CRC_ERROR through this specific TSCF entry point at
 * all). Same three triggers as L3307's own test, plus a genuine
 * CRC_ERROR member (the corrupted-second-member-of-two shape
 * test_dispatch_frame_e2e_verifies_each_member_independently() already
 * established, through the _tscf entry point this time). */
static void test_dispatch_frame_e2e_tscf_prev_errored_classifier_mcdc(void)
{
    rcp_mock_server_t              *srv = rcp_mock_server_new(); /* still HW_UNCONFIGURED */
    rcp_bytes_t                      dropped_member, rejected_member;
    rcp_acf_byte_message_info_t      abb_hdr = {0};
    rcp_acf_gbb_header_t              gbb_hdr;
    uint8_t                          frame[64];
    size_t                           frame_len;
    rcp_mock_frame_member_result_t   results[4];
    size_t                           n;

    abb_hdr.byte_bus_id     = 9;
    abb_hdr.transaction_num = 1;
    dropped_member = rcp_acf_encode_abb(&abb_hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(dropped_member.data);

    memset(&gbb_hdr, 0, sizeof(gbb_hdr));
    gbb_hdr.info.byte_bus_id     = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    gbb_hdr.info.transaction_num = 2;
    rejected_member = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(rejected_member.data);

    TEST_ASSERT_TRUE(dropped_member.len + rejected_member.len <= sizeof(frame));
    rcp_memcpy_bounded(frame, sizeof(frame), dropped_member.data, dropped_member.len);
    rcp_memcpy_bounded(frame + dropped_member.len, sizeof(frame) - dropped_member.len,
                        rejected_member.data, rejected_member.len);
    frame_len = dropped_member.len + rejected_member.len;

    n = rcp_mock_server_dispatch_frame_e2e_tscf(srv, RCP_AVTP_SUBTYPE_NTSCF, false, 1u, false, 0u, 0u,
                                                 0u, frame, frame_len, results, 4);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_DROPPED, results[0].result);
    TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_REJECTED, results[1].result);
    rcp_bytes_free(&results[0].response);
    rcp_bytes_free(&results[1].response);
    rcp_bytes_free(&dropped_member);
    rcp_bytes_free(&rejected_member);
    rcp_mock_server_destroy(srv);

    /* ERR_UNKNOWN_BUS. */
    {
        rcp_mock_server_t         *srv2 = rcp_mock_server_new();
        rcp_lifecycle_writer_ctx_t root = {true, false, false, false};
        rcp_bytes_t                 solo;
        rcp_acf_byte_message_info_t h2 = {0};

        TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
            rcp_mock_server_transition(srv2, RCP_LIFECYCLE_HW_CONFIGURED, &EMPTY_SNAP,
                                        (rcp_lifecycle_writer_ctx_t){0}, true));
        TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
            rcp_mock_server_transition(srv2, RCP_LIFECYCLE_RCP_CONFIGURED, &EMPTY_SNAP, root, true));

        h2.byte_bus_id     = 77;
        h2.transaction_num = 3;
        solo = rcp_acf_encode_abb(&h2, NULL, 0);
        TEST_ASSERT_NOT_NULL(solo.data);

        n = rcp_mock_server_dispatch_frame_e2e_tscf(srv2, RCP_AVTP_SUBTYPE_NTSCF, true, 1u, false, 0u,
                                                     0u, 0u, solo.data, solo.len, results, 4);
        TEST_ASSERT_EQUAL_size_t(1, n);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS, results[0].result);
        rcp_bytes_free(&results[0].response);
        rcp_bytes_free(&solo);
        rcp_mock_server_destroy(srv2);
    }

    /* CRC_ERROR -- two members, both addressed to a real, req_crc_enable
     * endpoint; only the second's own trailer is corrupted (the same
     * shape test_dispatch_frame_e2e_verifies_each_member_independently()
     * already established for the plain _e2e entry point). */
    {
        rcp_mock_server_t             *srv3 = rcp_mock_server_new();
        const uint8_t                   a[4] = {0xA1, 0xA2, 0xA3, 0xA4};
        const uint8_t                   b[4] = {0xB1, 0xB2, 0xB3, 0xB4};
        rcp_bytes_t                     m1   = make_abb(0, 0, 0, a, sizeof(a));
        rcp_bytes_t                     m2   = make_abb(0, 0, 0, b, sizeof(b));
        rcp_bytes_t                     w1 =
            rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m1.data, m1.len);
        rcp_bytes_t                     w2 =
            rcp_e2e_wrap(TEST_SUBTYPE, TEST_OCTET1, TEST_TU, TEST_SID, TEST_TS, m2.data, m2.len);
        uint8_t                          joined[32];

        TEST_ASSERT_EQUAL_UINT(16u, w1.len);
        TEST_ASSERT_EQUAL_UINT(16u, w2.len);
        rcp_memcpy_bounded(joined, sizeof(joined), w1.data, w1.len);
        rcp_memcpy_bounded(joined + w1.len, sizeof(joined) - w1.len, w2.data, w2.len);
        joined[31] ^= 0xFFu;

        to_rcp_configured(srv3);
        rcp_mock_server_add_endpoint(srv3, 0x11, 1, true, counting_handler, NULL);
        TEST_ASSERT_TRUE(rcp_mock_server_set_endpoint_req_crc_enable(srv3, 0x11, true));

        g_handler_called = false;
        n = rcp_mock_server_dispatch_frame_e2e_tscf(srv3, RCP_AVTP_SUBTYPE_TSCF, true, TEST_SID,
                                                     false, TEST_TS, 0u, 0u, joined, sizeof(joined),
                                                     results, 4);
        TEST_ASSERT_EQUAL_UINT(2u, n);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_OK, results[0].result);
        TEST_ASSERT_EQUAL(RCP_MOCK_DISPATCH_CRC_ERROR, results[1].result);
        TEST_ASSERT_TRUE(g_handler_called);

        rcp_bytes_free(&results[0].response);
        rcp_bytes_free(&results[1].response);
        rcp_bytes_free(&w2);
        rcp_bytes_free(&w1);
        rcp_bytes_free(&m2);
        rcp_bytes_free(&m1);
        rcp_mock_server_destroy(srv3);
    }
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
    RUN_TEST(test_watchdog_overflow_broadcasts_safe_state_to_stream_siblings);
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

    RUN_TEST(test_dispatch_e2e_fragment_single_fragment_matches_dispatch_e2e);
    RUN_TEST(test_dispatch_e2e_fragment_three_fragment_round_trip_succeeds);
    RUN_TEST(test_dispatch_e2e_fragment_final_fragment_non_aligned_payload_ok);
    RUN_TEST(test_dispatch_e2e_fragment_crc_mismatch_is_rejected_and_latches);
    RUN_TEST(test_dispatch_e2e_fragment_ntscf_forces_zero_timestamp_in_crc);
    RUN_TEST(test_dispatch_e2e_fragment_out_of_order_segment_is_rejected_and_resets);
    RUN_TEST(test_dispatch_e2e_fragment_too_large_reassembly_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_can_xl_write_request_round_trips);
    RUN_TEST(test_dispatch_e2e_fragment_can_xl_write_request_rejected_when_reassembled_total_exceeds_ceiling);
    RUN_TEST(test_dispatch_e2e_fragment_unresolvable_stream_falls_back_to_dispatch_e2e);
    RUN_TEST(test_dispatch_e2e_fragment_already_faulted_stream_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_plain_command_mode_falls_back_to_dispatch_plain);
    RUN_TEST(test_dispatch_e2e_fragment_too_short_header_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_gbb_two_fragment_round_trip_succeeds);
    RUN_TEST(test_dispatch_e2e_fragment_gbb_crc_mismatch_broadcasts_safe_state);

    RUN_TEST(test_dispatch_e2e_tscf_plain_mode_with_tv_true_postpones_a_standard_request);
    RUN_TEST(test_dispatch_e2e_tscf_safe_mode_with_tv_true_postpones_a_standard_request);
    RUN_TEST(test_dispatch_e2e_tscf_crc_mismatch_broadcasts_safe_state);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_with_tv_true_postpones_a_standard_request);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_single_fragment_matches_dispatch_e2e_tscf);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_three_fragment_round_trip_succeeds);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_gbb_two_fragment_round_trip_succeeds);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_crc_mismatch_broadcasts_safe_state);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_out_of_order_segment_is_rejected_and_resets);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_too_large_reassembly_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_unresolvable_stream_falls_back_to_dispatch_e2e_tscf);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_already_faulted_stream_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_gbb_encode_failure_with_payload_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_abb_encode_failure_with_payload_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_gbb_encode_failure_empty_payload_falls_through);
    RUN_TEST(test_dispatch_e2e_fragment_abb_encode_failure_empty_payload_falls_through);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_gbb_encode_failure_with_payload_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_abb_encode_failure_with_payload_is_rejected);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_gbb_encode_failure_empty_payload_falls_through);
    RUN_TEST(test_dispatch_e2e_fragment_tscf_abb_encode_failure_empty_payload_falls_through);
    RUN_TEST(test_dispatch_frame_e2e_prev_errored_classifier_mcdc);
    RUN_TEST(test_dispatch_frame_e2e_tscf_prev_errored_classifier_mcdc);
    RUN_TEST(test_dispatch_frame_e2e_tscf_with_tv_true_postpones_a_standard_request);
    RUN_TEST(test_dispatch_e2e_still_ignores_presentation_time_after_462);
    RUN_TEST(test_dispatch_e2e_fragment_still_ignores_presentation_time_after_462);
    RUN_TEST(test_dispatch_frame_e2e_still_ignores_presentation_time_after_462);

    RUN_TEST(test_crc_omits_pad_octets_wire_order_header_payload_crc_then_pad);
    return UNITY_END();
}
