/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ADC-001
//cfusa:test REQ-ADC-002
//cfusa:test REQ-ADC-003
//cfusa:test REQ-ADC-004
//cfusa:test REQ-ADC-005
//cfusa:test REQ-ADC-006
//cfusa:test REQ-ADC-007
//cfusa:test REQ-ADC-008
//cfusa:test REQ-ADC-009
//cfusa:test REQ-ADC-010
//cfusa:test REQ-ADC-011
//cfusa:test REQ-ADC-012
//cfusa:test REQ-ADC-013
//cfusa:test REQ-ADC-014
//cfusa:test REQ-ADC-015
//cfusa:test REQ-ADC-016
//cfusa:test REQ-ADC-017
//cfusa:test REQ-ADC-018
//cfusa:test REQ-ADC-019
//cfusa:test REQ-ADC-020
//cfusa:test REQ-ADC-021
//cfusa:test REQ-ADC-022
//cfusa:test REQ-ADC-023
//cfusa:test REQ-ADC-024
//cfusa:test REQ-ADC-025
//cfusa:test REQ-ADC-026
//cfusa:test REQ-ADC-027
//cfusa:test REQ-ADC-028
//cfusa:test REQ-ADC-029
//cfusa:test REQ-ADC-030
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_adc.h>
#include <rcp/ep_pwm.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Response geometry ─────────────────────────────────────────────────────── */

/* TC18 v0.5.1_RC §13.7.9.3 "ADC request handling":
 *
 *   The ADC request has no byte_msg_payload, while a wait-request needs a
 *   byte_msg_payload. Responses contain as much measurement values as
 *   requested by half the read size of a request.
 *
 * and the section's own worked example (Figure 33/34): "The readsize is
 * set to 16 such that 8 measurements are expected in return", answered by
 * an ADC response carrying "16 bit ADC value 0" through "16 bit ADC value
 * 7". So read_size 16 -> 8 values, and each value is 2 octets. */
static void test_response_value_count_is_half_read_size(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, rcp_ep_adc_response_value_count(16u));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_ep_adc_response_value_count(2u));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_ep_adc_response_value_count(0u));
    /* An odd read_size cannot describe a whole number of 2-octet values. */
    TEST_ASSERT_EQUAL_UINT(0u, rcp_ep_adc_response_value_count(3u));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, RCP_EP_ADC_VALUE_LEN);
}

/* ── Layer 1: rcp_ep_adc_average_interval() ──────────────────────────────────── */

/* TC18 v0.5.1_RC §13.7.9.2, the sentence immediately after Table 51:
 *
 *   If the response includes a timestamp, it shall represent the point in
 *   time when the last sample that was used for the first average value
 *   that is included in the response has been captured.
 *
 * So an averaging interval's own timestamp is that of its LAST
 * contributing sample -- the end of the window. For samples captured at
 * 1000/1001/1002 that is 1002, not 1000 (which is what this module
 * previously reported). */
static void test_average_interval_computes_mean(void)
{
    rcp_ep_adc_sample_t samples[3] = {
        {100, 1000}, {200, 1001}, {300, 1002},
    };
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 3);

    TEST_ASSERT_EQUAL_UINT16(200, result.value);
    TEST_ASSERT_EQUAL_UINT64(1002, result.timestamp);
}

static void test_average_interval_zero_samples_is_no_signal(void)
{
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(NULL, 0);

    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, result.value);
    TEST_ASSERT_EQUAL_UINT64(0, result.timestamp);
}

static void test_average_interval_skips_no_signal_samples(void)
{
    rcp_ep_adc_sample_t samples[3] = {
        {100, 2000}, {RCP_EP_PWM_IN_NO_SIGNAL, 2001}, {300, 2002},
    };
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 3);

    TEST_ASSERT_EQUAL_UINT16(200, result.value); /* mean of 100 and 300 only */
    /* §13.7.9.2: "the last sample that was USED". The timed-out sample at
     * 2001 fed nothing, so the interval's moment is 2002 -- the last
     * sample that did. */
    TEST_ASSERT_EQUAL_UINT64(2002, result.timestamp);
}

/* Same rule, with the timeout at the END of the interval: the last
 * *used* sample is the one at 4001, not the timed-out one at 4002. */
