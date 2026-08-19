/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ADC-001
//cfusa:test REQ-ADC-002
//cfusa:test REQ-ADC-003
//cfusa:test REQ-ADC-005
//cfusa:test REQ-ADC-006
//cfusa:test REQ-ADC-007
//cfusa:test REQ-ADC-008
//cfusa:test REQ-ADC-009
//cfusa:test REQ-ADC-010
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
//cfusa:test REQ-ADC-027
//cfusa:test REQ-ADC-028
//cfusa:test REQ-ADC-030
//cfusa:test REQ-ADC-040
/* REQ-ADC-004/011/025/026/029/031/037/038/039 and their splits
 * (REQ-ADC-041..055, c-RCP-18 audit issue #533 REQ-ADC batch) are
 * deliberately NOT stacked here -- per CONTRIBUTING.md's "Writing a
 * requirement" convention, each is tagged directly above the specific
 * test function(s) that prove it, below, rather than at this file
 * header. */
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

//cfusa:test REQ-ADC-004
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

//cfusa:test REQ-ADC-041
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
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.base_clk_divider);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.sample_interval);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.resolution);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.trigger_min);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.trigger_max);
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

/* ── Trigger outputs (Table 53), REQ-ADC-031 ─────────────────────────────── */

//cfusa:test REQ-ADC-031
static void test_trigger_state_init_has_no_previous_value(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);
    TEST_ASSERT_FALSE(s.has_previous);
}

/* The very first evaluate() call ever (no previous value tracked yet)
 * cannot fire any of the edge-triggered signals 0-3, regardless of
 * value -- there is nothing to detect a transition against. Trigger 4
 * (measurement_finished) is unaffected, since it has no previous-value
 * concept at all. */
//cfusa:test REQ-ADC-052
static void test_trigger_evaluate_first_call_never_fires_edge_triggers(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    /* Even a value far outside [min, max] fires nothing edge-related. */
    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 0, 100, 200, false));
    TEST_ASSERT_TRUE(s.has_previous);

    /* But it DOES now have a previous value, and measurement_finished
     * still fires independently of any of that. */
    rcp_ep_adc_trigger_state_init(&s);
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_MEASUREMENT_FINISHED,
                            rcp_ep_adc_trigger_evaluate(&s, 9999, 100, 200, true));
}

/* A genuine downward crossing of trigger_min fires BELOW_MIN exactly
 * once, on the call where the crossing happens -- not on every
 * subsequent call that stays below it. */
//cfusa:test REQ-ADC-048
static void test_trigger_evaluate_below_min_fires_once_per_crossing(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 150, 100, 200, false)); /* seed: in range */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_BELOW_MIN,
                            rcp_ep_adc_trigger_evaluate(&s, 50, 100, 200, false)); /* crosses below min */
    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 40, 100, 200, false)); /* stays below: no re-fire */
}

/* The symmetric upward crossing of trigger_min. */
//cfusa:test REQ-ADC-049
static void test_trigger_evaluate_above_min_fires_on_upward_crossing(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 50, 100, 200, false)); /* seed: below min */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_ABOVE_MIN,
                            rcp_ep_adc_trigger_evaluate(&s, 150, 100, 200, false)); /* crosses above min */
}

/* trigger_max's own two crossings, independent of trigger_min's. */
//cfusa:test REQ-ADC-050
//cfusa:test REQ-ADC-051
static void test_trigger_evaluate_max_crossings(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 150, 100, 200, false)); /* seed: in range */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_ABOVE_MAX,
                            rcp_ep_adc_trigger_evaluate(&s, 250, 100, 200, false)); /* crosses above max */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_BELOW_MAX,
                            rcp_ep_adc_trigger_evaluate(&s, 150, 100, 200, false)); /* back below max */
}

/* A value sitting exactly AT a threshold, then moving strictly away
 * from it, still counts as a genuine crossing (the ">="/"<=" boundary
 * inclusion on the PREVIOUS side, not the current side). */
//cfusa:test REQ-ADC-048
static void test_trigger_evaluate_exact_threshold_value_still_crosses(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 100, 100, 200, false)); /* seed: exactly at min */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_BELOW_MIN,
                            rcp_ep_adc_trigger_evaluate(&s, 99, 100, 200, false)); /* strictly below now */
}

