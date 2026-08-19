/* SPDX-License-Identifier: MPL-2.0 */
/* SO_PRIORITY is a glibc extension -- same _DEFAULT_SOURCE requirement
 * as tsn.c's own file header, and for the identical reason (must be the
 * literal first thing in the translation unit). */
#define _DEFAULT_SOURCE

#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/rcp.h>
#include <rcp/request.h>
#include <rcp/scheduler.h>
#include <rcp/tsn.h>

#if defined(__linux__)
#include <sys/socket.h>
#include <unistd.h>
#endif

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

static rcp_bytes_t make_ntscf_frame(rcp_bytes_t acf_msg)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t              frame;

    hdr.sv          = 1;
    hdr.stream_id    = rcp_stream_id_make(kMac, 1);
    frame            = rcp_avtp_encode_ntscf(&hdr, acf_msg.data, acf_msg.len);
    rcp_bytes_free(&acf_msg);
    return frame;
}

static rcp_bytes_t make_standard_frame(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    hdr.byte_bus_id = 3;
    hdr.op          = RCP_ACF_OP_WRITE;
    return make_ntscf_frame(rcp_acf_encode_abb(&hdr, NULL, 0));
}

static rcp_bytes_t make_cancellation_frame(void)
{
    return make_ntscf_frame(rcp_cancel_encode_clear_all(3, 1));
}

/* ── Default config ───────────────────────────────────────────────────────── */

//cfusa:test REQ-TSN-008
static void test_default_config_values(void)
{
    rcp_tsn_config_t cfg = rcp_tsn_default_config();
    rcp_tsn_pcp_map_t default_map = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_MEMORY(&default_map, &cfg.pcp_map, sizeof(default_map));
    TEST_ASSERT_EQUAL_INT(0, cfg.vlan_id);
    TEST_ASSERT_EQUAL_INT(0, cfg.cycle_ns);
}

/* ── PCP map ────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-TSN-001
static void test_default_pcp_map_mirrors_sched_kind_rank(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();
    rcp_sched_kind_t   k;

    for (k = RCP_SCHED_KIND_STANDARD; k <= RCP_SCHED_KIND_CANCELLATION; k++) {
        TEST_ASSERT_EQUAL_UINT8(rcp_sched_kind_rank(k), rcp_tsn_pcp_for(&m, k));
    }
}

//cfusa:test REQ-TSN-001
static void test_cancellation_maps_to_highest_default_pcp(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(6, rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_CANCELLATION));
    TEST_ASSERT_TRUE(rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_CANCELLATION) >
                      rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_STANDARD));
}

//cfusa:test REQ-TSN-002
static void test_pcp_for_fails_safe_on_out_of_range_kind(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_STANDARD),
                             rcp_tsn_pcp_for(&m, (rcp_sched_kind_t)99));
}

/* This test was written to demonstrate `kind < RCP_SCHED_KIND_STANDARD`'s
 * (src/tsn.c L37) own independent contribution to `if (kind <
 * RCP_SCHED_KIND_STANDARD || kind > RCP_SCHED_KIND_CANCELLATION)`, the
 * same defensive/corrupted-state idiom test_pcp_for_fails_safe_on_out_
 * of_range_kind() above uses (tests/test_power.c,
 * tests/test_lifecycle.c). It does not close that gap -- left in
 * because it is still a genuine, meaningful fail-safe assertion (a
 * garbage rcp_sched_kind_t must still resolve to Standard's PCP), and
 * the MC/DC note below explains why no test can close it.
 *
 * MC/DC note (not a full test): `kind < RCP_SCHED_KIND_STANDARD` (0) is
 * structurally unreachable given rcp_sched_kind_t's actual
 * representation. All seven enumerators (scheduler.h) are non-negative,
 * so per C99 §6.7.2.2p4 clang is free to -- and, empirically confirmed
 * against this exact build (`enumtest.c`, same clang/flags, in this
 * session's own scratch investigation), does -- give rcp_sched_kind_t
 * an unsigned underlying representation for same-enum-type comparisons.
 * `(rcp_sched_kind_t)-1` does not read back as a negative value under
 * `<`: its bit pattern is reinterpreted as UINT_MAX, so `kind <
 * RCP_SCHED_KIND_STANDARD` evaluates false (confirmed: this test alone
 * does NOT flip src/tsn.c L37's first MC/DC operand to independent --
 * re-verified by re-running the exported .json after adding it). Every
 * bit pattern an rcp_sched_kind_t object can actually hold compares as
 * unsigned here, and no unsigned value is ever < 0 -- there is no
 * legitimate (non-UB-relying) cast that produces the missing vector.
 * No fake/whitebox test is added to force it. */
