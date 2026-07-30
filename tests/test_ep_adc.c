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

/* ── adc_combine_avg_values: the layer-3 combine modes ──────────────────────── */

static void test_combine_mode_valid(void)
{
    uint8_t v;

    for (v = 0; v <= 3; v++) {
        TEST_ASSERT_TRUE(rcp_ep_adc_combine_mode_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_adc_combine_mode_valid(4));
    TEST_ASSERT_FALSE(rcp_ep_adc_combine_mode_valid(255));
}

/* ── Layer 1: rcp_ep_adc_average_interval() ──────────────────────────────────── */

static void test_average_interval_computes_mean(void)
{
    rcp_ep_adc_sample_t samples[3] = {
        {100, 1000}, {200, 1001}, {300, 1002},
    };
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 3);

    TEST_ASSERT_EQUAL_UINT16(200, result.value);
    TEST_ASSERT_EQUAL_UINT64(1000, result.timestamp);
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
    TEST_ASSERT_EQUAL_UINT64(2000, result.timestamp);
}

static void test_average_interval_all_no_signal_is_no_signal(void)
{
    rcp_ep_adc_sample_t samples[2] = {
        {RCP_EP_PWM_IN_NO_SIGNAL, 3000}, {RCP_EP_PWM_IN_NO_SIGNAL, 3001},
    };
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 2);

    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, result.value);
    TEST_ASSERT_EQUAL_UINT64(3000, result.timestamp); /* first sample's
                                                           timestamp still
                                                           reported */
}

static void test_average_interval_timestamp_is_always_first_sample(void)
{
    rcp_ep_adc_sample_t samples[2] = {{10, 42}, {20, 99}};
    rcp_ep_adc_avg_value_t result = rcp_ep_adc_average_interval(samples, 2);

    TEST_ASSERT_EQUAL_UINT64(42, result.timestamp);
}

/* ── Layers 2/3: rcp_ep_adc_combine_avg_values() ─────────────────────────────── */

static void test_combine_average_mode(void)
{
    rcp_ep_adc_avg_value_t avgs[3] = {
        {100, 0}, {200, 0}, {300, 0},
    };

    TEST_ASSERT_EQUAL_UINT16(200, rcp_ep_adc_combine_avg_values(avgs, 3, RCP_EP_ADC_COMBINE_AVERAGE));
}

static void test_combine_min_mode(void)
{
    rcp_ep_adc_avg_value_t avgs[3] = {
        {300, 0}, {100, 0}, {200, 0},
    };

    TEST_ASSERT_EQUAL_UINT16(100, rcp_ep_adc_combine_avg_values(avgs, 3, RCP_EP_ADC_COMBINE_MIN));
}

static void test_combine_max_mode(void)
{
    rcp_ep_adc_avg_value_t avgs[3] = {
        {300, 0}, {100, 0}, {200, 0},
    };

    TEST_ASSERT_EQUAL_UINT16(300, rcp_ep_adc_combine_avg_values(avgs, 3, RCP_EP_ADC_COMBINE_MAX));
}

static void test_combine_latest_mode(void)
{
    rcp_ep_adc_avg_value_t avgs[3] = {
        {300, 0}, {100, 0}, {200, 0},
    };

    TEST_ASSERT_EQUAL_UINT16(200, rcp_ep_adc_combine_avg_values(avgs, 3, RCP_EP_ADC_COMBINE_LATEST));
}

static void test_combine_latest_mode_reports_no_signal_verbatim(void)
{
    rcp_ep_adc_avg_value_t avgs[2] = {
        {300, 0}, {RCP_EP_PWM_IN_NO_SIGNAL, 0},
    };

    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL,
                              rcp_ep_adc_combine_avg_values(avgs, 2, RCP_EP_ADC_COMBINE_LATEST));
}

static void test_combine_zero_count_is_no_signal(void)
{
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL,
                              rcp_ep_adc_combine_avg_values(NULL, 0, RCP_EP_ADC_COMBINE_AVERAGE));
}

static void test_combine_all_no_signal_is_no_signal_for_average_min_max(void)
{
    rcp_ep_adc_avg_value_t avgs[2] = {
        {RCP_EP_PWM_IN_NO_SIGNAL, 0}, {RCP_EP_PWM_IN_NO_SIGNAL, 0},
    };

    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL,
                              rcp_ep_adc_combine_avg_values(avgs, 2, RCP_EP_ADC_COMBINE_AVERAGE));
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL,
                              rcp_ep_adc_combine_avg_values(avgs, 2, RCP_EP_ADC_COMBINE_MIN));
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL,
                              rcp_ep_adc_combine_avg_values(avgs, 2, RCP_EP_ADC_COMBINE_MAX));
}

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
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ADC_COMBINE_AVERAGE, cfg.adc_combine_avg_values);
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_FALSE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_adc_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, writer));
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

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_adc_set_avg_intervals_per_request(&cfg, 4, RCP_LIFECYCLE_HW_CONFIGURED,
                                                               writer));
    TEST_ASSERT_EQUAL_UINT16(4, cfg.adc_avg_intervals_per_request);
}