static void test_average_interval_timestamp_skips_trailing_no_signal(void)
{
    rcp_ep_adc_sample_t samples[3] = {
        {100, 4000}, {300, 4001}, {RCP_EP_PWM_IN_NO_SIGNAL, 4002},
    };
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 3);

    TEST_ASSERT_EQUAL_UINT16(200, result.value);
    TEST_ASSERT_EQUAL_UINT64(4001, result.timestamp);
}

static void test_average_interval_all_no_signal_is_no_signal(void)
{
    rcp_ep_adc_sample_t samples[2] = {
        {RCP_EP_PWM_IN_NO_SIGNAL, 3000}, {RCP_EP_PWM_IN_NO_SIGNAL, 3001},
    };
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 2);

    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, result.value);
    /* No sample was "used" at all, so the interval's closing moment --
     * its last sample -- is reported. */
    TEST_ASSERT_EQUAL_UINT64(3001, result.timestamp);
}

/* §13.7.9.2's timestamp rule again, minimal case: two samples, the
 * interval's moment is the second one's. */
static void test_average_interval_timestamp_is_last_used_sample(void)
{
    rcp_ep_adc_sample_t samples[2] = {{10, 42}, {20, 99}};
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 2);

    TEST_ASSERT_EQUAL_UINT64(99, result.timestamp);
}

/* ── Layers 2/3: rcp_ep_adc_collect_response_values() ────────────────────────── */

/* TC18 v0.5.1_RC Table 51 "adc functional configuration" (§13.7.9.2),
 * relative address 0x000C:
 *
 *   adc_combine_avg_values | 8 bit | R/W | Nr of output values to be
 *                                          combined in one response
 *
 * and §13.7.9.1: "the COMBINE_NR_VAL defines how many measurement values
 * (i.e. averaging results) will be put in one response frame." It is a
 * COUNT of output values, not a four-way AVERAGE/MIN/MAX/LATEST reduction
 * mode -- the modelling this module previously used, which collapsed
 * every averaged value into a single 2-octet response payload. The
 * averaged values go into the response in capture order, unreduced. */
static void test_collect_response_values_packs_in_capture_order(void)
{
    rcp_ep_adc_avg_value_t avgs[3] = {
        {100, 0}, {200, 0}, {300, 0},
    };
    uint16_t out[3] = {0};

    TEST_ASSERT_EQUAL_UINT(3u, rcp_ep_adc_collect_response_values(avgs, 3, out, 3));
    TEST_ASSERT_EQUAL_UINT16(100, out[0]);
    TEST_ASSERT_EQUAL_UINT16(200, out[1]);
    TEST_ASSERT_EQUAL_UINT16(300, out[2]);
}

/* A COMBINE_NR_VAL smaller than the number of averages available takes
 * the leading ones -- the response is exactly COMBINE_NR_VAL values wide. */
static void test_collect_response_values_takes_leading_count(void)
{
    rcp_ep_adc_avg_value_t avgs[4] = {
        {10, 0}, {20, 0}, {30, 0}, {40, 0},
    };
    uint16_t out[2] = {0xEEEE, 0xEEEE};

    TEST_ASSERT_EQUAL_UINT(2u, rcp_ep_adc_collect_response_values(avgs, 4, out, 2));
    TEST_ASSERT_EQUAL_UINT16(10, out[0]);
    TEST_ASSERT_EQUAL_UINT16(20, out[1]);
}

/* §13.7.9.2: "When adc_combine_avg_values > adc_avg_intervals_per_request,
 * then multiple requests need to be executed, before a response will be
 * generated." Reported here as a short count, so a caller knows how many
 * values it still owes before it may send the response. */
static void test_collect_response_values_reports_short_count(void)
{
    rcp_ep_adc_avg_value_t avgs[2] = {{10, 0}, {20, 0}};
    uint16_t out[8];
    size_t i;

    for (i = 0; i < 8; i++) out[i] = 0xEEEE;

    TEST_ASSERT_EQUAL_UINT(2u, rcp_ep_adc_collect_response_values(avgs, 2, out, 8));
    TEST_ASSERT_EQUAL_UINT16(10, out[0]);
    TEST_ASSERT_EQUAL_UINT16(20, out[1]);
    TEST_ASSERT_EQUAL_UINT16(0xEEEE, out[2]); /* untouched */
}

