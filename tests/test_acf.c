//cfusa:test REQ-ACF-001
//cfusa:test REQ-ACF-002
//cfusa:test REQ-ACF-003
//cfusa:test REQ-ACF-004
//cfusa:test REQ-ACF-005
//cfusa:test REQ-ACF-006
//cfusa:test REQ-ACF-007
//cfusa:test REQ-ACF-008
//cfusa:test REQ-ACF-009
//cfusa:test REQ-ACF-010
//cfusa:test REQ-ACF-011
//cfusa:test REQ-ACF-012
//cfusa:test REQ-ACF-013
//cfusa:test REQ-ACF-014
//cfusa:test REQ-ACF-015
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Message type constants ────────────────────────────────────────────────── */

static void test_msg_type_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x0E, RCP_ACF_MSG_TYPE_ABB);
    TEST_ASSERT_EQUAL_HEX8(0x0D, RCP_ACF_MSG_TYPE_GBB);
}

static void test_gbb_header_is_exactly_8_bytes_longer_than_abb(void)
{
    /* The presence/absence of message_timestamp is the only structural
     * difference between the two variants. */
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN + 8, RCP_ACF_GBB_HEADER_LEN);
}

/* ── ACF_ABB round-trip ─────────────────────────────────────────────────────── */

static void test_abb_roundtrip_no_payload(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.byte_bus_id    = 5;
    hdr.transaction_num = 0x77;
    hdr.op              = RCP_ACF_OP_WRITE;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, frame.data[0]);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, out.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT16(0, out.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT8(hdr.byte_bus_id, out.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(hdr.transaction_num, out.transaction_num);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_WRITE, out.op);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_MTV_UNTIMED, out.mtv);
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_abb_roundtrip_with_payload_and_all_header_fields(void)
{
    uint8_t body[] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.byte_bus_id               = 9;
    hdr.pad                       = 3;
    hdr.hs                        = 1;
    hdr.cs                        = 1;
    hdr.rsp                       = 1;
    hdr.err                       = 0;
    hdr.ms                        = 1;
    hdr.evt                       = 0xA;
    hdr.op                        = RCP_ACF_OP_READ;
    hdr.transaction_num           = 0x21;
    hdr.read_size_or_segment_num  = 4;

    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN + sizeof(body), frame.len);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(body), out.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    TEST_ASSERT_EQUAL_UINT8(hdr.byte_bus_id, out.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(hdr.pad, out.pad);
    TEST_ASSERT_EQUAL_UINT8(hdr.hs, out.hs);
    TEST_ASSERT_EQUAL_UINT8(hdr.cs, out.cs);
    TEST_ASSERT_EQUAL_UINT8(hdr.rsp, out.rsp);
    TEST_ASSERT_EQUAL_UINT8(hdr.err, out.err);
    TEST_ASSERT_EQUAL_UINT8(hdr.ms, out.ms);
    TEST_ASSERT_EQUAL_UINT8(hdr.evt, out.evt);
    TEST_ASSERT_EQUAL_UINT8(hdr.op, out.op);
    TEST_ASSERT_EQUAL_UINT8(hdr.transaction_num, out.transaction_num);
    TEST_ASSERT_EQUAL_UINT8(hdr.read_size_or_segment_num, out.read_size_or_segment_num);

    rcp_bytes_free(&frame);
}