//cfusa:test REQ-TSN-002
static void test_pcp_for_fails_safe_on_negative_kind(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_STANDARD),
                             rcp_tsn_pcp_for(&m, (rcp_sched_kind_t)-1));
}

/* ── Frame classification ──────────────────────────────────────────────────── */

//cfusa:test REQ-TSN-003
static void test_classify_standard_frame(void)
{
    rcp_bytes_t frame = make_standard_frame();
    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(frame.data, frame.len));
    rcp_bytes_free(&frame);
}

//cfusa:test REQ-TSN-003
static void test_classify_cancellation_frame(void)
{
    rcp_bytes_t frame = make_cancellation_frame();
    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_CANCELLATION, rcp_tsn_classify_frame(frame.data, frame.len));
    rcp_bytes_free(&frame);
}

/* MC/DC note (not a test): rcp_tsn_classify_frame()'s `if (payload_len
 * == 0 || rcp_acf_peek_msg_type(payload, payload_len, &acf_type) !=
 * RCP_ACF_OK)` (src/tsn.c) has a second operand
 * (rcp_acf_peek_msg_type(...) != RCP_ACF_OK) whose independence cannot
 * be demonstrated -- not merely "hasn't been", but structurally cannot
 * be, at this call site. rcp_acf_peek_msg_type() (src/acf.c) has
 * exactly one failure condition: `if (len < 1) return
 * RCP_ACF_ERR_SHORT_FRAME;`. It is only ever called here after the
 * first operand (`payload_len == 0`) has already been false, i.e. only
 * when payload_len >= 1 (payload_len is size_t, so "!= 0" and ">= 1"
 * are the same fact) -- which is exactly the one condition under which
 * rcp_acf_peek_msg_type()'s own `len < 1` guard can never trip. So
 * whenever this decision's second operand is actually evaluated, it is
 * always RCP_ACF_OK, i.e. always false: this call site can never
 * observe rcp_acf_peek_msg_type() fail. No test (real or fault-
 * injected) can produce the missing vector, because doing so would
 * require calling rcp_acf_peek_msg_type() with a length that is
 * simultaneously >= 1 (to reach it past the first operand) and < 1 (to
 * fail it) -- a contradiction. This is genuine dead-branch defensive
 * code (harmless, and arguably worth keeping in case
 * rcp_acf_peek_msg_type()'s own contract ever grows a second failure
 * mode), not a testing gap; no fake/whitebox test is added to force it. */

//cfusa:test REQ-TSN-003
static void test_classify_malformed_frame_fails_safe_to_standard(void)
{
    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(NULL, 0));

    {
        uint8_t junk[] = {0xFF, 0xFF, 0xFF, 0xFF};
        TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(junk, sizeof(junk)));
    }
}

/* Every remaining rcp_tsn_classify_frame() fail-safe-to-Standard return
 * (issue #520 category 2): a valid subtype byte but a body too short for
 * rcp_avtp_decode_ntscf() itself to parse -- distinct from the malformed
 * (non-NTSCF/TSCF-subtype) junk bytes test_classify_malformed_frame_...
 * above already covers. */
//cfusa:test REQ-TSN-003
static void test_classify_ntscf_decode_failure_fails_safe_to_standard(void)
{
    rcp_bytes_t frame = make_standard_frame();
    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(frame.data, 1));
    rcp_bytes_free(&frame);
}

/* The TSCF-subtype branch (src/tsn.c) was previously entirely unexercised
 * -- every other classify test builds an NTSCF-wrapped frame. A valid TSCF
 * frame with no ACF body decodes successfully (payload_len == 0), which
 * itself fails the very next guard (payload_len == 0 -> fail-safe
 * Standard), covering both the TSCF success branch and that guard in one
 * frame. */
//cfusa:test REQ-TSN-003
static void test_classify_tscf_frame_with_empty_body_fails_safe_to_standard(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;

    hdr.stream_id = rcp_stream_id_make(kMac, 1);
    frame = rcp_avtp_encode_tscf(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(frame.data, frame.len));

    rcp_bytes_free(&frame);
}

