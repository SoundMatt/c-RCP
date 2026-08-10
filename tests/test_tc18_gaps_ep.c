/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ACF-017
//cfusa:test REQ-ACF-018
//cfusa:test REQ-ACF-019
//cfusa:test REQ-ACF-020
//cfusa:test REQ-ACF-021
//cfusa:test REQ-GPIO-033
//cfusa:test REQ-GPIO-034
//cfusa:test REQ-GPIO-035
//cfusa:test REQ-GPIO-036
//cfusa:test REQ-SPI-033
//cfusa:test REQ-SPI-034
//cfusa:test REQ-SPI-035
//cfusa:test REQ-SPI-036
//cfusa:test REQ-SPI-037
//cfusa:test REQ-I2C-019
//cfusa:test REQ-I2C-020
//cfusa:test REQ-PWM-055
//cfusa:test REQ-PWM-056
//cfusa:test REQ-PWM-057
//cfusa:test REQ-PWM-058
//cfusa:test REQ-WAKEUP-017
//cfusa:test REQ-WAKEUP-018
//cfusa:test REQ-WAKEUP-019
//cfusa:test REQ-WAKEUP-020
//cfusa:test REQ-WAKEUP-021
//cfusa:test REQ-WAKEUP-022
//cfusa:test REQ-E2E-028
//cfusa:test REQ-E2E-029
//cfusa:test REQ-E2E-030

/* test_tc18_gaps_ep.c -- a spec-literal conformance-and-deviation suite for
 * the TC18 clauses catalogued by the v0.105.0 requirements-corpus
 * completeness pass that this library's endpoint functional-configuration
 * register blocks and request handling (TC18 13.7.1-13.7.7), the ACF
 * byte_message_info header (11.2), and the E2E safe-point framing (12.7.7)
 * either implement, implement only in part, or do not implement at all.
 *
 * For a requirement catalogued "implemented", the test below asserts the
 * specified behaviour literally, against the constant the cited TC18 clause
 * itself fixes. For a requirement catalogued "partial" or "not-implemented",
 * the test is a DEVIATION PIN: it asserts the current, real, observable
 * behaviour of this code, with a comment naming the TC18 clause that is not
 * met and what a conforming implementation would do instead. Such a test
 * failing means the deviation was closed (or moved) -- the requirement's
 * status and this test must then be updated together. */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/e2e.h>
#include <rcp/ep_gpio.h>
#include <rcp/ep_i2c.h>
#include <rcp/ep_pwm.h>
#include <rcp/ep_spi.h>
#include <rcp/ep_wakeup.h>
#include <rcp/errors.h>
#include <rcp/lifecycle.h>
#include <rcp/power.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/request_timed.h>
#include <rcp/server.h>

#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── ACF byte_message_info (TC18 11.2.1 Table 4 / 11.2.2.1 Table 6) ───────── */

/* REQ-ACF-017 (implemented): the two acf_msg_type values TC18 11.2.1 Table 4
 * and 11.2.2.1 Table 6 assign to ACF_ABB and ACF_GBB.
 * REQ-ACF-019 (implemented): the 3-state application enum maps onto the
 * single op wire bit at octet 6 bit 7 -- 0b for a read (a response with data
 * is expected), 1b otherwise; a decode never reproduces RCP_ACF_OP_NONE. */
static void test_acf_msg_type_constants_and_op_wire_bit(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t              *payload     = NULL;
    size_t                      payload_len = 0;
    rcp_bytes_t                 frame;

    TEST_ASSERT_EQUAL_HEX8(0x0Eu, RCP_ACF_MSG_TYPE_ABB);
    TEST_ASSERT_EQUAL_HEX8(0x0Du, RCP_ACF_MSG_TYPE_GBB);

    /* op = RCP_ACF_OP_NONE is encode-only: it must land on the wire as the
     * write sense (bit set) and decode back as RCP_ACF_OP_WRITE. */
    hdr.op = RCP_ACF_OP_NONE;
    frame  = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_HEX8(0x80u, (uint8_t)(frame.data[6] & 0x80u));
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_WRITE, out.op);
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, out.acf_msg_type);
    rcp_bytes_free(&frame);

    hdr.op = RCP_ACF_OP_READ;
    frame  = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(frame.data[6] & 0x80u));
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_READ, out.op);
    rcp_bytes_free(&frame);
}

/* REQ-ACF-018 (partial) DEVIATION PIN: TC18 11.2.1 Table 4 / 11.2.2.1 Table 6
 * make the 12-bit read_size_or_segment_num field mean read_size when op == 0
 * (read) and segment_num otherwise. A conforming API would report which of
 * the two a caller is looking at; c-RCP exposes one uninterpreted slot whose
 * decoded value is identical under either op sense. (This test used to also
 * carry REQ-ACF-020's own byte_bus_id-width deviation pin -- split out into
 * test_acf_bus_id_is_now_eleven_bits_wide below once that gap closed, so
 * this test's own remaining scope is exactly its own single requirement.) */
static void test_acf_read_size_slot_is_ambiguous(void)
{
    rcp_acf_byte_message_info_t hdr    = {0};
    rcp_acf_byte_message_info_t out    = {0};
    uint8_t                     packed[8];

    hdr.read_size_or_segment_num = 0x1321u; /* 12-bit field: 0x321 survives */

    hdr.op = RCP_ACF_OP_READ;
    rcp_acf_pack_header(packed, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(packed, &out));
    TEST_ASSERT_EQUAL_HEX16(0x321u, out.read_size_or_segment_num);

    hdr.op = RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(packed, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(packed, &out));
    /* Same slot, same value: nothing selects the segment_num reading. */
    TEST_ASSERT_EQUAL_HEX16(0x321u, out.read_size_or_segment_num);
}

/* REQ-ACF-020: byte_bus_id is 11 bits on the wire (Figure 7: [10:8] in
 * octet 2 bits 2:0, [7:0] in octet 3). Fixed (REQ-RMAP-053's own
 * companion requirement): rcp_byte_bus_id_t is now wide enough to hold
 * the full range, so endpoint identifiers up to 2047 are representable,
 * encodable, and decodable -- split out of the combined test above,
 * which used to also pin this as a still-open deviation. */
static void test_acf_bus_id_is_now_eleven_bits_wide(void)
{
    rcp_acf_byte_message_info_t hdr    = {0};
    rcp_acf_byte_message_info_t out    = {0};
    uint8_t                     packed[8];

    hdr.byte_bus_id = 0x1FFu; /* endpoint 511 -- above the old 8-bit ceiling */
    hdr.op          = RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(packed, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL_HEX8(0x01u, (uint8_t)(packed[2] & 0x07u)); /* [10:8] now real */
    TEST_ASSERT_EQUAL_HEX8(0xFFu, packed[3]);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(packed, &out));
    TEST_ASSERT_EQUAL_UINT16(0x1FFu, out.byte_bus_id);
}

