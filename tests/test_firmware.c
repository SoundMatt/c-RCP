//cfusa:test REQ-FW-001
//cfusa:test REQ-FW-002
//cfusa:test REQ-FW-003
//cfusa:test REQ-FW-004
//cfusa:test REQ-FW-005
//cfusa:test REQ-FW-006
//cfusa:test REQ-FW-007
//cfusa:test REQ-FW-008
#include "unity.h"

#include <rcp/firmware.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_controller_t *make_mock(void)
{
    return rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
}

static void test_session_starts_idle(void)
{
    rcp_controller_t *ctrl = make_mock();
    rcp_firmware_session_t *s = rcp_firmware_session_new(ctrl, rcp_fw_default_config());

    TEST_ASSERT_EQUAL(RCP_FW_STATE_IDLE, rcp_firmware_session_state(s));

    rcp_firmware_session_destroy(s);
    rcp_controller_release(ctrl);
}

static void test_initiate_transitions_to_initiated(void)
{
    rcp_controller_t *ctrl = make_mock();
    rcp_firmware_session_t *s = rcp_firmware_session_new(ctrl, rcp_fw_default_config());
    rcp_context_t ctx = rcp_context_background();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_initiate(s, &ctx, "1.2.3"));
    TEST_ASSERT_EQUAL(RCP_FW_STATE_INITIATED, rcp_firmware_session_state(s));

    rcp_firmware_session_destroy(s);
    rcp_controller_release(ctrl);
}

static void test_double_initiate_fails(void)
{
    rcp_controller_t *ctrl = make_mock();
    rcp_firmware_session_t *s = rcp_firmware_session_new(ctrl, rcp_fw_default_config());
    rcp_context_t ctx = rcp_context_background();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_initiate(s, &ctx, "1.0"));
    TEST_ASSERT_EQUAL(RCP_FW_ERR_BAD_STATE, rcp_firmware_session_initiate(s, &ctx, "1.0"));

    rcp_firmware_session_destroy(s);
    rcp_controller_release(ctrl);
}

static void test_transfer_requires_initiated_state(void)
{
    rcp_controller_t *ctrl = make_mock();
    rcp_firmware_session_t *s = rcp_firmware_session_new(ctrl, rcp_fw_default_config());
    rcp_context_t ctx = rcp_context_background();
    uint8_t image[8192];

    memset(image, 0xAA, sizeof(image));
    TEST_ASSERT_EQUAL(RCP_FW_ERR_BAD_STATE, rcp_firmware_session_transfer(s, &ctx, image, sizeof(image), NULL, NULL));

    rcp_firmware_session_destroy(s);
    rcp_controller_release(ctrl);
}

static void count_progress(const rcp_fw_progress_t *p, void *user_data)
{
    size_t *calls = (size_t *)user_data;
    (void)p;
    (*calls)++;
}

static void test_full_happy_path(void)
{
    rcp_controller_t *ctrl = make_mock();
    rcp_firmware_session_t *s = rcp_firmware_session_new(ctrl, rcp_fw_default_config());
    rcp_context_t ctx = rcp_context_background();
    uint8_t image[4096];
    size_t progress_calls = 0;

    memset(image, 0xFF, sizeof(image));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_initiate(s, &ctx, "2.0.0"));
    TEST_ASSERT_EQUAL(RCP_FW_STATE_INITIATED, rcp_firmware_session_state(s));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_transfer(s, &ctx, image, sizeof(image), count_progress, &progress_calls));
    TEST_ASSERT_EQUAL_UINT(1, progress_calls); /* 4096 bytes / 4096 chunk_size = 1 chunk */

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_verify(s, &ctx));
    TEST_ASSERT_EQUAL(RCP_FW_STATE_VERIFYING, rcp_firmware_session_state(s));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_activate(s, &ctx));
    TEST_ASSERT_EQUAL(RCP_FW_STATE_ACTIVATED, rcp_firmware_session_state(s));

    rcp_firmware_session_destroy(s);
    rcp_controller_release(ctrl);
}

typedef struct {
    size_t calls;
    size_t last_total_chunks;
    size_t last_chunk_index;
} progress_track_t;

static void track_progress(const rcp_fw_progress_t *p, void *user_data)
{
    progress_track_t *t = (progress_track_t *)user_data;
    t->calls++;
    t->last_total_chunks = p->total_chunks;
    t->last_chunk_index  = p->chunk_index;
    TEST_ASSERT_EQUAL_UINT(4096, p->total_bytes);
}

static void test_transfer_chunks_image_with_per_chunk_progress(void)
{
    rcp_controller_t *ctrl = make_mock();
    rcp_fw_config_t cfg = rcp_fw_default_config();
    rcp_firmware_session_t *s;
    rcp_context_t ctx = rcp_context_background();
    uint8_t image[4096];
    progress_track_t t = {0, 0, 0};

    cfg.chunk_size = 1024;
    s = rcp_firmware_session_new(ctrl, cfg);
    memset(image, 0x5A, sizeof(image));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_initiate(s, &ctx, "3.0.0"));

    /* 4096 bytes / 1024 chunk_size = exactly 4 chunks. */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_transfer(s, &ctx, image, sizeof(image), track_progress, &t));
    TEST_ASSERT_EQUAL_UINT(4, t.calls);             /* one progress callback per chunk (REQ-FW-008) */
    TEST_ASSERT_EQUAL_UINT(4, t.last_total_chunks); /* image split into chunk_size segments (REQ-FW-006) */
    TEST_ASSERT_EQUAL_UINT(4, t.last_chunk_index);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_verify(s, &ctx));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_activate(s, &ctx));

    rcp_firmware_session_destroy(s);
    rcp_controller_release(ctrl);
}

static void test_rollback_resets_to_idle(void)
{
    rcp_controller_t *ctrl = make_mock();
    rcp_firmware_session_t *s = rcp_firmware_session_new(ctrl, rcp_fw_default_config());
    rcp_context_t ctx = rcp_context_background();
    uint8_t image[1] = {0};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_initiate(s, &ctx, "1.0"));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_transfer(s, &ctx, image, sizeof(image), NULL, NULL));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_verify(s, &ctx));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_activate(s, &ctx));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_firmware_session_rollback(s, &ctx));
    TEST_ASSERT_EQUAL(RCP_FW_STATE_IDLE, rcp_firmware_session_state(s));

    rcp_firmware_session_destroy(s);
    rcp_controller_release(ctrl);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_session_starts_idle);
    RUN_TEST(test_initiate_transitions_to_initiated);
    RUN_TEST(test_double_initiate_fails);
    RUN_TEST(test_transfer_requires_initiated_state);
    RUN_TEST(test_full_happy_path);
    RUN_TEST(test_transfer_chunks_image_with_per_chunk_progress);
    RUN_TEST(test_rollback_resets_to_idle);

    return UNITY_END();
}
