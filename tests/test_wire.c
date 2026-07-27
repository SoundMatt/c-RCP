//cfusa:test REQ-UDP-001
//cfusa:test REQ-UDP-002
//cfusa:test REQ-UDP-003
//cfusa:test REQ-UDP-004
//cfusa:test REQ-UDP-005
//cfusa:test REQ-UDP-006
//cfusa:test REQ-UDP-007
//cfusa:test REQ-UDP-008
//cfusa:test REQ-UDP-009
//cfusa:test REQ-UDP-010
//cfusa:test REQ-UDP-011
//cfusa:test REQ-UDP-012
//cfusa:test REQ-UDP-013
//cfusa:test REQ-UDP-014
#include "unity.h"

#include <rcp/rcp.h>
#include <rcp/wire.h>

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Header constants ──────────────────────────────────────────────────────── */

static void test_wire_header_length_and_magic(void)
{
    TEST_ASSERT_EQUAL_UINT(16, RCP_WIRE_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT8('R', RCP_WIRE_MAGIC_0);
    TEST_ASSERT_EQUAL_UINT8('C', RCP_WIRE_MAGIC_1);
}

static void test_wire_version_is_1(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, RCP_WIRE_PROTO_VERSION);
}

static void test_wire_max_payload_nonzero(void)
{
    TEST_ASSERT_TRUE(RCP_WIRE_MAX_PAYLOAD > 0);
}

/* ── Command round-trip ────────────────────────────────────────────────────── */

static void test_command_roundtrip_no_payload(void)
{
    rcp_command_t cmd = {0};
    rcp_bytes_t frame;
    rcp_command_t out = {0};

    cmd.id       = 0xDEADBEEF;
    cmd.zone     = RCP_ZONE_FRONT_LEFT;
    cmd.type     = RCP_CMD_SET;
    cmd.priority = RCP_PRIORITY_HIGH;

    frame = rcp_wire_encode_command(&cmd);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len >= RCP_WIRE_HEADER_LEN);

    TEST_ASSERT_EQUAL(RCP_WIRE_OK, rcp_wire_decode_command(frame.data, frame.len, &out));
    TEST_ASSERT_EQUAL_UINT32(cmd.id, out.id);
    TEST_ASSERT_EQUAL(cmd.zone, out.zone);
    TEST_ASSERT_EQUAL(cmd.type, out.type);
    TEST_ASSERT_EQUAL(cmd.priority, out.priority);
    TEST_ASSERT_EQUAL_UINT(0, out.payload.len);

    rcp_bytes_free(&frame);
    rcp_bytes_free(&out.payload);
}

static void test_command_roundtrip_with_payload(void)
{
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    rcp_command_t cmd = {0};
    rcp_bytes_t frame;
    rcp_command_t out = {0};

    cmd.id           = 42;
    cmd.zone         = RCP_ZONE_REAR_RIGHT;
    cmd.type         = RCP_CMD_GET;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    frame = rcp_wire_encode_command(&cmd);
    TEST_ASSERT_EQUAL(RCP_WIRE_OK, rcp_wire_decode_command(frame.data, frame.len, &out));
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), out.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.payload.data, sizeof(payload));

    rcp_bytes_free(&frame);
    rcp_bytes_free(&out.payload);
}

static void test_decode_command_rejects_truncated(void)
{
    rcp_command_t cmd = {0};
    rcp_bytes_t frame;
    rcp_command_t out = {0};

    cmd.id   = 1;
    cmd.zone = RCP_ZONE_CENTRAL;
    frame = rcp_wire_encode_command(&cmd);

    TEST_ASSERT_EQUAL(RCP_WIRE_ERR_SHORT_FRAME,
                       rcp_wire_decode_command(frame.data, RCP_WIRE_HEADER_LEN - 1, &out));

    rcp_bytes_free(&frame);
}

/* ── Response round-trip ───────────────────────────────────────────────────── */

static void test_response_roundtrip(void)
{
    uint8_t payload[] = {0xFF, 0xFE};
    rcp_response_t resp = {0};
    rcp_bytes_t frame;
    rcp_response_t out = {0};

    resp.command_id  = 0x1234;
    resp.zone        = RCP_ZONE_FRONT_RIGHT;
    resp.status      = RCP_RESPONSE_OK;
    resp.payload.data = payload;
    resp.payload.len  = sizeof(payload);

    frame = rcp_wire_encode_response(&resp);
    TEST_ASSERT_EQUAL(RCP_WIRE_OK, rcp_wire_decode_response(frame.data, frame.len, &out));
    TEST_ASSERT_EQUAL_UINT32(resp.command_id, out.command_id);
    TEST_ASSERT_EQUAL(resp.zone, out.zone);
    TEST_ASSERT_EQUAL(resp.status, out.status);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.payload.data, sizeof(payload));

    rcp_bytes_free(&frame);
    rcp_response_free(&out);
}