/* REQ-ACF-021 (partial) DEVIATION PIN: TC18 11.2.1 Table 4 / 11.2.2.3 Table 8
 * require an encoded request to carry rsv = 00b, hs = 0b, err = 0b and
 * rsp = 0b (rsp = 0b is what makes a message a request rather than a
 * response), and a received message with rsp set must not be admitted as a
 * request. c-RCP forces only rsv to zero; hs, cs, rsp, err and ms are
 * round-tripped verbatim on a request, and the admission path never inspects
 * rsp at all. */
static void test_acf_request_flags_round_trip_unconstrained(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t              *payload     = NULL;
    size_t                      payload_len = 0;
    rcp_bytes_t                 frame;
    rcp_server_endpoint_t       ep;
    uint8_t                     request_type = 0xFFu;

    hdr.hs  = 1u;
    hdr.cs  = 1u;
    hdr.rsp = 1u;
    hdr.err = 1u;
    hdr.ms  = 1u;
    hdr.op  = RCP_ACF_OP_READ;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    /* rsv (octet 2 bits 4:3 and octet 4 bits 3:2) IS forced to zero. */
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(frame.data[2] & 0x18u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, (uint8_t)(frame.data[4] & 0x0Cu));

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(1u, out.hs);  /* a conforming request: 0b */
    TEST_ASSERT_EQUAL_UINT8(1u, out.rsp); /* a conforming request: 0b */
    TEST_ASSERT_EQUAL_UINT8(1u, out.err); /* a conforming request: 0b */
    TEST_ASSERT_EQUAL_UINT8(1u, out.cs);
    TEST_ASSERT_EQUAL_UINT8(1u, out.ms);

    /* ... and the rsp = 1b message is still admitted as an ordinary
     * request, which TC18 11.2.2.3 forbids. */
    rcp_server_endpoint_init(&ep, true);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, &request_type,
                                                NULL, NULL));
    rcp_server_endpoint_destroy(&ep);
    rcp_bytes_free(&frame);
}

/* ── GPIO endpoint (TC18 13.7.4) ──────────────────────────────────────────── */

/* REQ-GPIO-033 (partial): TC18 13.7.4.1 fixes the GPIO request payload at
 * exactly four octets, with an endpoint of fewer than 32 pins mapped onto the
 * least-significant bits. c-RCP enforces the length; the DEVIATION is that a
 * violation is reported only as the module-local code
 * RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN -- the numbered wire code the clause calls
 * for, RCP_ERROR_INVALID_PARAMETER, is a different value and is never
 * produced from this path. */
static void test_gpio_request_payload_is_four_octets(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    uint8_t                     short_p[3] = {0x01u, 0x02u, 0x03u};
    rcp_bytes_t                 frame;
    uint32_t                    bitmask = 0;
    uint8_t                     evt = 0, tn = 0;

    TEST_ASSERT_EQUAL_UINT(4u, RCP_EP_GPIO_PAYLOAD_LEN);
    TEST_ASSERT_EQUAL_UINT8(32u, RCP_EP_GPIO_MAX_PINS);
    /* Least-significant-bit-first mapping: pin 0 is bit 0 of the 32-bit
     * payload, pin 31 its most significant bit. */
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, rcp_ep_gpio_pin_mask(0u));
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, rcp_ep_gpio_pin_mask(31u));
    TEST_ASSERT_TRUE(rcp_ep_gpio_pin_get(0x00000001u, 0u));
    TEST_ASSERT_FALSE(rcp_ep_gpio_pin_get(0x00000001u, 1u));

    frame = rcp_ep_gpio_encode_write_request(7u, 0x0000000Fu, RCP_EP_GPIO_WRITE_REPLACE, 0x21u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                      rcp_ep_gpio_decode_write_request(frame.data, frame.len, 7u, &bitmask, &evt,
                                                       &tn));
    TEST_ASSERT_EQUAL_HEX32(0x0000000Fu, bitmask);
    rcp_bytes_free(&frame);

    hdr.byte_bus_id = 7u;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, short_p, sizeof(short_p));
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN,
                      rcp_ep_gpio_decode_write_request(frame.data, frame.len, 7u, &bitmask, &evt,
                                                       &tn));
    /* The wire code TC18 13.7.4.1 names for this case is a different,
     * unreachable value. */
    {
        /* Held in ints, not compared as their own enum types: MSVC's C5287
         * rejects a direct comparison of two different enumerations, and
         * the point here is precisely that these are two unrelated
         * numbering schemes. */
        const int wire_code  = (int)RCP_ERROR_INVALID_PARAMETER;
        const int local_code = (int)RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN;

        TEST_ASSERT_EQUAL_INT(15, wire_code);
        TEST_ASSERT_NOT_EQUAL_INT(wire_code, local_code);
    }
    rcp_bytes_free(&frame);
}

/* REQ-GPIO-034 (partial) DEVIATION PIN: the three trigger CONDITIONS exist,
 * but TC18 13.7.4.1 Table 40's trigger signal NUMBERING (0 = execution done;
 * per pin IOn: 3n+1 change, 3n+2 rising, 3n+3 falling, up to 96 for IO31
 * falling) is not represented -- the selector is a 4-value per-pin enum whose
 * whole range is 0..3, so no Table 40 signal number above 3 can be named.
 * REQ-GPIO-035 (not-implemented) DEVIATION PIN: none of Table 41's
 * gpio_base_clk / gpio_clk_divider / gpio_ep_status / the 32 per-pin
 * gpio_debounce_IOn registers is modelled, and no debounce filtering exists:
 * the functional config is exactly the shared prefix plus 32 x
 * {pin_property, trigger}. */
static void test_gpio_trigger_numbering_and_functional_cfg_gaps(void)
{
    rcp_ep_gpio_functional_cfg_t cfg;

    TEST_ASSERT_EQUAL_INT(0, (int)RCP_EP_GPIO_TRIGGER_NONE);
    TEST_ASSERT_EQUAL_INT(3, (int)RCP_EP_GPIO_TRIGGER_FALLING);
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_RISING, true, false));
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_FALLING, true, false));
    TEST_ASSERT_TRUE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_ANY_CHANGE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_gpio_trigger_fires(RCP_EP_GPIO_TRIGGER_NONE, false, true));

    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.pins[0].pin_property);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_GPIO_TRIGGER_NONE, cfg.pins[31].trigger);
    /* Struct exhaustiveness: the shared prefix, then pins[32], then nothing
     * -- no room for a base clock, a divider, a status word or 32 debounce
     * registers. */
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_ep_functional_cfg_t),
                           offsetof(rcp_ep_gpio_functional_cfg_t, pins));
    TEST_ASSERT_EQUAL_UINT(sizeof(cfg),
                           offsetof(rcp_ep_gpio_functional_cfg_t, pins) + sizeof(cfg.pins));
    TEST_ASSERT_EQUAL_UINT(2u * (size_t)RCP_EP_GPIO_MAX_PINS, sizeof(cfg.pins));
}

/* REQ-GPIO-036 (not-implemented) DEVIATION PIN: TC18 13.7.4.3 makes GPIO
 * response timing depend on the request -- a read carrying no
 * byte_msg_payload responds immediately on execution, while a payload-bearing
 * read or any write first drives the pins and only responds once the
 * configured debounce time has elapsed. c-RCP models no debounce and no
 * response delay: the payload-less read and the 4-octet write differ only in
 * their payload length, and both are answered through the same untimed
 * response path. */
