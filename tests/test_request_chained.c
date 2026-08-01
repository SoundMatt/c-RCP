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
//cfusa:test REQ-CHAIN-011
//cfusa:test REQ-CHAIN-012
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
    const char *f = rcp_chained_strerror(RCP_CHAINED_ERR_RESERVED_NONZERO);
    const char *unk = rcp_chained_strerror((rcp_chained_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
    TEST_ASSERT_TRUE(strcmp(c, d) != 0);
    TEST_ASSERT_TRUE(strcmp(d, e) != 0);
    TEST_ASSERT_TRUE(strcmp(e, f) != 0);
}

/* ── Chain member encode/decode ───────────────────────────────────────────── */

static void test_member_round_trip(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint16_t chain_exec_delay = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;
    uint8_t body[2] = {0x11u, 0x22u};

    frame = rcp_chained_encode_member(3, 4321, RCP_CHAINED_CS_ABORT_ON_ERROR, 55, body,
                                       sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_OK,
                           rcp_chained_decode_member(frame.data, frame.len, &bbid,
                                                      &chain_exec_delay, &cs, &payload,
                                                      &payload_len, &txn));

    TEST_ASSERT_EQUAL_UINT8(3, bbid);
    TEST_ASSERT_EQUAL_UINT16(4321, chain_exec_delay);
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
    uint16_t chain_exec_delay = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_chained_encode_member(1, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_OK,
                           rcp_chained_decode_member(frame.data, frame.len, &bbid,
                                                      &chain_exec_delay, &cs, &payload,
                                                      &payload_len, &txn));
    TEST_ASSERT_EQUAL_UINT8(RCP_CHAINED_CS_CONTINUE_ON_ERROR, cs);
    TEST_ASSERT_EQUAL_UINT16(0, chain_exec_delay);
    TEST_ASSERT_EQUAL_size_t(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_decode_rejects_short_frame(void)
{
    uint8_t buf[4] = {0};
    rcp_byte_bus_id_t bbid = 0;
    uint16_t chain_exec_delay = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_ERR_SHORT_FRAME,
                           rcp_chained_decode_member(buf, sizeof(buf), &bbid, &chain_exec_delay,
                                                      &cs, &payload, &payload_len, &txn));
}

static void test_decode_rejects_unknown_request_type(void)
{
    rcp_bytes_t frame;
    rcp_byte_bus_id_t bbid = 0;
    uint16_t chain_exec_delay = 0;
    uint8_t cs = 0;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t txn = 0;

    frame = rcp_chained_encode_member(0, 0, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    frame.data[RCP_ACF_ABB_HEADER_LEN] = 0x0Fu; /* overwrite opcode byte */

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_ERR_UNKNOWN_TYPE,
                           rcp_chained_decode_member(frame.data, frame.len, &bbid,
                                                      &chain_exec_delay, &cs, &payload,
                                                      &payload_len, &txn));

    rcp_bytes_free(&frame);
}

/* ── Literal wire layout ──────────────────────────────────────────────────────
 *
 * Written from the TC18 v0.5.1_RC chained-request figure and field table
 * (§11.2.2.4, Figure 11 / Table 9), NOT copied back out of this encoder.
 * The figure lays the repurposed message_timestamp region out as just:
 *
 *   offset 0     request_type     (one octet, 0x01)
 *   offsets 1..3 reserved         (all bits zero)
 *   offsets 4..5 chain_exec_delay (two octets, big-endian)
 *   offsets 6..7 reserved         (all bits zero)
 *
 * There are no chain_length or chain_position sub-fields: before
 * v0.102.0 this module invented both, at offsets 1 and 2, overwriting
 * octets the specification mandates be transmitted as zero. */

#define TS_OFF RCP_ACF_ABB_HEADER_LEN

static void test_chained_wire_sub_field_offsets(void)
{
    rcp_bytes_t frame = rcp_chained_encode_member(7, 0x1234, RCP_CHAINED_CS_ABORT_ON_ERROR, 1,
                                                   NULL, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len >= TS_OFF + 8u);

    TEST_ASSERT_EQUAL_HEX8(0x01, frame.data[TS_OFF + 0]); /* request_type = chained */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 1]); /* reserved */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 2]); /* reserved */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 3]); /* reserved */
    TEST_ASSERT_EQUAL_HEX8(0x12, frame.data[TS_OFF + 4]); /* chain_exec_delay hi */
    TEST_ASSERT_EQUAL_HEX8(0x34, frame.data[TS_OFF + 5]); /* chain_exec_delay lo */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 6]); /* reserved */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[TS_OFF + 7]); /* reserved */

    rcp_bytes_free(&frame);
}

/* Table 9: reserved "All bits shall be written as 0, else the request
 * shall be rejected". */
static void test_chained_decode_rejects_nonzero_reserved(void)
{
    size_t offsets[5] = {1, 2, 3, 6, 7};
    size_t i;

    for (i = 0; i < 5; i++) {
        rcp_bytes_t frame;
        rcp_byte_bus_id_t bbid = 0;
        uint16_t chain_exec_delay = 0;
        uint8_t cs = 0;
        const uint8_t *payload = NULL;
        size_t payload_len = 0;
        uint8_t txn = 0;

        frame = rcp_chained_encode_member(0, 9, RCP_CHAINED_CS_CONTINUE_ON_ERROR, 0, NULL, 0);
        TEST_ASSERT_NOT_NULL(frame.data);
        frame.data[TS_OFF + offsets[i]] = 0x01u;

        TEST_ASSERT_EQUAL_INT(RCP_CHAINED_ERR_RESERVED_NONZERO,
                               rcp_chained_decode_member(frame.data, frame.len, &bbid,
                                                          &chain_exec_delay, &cs, &payload,
                                                          &payload_len, &txn));
        rcp_bytes_free(&frame);
    }
}