/* The symmetric direction: a value that moves UP to (not past) a
 * threshold from below must NOT fire the "rises above" trigger -- only
 * a value that becomes STRICTLY greater than the threshold does. This
 * is the discriminating case that distinguishes a strict ">" comparison
 * from an off-by-one ">=" on rcp_ep_adc_trigger_evaluate()'s own
 * current-value side. */
//cfusa:test REQ-ADC-049
static void test_trigger_evaluate_moving_exactly_to_threshold_does_not_fire_above(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 50, 100, 200, false)); /* seed: below min */
    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 100, 100, 200, false)); /* AT min, not above it */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_ABOVE_MIN,
                            rcp_ep_adc_trigger_evaluate(&s, 101, 100, 200, false)); /* NOW strictly above */
}

/* The symmetric downward case: a value that moves DOWN to (not past) a
 * threshold from above must NOT fire the "falls below" trigger. */
//cfusa:test REQ-ADC-048
static void test_trigger_evaluate_moving_exactly_to_threshold_does_not_fire_below(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 150, 100, 200, false)); /* seed: above min */
    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 100, 100, 200, false)); /* AT min, not below it */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_BELOW_MIN,
                            rcp_ep_adc_trigger_evaluate(&s, 99, 100, 200, false)); /* NOW strictly below */
}

/* trigger_max's own two symmetric "moves exactly to, not past" cases. */
//cfusa:test REQ-ADC-050
//cfusa:test REQ-ADC-051
static void test_trigger_evaluate_max_moving_exactly_to_threshold_does_not_fire(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 150, 100, 200, false)); /* seed: below max */
    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 200, 100, 200, false)); /* AT max, not above it */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_ABOVE_MAX,
                            rcp_ep_adc_trigger_evaluate(&s, 201, 100, 200, false)); /* NOW strictly above */

    rcp_ep_adc_trigger_state_init(&s);
    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 250, 100, 200, false)); /* seed: above max */
    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 200, 100, 200, false)); /* AT max, not below it */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_BELOW_MAX,
                            rcp_ep_adc_trigger_evaluate(&s, 199, 100, 200, false)); /* NOW strictly below */
}

/* A jump that crosses BOTH min and max in a single call (e.g. from deep
 * below min to deep above max) fires every threshold trigger whose own
 * direction the jump satisfies -- not just one. */
//cfusa:test REQ-ADC-049
//cfusa:test REQ-ADC-051
static void test_trigger_evaluate_large_jump_fires_multiple_triggers(void)
{
    rcp_ep_adc_trigger_state_t s;
    uint8_t                     fired;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 0, 100, 200, false)); /* seed: below min */
    fired = rcp_ep_adc_trigger_evaluate(&s, 9999, 100, 200, false); /* jumps clear past max */
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_ABOVE_MIN | RCP_EP_ADC_TRIGGER_ABOVE_MAX, fired);
}

/* Trigger 4 composes with any of triggers 0-3 in the same call --
 * Table 53's own 5 signals are independent, not mutually exclusive. */
//cfusa:test REQ-ADC-052
static void test_trigger_evaluate_measurement_finished_composes_with_edge_triggers(void)
{
    rcp_ep_adc_trigger_state_t s;
    rcp_ep_adc_trigger_state_init(&s);

    TEST_ASSERT_EQUAL_UINT8(0, rcp_ep_adc_trigger_evaluate(&s, 150, 100, 200, false));
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_ADC_TRIGGER_BELOW_MIN | RCP_EP_ADC_TRIGGER_MEASUREMENT_FINISHED,
                            rcp_ep_adc_trigger_evaluate(&s, 50, 100, 200, true));
}

/* ── The EP_func register block ────────────────────────────────────────────── */