static void test_gpio_response_timing_is_not_modelled(void)
{
    rcp_acf_byte_message_info_t hdr         = {0};
    const uint8_t              *payload     = NULL;
    size_t                      payload_len = 99u;
    rcp_bytes_t                 frame;
    uint32_t                    bitmask = 0;
    uint64_t                    ts      = 7u;
    bool                        timed   = true;
    uint8_t                     tn      = 0;

    frame = rcp_ep_gpio_encode_read_request(3u, 0x11u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT(0u, payload_len); /* pure read: immediate per TC18 */
    rcp_bytes_free(&frame);

    frame = rcp_ep_gpio_encode_write_request(3u, 0xDEADBEEFu, RCP_EP_GPIO_WRITE_REPLACE, 0x12u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT(RCP_EP_GPIO_PAYLOAD_LEN, payload_len);
    rcp_bytes_free(&frame);

    /* The response the write is answered with carries no notion of the
     * elapsed debounce interval a conforming endpoint would have waited. */
    frame = rcp_ep_gpio_encode_response(3u, 0xDEADBEEFu, 0x12u, false, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                      rcp_ep_gpio_decode_response(frame.data, frame.len, 3u, &bitmask, &timed, &ts,
                                                  &tn));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0u, ts);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, bitmask);
    rcp_bytes_free(&frame);
}

/* ── SPI endpoint (TC18 13.7.3) ───────────────────────────────────────────── */

/* REQ-SPI-033 (partial): TC18 13.7.3.1/13.7.3.2 give the SPI endpoint six
 * independently pre-configured channels, selected by a request's evt[2:0].
 * Six channel configurations exist and evt[2:0] selects them; the DEVIATION
 * is only that the catalogue, not the code, was missing that statement. */
static void test_spi_six_channels_selected_by_evt(void)
{
    rcp_ep_spi_functional_cfg_t cfg;
    rcp_bytes_t                 frame;
    const uint8_t               tx[2] = {0xAAu, 0x55u};
    const uint8_t              *rx    = NULL;
    size_t                      rx_len = 0;
    uint8_t                     channel = 0xFFu, tn = 0;

    TEST_ASSERT_EQUAL_UINT8(6u, RCP_EP_SPI_MAX_CHANNELS);
    TEST_ASSERT_TRUE(rcp_ep_spi_channel_valid(0u));
    TEST_ASSERT_TRUE(rcp_ep_spi_channel_valid(5u));
    TEST_ASSERT_FALSE(rcp_ep_spi_channel_valid(6u)); /* evt[2:0] = 110b/111b */
    TEST_ASSERT_FALSE(rcp_ep_spi_channel_valid(7u));

    rcp_ep_spi_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT(6u * sizeof(rcp_ep_spi_channel_cfg_t), sizeof(cfg.channels));

    frame = rcp_ep_spi_encode_transfer_request(9u, 5u, tx, sizeof(tx), 0x31u);
    TEST_ASSERT_NOT_NULL(frame.data);
    /* evt occupies octet 4 bits 7:4; its low three bits carry the channel. */
    TEST_ASSERT_EQUAL_UINT8(5u, (uint8_t)((frame.data[4] >> 4) & 0x07u));
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
                      rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 9u, &channel, &rx,
                                                         &rx_len, &tn));
    TEST_ASSERT_EQUAL_UINT8(5u, channel);
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), rx_len);
    rcp_bytes_free(&frame);
}

/* REQ-SPI-034 (not-implemented) DEVIATION PIN: TC18 13.7.3.1 Table 38 numbers
 * fourteen SPI trigger outputs -- 0 execution done, 1 reserved, and 2..13
 * pairing CS0..CS5 with an asserted and a de-asserted event. c-RCP has a
 * 4-value per-channel selector whose whole range is 0..3 and whose evaluation
 * takes no chip-select index at all, so signals 4..13 cannot be named and no
 * CS transition is a trigger source.
 * REQ-SPI-035 (partial) DEVIATION PIN: Table 39's per-channel register block
 * is modelled in reduced form -- CPOL/CPHA are folded into one `mode` byte
 * instead of spi_clk_polarityN/spi_clk_phaseN, there is no spi_baud_rateN in
 * kbit/s, no spi_use_csN, no spi_bits_maxN, and the lead/trail times are
 * nanoseconds rather than multiples of spi_clk cycles. */
static void test_spi_trigger_numbering_and_channel_cfg_reduced(void)
{
    rcp_ep_spi_channel_cfg_t ch = {0};

    TEST_ASSERT_EQUAL_INT(0, (int)RCP_EP_SPI_TRIGGER_NONE);
    TEST_ASSERT_EQUAL_INT(3, (int)RCP_EP_SPI_TRIGGER_CS_DEASSERT);
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_ASSERT,
                                              RCP_EP_SPI_EVENT_CS_ASSERT));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_CS_ASSERT,
                                               RCP_EP_SPI_EVENT_CS_DEASSERT));
    TEST_ASSERT_TRUE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_TRANSFER_DONE,
                                              RCP_EP_SPI_EVENT_TRANSFER_DONE));
    TEST_ASSERT_FALSE(rcp_ep_spi_trigger_fires(RCP_EP_SPI_TRIGGER_NONE,
                                               RCP_EP_SPI_EVENT_TRANSFER_DONE));

    /* CPOL and CPHA are derived from one folded mode byte, not held as the
     * two separate Table 39 registers. */
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_1));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_1));
    TEST_ASSERT_TRUE(rcp_ep_spi_mode_cpol(RCP_EP_SPI_MODE_2));
    TEST_ASSERT_FALSE(rcp_ep_spi_mode_cpha(RCP_EP_SPI_MODE_2));

    /* Struct exhaustiveness: the block ends at inter_transfer_delay_ns. */
    ch.inter_transfer_delay_ns = 1000u;
    TEST_ASSERT_EQUAL_UINT32(1000u, ch.inter_transfer_delay_ns);
    TEST_ASSERT_EQUAL_UINT(sizeof(ch), offsetof(rcp_ep_spi_channel_cfg_t,
                                                inter_transfer_delay_ns)
                                            + sizeof(ch.inter_transfer_delay_ns));
}

/* REQ-SPI-036 (not-implemented) DEVIATION PIN: TC18 13.7.3.3 derives the
 * transfer length from read_size and the payload -- zero octets are appended
 * when read_size exceeds the byte_msg_payload, and the whole payload still
 * goes out on PICO when read_size is smaller. c-RCP's request encoder has no
 * read_size parameter at all: the header slot stays 0 whatever the payload
 * length, so no transfer length is derived and nothing is zero-filled.
 * REQ-SPI-037 (not-implemented) DEVIATION PIN: TC18 13.7.3.3 requires a
 * stopped SPI endpoint to latch an error state with its EP_config enable bit
 * RESET (the client must clear it and re-enable), and a clamped IO pin to set
 * the err flag in every subsequent response. c-RCP's codec is stateless: a
 * rejected request leaves the endpoint's enable bit exactly as it was. */