static void test_set_combine_mode_rejects_invalid_mode_or_unauthorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_adc_set_combine_mode(&cfg, (rcp_ep_adc_combine_mode_t)9,
                                                   RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_FALSE(rcp_ep_adc_set_combine_mode(&cfg, RCP_EP_ADC_COMBINE_MAX,
                                                   RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ADC_COMBINE_AVERAGE, cfg.adc_combine_avg_values);
}

static void test_set_combine_mode_applies_when_valid_and_authorized(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_adc_set_combine_mode(&cfg, RCP_EP_ADC_COMBINE_MAX,
                                                  RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ADC_COMBINE_MAX, cfg.adc_combine_avg_values);
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_adc_errc_t codes[] = {
        RCP_EP_ADC_OK,               RCP_EP_ADC_ERR_SHORT_FRAME,
        RCP_EP_ADC_ERR_BAD_MSG_TYPE, RCP_EP_ADC_ERR_WRONG_BUS,
        RCP_EP_ADC_ERR_WRONG_OP,     RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN,
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

static void test_read_request_round_trip(void)
{
    rcp_bytes_t       frame = rcp_ep_adc_encode_read_request(6, 21);
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    TEST_ASSERT_NOT_NULL(frame.data);

    rc = rcp_ep_adc_decode_read_request(frame.data, frame.len, 6, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(21, out_tn);

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_bus(void)
{
    rcp_bytes_t       frame = rcp_ep_adc_encode_read_request(6, 21);
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc = rcp_ep_adc_decode_read_request(frame.data, frame.len, 7, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_WRONG_BUS, rc);
    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint8_t                     out_tn;
    rcp_ep_adc_errc_t           rc;

    hdr.byte_bus_id = 6;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    rc = rcp_ep_adc_decode_read_request(frame.data, frame.len, 6, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_WRONG_OP, rc);

    rcp_bytes_free(&frame);
}

static void test_read_request_rejects_short_frame(void)
{
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc = rcp_ep_adc_decode_read_request(NULL, 0, 6, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_SHORT_FRAME, rc);
}

/* ── Response ───────────────────────────────────────────────────────────────── */

static void test_response_round_trip_untimed(void)
{
    rcp_bytes_t       frame;
    uint16_t          out_value;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    frame = rcp_ep_adc_encode_response(8, 12345, 33, false, 0);
    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(12345, out_value);
    TEST_ASSERT_FALSE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(0, out_ts);
    TEST_ASSERT_EQUAL_UINT8(33, out_tn);

    rcp_bytes_free(&frame);
}

static void test_response_round_trip_timed(void)
{
    rcp_bytes_t       frame;
    uint16_t          out_value;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    frame = rcp_ep_adc_encode_response(8, 54321, 34, true, 987654321ULL);
    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_TRUE(out_timed);
    TEST_ASSERT_EQUAL_UINT64(987654321ULL, out_ts);

    rcp_bytes_free(&frame);
}

static void test_response_decode_rejects_bad_payload_len(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                     bad_payload[3] = {1, 2, 3};
    rcp_bytes_t                 frame;
    uint16_t                    out_value;
    bool                        out_timed;
    uint64_t                    out_ts;
    uint8_t                     out_tn;
    rcp_ep_adc_errc_t           rc;

    hdr.byte_bus_id = 8;
    hdr.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_abb(&hdr, bad_payload, sizeof(bad_payload));

    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, &out_value, &out_timed, &out_ts, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN, rc);

    rcp_bytes_free(&frame);
}

static void test_response_no_signal_sentinel_round_trips(void)
{
    rcp_bytes_t       frame;
    uint16_t          out_value;
    bool              out_timed;
    uint64_t          out_ts;
    uint8_t           out_tn;
    rcp_ep_adc_errc_t rc;

    frame = rcp_ep_adc_encode_response(8, RCP_EP_PWM_IN_NO_SIGNAL, 40, false, 0);
    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, &out_value, &out_timed, &out_ts, &out_tn);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_OK, rc);
    TEST_ASSERT_EQUAL_UINT16(RCP_EP_PWM_IN_NO_SIGNAL, out_value);

    rcp_bytes_free(&frame);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_combine_mode_valid);

    RUN_TEST(test_average_interval_computes_mean);
    RUN_TEST(test_average_interval_zero_samples_is_no_signal);
    RUN_TEST(test_average_interval_skips_no_signal_samples);
    RUN_TEST(test_average_interval_all_no_signal_is_no_signal);
    RUN_TEST(test_average_interval_timestamp_is_always_first_sample);

    RUN_TEST(test_combine_average_mode);
    RUN_TEST(test_combine_min_mode);
    RUN_TEST(test_combine_max_mode);
    RUN_TEST(test_combine_latest_mode);
    RUN_TEST(test_combine_latest_mode_reports_no_signal_verbatim);
    RUN_TEST(test_combine_zero_count_is_no_signal);
    RUN_TEST(test_combine_all_no_signal_is_no_signal_for_average_min_max);

    RUN_TEST(test_capture_moment_timestamp_is_first_avg_value);
    RUN_TEST(test_capture_moment_timestamp_zero_count_is_zero);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_true_hw_configured_any_writer);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);
    RUN_TEST(test_set_samples_per_avg_interval_rejects_unauthorized);
    RUN_TEST(test_set_samples_per_avg_interval_applies_when_authorized);
    RUN_TEST(test_set_avg_intervals_per_request_rejects_unauthorized);
    RUN_TEST(test_set_avg_intervals_per_request_applies_when_authorized);
    RUN_TEST(test_set_combine_mode_rejects_invalid_mode_or_unauthorized);
    RUN_TEST(test_set_combine_mode_applies_when_valid_and_authorized);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_read_request_round_trip);
    RUN_TEST(test_read_request_rejects_wrong_bus);
    RUN_TEST(test_read_request_rejects_wrong_op);
    RUN_TEST(test_read_request_rejects_short_frame);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_bad_payload_len);
    RUN_TEST(test_response_no_signal_sentinel_round_trips);

    return UNITY_END();
}