static void test_chained_exec_delay_elapsed(void)
{
    TEST_ASSERT_FALSE(rcp_chained_exec_delay_elapsed(100, 99));
    TEST_ASSERT_TRUE(rcp_chained_exec_delay_elapsed(100, 100));
    TEST_ASSERT_TRUE(rcp_chained_exec_delay_elapsed(0, 0));
}

/* ── Sequencing: the cs-bit-driven conditional-start rule ─────────────────── */

/* Table 9, cs ("Conditional start"): "0b: request will be executed even in
 * case the previous request returned an error; 1b: request will not be
 * executed in case the previous request returned an error, as a
 * consequence the remainder of the chain is not executed, and the request
 * send an error response with error code = CHAIN_ABORTED." So cs is read
 * off the member about to run, about its predecessor's outcome. */

static void test_advance_all_ok(void)
{
    bool aborted = false;

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, true, false,
                                                RCP_CHAINED_CS_ABORT_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, true, false,
                                                RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);
}

/* A member with no predecessor at all: the whole chain is ignored. */
static void test_advance_first_member_without_predecessor_is_chain_error(void)
{
    bool aborted = false;

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ERROR,
                           rcp_chained_advance(&aborted, false, false,
                                                RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_TRUE(aborted);

    /* And every member after it is aborted too. */
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED,
                           rcp_chained_advance(&aborted, true, false,
                                                RCP_CHAINED_CS_CONTINUE_ON_ERROR));
}

static void test_advance_abort_on_error_stops_the_chain(void)
{
    bool aborted = false;

    /* Predecessor errored and this member selects abort-on-error: it does
     * not run, and neither does anything after it. */
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED,
                           rcp_chained_advance(&aborted, true, true,
                                                RCP_CHAINED_CS_ABORT_ON_ERROR));
    TEST_ASSERT_TRUE(aborted);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED,
                           rcp_chained_advance(&aborted, true, false,
                                                RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_TRUE(aborted);
}

static void test_advance_continue_on_error_runs_anyway(void)
{
    bool aborted = false;

    /* Predecessor errored, but this member selects continue-on-error: it
     * runs, and the chain is not aborted. */
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, true, true,
                                                RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, true, false,
                                                RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);
}

/* The corrected semantics, stated as the difference from the old ones: an
 * erroring member whose OWN cs is abort-on-error does NOT by itself stop
 * the chain -- it is the successor's cs that decides. */
static void test_advance_erroring_members_own_cs_does_not_decide(void)
{
    bool aborted = false;

    /* Member 1 runs (predecessor fine) and then errors. Its own cs was
     * abort-on-error, but that is about ITS predecessor, not its
     * successor. */
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, true, false,
                                                RCP_CHAINED_CS_ABORT_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);

    /* Member 2 selects continue-on-error, so it runs despite member 1's
     * error. */
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK,
                           rcp_chained_advance(&aborted, true, true,
                                                RCP_CHAINED_CS_CONTINUE_ON_ERROR));
    TEST_ASSERT_FALSE(aborted);
}

static void test_advance_full_chain_sequence(void)
{
    /* A 4-member chain: member 0 runs after a fine predecessor and then
     * errors; member 1 selects abort-on-error so it does not run and ends
     * the chain; members 2 and 3 are aborted regardless of their own cs. */
    bool aborted = false;
    rcp_chained_member_outcome_t outcomes[4];

    outcomes[0] = rcp_chained_advance(&aborted, true, false, RCP_CHAINED_CS_ABORT_ON_ERROR);
    outcomes[1] = rcp_chained_advance(&aborted, true, true, RCP_CHAINED_CS_ABORT_ON_ERROR);
    outcomes[2] = rcp_chained_advance(&aborted, true, false, RCP_CHAINED_CS_CONTINUE_ON_ERROR);
    outcomes[3] = rcp_chained_advance(&aborted, true, false, RCP_CHAINED_CS_CONTINUE_ON_ERROR);

    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_OK, outcomes[0]);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED, outcomes[1]);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED, outcomes[2]);
    TEST_ASSERT_EQUAL_INT(RCP_CHAINED_MEMBER_CHAIN_ABORTED, outcomes[3]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_member_round_trip);
    RUN_TEST(test_member_round_trip_continue_on_error);
    RUN_TEST(test_decode_rejects_short_frame);
    RUN_TEST(test_decode_rejects_unknown_request_type);

    RUN_TEST(test_chained_wire_sub_field_offsets);
    RUN_TEST(test_chained_decode_rejects_nonzero_reserved);
    RUN_TEST(test_chained_exec_delay_elapsed);

    RUN_TEST(test_advance_all_ok);
    RUN_TEST(test_advance_first_member_without_predecessor_is_chain_error);
    RUN_TEST(test_advance_abort_on_error_stops_the_chain);
    RUN_TEST(test_advance_continue_on_error_runs_anyway);
    RUN_TEST(test_advance_erroring_members_own_cs_does_not_decide);
    RUN_TEST(test_advance_full_chain_sequence);

    return UNITY_END();
}
