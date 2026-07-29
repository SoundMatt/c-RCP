//cfusa:test REQ-SHMEM-001
//cfusa:test REQ-SHMEM-002
//cfusa:test REQ-SHMEM-003
//cfusa:test REQ-SHMEM-004
//cfusa:test REQ-SHMEM-005
//cfusa:test REQ-SHMEM-006
//cfusa:test REQ-SHMEM-007
//cfusa:test REQ-SHMEM-008
//cfusa:test REQ-SHMEM-009
#include "unity.h"

#include <rcp/avtp.h>
#include <rcp/rcp.h>
#include <rcp/shmem.h>

void setUp(void) {}
void tearDown(void) {}

//cfusa:test REQ-SHMEM-001
static void test_pair_new_returns_two_usable_sides(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 4, &a, &b));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-002
static void test_a_send_delivers_to_b_recv_in_fifo_order(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    rcp_context_t           ctx = rcp_context_background();
    uint8_t                 frame1[] = {0x01, 0x02};
    uint8_t                 frame2[] = {0x03, 0x04, 0x05};
    uint8_t                 buf[16];
    size_t                   out_len = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 4, &a, &b));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(a, frame1, sizeof(frame1)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(a, frame2, sizeof(frame2)));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(b, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame1), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame1, buf, sizeof(frame1));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(b, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame2), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame2, buf, sizeof(frame2));

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-003
static void test_b_send_delivers_to_a_recv(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    rcp_context_t           ctx = rcp_context_background();
    uint8_t                 frame[] = {0xAA, 0xBB};
    uint8_t                 buf[16];
    size_t                   out_len = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &a, &b));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(b, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(a, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, buf, sizeof(frame));

    /* Same direction check as the a->b test above, but confirms the
     * reverse queue does not also see it (no cross-talk). */
    {
        rcp_context_t short_ctx = rcp_context_with_timeout_ms(20);
        TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT,
                           rcp_avtp_transport_recv(b, &short_ctx, buf, sizeof(buf), &out_len));
    }

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-004
static void test_recv_times_out_when_empty(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    rcp_context_t           ctx = rcp_context_with_timeout_ms(20);
    uint8_t                 buf[16];
    size_t                   out_len = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 2, &a, &b));

    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_avtp_transport_recv(a, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-005
static void test_send_recv_on_closed_side_returns_closed(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    rcp_context_t           ctx = rcp_context_background();
    uint8_t                 frame[] = {0x01};
    uint8_t                 buf[16];
    size_t                   out_len = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 2, &a, &b));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(a));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(a, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_recv(a, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-005
static void test_recv_on_open_side_returns_closed_once_peer_closes_and_drains(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    rcp_context_t           ctx = rcp_context_background();
    uint8_t                 frame[] = {0x07};
    uint8_t                 buf[16];
    size_t                   out_len = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 2, &a, &b));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(a, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(a));

    /* b still drains what a already sent before a closed... */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(b, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), out_len);

    /* ...and only reports closed once that queue is actually empty. */
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_recv(b, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-006
static void test_send_rejects_when_own_queue_full(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    uint8_t                 frame[] = {0x01};

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 1, &a, &b));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(a, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_avtp_transport_send(a, frame, sizeof(frame)));

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-007
static void test_recv_into_too_small_buffer_leaves_item_queued(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    rcp_context_t           ctx = rcp_context_background();
    uint8_t                 frame[] = {0x01, 0x02, 0x03};
    uint8_t                 tiny[1];
    uint8_t                 big[16];
    size_t                   out_len = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 2, &a, &b));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(a, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_avtp_transport_recv(b, &ctx, tiny, sizeof(tiny), &out_len));

    /* Still there -- a big-enough buffer retrieves it on the next call. */
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(b, &ctx, big, sizeof(big), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, big, sizeof(frame));

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-008
static void test_releasing_one_side_leaves_other_usable_until_closed(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;
    rcp_context_t           ctx = rcp_context_background();
    uint8_t                 frame[] = {0x09};
    uint8_t                 buf[16];
    size_t                   out_len = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 2, &a, &b));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(a, frame, sizeof(frame)));
    rcp_avtp_transport_release(a); /* a's own last reference gone; b unaffected */

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(b, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame), out_len);

    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-009
static void test_pair_state_freed_after_releasing_both_sides(void)
{
    /* No public way to observe the shared core's own lifetime directly;
     * this pins the externally-visible contract instead: releasing both
     * sides, in either order, neither crashes nor leaks (verified by
     * this suite's own ASan/UBSan run per this project's stated
     * verification practice, not by an in-test assertion). */
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 2, &a, &b));
    rcp_avtp_transport_release(b);
    rcp_avtp_transport_release(a);

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 2, &a, &b));
    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

//cfusa:test REQ-SHMEM-001
static void test_pair_shares_time_sync_supported(void)
{
    rcp_avtp_transport_t *a = NULL;
    rcp_avtp_transport_t *b = NULL;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(true, 1, &a, &b));
    TEST_ASSERT_TRUE(a->time_sync_supported);
    TEST_ASSERT_TRUE(b->time_sync_supported);
    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pair_new_returns_two_usable_sides);
    RUN_TEST(test_a_send_delivers_to_b_recv_in_fifo_order);
    RUN_TEST(test_b_send_delivers_to_a_recv);
    RUN_TEST(test_recv_times_out_when_empty);
    RUN_TEST(test_send_recv_on_closed_side_returns_closed);
    RUN_TEST(test_recv_on_open_side_returns_closed_once_peer_closes_and_drains);
    RUN_TEST(test_send_rejects_when_own_queue_full);
    RUN_TEST(test_recv_into_too_small_buffer_leaves_item_queued);
    RUN_TEST(test_releasing_one_side_leaves_other_usable_until_closed);
    RUN_TEST(test_pair_state_freed_after_releasing_both_sides);
    RUN_TEST(test_pair_shares_time_sync_supported);

    return UNITY_END();
}