static void test_spi_read_size_unused_and_no_error_latch(void)
{
    rcp_acf_byte_message_info_t hdr    = {0};
    rcp_ep_spi_functional_cfg_t cfg;
    const uint8_t               tx[2]  = {0x01u, 0x02u};
    const uint8_t              *rx     = NULL;
    size_t                      rx_len = 0;
    rcp_bytes_t                 frame;
    uint8_t                     channel = 0, tn = 0;

    frame = rcp_ep_spi_encode_transfer_request(9u, 1u, tx, sizeof(tx), 0x44u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(frame.data, &hdr));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_READ, hdr.op); /* read sense: the slot is read_size */
    TEST_ASSERT_EQUAL_HEX16(0u, hdr.read_size_or_segment_num);

    rcp_ep_spi_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    /* A rejected request (wrong bus) neither latches an error state nor
     * clears the endpoint's enable bit. */
    TEST_ASSERT_EQUAL(RCP_EP_SPI_ERR_WRONG_BUS,
                      rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 10u, &channel, &rx,
                                                         &rx_len, &tn));
    TEST_ASSERT_TRUE(cfg.common.ep_enable);
    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
                      rcp_ep_spi_decode_transfer_request(frame.data, frame.len, 9u, &channel, &rx,
                                                         &rx_len, &tn));
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), rx_len);
    rcp_bytes_free(&frame);
}

/* ── I2C endpoint (TC18 13.7.7) ───────────────────────────────────────────── */

/* REQ-I2C-019 (partial) DEVIATION PIN: TC18 13.7.7.2 Table 46 defines an
 * Ultra-fast (5 Mbit/s) preset at i2c_mode value 4, which this library
 * rejects as invalid, and adds i2c_clock_divider (0x0006), i2c_trail
 * (0x0008), i2c_base_clk and i2c_ep_status -- none of which is modelled. The
 * functional config is the shared prefix plus i2c_mode and nothing else. */
static void test_i2c_mode_presets_and_register_block(void)
{
    rcp_ep_i2c_functional_cfg_t cfg;

    rcp_ep_i2c_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_I2C_MODE_STANDARD, cfg.i2c_mode);

    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(0u));
    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(1u));
    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(2u));
    TEST_ASSERT_TRUE(rcp_ep_i2c_mode_valid(3u));
    /* Table 46's Ultra-fast (5 Mbit/s) preset: a conforming implementation
     * accepts value 4. */
    TEST_ASSERT_FALSE(rcp_ep_i2c_mode_valid(4u));
    TEST_ASSERT_FALSE(rcp_ep_i2c_mode_valid(5u));
    /* The two Table 46 rows both numbered 3 are resolved conservatively to
     * the lower numbering: High-speed sits immediately after Fast mode plus. */
    TEST_ASSERT_EQUAL_INT(2, (int)RCP_EP_I2C_MODE_FAST_PLUS);
    TEST_ASSERT_EQUAL_INT(3, (int)RCP_EP_I2C_MODE_HIGH_SPEED);

    /* Struct exhaustiveness: prefix + i2c_mode, no divider/trail/base
     * clock/status register. */
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_ep_functional_cfg_t),
                           offsetof(rcp_ep_i2c_functional_cfg_t, i2c_mode));
    TEST_ASSERT_EQUAL_UINT(sizeof(cfg),
                           offsetof(rcp_ep_i2c_functional_cfg_t, i2c_mode) + sizeof(cfg.i2c_mode));
}

/* REQ-I2C-020 (implemented): TC18 13.7.7.3 makes byte_msg_payload the
 * complete I2C payload INCLUDING the target address, which the endpoint must
 * neither interpret, validate nor rewrite -- staying transparent to 7-bit and
 * 10-bit addressing alike. */
static void test_i2c_payload_address_carried_verbatim(void)
{
    /* 7-bit form: address 0x53 shifted left with the bus-level R/W bit; and
     * a 10-bit form, whose first octet is the 11110xx0 prefix. */
    const uint8_t    addr7[3]  = {0xA6u, 0x12u, 0x34u};
    const uint8_t    addr10[4] = {0xF2u, 0x34u, 0x56u, 0x78u};
    const uint8_t   *tx        = NULL;
    size_t           tx_len    = 0;
    uint16_t         read_size = 0xFFFFu;
    uint8_t          tn        = 0;
    rcp_ep_i2c_dir_t dir       = RCP_EP_I2C_DIR_READ;
    rcp_bytes_t      frame;

    frame = rcp_ep_i2c_encode_transfer_request(4u, RCP_EP_I2C_DIR_WRITE, addr7, sizeof(addr7), 0u,
                                               0x51u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
                      rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4u, &dir, &tx,
                                                         &tx_len, &read_size, &tn));
    TEST_ASSERT_EQUAL(RCP_EP_I2C_DIR_WRITE, dir);
    TEST_ASSERT_EQUAL_UINT(sizeof(addr7), tx_len);
    TEST_ASSERT_EQUAL_HEX8(0xA6u, tx[0]); /* address octet unchanged */
    TEST_ASSERT_EQUAL_INT(0, memcmp(addr7, tx, sizeof(addr7)));
    TEST_ASSERT_EQUAL_UINT8(0x51u, tn);
    rcp_bytes_free(&frame);

    frame = rcp_ep_i2c_encode_transfer_request(4u, RCP_EP_I2C_DIR_WRITE, addr10, sizeof(addr10),
                                               0u, 0x52u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_I2C_OK,
                      rcp_ep_i2c_decode_transfer_request(frame.data, frame.len, 4u, &dir, &tx,
                                                         &tx_len, &read_size, &tn));
    TEST_ASSERT_EQUAL_UINT(sizeof(addr10), tx_len);
    TEST_ASSERT_EQUAL_HEX8(0xF2u, tx[0]); /* 10-bit prefix octet unchanged */
    TEST_ASSERT_EQUAL_INT(0, memcmp(addr10, tx, sizeof(addr10)));
    rcp_bytes_free(&frame);
}

/* ── PWM endpoints (TC18 13.7.5 / 13.7.6) ─────────────────────────────────── */

/* REQ-PWM-055 (partial) DEVIATION PIN: TC18 13.7.5.1 generates PWM_OUT
 * trigger signals from the SKEW-DELAYED output (the break-before-make
 * provision for half/full-bridge drivers) and fires the mid-active-pulse
 * trigger even at 0% duty cycle. c-RCP's trigger evaluation is a pure
 * selector-vs-event match taking neither the skew register nor the duty
 * cycle as an input, so pwmo_skew is stored and never consulted.
 * REQ-PWM-056 (partial) DEVIATION PIN: TC18 13.7.5.2 Table 43 requires a
 * requested active time below pwmo_duty_cycle_min or above
 * pwmo_duty_cycle_max to be CAPPED to that limit. c-RCP stores both
 * registers and applies neither: the requested active duration is returned
 * verbatim, outside the configured window in both directions. */