/* A timed-out averaging interval goes out as NO_SIGNAL in its own slot,
 * rather than being averaged away with its neighbours. */
static void test_collect_response_values_carries_no_signal_verbatim(void)
{
    rcp_ep_adc_avg_value_t avgs[3] = {
        {300, 0}, {RCP_EP_PWM_IN_NO_SIGNAL, 0}, {100, 0},
    };
    uint16_t out[3] = {0};

    TEST_ASSERT_EQUAL_UINT(3u, rcp_ep_adc_collect_response_values(avgs, 3, out, 3));
    TEST_ASSERT_EQUAL_UINT16(300, out[0]);
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, out[1]);
    TEST_ASSERT_EQUAL_UINT16(100, out[2]);
}

static void test_collect_response_values_zero_counts(void)
{
    rcp_ep_adc_avg_value_t avgs[1] = {{10, 0}};
    uint16_t out[1] = {0xEEEE};

    TEST_ASSERT_EQUAL_UINT(0u, rcp_ep_adc_collect_response_values(NULL, 0, out, 1));
    TEST_ASSERT_EQUAL_UINT16(0xEEEE, out[0]);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_ep_adc_collect_response_values(avgs, 1, NULL, 0));
}

/* §13.7.9.2: the response's timestamp belongs to "the first average value
 * that is included in the response" -- avg_values[0], whose own timestamp
 * rcp_ep_adc_average_interval() already set to that interval's last used
 * sample. */
static void test_capture_moment_timestamp_is_first_avg_value(void)
{
    rcp_ep_adc_avg_value_t avgs[3] = {
        {100, 555}, {200, 556}, {300, 557},
    };

    TEST_ASSERT_EQUAL_UINT64(555, rcp_ep_adc_capture_moment_timestamp(avgs, 3));
}

static void test_capture_moment_timestamp_zero_count_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT64(0, rcp_ep_adc_capture_moment_timestamp(NULL, 0));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_adc_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.adc_samples_per_avg_interval);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.adc_avg_intervals_per_request);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.adc_combine_avg_values);
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_FALSE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream(void)
{
    rcp_lifecycle_writer_ctx_t none          = {0};
    rcp_lifecycle_writer_ctx_t via_ep0       = {0};
    rcp_lifecycle_writer_ctx_t via_stream    = {0};
    rcp_lifecycle_writer_ctx_t via_discovery = {0};

    via_ep0.via_root_client_ep0        = true;
    via_stream.via_owning_stream       = true;
    via_discovery.via_discovery_stream = true;

    /* REQ-LIFECYCLE-030/036: HW_CONFIGURED functional-config write access
     * now requires the root client via EP0, the endpoint's own owning
     * stream, or the discovery stream -- no longer any writer
     * unconditionally. */
    TEST_ASSERT_FALSE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t unauth = {0};
    rcp_lifecycle_writer_ctx_t auth   = {0};

    auth.via_root_client_ep0 = true;

    TEST_ASSERT_FALSE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, unauth));
    TEST_ASSERT_TRUE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, auth));
}

static void test_set_samples_per_avg_interval_rejects_unauthorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_adc_set_samples_per_avg_interval(&cfg, 16, RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                               writer));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.adc_samples_per_avg_interval);
}

static void test_set_samples_per_avg_interval_applies_when_authorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_adc_set_samples_per_avg_interval(&cfg, 16, RCP_LIFECYCLE_HW_CONFIGURED,
                                                              writer));
    TEST_ASSERT_EQUAL_UINT16(16, cfg.adc_samples_per_avg_interval);
}

static void test_set_avg_intervals_per_request_rejects_unauthorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_adc_set_avg_intervals_per_request(&cfg, 4, RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                                writer));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.adc_avg_intervals_per_request);
}

static void test_set_avg_intervals_per_request_applies_when_authorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_adc_set_avg_intervals_per_request(&cfg, 4, RCP_LIFECYCLE_HW_CONFIGURED,
                                                               writer));
    TEST_ASSERT_EQUAL_UINT16(4, cfg.adc_avg_intervals_per_request);
}

static void test_set_combine_avg_values_rejects_unauthorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_adc_set_combine_avg_values(&cfg, 8,
                                                         RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.adc_combine_avg_values);
}

/* Table 51 gives adc_combine_avg_values an 8-bit R/W field with no
 * enumerated legal values -- every count the field can hold is legal,
 * including the §13.7.9.3 worked example's 8. */