//cfusa:test REQ-ADC-038
static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     out[RCP_EP_ADC_EP_FUNC_LEN];

    rcp_ep_adc_functional_cfg_init(&cfg);
    cfg.common.ep_enable              = true;
    cfg.ep_status                      = 0x1234;
    cfg.base_clk_divider               = 0x55;
    cfg.sample_interval                = 0x66;
    cfg.adc_avg_intervals_per_request  = 0x77;
    cfg.adc_samples_per_avg_interval   = 0x88;
    cfg.adc_combine_avg_values         = 0x99;
    cfg.resolution                     = 12;
    cfg.trigger_min                    = 0x1111;
    cfg.trigger_max                    = 0x2222;

    rcp_ep_adc_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ADC_EP_FUNC_LEN, out[RCP_EP_ADC_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_RESERVED_01]);
    TEST_ASSERT_TRUE((out[RCP_EP_ADC_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_BASE_CLK]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_BASE_CLK + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_ADC_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_ADC_REG_EP_STATUS + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x55, out[RCP_EP_ADC_REG_BASE_CLK_DIVIDER]);
    TEST_ASSERT_EQUAL_UINT8(0x66, out[RCP_EP_ADC_REG_SAMPLE_INTERVAL]);
    TEST_ASSERT_EQUAL_UINT8(0x77, out[RCP_EP_ADC_REG_AVG_INTERVALS]);
    TEST_ASSERT_EQUAL_UINT8(0x88, out[RCP_EP_ADC_REG_SAMPLES_PER_AVG]);
    TEST_ASSERT_EQUAL_UINT8(0x99, out[RCP_EP_ADC_REG_COMBINE_AVG]);
    TEST_ASSERT_EQUAL_UINT8(12, out[RCP_EP_ADC_REG_RESOLUTION]);
    TEST_ASSERT_EQUAL_UINT8(0x11u, out[RCP_EP_ADC_REG_TRIGGER_MIN]);
    TEST_ASSERT_EQUAL_UINT8(0x11u, out[RCP_EP_ADC_REG_TRIGGER_MIN + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x22u, out[RCP_EP_ADC_REG_TRIGGER_MAX]);
    TEST_ASSERT_EQUAL_UINT8(0x22u, out[RCP_EP_ADC_REG_TRIGGER_MAX + 1]);

    TEST_ASSERT_EQUAL_UINT16(0x0012u, RCP_EP_ADC_EP_FUNC_LEN);
}

/* REQ-ADC's own wider uint16_t fields (adc_avg_intervals_per_request/
 * adc_samples_per_avg_interval) truncate to their low octet on render --
 * see the file header. */
//cfusa:test REQ-ADC-038
static void test_render_registers_truncates_wide_fields(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     out[RCP_EP_ADC_EP_FUNC_LEN];

    rcp_ep_adc_functional_cfg_init(&cfg);
    cfg.adc_avg_intervals_per_request = 0x0155u; /* > 0xFF */
    cfg.adc_samples_per_avg_interval  = 0x02AAu; /* > 0xFF */

    rcp_ep_adc_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8(0x55, out[RCP_EP_ADC_REG_AVG_INTERVALS]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[RCP_EP_ADC_REG_SAMPLES_PER_AVG]);
}

//cfusa:test REQ-ADC-039
static void test_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     payload[2 + 6];

    rcp_ep_adc_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_ADC_REG_EP_STATUS;
    payload[2] = 0xAB; payload[3] = 0xCD; /* ep_status */
    payload[4] = 0x11;                    /* base_clk_divider */
    payload[5] = 0x22;                    /* sample_interval */
    payload[6] = 0x33;                    /* avg_intervals_per_request */
    payload[7] = 0x44;                    /* samples_per_avg_interval */

    TEST_ASSERT_EQUAL(RCP_EP_ADC_RECONFIG_OK,
        rcp_ep_adc_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0x11, cfg.base_clk_divider);
    TEST_ASSERT_EQUAL_UINT8(0x22, cfg.sample_interval);
    TEST_ASSERT_EQUAL_UINT16(0x33, cfg.adc_avg_intervals_per_request);
    TEST_ASSERT_EQUAL_UINT16(0x44, cfg.adc_samples_per_avg_interval);
}

//cfusa:test REQ-ADC-039
static void test_apply_reconfig_writes_resolution_and_trigger_thresholds(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     payload[2 + 5];

    rcp_ep_adc_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_ADC_REG_RESOLUTION;
    payload[2] = 12;                      /* resolution */
    payload[3] = 0x11; payload[4] = 0x11; /* trigger_min */
    payload[5] = 0x22; payload[6] = 0x22; /* trigger_max */

    TEST_ASSERT_EQUAL(RCP_EP_ADC_RECONFIG_OK,
        rcp_ep_adc_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(12, cfg.resolution);
    TEST_ASSERT_EQUAL_UINT16(0x1111, cfg.trigger_min);
    TEST_ASSERT_EQUAL_UINT16(0x2222, cfg.trigger_max);
}

//cfusa:test REQ-ADC-039
static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     payload[2 + 4];

    rcp_ep_adc_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00), the reserved octet (0x01), and both octets of
     * base_clk (0x04-0x05) -- all read-only. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;
    payload[4] = 0xFF;
    payload[5] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_ADC_RECONFIG_OK,
        rcp_ep_adc_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_ADC_EP_FUNC_LEN];

        rcp_ep_adc_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_ADC_EP_FUNC_LEN, out[RCP_EP_ADC_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_RESERVED_01]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_BASE_CLK]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_BASE_CLK + 1]);
    }
}