static void test_pwm_out_trigger_and_duty_cap_gaps(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    rcp_ep_pwm_value_t              current = {1000u, 400u};
    rcp_ep_pwm_value_t              out;

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    cfg.skew           = 0x2Au;
    cfg.duty_cycle_min = 100u;
    cfg.duty_cycle_max = 500u;

    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE,
                                                  RCP_EP_PWM_OUT_EVENT_MID_PULSE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_MID_PULSE,
                                                   RCP_EP_PWM_OUT_EVENT_CYCLE_START));
    TEST_ASSERT_TRUE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_DONE,
                                                  RCP_EP_PWM_OUT_EVENT_DONE));
    TEST_ASSERT_FALSE(rcp_ep_pwm_out_trigger_fires(RCP_EP_PWM_OUT_TRIGGER_NONE,
                                                   RCP_EP_PWM_OUT_EVENT_DONE));
    TEST_ASSERT_EQUAL_UINT8(0x2Au, cfg.skew); /* held, never consulted above */

    /* Above pwmo_duty_cycle_max (500): a conforming endpoint caps to 500. */
    out = rcp_ep_pwm_out_apply_write(current, (rcp_ep_pwm_value_t){2000u, 900u},
                                     RCP_EP_PWM_OUT_WRITE_REPLACE);
    TEST_ASSERT_EQUAL_UINT16(900u, out.active_duration);
    TEST_ASSERT_EQUAL_UINT16(2000u, out.period);

    /* Below pwmo_duty_cycle_min (100): a conforming endpoint caps to 100. */
    out = rcp_ep_pwm_out_apply_write(current, (rcp_ep_pwm_value_t){2000u, 10u},
                                     RCP_EP_PWM_OUT_WRITE_REPLACE);
    TEST_ASSERT_EQUAL_UINT16(10u, out.active_duration);
    TEST_ASSERT_EQUAL_UINT16(500u, cfg.duty_cycle_max);
    TEST_ASSERT_EQUAL_UINT16(100u, cfg.duty_cycle_min);
}

/* REQ-PWM-057 (not-implemented) DEVIATION PIN: TC18 13.7.5.3 gives a PWM_OUT
 * request four rules c-RCP applies none of -- PWM_Period == 0 stops signal
 * generation; PWM_active == 0 with PWM_Period > 0 keeps the endpoint running
 * with the output disabled but the trigger signals still generated; a trigger
 * configuration request reads its first two payload octets as a PHASE SHIFT
 * rather than a period; and the output pin is read back during generation
 * with an error signalled if it does not toggle. Both fields are opaque
 * 16-bit setpoints stored verbatim, and the first two payload octets are
 * always decoded as the period. */
static void test_pwm_out_request_semantics_are_verbatim_setpoints(void)
{
    rcp_ep_pwm_value_t current = {1000u, 400u};
    rcp_ep_pwm_value_t out;
    rcp_ep_pwm_value_t decoded = {0u, 0u};
    rcp_bytes_t        frame;
    uint8_t            evt = 0xFFu, tn = 0;

    /* Period 0: stored, not treated as "stop signal generation". */
    out = rcp_ep_pwm_out_apply_write(current, (rcp_ep_pwm_value_t){0u, 400u},
                                     RCP_EP_PWM_OUT_WRITE_REPLACE);
    TEST_ASSERT_EQUAL_UINT16(0u, out.period);
    TEST_ASSERT_EQUAL_UINT16(400u, out.active_duration);

    /* Active 0 with period > 0: stored, with no output-disabled state. */
    out = rcp_ep_pwm_out_apply_write(current, (rcp_ep_pwm_value_t){800u, 0u},
                                     RCP_EP_PWM_OUT_WRITE_REPLACE);
    TEST_ASSERT_EQUAL_UINT16(800u, out.period);
    TEST_ASSERT_EQUAL_UINT16(0u, out.active_duration);

    /* The first two payload octets are always the period, never a phase
     * shift, whatever the request is meant to configure. */
    frame = rcp_ep_pwm_out_encode_write_request(6u, (rcp_ep_pwm_value_t){0x1234u, 0x5678u},
                                                RCP_EP_PWM_OUT_WRITE_REPLACE, 0x61u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_PWM_PAYLOAD_LEN, 4u);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK,
                      rcp_ep_pwm_out_decode_write_request(frame.data, frame.len, 6u, &decoded,
                                                          &evt, &tn));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, decoded.period);
    TEST_ASSERT_EQUAL_HEX16(0x5678u, decoded.active_duration);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_OUT_WRITE_REPLACE, evt);
    rcp_bytes_free(&frame);
}

/* REQ-PWM-058 (not-implemented) DEVIATION PIN: none of TC18 13.7.6.2
 * Table 45's PWM_IN registers exists -- pwmi_polarity, pwmi_err_on_max_period,
 * pwmi_continuous_mode, pwmi_max_period, pwmi_base_clk, pwmi_clk_divider and
 * pwmi_ep_status are all absent. The functional config is the shared prefix
 * plus a trigger selector, and the measurement-timeout sentinel is the only
 * other related surface. */
static void test_pwm_in_functional_cfg_is_trigger_only(void)
{
    rcp_ep_pwm_in_functional_cfg_t cfg;

    rcp_ep_pwm_in_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_PWM_IN_TRIGGER_NONE, cfg.trigger);
    TEST_ASSERT_FALSE(cfg.common.ep_enable);

    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_ep_functional_cfg_t),
                           offsetof(rcp_ep_pwm_in_functional_cfg_t, trigger));
    TEST_ASSERT_EQUAL_UINT(sizeof(cfg), offsetof(rcp_ep_pwm_in_functional_cfg_t, trigger)
                                            + sizeof(cfg.trigger));

    /* No pwmi_polarity: the capture-edge selector is the only polarity-like
     * control, and it is a two-edge enum, not an active-phase level. */
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, false, true));
    TEST_ASSERT_FALSE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_RISING, true, false));
    TEST_ASSERT_TRUE(rcp_ep_pwm_in_trigger_fires(RCP_EP_PWM_IN_TRIGGER_FALLING, true, false));

    /* No pwmi_max_period register to exceed: the timeout is reported only as
     * this sentinel measurement value. */
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, RCP_EP_PWM_IN_NO_SIGNAL);
}

/* ── WakeUp endpoint (TC18 12.4 / 12.5 / 13.7.2) ──────────────────────────── */

/* REQ-WAKEUP-017 (not-implemented) DEVIATION PIN: TC18 12.4.1 requires the
 * repetitive wake response to convey both a WakeUp message AND the WakeUp
 * source that caused the wake. c-RCP's WakeUp message is a single opcode
 * octet and its decoder recovers only the transaction number, so a client
 * cannot learn which configured source woke the server.
 * REQ-WAKEUP-018 (not-implemented) DEVIATION PIN: TC18 12.4.1 also makes the
 * WakeUp repetition time part of the endpoint's functional configuration.
 * The config is the shared prefix plus the wake-source table and nothing
 * else -- there is no interval field to configure or discover. */