static void test_set_combine_avg_values_applies_when_authorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_adc_set_combine_avg_values(&cfg, 8,
                                                        RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(8, cfg.adc_combine_avg_values);

    TEST_ASSERT_TRUE(rcp_ep_adc_set_combine_avg_values(&cfg, 255,
                                                        RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8(255, cfg.adc_combine_avg_values);
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_adc_errc_t codes[] = {
        RCP_EP_ADC_OK,               RCP_EP_ADC_ERR_SHORT_FRAME,
        RCP_EP_ADC_ERR_BAD_MSG_TYPE, RCP_EP_ADC_ERR_WRONG_BUS,
        RCP_EP_ADC_ERR_WRONG_OP,     RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN,
        RCP_EP_ADC_ERR_TOO_MANY_VALUES, RCP_EP_ADC_ERR_BAD_EVT,
    };
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_adc_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_adc_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_adc_strerror((rcp_ep_adc_errc_t)99));
}

/* ── Read request ──────────────────────────────────────────────────────────── */

/* TC18 v0.5.1_RC §13.7.9.3 and its Figure 33 "RC Client sends a standard
 * read request": the ADC request carries no byte_msg_payload; how many
 * measurements it asks for rides in read_size alone ("The readsize is set
 * to 16 such that 8 measurements are expected in return"). */
static void test_read_request_round_trip(void)
{
    rcp_bytes_t       frame = rcp_ep_adc_encode_read_request(6, 16, 21);
    uint16_t          out_read_size;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_adc_decode_read_request(frame.data, frame.len, 6, &out_read_size, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(16, out_read_size);
    TEST_ASSERT_EQUAL_UINT8(21, out_tn);
    /* ...which is the 8 measurements the same example expects back. */
    TEST_ASSERT_EQUAL_UINT(8u, rcp_ep_adc_response_value_count(out_read_size));

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t       frame = rcp_ep_adc_encode_read_request(6, 2, 21);
    uint16_t          out_read_size;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc = rcp_ep_adc_decode_read_request(frame.data, frame.len, 7,
                                                           &out_read_size, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_WRONG_BUS, rc);
    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint16_t                    out_read_size;
    uint8_t                     out_tn;
    rcp_ep_adc_errc_t           rc;

    hdr.byte_bus_id = 6;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    rc = rcp_ep_adc_decode_read_request(frame.data, frame.len, 6, &out_read_size, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_WRONG_OP, rc);

    rcp_bytes_free(&frame);
}

/* TC18 §13.5 Table 30: evt[2:0] = 000b is the only legal value for a
 * plain ADC read request; every other value (here, 0b101, a reserved
 * value in ADC's endpoint-type row) shall be rejected. */
static void test_read_request_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint16_t                    out_read_size;
    uint8_t                     out_tn;
    rcp_ep_adc_errc_t           rc;

    hdr.byte_bus_id = 6;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.evt         = 0x5;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    rc = rcp_ep_adc_decode_read_request(frame.data, frame.len, 6, &out_read_size, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_BAD_EVT, rc);

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_short_frame(void)
{
    uint16_t          out_read_size;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc = rcp_ep_adc_decode_read_request(NULL, 0, 6, &out_read_size, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_SHORT_FRAME, rc);
}

/* ── Response ───────────────────────────────────────────────────────────────── */

/* TC18 v0.5.1_RC Figure 34 "ADC response with 8 measurements": the
 * response payload is "16 bit ADC value 0" .. "16 bit ADC value 7", i.e.
 * eight 2-octet values, and Figure 34's own header shows read_size =
 * 0b000000010000 = 16 for that response. Verifies the full N-value
 * payload, not a single value. */
static void test_response_carries_eight_measurement_values(void)
{
    const uint16_t    values[8] = {0x0001, 0x0002, 0x0003, 0x0004,
                                    0x0005, 0x0006, 0x0007, 0x0008};
    rcp_bytes_t       frame;
    uint16_t          out_values[8];
    size_t            out_count;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;
    size_t            i;

    frame = rcp_ep_adc_encode_response(8, values, 8, 33, false, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 8, &out_count,
                                     &out_timed, &out_ts, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT(8u, out_count);
    for (i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_UINT16(values[i], out_values[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(33, out_tn);

    rcp_bytes_free(&frame);
}

/* The header's read_size must report the payload the response actually
 * carries -- 2 * value_count (Figure 34, read_size = 16 for 8 values). */
static void test_response_header_read_size_is_twice_value_count(void)
{
    const uint16_t              values[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    rcp_bytes_t                 frame = rcp_ep_adc_encode_response(8, values, 8, 33, false, 0);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;

    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(16u, hdr.read_size_or_segment_num);
    TEST_ASSERT_EQUAL_UINT(16u, payload_len);

    rcp_bytes_free(&frame);
}

static void test_response_round_trip_untimed(void)
{
    const uint16_t    values[1] = {12345};
    rcp_bytes_t       frame;
    uint16_t          out_values[4];
    size_t            out_count;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    frame = rcp_ep_adc_encode_response(8, values, 1, 33, false, 0);
    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 4, &out_count,
                                     &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT(1u, out_count);
    TEST_ASSERT_EQUAL_UINT16(12345, out_values[0]);
    TEST_ASSERT_FALSE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(0, out_ts);
    TEST_ASSERT_EQUAL_UINT8(33, out_tn);

    rcp_bytes_free(&frame);
}

static void test_response_round_trip_timed(void)
{
    const uint16_t    values[2] = {54321, 111};
    rcp_bytes_t       frame;
    uint16_t          out_values[4];
    size_t            out_count;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    frame = rcp_ep_adc_encode_response(8, values, 2, 34, true, 987654321ULL);
    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 4, &out_count,
                                     &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT(2u, out_count);
    TEST_ASSERT_EQUAL_UINT16(54321, out_values[0]);
    TEST_ASSERT_EQUAL_UINT16(111, out_values[1]);
    TEST_ASSERT_TRUE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(987654321ULL, out_ts);

    rcp_bytes_free(&frame);
}

/* A payload that is not a whole number of 2-octet values (Figure 34) is
 * not a well-formed ADC response. */
static void test_response_decode_rejects_bad_payload_len(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     bad_payload[3] = {1, 2, 3};
    rcp_bytes_t                 frame;
    uint16_t                    out_values[4];
    size_t                      out_count;
    bool                        out_timed;
    uint64_t                    out_ts;
    uint8_t                     out_tn;
    rcp_ep_adc_errc_t           rc;

    hdr.byte_bus_id = 8;
    hdr.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_abb(&hdr, bad_payload, sizeof(bad_payload));

    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 4, &out_count,
                                     &out_timed, &out_ts, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN, rc);

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_more_values_than_caller_can_hold(void)
{
    const uint16_t    values[4] = {1, 2, 3, 4};
    rcp_bytes_t       frame = rcp_ep_adc_encode_response(8, values, 4, 1, false, 0);
    uint16_t          out_values[2];
    size_t            out_count;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 2,
                                                       &out_count, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_TOO_MANY_VALUES, rc);
    rcp_bytes_free(&frame);
}

static void test_response_encode_rejects_zero_or_oversized_value_count(void)
{
    const uint16_t values[1] = {1};
    rcp_bytes_t    frame;

    frame = rcp_ep_adc_encode_response(8, values, 0, 1, false, 0);
    TEST_ASSERT_NULL(frame.data);

    frame = rcp_ep_adc_encode_response(8, values, RCP_EP_ADC_MAX_VALUES + 1u, 1, false, 0);
    TEST_ASSERT_NULL(frame.data);
}

static void test_response_no_signal_sentinel_round_trips(void)
{
    const uint16_t    values[2] = {RCP_EP_PWM_IN_NO_SIGNAL, 42};
    rcp_bytes_t       frame;
    uint16_t          out_values[2];
    size_t            out_count;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    frame = rcp_ep_adc_encode_response(8, values, 2, 40, false, 0);
    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 2, &out_count,
                                     &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT(2u, out_count);
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, out_values[0]);
    TEST_ASSERT_EQUAL_UINT16(42, out_values[1]);

    rcp_bytes_free(&frame);
}

/* End-to-end over the corrected model: raw samples -> per-interval
 * averages (each timestamped at its own last used sample, §13.7.9.2) ->
 * COMBINE_NR_VAL of them packed into one response (Table 51 0x000C) ->
 * a timed response whose message_timestamp is the first packed value's
 * capture moment. */
static void test_pipeline_end_to_end_multi_value_timed_response(void)
{
    rcp_ep_adc_sample_t    interval0[2] = {{100, 7000}, {300, 7001}};
    rcp_ep_adc_sample_t    interval1[2] = {{500, 7002}, {700, 7003}};
    rcp_ep_adc_avg_value_t avgs[2];
    uint16_t               values[2];
    size_t                 packed;
    rcp_bytes_t            frame;
    uint16_t               out_values[2];
    size_t                 out_count;
    bool                   out_timed;
    uint64_t               out_ts;
    uint8_t                out_tn;

    avgs[0] = rcp_ep_adc_average_interval(interval0, 2);
    avgs[1] = rcp_ep_adc_average_interval(interval1, 2);

    TEST_ASSERT_EQUAL_UINT16(200, avgs[0].value);
    TEST_ASSERT_EQUAL_UINT64(7001, avgs[0].timestamp); /* last used sample */
    TEST_ASSERT_EQUAL_UINT16(600, avgs[1].value);
    TEST_ASSERT_EQUAL_UINT64(7003, avgs[1].timestamp);

    packed = rcp_ep_adc_collect_response_values(avgs, 2, values, 2);
    TEST_ASSERT_EQUAL_UINT(2u, packed);

    frame = rcp_ep_adc_encode_response(8, values, packed, 55, true,
                                        rcp_ep_adc_capture_moment_timestamp(avgs, 2));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK,
                      rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 2,
                                                  &out_count, &out_timed, &out_ts, &out_tn));
    TEST_ASSERT_EQUAL_UINT(2u, out_count);
    TEST_ASSERT_EQUAL_UINT16(200, out_values[0]);
    TEST_ASSERT_EQUAL_UINT16(600, out_values[1]);
    TEST_ASSERT_TRUE(out_timed);
    /* First packed value's interval closed at 7001. */
    TEST_ASSERT_EQUAL_UINT64(7001, out_ts);

    rcp_bytes_free(&frame);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_response_value_count_is_half_read_size);

    RUN_TEST(test_average_interval_computes_mean);
    RUN_TEST(test_average_interval_zero_samples_is_no_signal);
    RUN_TEST(test_average_interval_skips_no_signal_samples);
    RUN_TEST(test_average_interval_timestamp_skips_trailing_no_signal);
    RUN_TEST(test_average_interval_all_no_signal_is_no_signal);
    RUN_TEST(test_average_interval_timestamp_is_last_used_sample);

    RUN_TEST(test_collect_response_values_packs_in_capture_order);
    RUN_TEST(test_collect_response_values_takes_leading_count);
    RUN_TEST(test_collect_response_values_reports_short_count);
    RUN_TEST(test_collect_response_values_carries_no_signal_verbatim);
    RUN_TEST(test_collect_response_values_zero_counts);

    RUN_TEST(test_capture_moment_timestamp_is_first_avg_value);
    RUN_TEST(test_capture_moment_timestamp_zero_count_is_zero);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_set_samples_per_avg_interval_rejects_unauthorized);
    RUN_TEST(test_set_samples_per_avg_interval_applies_when_authorized);
    RUN_TEST(test_set_avg_intervals_per_request_rejects_unauthorized);
    RUN_TEST(test_set_avg_intervals_per_request_applies_when_authorized);
    RUN_TEST(test_set_combine_avg_values_rejects_unauthorized);
    RUN_TEST(test_set_combine_avg_values_applies_when_authorized);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_read_request_round_trip);
    RUN_TEST(test_read_request_rejects_wrong_bus);
    RUN_TEST(test_read_request_rejects_wrong_op);
    RUN_TEST(test_read_request_rejects_nonzero_evt);
    RUN_TEST(test_read_request_rejects_short_frame);

    RUN_TEST(test_response_carries_eight_measurement_values);
    RUN_TEST(test_response_header_read_size_is_twice_value_count);
    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_bad_payload_len);
    RUN_TEST(test_response_decode_rejects_more_values_than_caller_can_hold);
    RUN_TEST(test_response_encode_rejects_zero_or_oversized_value_count);
    RUN_TEST(test_response_no_signal_sentinel_round_trips);
    RUN_TEST(test_pipeline_end_to_end_multi_value_timed_response);

    return UNITY_END();
}