/* MC/DC independence for reg_offset_read_only()'s third and fourth
 * conditions (addr == RCP_EP_ADC_REG_BASE_CLK and addr ==
 * RCP_EP_ADC_REG_BASE_CLK + 1): test_apply_reconfig_ignores_read_only_registers
 * above never actually addresses either byte of base_clk -- its write
 * spans offsets 0x00-0x03 (EP_LEN, the reserved octet, and both R/W
 * octets of enable_clr/options), landing only on this function's first
 * two conditions. This write instead spans offsets 0x04-0x06 so
 * reg_offset_read_only() is called once per octet with addr equal to
 * base_clk's low octet (third condition true, first/second/fourth
 * false), base_clk's high octet (fourth condition true, the other three
 * false), and ep_status's low octet (all four conditions false, giving
 * the "otherwise false" half of both pairs). */
//cfusa:test REQ-ADC-039
static void test_apply_reconfig_ignores_base_clk_octets_individually(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     payload[2 + 3];

    rcp_ep_adc_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_ADC_REG_BASE_CLK; /* start_address = 0x04 */
    payload[2] = 0xFF; /* addr 0x04 -- base_clk low octet, RO: ignored */
    payload[3] = 0xFF; /* addr 0x05 -- base_clk high octet, RO: ignored */
    payload[4] = 0x99; /* addr 0x06 -- ep_status low octet, R/W: applied */

    TEST_ASSERT_EQUAL(RCP_EP_ADC_RECONFIG_OK,
        rcp_ep_adc_apply_reconfig(&cfg, payload, sizeof(payload)));

    /* base_clk is never stored in cfg at all -- render always re-derives
     * both its octets as 0 (see rcp_ep_adc_render_registers), so the RO
     * write above having no effect on it is inherent, not merely
     * unobserved. What proves the guard actually fired is that
     * ep_status's low octet (0x06, addressed in the very same write) DID
     * take the 0x99 this loop wrote -- were the base_clk guard not
     * checked per-octet, offsets 0x04/0x05 would instead fall through to
     * writing into the block image at those positions, which render then
     * overwrites right back to 0 before this call returns -- so this
     * assertion alone cannot distinguish "guard fired" from "guard
     * skipped, then overwritten anyway"; the record in field index 9
     * for reg_offset_read_only()'s own decision is the actual proof,
     * verified separately via the MC/DC export. */
    {
        uint8_t out[RCP_EP_ADC_EP_FUNC_LEN];

        rcp_ep_adc_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_BASE_CLK]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_ADC_REG_BASE_CLK + 1]);
    }
    TEST_ASSERT_EQUAL_UINT16(0x9900, cfg.ep_status);
}

//cfusa:test REQ-ADC-039
static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_adc_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x12; /* == RCP_EP_ADC_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_ADC_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_adc_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.resolution);
}

//cfusa:test REQ-ADC-039
static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_adc_functional_cfg_t cfg;
    uint8_t                     addr_only[2] = {0x00, 0x08};

    rcp_ep_adc_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_ADC_RECONFIG_ERR_SHORT,
        rcp_ep_adc_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_RECONFIG_ERR_SHORT,
        rcp_ep_adc_apply_reconfig(&cfg, NULL, 0));
}

//cfusa:test REQ-ADC-054
static void test_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_adc_encode_reconfig_request(0x03, 0x0006, data, sizeof(data), 7);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(0x03, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL(RCP_ACF_OP_WRITE, hdr.op);
    TEST_ASSERT_EQUAL_UINT8(0x7u, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(7, hdr.transaction_num);
    TEST_ASSERT_EQUAL_UINT32(4, payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x06, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, payload[3]);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-ADC-054
static void test_encode_reconfig_request_rejects_empty_data(void)
{
    rcp_bytes_t frame = rcp_ep_adc_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

/* MC/DC independence for `data_len == 0 || data == NULL`'s second
 * condition: data_len is nonzero here (first condition false, so it does
 * NOT short-circuit the OR), and data is NULL -- the decision must still
 * come out true purely on the second condition, distinct from
 * test_encode_reconfig_request_rejects_empty_data's data_len==0 case
 * above (which short-circuits before data is even examined) and from
 * test_reconfig_request_round_trip's fully-valid call (both conditions
 * false). */