static void test_wakeup_message_and_repetition_time_gaps(void)
{
    rcp_ep_wakeup_functional_cfg_t cfg;
    rcp_acf_byte_message_info_t    hdr         = {0};
    const uint8_t                 *payload     = NULL;
    size_t                         payload_len = 0;
    rcp_bytes_t                    frame;
    uint8_t                        tn = 0;

    frame = rcp_ep_wakeup_encode_wakeup_message(2u, 0x77u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                      rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    /* One octet, the opcode, and no wake-source field of any width. */
    TEST_ASSERT_EQUAL_UINT(1u, payload_len);
    TEST_ASSERT_EQUAL_HEX8(RCP_EP_WAKEUP_WAKEUP_OPCODE, payload[0]);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, 2u, &tn));
    TEST_ASSERT_EQUAL_UINT8(0x77u, tn);
    rcp_bytes_free(&frame);

    rcp_ep_wakeup_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT(8u, RCP_EP_WAKEUP_MAX_SOURCES);
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_ep_functional_cfg_t),
                           offsetof(rcp_ep_wakeup_functional_cfg_t, sources));
    TEST_ASSERT_EQUAL_UINT(sizeof(cfg), offsetof(rcp_ep_wakeup_functional_cfg_t, sources)
                                            + sizeof(cfg.sources));
}

/* REQ-WAKEUP-019 (not-implemented) DEVIATION PIN: TC18 12.5 requires a
 * refused sleep/standby request to be answered with an ERROR response
 * carrying error code REQUEST_CANCELED. c-RCP answers with a positive-form
 * SleepCMD response whose err bit is clear and whose payload byte is the
 * module-local RCP_PWRMODE_ENTRY_REFUSED value -- a conforming RC Client
 * watching for an error response never sees the refusal. */
static void test_wakeup_refusal_is_positive_response_not_error(void)
{
    rcp_acf_byte_message_info_t hdr    = {0};
    rcp_pwrmode_entry_result_t  result = RCP_PWRMODE_ENTRY_OK;
    rcp_bytes_t                 frame;
    uint8_t                     tn = 0;

    frame = rcp_ep_wakeup_encode_sleepcmd_response(1u, RCP_PWRMODE_ENTRY_REFUSED, 0x33u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(frame.data, &hdr));

    /* Not an error response: err is clear and the message classifies as an
     * ordinary write response. */
    TEST_ASSERT_EQUAL_UINT8(0u, hdr.err);
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_WRITE, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_NOT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));

    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_sleepcmd_response(frame.data, frame.len, 1u, &result,
                                                             &tn));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, result);
    TEST_ASSERT_EQUAL_UINT8(0x33u, tn);
    /* The numbered wire code TC18 12.5 calls for is never carried anywhere
     * in this exchange. */
    {
        /* Held in ints, not compared as their own enum types: MSVC's C5287
         * rejects a direct comparison of two different enumerations, and
         * the point here is precisely that these are two unrelated
         * numbering schemes. */
        const int wire_code  = (int)RCP_ERROR_REQUEST_CANCELED;
        const int local_code = (int)RCP_PWRMODE_ENTRY_REFUSED;

        TEST_ASSERT_EQUAL_INT(5, wire_code);
        TEST_ASSERT_NOT_EQUAL_INT(wire_code, local_code);
    }
    rcp_bytes_free(&frame);
}

/* REQ-WAKEUP-020 (not-implemented) DEVIATION PIN: TC18 13.7.2.1 fixes the
 * WakeUp endpoint at endpoint number 1 -- the only endpoint that stays active
 * in Sleep mode. Every c-RCP entry point takes a caller-supplied
 * byte_bus_id and the library defines no fixed WakeUp endpoint index, so a
 * WakeUp message addressed to an arbitrary endpoint is perfectly valid. */
static void test_wakeup_codec_accepts_any_bus_id(void)
{
    rcp_bytes_t frame;
    uint8_t     tn = 0;

    frame = rcp_ep_wakeup_encode_wakeup_message(0xB7u, 0x0Au);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, 0xB7u, &tn));
    TEST_ASSERT_EQUAL_UINT8(0x0Au, tn);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_is_wakeup_echo(frame.data, frame.len, 0xB7u, 0x0Au));
    /* The fixed endpoint number the clause names is simply a different bus
     * id, rejected like any other mismatch. */
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_WRONG_BUS,
                      rcp_ep_wakeup_decode_wakeup_message(frame.data, frame.len, 1u, &tn));
    rcp_bytes_free(&frame);

    /* ep_type is this endpoint TYPE's regmap value, not an endpoint NUMBER
     * -- nothing pins the endpoint index. */
    TEST_ASSERT_EQUAL_HEX8(0x01u, RCP_EP_WAKEUP_EP_TYPE);
}

/* REQ-WAKEUP-021 (partial) DEVIATION PIN: TC18 13.7.2.2 Table 36 makes
 * wup_status a 16-bit register with one bit per wake-up source, cleared by
 * writing a 1 to that bit, and wup_io_scrN a packed [10:0] IO pin number plus
 * [15:11] IO behaviour with pin number 0 terminating the table; wup_ep_len
 * and wup_nr_io_pins_max also exist. c-RCP has a single boolean latch cleared
 * wholesale, and a source table of {enabled, active_high} pairs.
 * REQ-WAKEUP-022 (not-implemented) DEVIATION PIN: Table 37's IO_SRC[15:11]
 * behaviour encoding (0b00000 inactive, 0b00001 rising, 0b00010 falling,
 * 0b00011 both edges, 0b00100 high level, 0b00101 low level, rest reserved)
 * collapses to a level-only polarity bit: assertion is a function of the
 * CURRENT pin level alone, no edge mode is expressible, and no reserved
 * encoding is rejected because none can be expressed. */
static void test_wakeup_status_latch_and_source_cfg_reduced(void)
{
    rcp_ep_wakeup_wup_status_t     status;
    rcp_ep_wakeup_source_cfg_t     high = {true, true};
    rcp_ep_wakeup_source_cfg_t     low  = {true, false};
    rcp_ep_wakeup_functional_cfg_t cfg;
    bool                           levels[2] = {false, true};

    rcp_ep_wakeup_wup_status_init(&status);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&status));
    rcp_ep_wakeup_wup_status_latch(&status);
    TEST_ASSERT_FALSE(rcp_ep_wakeup_wup_status_is_clear(&status));
    /* One wholesale clear, not a per-source write-1-to-clear bit. */
    rcp_ep_wakeup_wup_status_clear(&status);
    TEST_ASSERT_TRUE(rcp_ep_wakeup_wup_status_is_clear(&status));
    TEST_ASSERT_EQUAL_UINT(sizeof(bool), sizeof(status));

    /* Level modes only: the predicate takes the current level and nothing
     * else, so rising/falling/both-edges have no representation. */
    TEST_ASSERT_TRUE(rcp_ep_wakeup_source_asserted(high, true));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_source_asserted(high, false));
    TEST_ASSERT_TRUE(rcp_ep_wakeup_source_asserted(low, false));
    TEST_ASSERT_FALSE(rcp_ep_wakeup_source_asserted(low, true));
    TEST_ASSERT_EQUAL_UINT(2u * sizeof(bool), sizeof(rcp_ep_wakeup_source_cfg_t));

    /* No IO pin number and no end-of-table sentinel: a source is addressed
     * by its table index, and a disabled slot never asserts. */
    rcp_ep_wakeup_functional_cfg_init(&cfg);
    TEST_ASSERT_FALSE(rcp_ep_wakeup_any_source_asserted(&cfg, levels, 2u));
    cfg.sources[1] = high;
    TEST_ASSERT_TRUE(rcp_ep_wakeup_any_source_asserted(&cfg, levels, 2u));
}