/* TSCF's own decode failure, mirroring the NTSCF case above. */
//cfusa:test REQ-TSN-003
static void test_classify_tscf_decode_failure_fails_safe_to_standard(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;

    hdr.stream_id = rcp_stream_id_make(kMac, 1);
    frame = rcp_avtp_encode_tscf(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(frame.data, 1));

    rcp_bytes_free(&frame);
}

/* A genuine, NTSCF-wrapped ACF_GBB frame that is *not* the
 * repurposed-compound-request encoding rcp_compound_peek_request_type()
 * looks for -- an ordinary timed endpoint response (mtv =
 * RCP_ACF_MTV_VALID, matching rcp_ep_gpio_encode_response()'s own timed
 * variant, src/ep_gpio.c), built directly via rcp_acf_encode_gbb() and
 * make_ntscf_frame() (not rcp_ep_gpio_encode_response() itself, which
 * returns a *raw* ACF_GBB payload with no NTSCF/TSCF framing at all --
 * ep_*.h's encode_response() functions are meant to be wrapped by
 * adapt.c's own build_frame(), not fed to rcp_tsn_classify_frame()
 * directly). RCP_ACF_MTV_VALID is not the RCP_ACF_MTV_UNTIMED marker the
 * compound-request repurposing trick requires, so
 * rcp_compound_peek_request_type() correctly reports
 * RCP_COMPOUND_ERR_NOT_REPURPOSED and classification fails safe. */
//cfusa:test REQ-TSN-003
static void test_classify_non_repurposed_gbb_frame_fails_safe_to_standard(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    uint8_t payload[4] = {0, 0, 0, 0};
    rcp_bytes_t acf_msg;
    rcp_bytes_t frame;

    hdr.info.byte_bus_id = 3;
    hdr.info.op           = RCP_ACF_OP_READ;
    hdr.info.rsp          = 1;
    hdr.info.mtv          = RCP_ACF_MTV_VALID;
    hdr.message_timestamp = 12345;

    acf_msg = rcp_acf_encode_gbb(&hdr, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(acf_msg.data);

    frame = make_ntscf_frame(acf_msg); /* frees acf_msg internally */
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(frame.data, frame.len));

    rcp_bytes_free(&frame);
}

/* ── Transport wrapper ─────────────────────────────────────────────────────── */

//cfusa:test REQ-TSN-004
static void test_send_applies_pcp_then_delegates_to_inner(void)
{
    rcp_avtp_transport_t *inner = rcp_avtp_loopback_transport_new(true, 4);
    /* fd = -1 -> SO_PRIORITY is skipped, send still delegates. */
    rcp_avtp_transport_t *tsn = rcp_tsn_avtp_transport_new(inner, -1, rcp_tsn_default_config());
    rcp_bytes_t             frame = make_cancellation_frame();
    rcp_context_t            ctx = rcp_context_background();
    uint8_t                  buf[128];
    size_t                    out_len = 0;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(tsn, frame.data, frame.len));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(inner, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(frame.len, out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, buf, frame.len);

    rcp_bytes_free(&frame);
    rcp_avtp_transport_release(tsn);
    rcp_avtp_transport_release(inner);
}

#if defined(__linux__)
/* REQ-TSN-004's own SO_PRIORITY claim: every other test in this file
 * passes socket_fd=-1, deliberately skipping the setsockopt() call
 * entirely (see test_send_applies_pcp_then_delegates_to_inner()'s own
 * comment) -- so nothing here has ever actually proven tsn_send() sets
 * SO_PRIORITY on a real socket to the PCP value the frame's own
 * classification maps to. SO_PRIORITY is a glibc/Linux-only setsockopt
 * option (tsn.c's own RCP_TSN_SO_PRIORITY guard), so this test -- and
 * its getsockopt() verification -- only compiles/runs on this repo's
 * own ubuntu CI jobs. */