/* ── Status round-trip ─────────────────────────────────────────────────────── */

static void test_status_roundtrip(void)
{
    uint8_t payload[] = {0xAB};
    rcp_status_t st = {0};
    rcp_bytes_t frame;
    rcp_status_t out = {0};

    st.zone         = RCP_ZONE_REAR_LEFT;
    st.seq          = 99;
    st.healthy      = true;
    st.payload.data = payload;
    st.payload.len  = sizeof(payload);

    frame = rcp_wire_encode_status(&st);
    TEST_ASSERT_EQUAL(RCP_WIRE_OK, rcp_wire_decode_status(frame.data, frame.len, &out));
    TEST_ASSERT_EQUAL(st.zone, out.zone);
    TEST_ASSERT_EQUAL_UINT32(st.seq, out.seq);
    TEST_ASSERT_EQUAL(st.healthy, out.healthy);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.payload.data, sizeof(payload));

    rcp_bytes_free(&frame);
    rcp_status_free(&out);
}

/* ── Frame magic / version ─────────────────────────────────────────────────── */

static void test_frame_begins_with_magic_and_version(void)
{
    rcp_command_t cmd = {0};
    rcp_bytes_t frame;

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    frame = rcp_wire_encode_command(&cmd);

    TEST_ASSERT_EQUAL_UINT8('R', frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8('C', frame.data[1]);
    TEST_ASSERT_EQUAL_UINT8(RCP_WIRE_PROTO_VERSION, frame.data[2]);

    rcp_bytes_free(&frame);
}

static void test_decode_command_rejects_wrong_magic(void)
{
    rcp_command_t cmd = {0};
    rcp_bytes_t frame;
    rcp_command_t out = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    frame = rcp_wire_encode_command(&cmd);
    frame.data[0] = 'X'; /* corrupt magic */

    TEST_ASSERT_EQUAL(RCP_WIRE_ERR_BAD_MAGIC, rcp_wire_decode_command(frame.data, frame.len, &out));

    rcp_bytes_free(&frame);
}

static void test_decode_command_rejects_wrong_version(void)
{
    rcp_command_t cmd = {0};
    rcp_bytes_t frame;
    rcp_command_t out = {0};

    cmd.zone = RCP_ZONE_FRONT_LEFT;
    frame = rcp_wire_encode_command(&cmd);
    frame.data[2] = RCP_WIRE_PROTO_VERSION + 1; /* bump version */

    TEST_ASSERT_EQUAL(RCP_WIRE_ERR_BAD_VERSION, rcp_wire_decode_command(frame.data, frame.len, &out));

    rcp_bytes_free(&frame);
}

/* ── Overflow guard (body_len near UINT32_MAX) ─────────────────────────────── */

/* Hand-crafts a header-only "frame" with an attacker-controlled body_len at
 * offset 12, simulating a malicious UDP datagram whose declared payload
 * length vastly exceeds both RCP_WIRE_MAX_PAYLOAD and the actual bytes
 * received. On a 32-bit size_t target, RCP_WIRE_HEADER_LEN + body_len can
 * wrap around to a small value, defeating a naive `len < header+body_len`
 * check; the fix rejects any body_len > RCP_WIRE_MAX_PAYLOAD outright,
 * before that addition ever happens, so this must fail regardless of the
 * host's size_t width. */
static void make_oversized_header(uint8_t *b, uint8_t msg_type, uint32_t body_len)
{
    memset(b, 0, RCP_WIRE_HEADER_LEN);
    b[0] = RCP_WIRE_MAGIC_0;
    b[1] = RCP_WIRE_MAGIC_1;
    b[2] = RCP_WIRE_PROTO_VERSION;
    b[3] = msg_type;
    b[12] = (uint8_t)(body_len >> 24);
    b[13] = (uint8_t)((body_len >> 16) & 0xFFu);
    b[14] = (uint8_t)((body_len >> 8) & 0xFFu);
    b[15] = (uint8_t)(body_len & 0xFFu);
}

static void test_decode_command_rejects_oversized_body_len(void)
{
    uint8_t b[RCP_WIRE_HEADER_LEN];
    rcp_command_t out = {0};

    make_oversized_header(b, RCP_WIRE_TYPE_COMMAND, 0xFFFFFFF0u);

    TEST_ASSERT_EQUAL(RCP_WIRE_ERR_SHORT_FRAME,
                       rcp_wire_decode_command(b, RCP_WIRE_HEADER_LEN, &out));
}

static void test_decode_response_rejects_oversized_body_len(void)
{
    uint8_t b[RCP_WIRE_HEADER_LEN];
    rcp_response_t out = {0};

    make_oversized_header(b, RCP_WIRE_TYPE_RESPONSE, 0xFFFFFFF0u);

    TEST_ASSERT_EQUAL(RCP_WIRE_ERR_SHORT_FRAME,
                       rcp_wire_decode_response(b, RCP_WIRE_HEADER_LEN, &out));
}

static void test_decode_status_rejects_oversized_body_len(void)
{
    uint8_t b[RCP_WIRE_HEADER_LEN];
    rcp_status_t out = {0};

    make_oversized_header(b, RCP_WIRE_TYPE_STATUS, 0xFFFFFFF0u);

    TEST_ASSERT_EQUAL(RCP_WIRE_ERR_SHORT_FRAME,
                       rcp_wire_decode_status(b, RCP_WIRE_HEADER_LEN, &out));
}

/* Unlike the tests above (which pass a header-only buffer far shorter than
 * the claimed body_len, so even the pre-fix `len < header+body_len` check
 * would have caught it on a 64-bit host), this test provides a REAL,
 * fully-backed buffer exactly as long as `header + body_len` demands --
 * i.e. long enough that the old length check alone would have let it
 * through. Only the new `body_len > RCP_WIRE_MAX_PAYLOAD` ceiling check
 * (independent of the addition, and thus independent of size_t width)
 * rejects it, which is the actual guarantee this fix adds. */
static void test_decode_command_rejects_body_len_just_over_max_even_with_real_buffer(void)
{
    uint32_t body_len = (uint32_t)RCP_WIRE_MAX_PAYLOAD + 1u;
    size_t total = RCP_WIRE_HEADER_LEN + (size_t)body_len;
    uint8_t *b = (uint8_t *)malloc(total);
    rcp_command_t out = {0};

    TEST_ASSERT_NOT_NULL(b);
    make_oversized_header(b, RCP_WIRE_TYPE_COMMAND, body_len);

    TEST_ASSERT_EQUAL(RCP_WIRE_ERR_SHORT_FRAME, rcp_wire_decode_command(b, total, &out));

    free(b);
}

/* ── Control frame ─────────────────────────────────────────────────────────── */

static void test_encode_control_produces_header_only_frame(void)
{
    rcp_bytes_t frame = rcp_wire_encode_control(RCP_WIRE_TYPE_SUBSCRIBE, RCP_ZONE_CENTRAL);

    TEST_ASSERT_EQUAL_UINT(RCP_WIRE_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_UINT8('R', frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(RCP_WIRE_TYPE_SUBSCRIBE, frame.data[3]);

    rcp_bytes_free(&frame);
}

static void test_wire_strerror_unique_nonempty(void)
{
    const rcp_wire_errc_t codes[] = {
        RCP_WIRE_OK, RCP_WIRE_ERR_SHORT_FRAME,
        RCP_WIRE_ERR_BAD_MAGIC, RCP_WIRE_ERR_BAD_VERSION,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_wire_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(s, rcp_wire_strerror(codes[j])) != 0 ? 1 : 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_wire_header_length_and_magic);
    RUN_TEST(test_wire_version_is_1);
    RUN_TEST(test_wire_max_payload_nonzero);

    RUN_TEST(test_command_roundtrip_no_payload);
    RUN_TEST(test_command_roundtrip_with_payload);
    RUN_TEST(test_decode_command_rejects_truncated);

    RUN_TEST(test_response_roundtrip);
    RUN_TEST(test_status_roundtrip);

    RUN_TEST(test_frame_begins_with_magic_and_version);
    RUN_TEST(test_decode_command_rejects_wrong_magic);
    RUN_TEST(test_decode_command_rejects_wrong_version);

    RUN_TEST(test_decode_command_rejects_oversized_body_len);
    RUN_TEST(test_decode_response_rejects_oversized_body_len);
    RUN_TEST(test_decode_status_rejects_oversized_body_len);
    RUN_TEST(test_decode_command_rejects_body_len_just_over_max_even_with_real_buffer);

    RUN_TEST(test_encode_control_produces_header_only_frame);
    RUN_TEST(test_wire_strerror_unique_nonempty);

    return UNITY_END();
}