/* ── E2E request-stream configuration (TC18 12.7.7 Table 22) ──────────────── */

/* REQ-E2E-028/029 (partial) DEVIATION PIN: TC18 12.7.7 Table 22 relative
 * address 0x000D bits 1/2 (rx_enforce_seq/rx_seq_safestate_enable) require
 * rejecting a request whose AVTPDU sequence number has not increased, and
 * escalating every endpoint on the stream to its configured safe state on a
 * discontinuity. rcp_e2e_seq_evaluate() (e2e.h) now implements both
 * decisions as a pure, directly-testable primitive -- see the tests below --
 * but rcp_server_endpoint_admit() itself still has no sequence_num input at
 * all (it operates on the ACF payload only; the AVTPDU header, including
 * sequence_num, is decoded and discarded one layer above it, in whatever
 * caller demultiplexes an AVTP frame before handing admit() the ACF
 * message). Wiring rcp_e2e_seq_evaluate() into the actual admission path
 * therefore needs sequence_num threaded through that caller -- e.g.
 * mock.c's rcp_mock_server_dispatch_frame()/rcp_mock_server_dispatch() --
 * which is a larger, separate integration change, not done here. The very
 * same frame replayed byte-for-byte is still admitted and stored a second
 * time by admit() itself. */
static void test_e2e_replayed_request_is_admitted_again(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame;
    const uint8_t         payload[2]   = {0xC0u, 0xDEu};
    uint8_t               request_type = 0;
    size_t                index        = 0;

    frame = rcp_timed_encode_request(3u, 0x1000u, 0x40u, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(frame.data);

    rcp_server_endpoint_init(&ep, true);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, &request_type,
                                                &index, NULL));
    /* Identical AVTPDU content, no sequence advance: filed again anyway --
     * admit() itself still has no sequence_num input to gate on. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, &request_type,
                                                &index, NULL));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_server_endpoint_pending_count(&ep));

    rcp_server_endpoint_destroy(&ep);
    rcp_bytes_free(&frame);
}

/* Companion to the deviation pin above: the watchdog remains the only
 * safe-state entry path admit()'s own call chain can reach; rcp_e2e_wd_evaluate()
 * still takes no sequence number, and with the watchdog disabled no elapsed
 * time whatsoever produces enter_safe_state. rcp_e2e_seq_evaluate()'s own
 * enter_safe_state field (tested separately below) is the sequence-driven
 * counterpart, available as a primitive but not yet wired into any
 * admission call chain -- see the deviation pin above. */
static void test_e2e_safe_state_only_reachable_via_watchdog(void)
{
    rcp_e2e_wd_result_t r;

    /* Watchdog off: no input to this module can drive the safe state. */
    r = rcp_e2e_wd_evaluate(false, 10u, true, true, 1000000u);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);

    /* Watchdog on and expired: this is the ONLY route to enter_safe_state. */
    r = rcp_e2e_wd_evaluate(true, 10u, true, false, 10u);
    TEST_ASSERT_TRUE(r.overflowed);
    TEST_ASSERT_TRUE(r.enter_safe_state);
    TEST_ASSERT_FALSE(r.notify);

    r = rcp_e2e_wd_evaluate(true, 10u, false, true, 11u);
    TEST_ASSERT_TRUE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_TRUE(r.notify);

    /* Not yet expired: still nothing. */
    r = rcp_e2e_wd_evaluate(true, 10u, true, true, 9u);
    TEST_ASSERT_FALSE(r.overflowed);
    TEST_ASSERT_FALSE(r.enter_safe_state);
}

/* REQ-E2E-028: the first call on a fresh tracker has nothing to compare
 * against, so it always accepts and is never a discontinuity, regardless
 * of what seq it's given or how rx_enforce_seq/rx_seq_safestate_enable are
 * set. */
static void test_e2e_seq_evaluate_first_call_always_accepts(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    TEST_ASSERT_FALSE(t.has_prev);

    r = rcp_e2e_seq_evaluate(&t, true, true, 200u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_TRUE(t.has_prev);
    TEST_ASSERT_EQUAL_UINT8(200u, t.prev_seq);
}

/* REQ-E2E-028: a single-increment sequence always accepts and is never a
 * discontinuity, with rx_enforce_seq on. */
static void test_e2e_seq_evaluate_single_increment_is_clean(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 10u);

    r = rcp_e2e_seq_evaluate(&t, true, true, 11u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(11u, t.prev_seq);
}

/* REQ-E2E-028/029: a gap (increase by more than one) still accepts -- order
 * was preserved, just not every seq in between -- but IS a discontinuity,
 * so enter_safe_state tracks rx_seq_safestate_enable independently of
 * accept. */
static void test_e2e_seq_evaluate_gap_accepts_but_is_a_discontinuity(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 10u);

    r = rcp_e2e_seq_evaluate(&t, true, true, 13u); /* skipped 11, 12 */
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_TRUE(r.discontinuity);
    TEST_ASSERT_TRUE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(13u, t.prev_seq);

    /* Same gap, but rx_seq_safestate_enable off: still a discontinuity,
     * just no escalation. */
    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, false, 10u);
    r = rcp_e2e_seq_evaluate(&t, true, false, 13u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_TRUE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
}

/* REQ-E2E-028: a stale/replayed or reordered-backward seq is rejected when
 * rx_enforce_seq is on, and the tracker's prev_seq does NOT move -- a
 * rejected replay must not become the new reference point (see this
 * primitive's own doc comment for why). */
static void test_e2e_seq_evaluate_replay_is_rejected_and_does_not_move_tracker(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 50u);

    /* Exact replay of the same seq. */
    r = rcp_e2e_seq_evaluate(&t, true, true, 50u);
    TEST_ASSERT_FALSE(r.accept);
    TEST_ASSERT_EQUAL_UINT8(50u, t.prev_seq); /* unmoved */

    /* An older seq (reordered-backward). */
    r = rcp_e2e_seq_evaluate(&t, true, true, 40u);
    TEST_ASSERT_FALSE(r.accept);
    TEST_ASSERT_EQUAL_UINT8(50u, t.prev_seq); /* still unmoved */

    /* The genuine next seq is still correctly accepted afterward -- the
     * rejected replays never displaced the real reference point. */
    r = rcp_e2e_seq_evaluate(&t, true, true, 51u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
}

