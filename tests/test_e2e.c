//cfusa:test REQ-E2E-001
//cfusa:test REQ-E2E-002
//cfusa:test REQ-E2E-003
//cfusa:test REQ-E2E-004
//cfusa:test REQ-E2E-005
//cfusa:test REQ-E2E-006
//cfusa:test REQ-E2E-007
//cfusa:test REQ-E2E-008
//cfusa:test REQ-E2E-009
//cfusa:test REQ-E2E-010
//cfusa:test REQ-E2E-011
//cfusa:test REQ-E2E-012
#include "unity.h"

#include <rcp/e2e.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── wrap / unwrap round-trip ─────────────────────────────────────────────── */

static void test_wrap_unwrap_round_trip_preserves_payload(void)
{
    uint8_t payload[] = {1, 2, 3, 4, 5};
    rcp_bytes_t frame = rcp_e2e_wrap(1, payload, sizeof(payload));
    uint32_t seq_out;
    rcp_bytes_t payload_out;

    TEST_ASSERT_EQUAL_UINT(sizeof(payload) + RCP_E2E_HEADER_LEN, frame.len);

    TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_unwrap(frame.data, frame.len, &seq_out, &payload_out));
    TEST_ASSERT_EQUAL_UINT32(1, seq_out);
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), payload_out.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, payload_out.data, sizeof(payload));

    rcp_bytes_free(&payload_out);
    rcp_bytes_free(&frame);
}

static void test_wrap_produces_frame_with_header_prepended(void)
{
    uint8_t payload[] = {0xAB, 0xCD};
    rcp_bytes_t frame = rcp_e2e_wrap(0xCAFEBABEu, payload, sizeof(payload));
    uint32_t seq;

    TEST_ASSERT_EQUAL_UINT(sizeof(payload) + RCP_E2E_HEADER_LEN, frame.len);

    seq = ((uint32_t)frame.data[0] << 24) | ((uint32_t)frame.data[1] << 16)
        | ((uint32_t)frame.data[2] << 8)  |  (uint32_t)frame.data[3];
    TEST_ASSERT_EQUAL_UINT32(0xCAFEBABEu, seq);

    rcp_bytes_free(&frame);
}

static void test_unwrap_rejects_truncated_frame(void)
{
    uint8_t short_frame[RCP_E2E_HEADER_LEN - 1] = {0};
    uint32_t seq;
    rcp_bytes_t out;

    TEST_ASSERT_EQUAL(RCP_E2E_ERR_SHORT_FRAME, rcp_e2e_unwrap(short_frame, sizeof(short_frame), &seq, &out));
    TEST_ASSERT_NULL(out.data);
}

static void test_unwrap_rejects_corrupt_crc(void)
{
    uint8_t payload[] = {1, 2, 3};
    rcp_bytes_t frame = rcp_e2e_wrap(1, payload, sizeof(payload));
    uint32_t seq;
    rcp_bytes_t out;

    frame.data[frame.len - 1] ^= 0xFF; /* corrupt last byte (CRC) */

    TEST_ASSERT_EQUAL(RCP_E2E_ERR_CRC_MISMATCH, rcp_e2e_unwrap(frame.data, frame.len, &seq, &out));

    rcp_bytes_free(&frame);
}

/* ── ReplayGuard ──────────────────────────────────────────────────────────── */

static void test_replay_guard_accepts_fresh_sequence_numbers(void)
{
    rcp_e2e_replay_guard_t *guard = rcp_e2e_replay_guard_new();

    TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_replay_guard_check(guard, 1));
    TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_replay_guard_check(guard, 2));
    TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_replay_guard_check(guard, 3));

    rcp_e2e_replay_guard_destroy(guard);
}

static void test_replay_guard_rejects_duplicates(void)
{
    rcp_e2e_replay_guard_t *guard = rcp_e2e_replay_guard_new();

    TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_replay_guard_check(guard, 10));
    TEST_ASSERT_EQUAL(RCP_E2E_ERR_REPLAY, rcp_e2e_replay_guard_check(guard, 10));

    rcp_e2e_replay_guard_destroy(guard);
}

static void test_replay_guard_rejects_old_seq_outside_window(void)
{
    rcp_e2e_replay_guard_t *guard = rcp_e2e_replay_guard_new();
    uint32_t i;

    for (i = 1; i <= RCP_E2E_REPLAY_WINDOW_SIZE + 5; i++) {
        TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_replay_guard_check(guard, i));
    }
    /* seq=1 is now outside the window. */
    TEST_ASSERT_EQUAL(RCP_E2E_ERR_REPLAY, rcp_e2e_replay_guard_check(guard, 1));

    rcp_e2e_replay_guard_destroy(guard);
}

