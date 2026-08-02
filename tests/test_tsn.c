/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-TSN-001
//cfusa:test REQ-TSN-002
//cfusa:test REQ-TSN-003
//cfusa:test REQ-TSN-004
//cfusa:test REQ-TSN-005
//cfusa:test REQ-TSN-006
//cfusa:test REQ-TSN-007
//cfusa:test REQ-TSN-008
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/rcp.h>
#include <rcp/request_cancel.h>
#include <rcp/scheduler.h>
#include <rcp/tsn.h>

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

static void test_default_pcp_map_mirrors_sched_kind_rank(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();
    rcp_sched_kind_t   k;

    for (k = RCP_SCHED_KIND_STANDARD; k <= RCP_SCHED_KIND_CANCELLATION; k++) {
        TEST_ASSERT_EQUAL_UINT8(rcp_sched_kind_rank(k), rcp_tsn_pcp_for(&m, k));
    }
}

static void test_cancellation_maps_to_highest_default_pcp(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(6, rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_CANCELLATION));
    TEST_ASSERT_TRUE(rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_CANCELLATION) >
                      rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_STANDARD));
}

static void test_pcp_for_fails_safe_on_out_of_range_kind(void)
{
    rcp_tsn_pcp_map_t m = rcp_tsn_default_pcp_map();

    TEST_ASSERT_EQUAL_UINT8(rcp_tsn_pcp_for(&m, RCP_SCHED_KIND_STANDARD),
                             rcp_tsn_pcp_for(&m, (rcp_sched_kind_t)99));
}

/* ── Frame classification ──────────────────────────────────────────────────── */

static void test_classify_standard_frame(void)
{
    rcp_bytes_t frame = make_standard_frame();
    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(frame.data, frame.len));
    rcp_bytes_free(&frame);
}

static void test_classify_cancellation_frame(void)
{
    rcp_bytes_t frame = make_cancellation_frame();
    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_CANCELLATION, rcp_tsn_classify_frame(frame.data, frame.len));
    rcp_bytes_free(&frame);
}

static void test_classify_malformed_frame_fails_safe_to_standard(void)
{
    TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(NULL, 0));

    {
        uint8_t junk[] = {0xFF, 0xFF, 0xFF, 0xFF};
        TEST_ASSERT_EQUAL(RCP_SCHED_KIND_STANDARD, rcp_tsn_classify_frame(junk, sizeof(junk)));
    }
}

/* ── Transport wrapper ─────────────────────────────────────────────────────── */

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
    RUN_TEST(test_classify_standard_frame);
    RUN_TEST(test_classify_cancellation_frame);
    RUN_TEST(test_classify_malformed_frame_fails_safe_to_standard);
    RUN_TEST(test_send_applies_pcp_then_delegates_to_inner);
    RUN_TEST(test_recv_delegates_to_inner);
    RUN_TEST(test_close_delegates_to_inner);
    RUN_TEST(test_constructor_mirrors_inner_time_sync_supported);

    return UNITY_END();
}