/* REQ-E2E-028: rx_enforce_seq off means every seq accepts regardless of
 * direction (a real replay included), while discontinuity/enter_safe_state
 * are still computed and reported -- rx_enforce_seq and
 * rx_seq_safestate_enable are independent bits, per this primitive's own
 * doc comment. */
static void test_e2e_seq_evaluate_enforce_off_always_accepts(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, false, true, 50u);

    r = rcp_e2e_seq_evaluate(&t, false, true, 10u); /* a real backward jump */
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_TRUE(r.discontinuity);
    TEST_ASSERT_TRUE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(10u, t.prev_seq); /* advances: this call accepted */
}

/* REQ-E2E-028: sequence_num is an 8-bit free-running counter (avtp.h) that
 * wraps 0xFF -> 0x00 over any long-lived stream; that wrap is a single
 * increment, not a discontinuity -- see this primitive's own doc comment
 * for the RFC 1982 comparison technique this relies on. */
static void test_e2e_seq_evaluate_wraparound_is_a_clean_single_increment(void)
{
    rcp_e2e_seq_tracker_t t;
    rcp_e2e_seq_result_t  r;

    rcp_e2e_seq_tracker_init(&t);
    (void)rcp_e2e_seq_evaluate(&t, true, true, 0xFFu);

    r = rcp_e2e_seq_evaluate(&t, true, true, 0x00u);
    TEST_ASSERT_TRUE(r.accept);
    TEST_ASSERT_FALSE(r.discontinuity);
    TEST_ASSERT_FALSE(r.enter_safe_state);
    TEST_ASSERT_EQUAL_UINT8(0x00u, t.prev_seq);
}

/* REQ-E2E-030 (partial) DEVIATION PIN: TC18 12.7.7 Table 22 relative
 * address 0x000D bit 5 (rx_ovrflw_safestate_enable) brings every endpoint
 * bound to the request stream into its configured safe state when any one
 * endpoint's request storage overflows. rcp_server_endpoint_admit() now
 * reports the per-request half conformantly -- *out_error is
 * RCP_ERROR_REQUEST_STORAGE_OVERFLOW, letting a caller build a real Table 27
 * error response via rcp_acf_build_error_response() (see mock.c's
 * finish_admission() for a worked example) -- but the stream-wide safe-state
 * escalation itself is still not performed by this call: this library's
 * rcp_server_endpoint_t type has no notion of "every other endpoint bound to
 * the same request stream" for a single endpoint's admit() to reach across
 * into. rcp_e2e_overflow_should_enter_safe_state() is the pure decision a
 * caller-owned orchestrator would consult to actually perform that
 * escalation once such a caller exists. */
static void test_e2e_request_store_overflow_reports_error_code_but_not_escalation(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame;
    uint8_t               request_type = 0;
    rcp_wire_error_t      err          = RCP_ERROR_NONE;
    size_t                i;

    frame = rcp_timed_encode_request(3u, 0x2000u, 0x41u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);

    rcp_server_endpoint_init(&ep, true);
    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                          rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, &request_type,
                                                    NULL, &err));
        TEST_ASSERT_EQUAL(RCP_ERROR_NONE, err);
    }
    TEST_ASSERT_EQUAL_UINT(RCP_SERVER_MAX_PENDING, rcp_server_endpoint_pending_count(&ep));

    /* Overflow: rejected and dropped, but now with a real Table 27 code a
     * caller can turn into a conformant error response. */
    err = RCP_ERROR_NONE;
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_REJECTED,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u, &request_type,
                                                NULL, &err));
    TEST_ASSERT_EQUAL(RCP_ERROR_REQUEST_STORAGE_OVERFLOW, err);
    TEST_ASSERT_EQUAL_UINT(RCP_SERVER_MAX_PENDING, rcp_server_endpoint_pending_count(&ep));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_server_endpoint_queue_len(&ep));

    rcp_server_endpoint_destroy(&ep);
    rcp_bytes_free(&frame);
}

/* REQ-E2E-030: the pure escalation-decision primitive a stream-wide
 * orchestrator (not yet part of this codebase -- see the deviation pin
 * above) would consult once request-storage overflow has already been
 * detected. Trivially rx_ovrflw_safestate_enable, gated on nothing else,
 * since the overflow condition is a given by the time this is called. */
static void test_e2e_overflow_should_enter_safe_state_is_gated_only_on_the_config_bit(void)
{
    TEST_ASSERT_TRUE(rcp_e2e_overflow_should_enter_safe_state(true));
    TEST_ASSERT_FALSE(rcp_e2e_overflow_should_enter_safe_state(false));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_acf_msg_type_constants_and_op_wire_bit);
    RUN_TEST(test_acf_read_size_slot_is_ambiguous);
    RUN_TEST(test_acf_bus_id_is_now_eleven_bits_wide);
    RUN_TEST(test_acf_request_flags_round_trip_unconstrained);

    RUN_TEST(test_gpio_request_payload_is_four_octets);
    RUN_TEST(test_gpio_trigger_numbering_and_functional_cfg_gaps);
    RUN_TEST(test_gpio_response_timing_is_not_modelled);

    RUN_TEST(test_spi_six_channels_selected_by_evt);
    RUN_TEST(test_spi_trigger_numbering_and_channel_cfg_reduced);
    RUN_TEST(test_spi_read_size_unused_and_no_error_latch);

    RUN_TEST(test_i2c_mode_presets_and_register_block);
    RUN_TEST(test_i2c_payload_address_carried_verbatim);

    RUN_TEST(test_pwm_out_trigger_and_duty_cap_gaps);
    RUN_TEST(test_pwm_out_request_semantics_are_verbatim_setpoints);
    RUN_TEST(test_pwm_in_functional_cfg_is_trigger_only);

    RUN_TEST(test_wakeup_message_and_repetition_time_gaps);
    RUN_TEST(test_wakeup_refusal_is_positive_response_not_error);
    RUN_TEST(test_wakeup_codec_accepts_any_bus_id);
    RUN_TEST(test_wakeup_status_latch_and_source_cfg_reduced);

    RUN_TEST(test_e2e_replayed_request_is_admitted_again);
    RUN_TEST(test_e2e_safe_state_only_reachable_via_watchdog);
    RUN_TEST(test_e2e_seq_evaluate_first_call_always_accepts);
    RUN_TEST(test_e2e_seq_evaluate_single_increment_is_clean);
    RUN_TEST(test_e2e_seq_evaluate_gap_accepts_but_is_a_discontinuity);
    RUN_TEST(test_e2e_seq_evaluate_replay_is_rejected_and_does_not_move_tracker);
    RUN_TEST(test_e2e_seq_evaluate_enforce_off_always_accepts);
    RUN_TEST(test_e2e_seq_evaluate_wraparound_is_a_clean_single_increment);
    RUN_TEST(test_e2e_request_store_overflow_reports_error_code_but_not_escalation);
    RUN_TEST(test_e2e_overflow_should_enter_safe_state_is_gated_only_on_the_config_bit);

    return UNITY_END();
}