//cfusa:test REQ-ADC-054
static void test_encode_reconfig_request_rejects_null_data_with_nonzero_len(void)
{
    rcp_bytes_t frame = rcp_ep_adc_encode_reconfig_request(0x00, 0, NULL, 5, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-ADC-055
static void test_reconfig_strerror_never_null(void)
{
    rcp_ep_adc_reconfig_errc_t codes[] = {
        RCP_EP_ADC_RECONFIG_OK, RCP_EP_ADC_RECONFIG_ERR_SHORT,
        RCP_EP_ADC_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_adc_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_adc_reconfig_strerror((rcp_ep_adc_reconfig_errc_t)99));
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
//cfusa:test REQ-ADC-025
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

/* REQ-ADC-043 (split 2026-08-18, c-RCP-18 audit issue #533, from
 * REQ-ADC-025): extraction §5.9.3 -- the ADC request has no
 * byte_msg_payload of its own; how many measurement values a response
 * should carry is conveyed by read_size alone. Decodes the raw ACF_ABB
 * frame directly (not via rcp_ep_adc_decode_read_request(), which
 * discards payload_len) so this genuinely pins the wire-level encode
 * output, independently of REQ-ADC-025's own round-trip contract. Before
 * this split, no test anywhere in this file asserted the encoded frame's
 * payload length. */
//cfusa:test REQ-ADC-043
static void test_read_request_carries_no_payload(void)
{
    rcp_bytes_t                 frame = rcp_ep_adc_encode_read_request(6, 16, 21);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT(0u, payload_len);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-ADC-044
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

//cfusa:test REQ-ADC-045
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
//cfusa:test REQ-ADC-046
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

//cfusa:test REQ-ADC-026
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
//cfusa:test REQ-ADC-011
//cfusa:test REQ-ADC-027
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
//cfusa:test REQ-ADC-011
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

//cfusa:test REQ-ADC-027
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

//cfusa:test REQ-ADC-028
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
//cfusa:test REQ-ADC-029
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

/* MC/DC independence for `payload_len == 0 || (payload_len %
 * RCP_EP_ADC_VALUE_LEN) != 0`'s first condition: an entirely empty
 * payload -- 0 % RCP_EP_ADC_VALUE_LEN is 0, so the second condition is
 * false here and only payload_len==0 drives the true outcome. Distinct
 * from test_response_decode_rejects_bad_payload_len above (payload_len=3,
 * first condition false, second condition true) and from any successful
 * decode (e.g. test_response_round_trip_untimed's payload_len=2), which
 * holds both conditions false. */
//cfusa:test REQ-ADC-029
static void test_response_decode_rejects_empty_payload(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    uint16_t                    out_values[4];
    size_t                      out_count;
    bool                        out_timed;
    uint64_t                    out_ts;
    uint8_t                     out_tn;
    rcp_ep_adc_errc_t           rc;

    hdr.byte_bus_id = 8;
    hdr.op          = RCP_ACF_OP_READ;
    frame = rcp_acf_encode_abb(&hdr, NULL, 0); /* zero-length payload */

    rc = rcp_ep_adc_decode_response(frame.data, frame.len, 8, out_values, 4, &out_count,
                                     &out_timed, &out_ts, &out_tn);
    TEST_ASSERT_EQUAL(RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN, rc);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-ADC-047
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

//cfusa:test REQ-ADC-042
static void test_response_encode_rejects_zero_or_oversized_value_count(void)
{
    const uint16_t values[1] = {1};
    rcp_bytes_t    frame;

    frame = rcp_ep_adc_encode_response(8, values, 0, 1, false, 0);
    TEST_ASSERT_NULL(frame.data);

    frame = rcp_ep_adc_encode_response(8, values, RCP_EP_ADC_MAX_VALUES + 1u, 1, false, 0);
    TEST_ASSERT_NULL(frame.data);
}

/* MC/DC independence for `values == NULL || value_count == 0 ||
 * value_count > RCP_EP_ADC_MAX_VALUES`'s first condition: value_count
 * here is nonzero and within range (both later conditions false), so
 * only values==NULL can be driving the true outcome -- distinct from
 * test_response_encode_rejects_zero_or_oversized_value_count above
 * (which exercises the other two conditions with values always
 * non-NULL) and from any successful encode (e.g.
 * test_response_carries_eight_measurement_values), which holds all
 * three conditions false. */
//cfusa:test REQ-ADC-042
static void test_response_encode_rejects_null_values_with_valid_count(void)
{
    rcp_bytes_t frame = rcp_ep_adc_encode_response(8, NULL, 4, 1, false, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-ADC-030
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

/* REQ-ADC-037, TC18 §13.7.9.2's three cadence cases -- see the file
 * header's own "three documented cadence cases" paragraph and
 * rcp_ep_adc_cadence_case()'s doc comment. */
//cfusa:test REQ-ADC-037
static void test_cadence_case_accumulate_when_combine_exceeds_intervals(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_ACCUMULATE, rcp_ep_adc_cadence_case(2, 5));
}

//cfusa:test REQ-ADC-037
static void test_cadence_case_one_to_one_when_combine_equals_intervals(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_ONE_TO_ONE, rcp_ep_adc_cadence_case(4, 4));
}

//cfusa:test REQ-ADC-037
static void test_cadence_case_fan_out_when_combine_below_intervals(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_FAN_OUT, rcp_ep_adc_cadence_case(6, 2));
}

//cfusa:test REQ-ADC-037
static void test_cadence_case_boundary_values(void)
{
    /* intervals == 0 with a nonzero combine can never be reached by any
     * amount of accumulation from a zero-sized execution -- still
     * correctly classified as ACCUMULATE (combine > intervals), the
     * "caller's problem" fittingly stays the caller's problem: this
     * function only names the case, it doesn't validate reachability. */
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_ACCUMULATE, rcp_ep_adc_cadence_case(0, 1));
    /* Both zero: equal, so ONE_TO_ONE by the same rule as any other
     * equal pair. */
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_ONE_TO_ONE, rcp_ep_adc_cadence_case(0, 0));
    /* Widest possible values on each side, still compared correctly
     * (regression pin for the uint16_t/uint8_t width mismatch this
     * function's own parameters have). */
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_FAN_OUT, rcp_ep_adc_cadence_case(65535, 1));
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_ACCUMULATE, rcp_ep_adc_cadence_case(1, 255));
}