static void test_replay_guard_accepts_seq_zero_exactly_once(void)
{
    rcp_e2e_replay_guard_t *guard = rcp_e2e_replay_guard_new();

    TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_replay_guard_check(guard, 0));
    TEST_ASSERT_EQUAL(RCP_E2E_ERR_REPLAY, rcp_e2e_replay_guard_check(guard, 0));

    rcp_e2e_replay_guard_destroy(guard);
}

/* ── Controller wrapper ───────────────────────────────────────────────────── */

static void unwrap_capture_handler(const rcp_command_t *cmd, rcp_response_t *out, void *user_data)
{
    uint32_t *last_seq = (uint32_t *)user_data;
    rcp_bytes_t payload_out;

    TEST_ASSERT_EQUAL(RCP_E2E_OK, rcp_e2e_unwrap(cmd->payload.data, cmd->payload.len, last_seq, &payload_out));
    rcp_bytes_free(&payload_out);

    out->command_id = cmd->id;
    out->zone       = cmd->zone;
    out->status     = RCP_RESPONSE_OK;
}

static void test_controller_wraps_payload_with_incrementing_seq(void)
{
    uint32_t last_seq = 0;
    rcp_controller_t *inner = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, unwrap_capture_handler, &last_seq);
    rcp_controller_t *ctrl  = rcp_e2e_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};
    uint8_t payload[] = {9, 9};

    cmd.zone         = RCP_ZONE_FRONT_LEFT;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL_UINT32(1, last_seq);
    rcp_response_free(&resp);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_send(ctrl, &ctx, &cmd, &resp));
    TEST_ASSERT_EQUAL_UINT32(2, last_seq); /* increments per send */
    rcp_response_free(&resp);

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_e2e_ctrl_zone_delegates_to_inner(void)
{
    rcp_controller_t *inner = rcp_mock_controller_new(RCP_ZONE_REAR_LEFT, NULL, NULL);
    rcp_controller_t *ctrl = rcp_e2e_controller_new(inner);

    TEST_ASSERT_EQUAL(RCP_ZONE_REAR_LEFT, rcp_controller_zone(ctrl));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_e2e_ctrl_subscribe_delegates_to_inner(void)
{
    rcp_controller_t *inner = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrl = rcp_e2e_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_subscribe(ctrl, &ctx, &ch));
    TEST_ASSERT_NOT_NULL(ch);

    rcp_status_channel_release(ch);
    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_e2e_ctrl_close_delegates_to_inner(void)
{
    rcp_controller_t *inner = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
    rcp_controller_t *ctrl = rcp_e2e_controller_new(inner);
    rcp_context_t ctx = rcp_context_background();
    rcp_command_t cmd = {0};
    rcp_response_t resp = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_controller_close(ctrl));

    /* Confirm close() really reached the inner controller, not just the
     * wrapper -- sending through the inner directly must now fail. */
    cmd.zone = RCP_ZONE_FRONT_LEFT;
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_controller_send(inner, &ctx, &cmd, &resp));

    rcp_controller_release(ctrl);
    rcp_controller_release(inner);
}

static void test_e2e_strerror_unique_nonempty(void)
{
    const rcp_e2e_errc_t codes[] = {
        RCP_E2E_OK, RCP_E2E_ERR_CRC_MISMATCH,
        RCP_E2E_ERR_SHORT_FRAME, RCP_E2E_ERR_REPLAY,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_e2e_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(s, rcp_e2e_strerror(codes[j])) != 0 ? 1 : 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_wrap_unwrap_round_trip_preserves_payload);
    RUN_TEST(test_wrap_produces_frame_with_header_prepended);
    RUN_TEST(test_unwrap_rejects_truncated_frame);
    RUN_TEST(test_unwrap_rejects_corrupt_crc);
    RUN_TEST(test_replay_guard_accepts_fresh_sequence_numbers);
    RUN_TEST(test_replay_guard_rejects_duplicates);
    RUN_TEST(test_replay_guard_rejects_old_seq_outside_window);
    RUN_TEST(test_replay_guard_accepts_seq_zero_exactly_once);
    RUN_TEST(test_controller_wraps_payload_with_incrementing_seq);
    RUN_TEST(test_e2e_ctrl_zone_delegates_to_inner);
    RUN_TEST(test_e2e_ctrl_subscribe_delegates_to_inner);
    RUN_TEST(test_e2e_ctrl_close_delegates_to_inner);
    RUN_TEST(test_e2e_strerror_unique_nonempty);

    return UNITY_END();
}