static void test_abb_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_ACF_ABB_HEADER_LEN - 1] = {0};
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_abb(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_abb_decode_rejects_wrong_msg_type(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    frame.data[0] = RCP_ACF_MSG_TYPE_GBB;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_BAD_MSG_TYPE,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_abb_decode_rejects_declared_length_past_buffer(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3};

    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_abb(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_abb_encode_rejects_oversized_payload(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame = rcp_acf_encode_abb(&hdr, NULL, RCP_ACF_MAX_PAYLOAD + 1);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(0, frame.len);
}

/* ── ACF_GBB round-trip ─────────────────────────────────────────────────────── */

static void test_gbb_roundtrip_no_payload(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.byte_bus_id = 2;
    hdr.info.mtv         = RCP_ACF_MTV_VALID;
    hdr.message_timestamp = 0x0102030405060708ULL;

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_GBB_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_GBB, frame.data[0]);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_GBB, out.info.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT8(hdr.info.byte_bus_id, out.info.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_MTV_VALID, out.info.mtv);
    TEST_ASSERT_EQUAL_UINT64(hdr.message_timestamp, out.message_timestamp);
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_gbb_roundtrip_with_payload(void)
{
    uint8_t body[] = {0x01, 0x02};
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.byte_bus_id = 1;
    hdr.info.mtv         = RCP_ACF_MTV_VALID;
    hdr.info.op          = RCP_ACF_OP_READ;
    hdr.message_timestamp = 0xAABBCCDDEEFF0011ULL;

    frame = rcp_acf_encode_gbb(&hdr, body, sizeof(body));
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_GBB_HEADER_LEN + sizeof(body), frame.len);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(body), out.info.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));
    TEST_ASSERT_EQUAL_UINT64(hdr.message_timestamp, out.message_timestamp);

    rcp_bytes_free(&frame);
}

static void test_gbb_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_ACF_GBB_HEADER_LEN - 1] = {0};
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_gbb(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_gbb_decode_rejects_wrong_msg_type(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    frame.data[0] = RCP_ACF_MSG_TYPE_ABB;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_BAD_MSG_TYPE,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_gbb_decode_rejects_declared_length_past_buffer(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3, 4};

    frame = rcp_acf_encode_gbb(&hdr, body, sizeof(body));

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_gbb(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_gbb_encode_rejects_oversized_payload(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame = rcp_acf_encode_gbb(&hdr, NULL, RCP_ACF_MAX_PAYLOAD + 1);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(0, frame.len);
}

/* ── Timestamp-validity folding ────────────────────────────────────────────── */

static void test_gbb_is_timed_true_only_when_valid(void)
{
    rcp_acf_gbb_header_t hdr = {0};

    hdr.info.mtv = RCP_ACF_MTV_VALID;
    TEST_ASSERT_TRUE(rcp_acf_gbb_is_timed(&hdr));
}

static void test_gbb_is_timed_false_when_untimed(void)
{
    rcp_acf_gbb_header_t hdr = {0};

    hdr.info.mtv = RCP_ACF_MTV_UNTIMED;
    TEST_ASSERT_FALSE(rcp_acf_gbb_is_timed(&hdr));
}

static void test_gbb_is_timed_false_when_uncertain(void)
{
    /* The "tu" uncertain state folds into the same not-confidently-timed
     * treatment as untimed. */
    rcp_acf_gbb_header_t hdr = {0};

    hdr.info.mtv = RCP_ACF_MTV_UNCERTAIN;
    hdr.message_timestamp = 0x1234;
    TEST_ASSERT_FALSE(rcp_acf_gbb_is_timed(&hdr));
}

static void test_gbb_encode_zeroes_timestamp_region_when_untimed(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.mtv          = RCP_ACF_MTV_UNTIMED;
    hdr.message_timestamp = 0xFFFFFFFFFFFFFFFFULL; /* nonzero on purpose */

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));

    TEST_ASSERT_EQUAL_UINT64(0, out.message_timestamp);
    TEST_ASSERT_FALSE(rcp_acf_gbb_is_timed(&out));

    rcp_bytes_free(&frame);
}

static void test_gbb_encode_preserves_timestamp_when_uncertain(void)
{
    /* Only the untimed (mtv=0) case forces a zeroed wire region -- an
     * uncertain timestamp still carries whatever value the sender had,
     * it's just not to be trusted for scheduling. */
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.mtv          = RCP_ACF_MTV_UNCERTAIN;
    hdr.message_timestamp = 0x42;

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));

    TEST_ASSERT_EQUAL_UINT64(0x42, out.message_timestamp);
    TEST_ASSERT_FALSE(rcp_acf_gbb_is_timed(&out));

    rcp_bytes_free(&frame);
}

/* ── Four response-type identification rules ───────────────────────────────── */

static void test_classify_response_error_takes_priority(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.err = 1;
    hdr.op  = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
}