//cfusa:test REQ-ADC-053
static void test_cadence_response_ready_true_when_pending_meets_combine(void)
{
    TEST_ASSERT_TRUE(rcp_ep_adc_cadence_response_ready(5u, 5u));
    TEST_ASSERT_TRUE(rcp_ep_adc_cadence_response_ready(6u, 5u));
}

//cfusa:test REQ-ADC-053
static void test_cadence_response_ready_false_when_pending_short(void)
{
    TEST_ASSERT_FALSE(rcp_ep_adc_cadence_response_ready(4u, 5u));
    TEST_ASSERT_FALSE(rcp_ep_adc_cadence_response_ready(0u, 1u));
}

//cfusa:test REQ-ADC-053
static void test_cadence_response_ready_zero_combine_always_ready(void)
{
    TEST_ASSERT_TRUE(rcp_ep_adc_cadence_response_ready(0u, 0u));
}

//cfusa:test REQ-ADC-053
static void test_cadence_response_ready_drives_accumulate_case_across_executions(void)
{
    /* intervals=2, combine=5: not ready after 1 or 2 executions (2, 4
     * pending), ready after the 3rd (6 pending) -- the same "several
     * executions feed one response" rule RCP_EP_ADC_CADENCE_ACCUMULATE
     * names, exercised end-to-end via repeated readiness checks. */
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_ACCUMULATE, rcp_ep_adc_cadence_case(2, 5));
    TEST_ASSERT_FALSE(rcp_ep_adc_cadence_response_ready(2u, 5u));
    TEST_ASSERT_FALSE(rcp_ep_adc_cadence_response_ready(4u, 5u));
    TEST_ASSERT_TRUE(rcp_ep_adc_cadence_response_ready(6u, 5u));
}

