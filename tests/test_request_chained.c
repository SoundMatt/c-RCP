/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-CHAIN-001
//cfusa:test REQ-CHAIN-002
//cfusa:test REQ-CHAIN-003
//cfusa:test REQ-CHAIN-004
//cfusa:test REQ-CHAIN-005
//cfusa:test REQ-CHAIN-006
//cfusa:test REQ-CHAIN-007
//cfusa:test REQ-CHAIN-008
//cfusa:test REQ-CHAIN-009
//cfusa:test REQ-CHAIN-010
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/request_chained.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    const char *a = rcp_chained_strerror(RCP_CHAINED_OK);
    const char *b = rcp_chained_strerror(RCP_CHAINED_ERR_SHORT_FRAME);
    const char *c = rcp_chained_strerror(RCP_CHAINED_ERR_BAD_MSG_TYPE);
    const char *d = rcp_chained_strerror(RCP_CHAINED_ERR_NOT_REPURPOSED);
    const char *e = rcp_chained_strerror(RCP_CHAINED_ERR_UNKNOWN_TYPE);
    const char *f = rcp_chained_strerror(RCP_CHAINED_ERR_TOO_FEW_MEMBERS);
    const char *g = rcp_chained_strerror(RCP_CHAINED_ERR_POSITION_OUT_OF_RANGE);
    const char *unk = rcp_chained_strerror((rcp_chained_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
    TEST_ASSERT_TRUE(strcmp(c, d) != 0);
    TEST_ASSERT_TRUE(strcmp(d, e) != 0);
    TEST_ASSERT_TRUE(strcmp(e, f) != 0);
    TEST_ASSERT_TRUE(strcmp(f, g) != 0);
}

/* ── Chain member encode/decode ───────────────────────────────────────────── */

static void test_member_round_trip(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint8_t chain_length = 0;
    uint8_t chain_position = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;
    uint8_t body[2] = {0x11u, 0x22u};

    frame = rcp_chained_encode_member(3, 4, 2, RCP_CHAINED_CS_ABORT_ON_ERROR, 55, body,
                                       sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_OK,
                           rcp_chained_decode_member(frame.data, frame.len, &bbid, &chain_length,
                                                      &chain_position, &cs, &payload, &payload_len,
                                                      &txn));

    TEST_ASSERT_EQUAL_UINT8(3, bbid);
    TEST_ASSERT_EQUAL_UINT8(4, chain_length);
    TEST_ASSERT_EQUAL_UINT8(2, chain_position);
    TEST_ASSERT_EQUAL_UINT8(RCP_CHAINED_CS_ABORT_ON_ERROR, cs);
    TEST_ASSERT_EQUAL_UINT8(55, txn);
    TEST_ASSERT_EQUAL_size_t(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    rcp_bytes_free(&frame);
}

static void test_member_round_trip_continue_on_error(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint8_t chain_length = 0;
    uint8_t chain_position = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_chained_encode_member(1, 2, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_OK,
                           rcp_chained_decode_member(frame.data, frame.len, &bbid, &chain_length,
                                                      &chain_position, &cs, &payload, &payload_len,
                                                      &txn));
    TEST_ASSERT_EQUAL_UINT8(RCP_CHAINED_CS_CONTINUE_ON_ERROR, cs);
    TEST_ASSERT_EQUAL_size_t(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_encode_rejects_too_few_members(void)
{
    rcp_bytes_t frame = rcp_chained_encode_member(0, 1, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 0,
                                                   NULL, 0);
    TEST_ASSERT_NULL(frame.data);
}

static void test_encode_rejects_position_out_of_range(void)
{
    rcp_bytes_t frame = rcp_chained_encode_member(0, 2, 2, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 0,
                                                   NULL, 0);
    TEST_ASSERT_NULL(frame.data);
}

static void test_decode_rejects_short_frame(void)
{
    uint8_t buf[4] = {0};
    rcp_byte_bus_id_t bbid = 0;
    uint8_t chain_length = 0;
    uint8_t chain_position = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_ERR_SHORT_FRAME,
                           rcp_chained_decode_member(buf, sizeof(buf), &bbid, &chain_length,
                                                      &chain_position, &cs, &payload, &payload_len,
                                                      &txn));
}

static void test_decode_rejects_unknown_request_type(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint8_t chain_length = 0;
    uint8_t chain_position = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_chained_encode_member(0, 2, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    frame.data[RCP_ACF_ABB_HEADER_LEN] = 0x0Fu; /* overwrite opcode byte */

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_ERR_UNKNOWN_TYPE,
                           rcp_chained_decode_member(frame.data, frame.len, &bbid, &chain_length,
                                                      &chain_position, &cs, &payload, &payload_len,
                                                      &txn));

    rcp_bytes_free(&frame);
}

/* ── Sequencing: the cs-bit-driven abort/continue rule ────────────────────── */

static void test_advance_all_ok(void)
{
    bool aborted = false;

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, false, RCP_CHAINED_CS_ABORT_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, false, RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);
}

static void test_advance_abort_on_error_cascades(void)
{
    bool aborted = false;

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ERROR,
                           rcp_chained_advance(&aborted, true, RCP_CHAINED_CS_ABORT_ON_ERROR));
    TEST_ASSERT_TRUE(aborted);

    /* Every member from here on is reported aborted, regardless of its
     * own errored/cs inputs. */
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED,
                           rcp_chained_advance(&aborted, false, RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_TRUE(aborted);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED,
                           rcp_chained_advance(&aborted, true, RCP_CHAINED_CS_ABORT_ON_ERROR));
}

static void test_advance_continue_on_error_does_not_cascade(void)
{
    bool aborted = false;

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ERROR,
                           rcp_chained_advance(&aborted, true, RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, false, RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);
}

static void test_advance_full_chain_sequence(void)
{
    /* A 4-member chain: member 0 ok, member 1 errors with abort-on-error
     * cs, members 2 and 3 must both report aborted without their own
     * errored/cs inputs mattering. */
    bool aborted = false;
    rcp_chained_member_outcome_t outcomes[4];

    outcomes[0] = rcp_chained_advance(&aborted, false, RCP_CHAINED_CS_ABORT_ON_ERROR);
    outcomes[1] = rcp_chained_advance(&aborted, true, RCP_CHAINED_CS_ABORT_ON_ERROR);
    outcomes[2] = rcp_chained_advance(&aborted, false, RCP_CHAINED_CS_CONTINUE_ON_ERROR);
    outcomes[3] = rcp_chained_advance(&aborted, false, RCP_CHAINED_CS_CONTINUE_ON_ERROR);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK, outcomes[0]);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ERROR, outcomes[1]);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED, outcomes[2]);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED, outcomes[3]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_member_round_trip);
    RUN_TEST(test_member_round_trip_continue_on_error);
    RUN_TEST(test_encode_rejects_too_few_members);
    RUN_TEST(test_encode_rejects_position_out_of_range);
    RUN_TEST(test_decode_rejects_short_frame);
    RUN_TEST(test_decode_rejects_unknown_request_type);

    RUN_TEST(test_advance_all_ok);
    RUN_TEST(test_advance_abort_on_error_cascades);
    RUN_TEST(test_advance_continue_on_error_does_not_cascade);
    RUN_TEST(test_advance_full_chain_sequence);

    return UNITY_END();
}