//cfusa:test REQ-TSN-004
static void test_send_sets_so_priority_to_the_frames_own_pcp(void)
{
    rcp_avtp_transport_t *inner = rcp_avtp_loopback_transport_new(true, 4);
    rcp_tsn_config_t         cfg = rcp_tsn_default_config();
    int                        real_fd;
    rcp_avtp_transport_t     *tsn;
    rcp_bytes_t                frame = make_cancellation_frame();
    rcp_context_t              ctx = rcp_context_background();
    uint8_t                    buf[128];
    size_t                      out_len = 0;
    int                          got_priority = -1;
    socklen_t                    got_len = sizeof(got_priority);
    int                          expected_pcp =
        (int)rcp_tsn_pcp_for(&cfg.pcp_map, RCP_SCHED_KIND_CANCELLATION);

    real_fd = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_TRUE(real_fd >= 0);

    tsn = rcp_tsn_avtp_transport_new(inner, real_fd, cfg);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(tsn, frame.data, frame.len));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(inner, &ctx, buf, sizeof(buf), &out_len));

    TEST_ASSERT_EQUAL_INT(0, getsockopt(real_fd, SOL_SOCKET, SO_PRIORITY, &got_priority, &got_len));
    TEST_ASSERT_EQUAL_INT(expected_pcp, got_priority);

    rcp_bytes_free(&frame);
    rcp_avtp_transport_release(tsn);
    rcp_avtp_transport_release(inner);
    close(real_fd);
}
#endif /* __linux__ */

//cfusa:test REQ-TSN-005
static void test_recv_delegates_to_inner(void)
{
    rcp_avtp_transport_t *inner = rcp_avtp_loopback_transport_new(true, 4);
    rcp_avtp_transport_t *tsn = rcp_tsn_avtp_transport_new(inner, -1, rcp_tsn_default_config());
    rcp_context_t            ctx = rcp_context_background();
    uint8_t                  frame[] = {0x01, 0x02, 0x03};
    uint8_t                  buf[16];
    size_t                    out_len = 0;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(inner, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(tsn, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, buf, sizeof(frame));

    rcp_avtp_transport_release(tsn);
    rcp_avtp_transport_release(inner);
}

//cfusa:test REQ-TSN-006
static void test_close_delegates_to_inner(void)
{
    rcp_avtp_transport_t *inner = rcp_avtp_loopback_transport_new(true, 4);
    rcp_avtp_transport_t *tsn = rcp_tsn_avtp_transport_new(inner, -1, rcp_tsn_default_config());
    rcp_bytes_t              frame = make_standard_frame();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(tsn));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(inner, frame.data, frame.len));

    rcp_bytes_free(&frame);
    rcp_avtp_transport_release(tsn);
    rcp_avtp_transport_release(inner);
}

//cfusa:test REQ-TSN-007
static void test_constructor_mirrors_inner_time_sync_supported(void)
{
    rcp_avtp_transport_t *inner = rcp_avtp_loopback_transport_new(false, 1);
    rcp_avtp_transport_t *tsn = rcp_tsn_avtp_transport_new(inner, -1, rcp_tsn_default_config());

    TEST_ASSERT_FALSE(tsn->time_sync_supported);

    rcp_avtp_transport_release(tsn);
    rcp_avtp_transport_release(inner);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_config_values);
    RUN_TEST(test_default_pcp_map_mirrors_sched_kind_rank);
    RUN_TEST(test_cancellation_maps_to_highest_default_pcp);
    RUN_TEST(test_pcp_for_fails_safe_on_out_of_range_kind);
    RUN_TEST(test_pcp_for_fails_safe_on_negative_kind);
    RUN_TEST(test_classify_standard_frame);
    RUN_TEST(test_classify_cancellation_frame);
    RUN_TEST(test_classify_malformed_frame_fails_safe_to_standard);
    RUN_TEST(test_classify_ntscf_decode_failure_fails_safe_to_standard);
    RUN_TEST(test_classify_tscf_frame_with_empty_body_fails_safe_to_standard);
    RUN_TEST(test_classify_tscf_decode_failure_fails_safe_to_standard);
    RUN_TEST(test_classify_non_repurposed_gbb_frame_fails_safe_to_standard);
    RUN_TEST(test_send_applies_pcp_then_delegates_to_inner);
#if defined(__linux__)
    RUN_TEST(test_send_sets_so_priority_to_the_frames_own_pcp);
#endif
    RUN_TEST(test_recv_delegates_to_inner);
    RUN_TEST(test_close_delegates_to_inner);
    RUN_TEST(test_constructor_mirrors_inner_time_sync_supported);

    return UNITY_END();
}