//cfusa:test REQ-ADC-053
static void test_cadence_response_ready_drives_fan_out_case_across_responses(void)
{
    /* intervals=6, combine=2: one execution (6 pending) is immediately
     * ready, and stays ready after peeling off two 2-value responses (4,
     * then 2 pending) -- RCP_EP_ADC_CADENCE_FAN_OUT's "one execution
     * yields several responses" rule, exercised the same way. */
    TEST_ASSERT_EQUAL(RCP_EP_ADC_CADENCE_FAN_OUT, rcp_ep_adc_cadence_case(6, 2));
    TEST_ASSERT_TRUE(rcp_ep_adc_cadence_response_ready(6u, 2u));
    TEST_ASSERT_TRUE(rcp_ep_adc_cadence_response_ready(4u, 2u));
    TEST_ASSERT_TRUE(rcp_ep_adc_cadence_response_ready(2u, 2u));
    TEST_ASSERT_FALSE(rcp_ep_adc_cadence_response_ready(0u, 2u));
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
    RUN_TEST(test_trigger_state_init_has_no_previous_value);
    RUN_TEST(test_trigger_evaluate_first_call_never_fires_edge_triggers);
    RUN_TEST(test_trigger_evaluate_below_min_fires_once_per_crossing);
    RUN_TEST(test_trigger_evaluate_above_min_fires_on_upward_crossing);
    RUN_TEST(test_trigger_evaluate_max_crossings);
    RUN_TEST(test_trigger_evaluate_exact_threshold_value_still_crosses);
    RUN_TEST(test_trigger_evaluate_moving_exactly_to_threshold_does_not_fire_above);
    RUN_TEST(test_trigger_evaluate_moving_exactly_to_threshold_does_not_fire_below);
    RUN_TEST(test_trigger_evaluate_max_moving_exactly_to_threshold_does_not_fire);
    RUN_TEST(test_trigger_evaluate_large_jump_fires_multiple_triggers);
    RUN_TEST(test_trigger_evaluate_measurement_finished_composes_with_edge_triggers);

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_render_registers_truncates_wide_fields);
    RUN_TEST(test_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_apply_reconfig_writes_resolution_and_trigger_thresholds);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_ignores_base_clk_octets_individually);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_encode_reconfig_request_rejects_null_data_with_nonzero_len);
    RUN_TEST(test_reconfig_strerror_never_null);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_read_request_round_trip);
    RUN_TEST(test_read_request_carries_no_payload);
    RUN_TEST(test_read_request_rejects_wrong_bus);
    RUN_TEST(test_read_request_rejects_wrong_op);
    RUN_TEST(test_read_request_rejects_nonzero_evt);
    RUN_TEST(test_read_request_rejects_short_frame);

    RUN_TEST(test_response_carries_eight_measurement_values);
    RUN_TEST(test_response_header_read_size_is_twice_value_count);
    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_bad_payload_len);
    RUN_TEST(test_response_decode_rejects_empty_payload);
    RUN_TEST(test_response_decode_rejects_more_values_than_caller_can_hold);
    RUN_TEST(test_response_encode_rejects_zero_or_oversized_value_count);
    RUN_TEST(test_response_encode_rejects_null_values_with_valid_count);
    RUN_TEST(test_response_no_signal_sentinel_round_trips);
    RUN_TEST(test_pipeline_end_to_end_multi_value_timed_response);

    RUN_TEST(test_cadence_case_accumulate_when_combine_exceeds_intervals);
    RUN_TEST(test_cadence_case_one_to_one_when_combine_equals_intervals);
    RUN_TEST(test_cadence_case_fan_out_when_combine_below_intervals);
    RUN_TEST(test_cadence_case_boundary_values);
    RUN_TEST(test_cadence_response_ready_true_when_pending_meets_combine);
    RUN_TEST(test_cadence_response_ready_false_when_pending_short);
    RUN_TEST(test_cadence_response_ready_zero_combine_always_ready);
    RUN_TEST(test_cadence_response_ready_drives_accumulate_case_across_executions);
    RUN_TEST(test_cadence_response_ready_drives_fan_out_case_across_responses);

    return UNITY_END();
}