static void test_classify_response_write(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_WRITE;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_WRITE, rcp_acf_classify_response(&hdr));
}

static void test_classify_response_read(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_READ, rcp_acf_classify_response(&hdr));
}

static void test_classify_response_acknowledge(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_NONE;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
}

static void test_ack_has_event_true_when_evt_nonzero(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op  = RCP_ACF_OP_NONE;
    hdr.evt = 3;
    TEST_ASSERT_TRUE(rcp_acf_hdr_ack_has_event(&hdr));
}

static void test_ack_has_event_false_when_evt_zero(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op  = RCP_ACF_OP_NONE;
    hdr.evt = 0;
    TEST_ASSERT_FALSE(rcp_acf_hdr_ack_has_event(&hdr));
}

static void test_ack_has_event_false_when_not_an_acknowledge(void)
{
    /* evt is only meaningful for classifying an Acknowledge; a Write or
     * Read response with a nonzero evt is not "an acknowledge with an
     * event" by this rule. */
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op  = RCP_ACF_OP_WRITE;
    hdr.evt = 7;
    TEST_ASSERT_FALSE(rcp_acf_hdr_ack_has_event(&hdr));
}

/* ── Message-type dispatch ─────────────────────────────────────────────────── */

static void test_peek_msg_type_reads_first_byte(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    uint8_t msg_type = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_peek_msg_type(frame.data, frame.len, &msg_type));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, msg_type);

    rcp_bytes_free(&frame);
}

static void test_peek_msg_type_rejects_empty_buffer(void)
{
    uint8_t msg_type = 0;
    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME, rcp_acf_peek_msg_type(NULL, 0, &msg_type));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_acf_strerror_unique_nonempty(void)
{
    const rcp_acf_errc_t codes[] = {
        RCP_ACF_OK, RCP_ACF_ERR_SHORT_FRAME, RCP_ACF_ERR_BAD_MSG_TYPE,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_acf_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_acf_strerror(codes[j])) != 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_msg_type_constants);
    RUN_TEST(test_gbb_header_is_exactly_8_bytes_longer_than_abb);

    RUN_TEST(test_abb_roundtrip_no_payload);
    RUN_TEST(test_abb_roundtrip_with_payload_and_all_header_fields);
    RUN_TEST(test_abb_decode_rejects_short_frame);
    RUN_TEST(test_abb_decode_rejects_wrong_msg_type);
    RUN_TEST(test_abb_decode_rejects_declared_length_past_buffer);
    RUN_TEST(test_abb_encode_rejects_oversized_payload);

    RUN_TEST(test_gbb_roundtrip_no_payload);
    RUN_TEST(test_gbb_roundtrip_with_payload);
    RUN_TEST(test_gbb_decode_rejects_short_frame);
    RUN_TEST(test_gbb_decode_rejects_wrong_msg_type);
    RUN_TEST(test_gbb_decode_rejects_declared_length_past_buffer);
    RUN_TEST(test_gbb_encode_rejects_oversized_payload);

    RUN_TEST(test_gbb_is_timed_true_only_when_valid);
    RUN_TEST(test_gbb_is_timed_false_when_untimed);
    RUN_TEST(test_gbb_is_timed_false_when_uncertain);
    RUN_TEST(test_gbb_encode_zeroes_timestamp_region_when_untimed);
    RUN_TEST(test_gbb_encode_preserves_timestamp_when_uncertain);

    RUN_TEST(test_classify_response_error_takes_priority);
    RUN_TEST(test_classify_response_write);
    RUN_TEST(test_classify_response_read);
    RUN_TEST(test_classify_response_acknowledge);
    RUN_TEST(test_ack_has_event_true_when_evt_nonzero);
    RUN_TEST(test_ack_has_event_false_when_evt_zero);
    RUN_TEST(test_ack_has_event_false_when_not_an_acknowledge);

    RUN_TEST(test_peek_msg_type_reads_first_byte);
    RUN_TEST(test_peek_msg_type_rejects_empty_buffer);

    RUN_TEST(test_acf_strerror_unique_nonempty);

    return UNITY_END();
}
