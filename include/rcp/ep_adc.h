/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-ADC-001
//cfusa:req REQ-ADC-002
//cfusa:req REQ-ADC-003
//cfusa:req REQ-ADC-004
//cfusa:req REQ-ADC-005
//cfusa:req REQ-ADC-006
//cfusa:req REQ-ADC-007
//cfusa:req REQ-ADC-008
//cfusa:req REQ-ADC-009
//cfusa:req REQ-ADC-010
//cfusa:req REQ-ADC-011
//cfusa:req REQ-ADC-012
//cfusa:req REQ-ADC-013
//cfusa:req REQ-ADC-014
//cfusa:req REQ-ADC-015
//cfusa:req REQ-ADC-016
//cfusa:req REQ-ADC-017
//cfusa:req REQ-ADC-018
//cfusa:req REQ-ADC-019
//cfusa:req REQ-ADC-020
//cfusa:req REQ-ADC-021
//cfusa:req REQ-ADC-022
//cfusa:req REQ-ADC-023
//cfusa:req REQ-ADC-024
//cfusa:req REQ-ADC-025
//cfusa:req REQ-ADC-026
//cfusa:req REQ-ADC-027
//cfusa:req REQ-ADC-028
//cfusa:req REQ-ADC-029
//cfusa:req REQ-ADC-030

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-ADC-031
//cfusa:req REQ-ADC-032
//cfusa:req REQ-ADC-033
//cfusa:req REQ-ADC-034
//cfusa:req REQ-ADC-035
//cfusa:req REQ-ADC-036
/*
 * ep_adc.h -- ADC endpoint for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 16, "Basic Endpoints", milestone 67).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any prior endpoint file
 * (ep_gpio.h/.c, ep_spi.h/.c, ep_i2c.h/.c, ep_uart.h/.c) is touched here --
 * the same layering discipline those modules established, followed
 * structurally throughout by this module too. This module does depend on
 * ep_pwm.h, landing in this same milestone (67) alongside it, solely to
 * reuse RCP_EP_PWM_IN_NO_SIGNAL as its own raw-sample timeout sentinel
 * rather than declaring a second, potentially inconsistent, constant of
 * its own -- see the "measurement timeout" section below.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── Framing level, single channel per byte_bus_id ───────────────────────────
 *
 * As with every prior endpoint type, ADC request/response traffic is
 * ordinary endpoint traffic: whether it rides an NTSCF or TSCF frame is a
 * transport/scheduling choice made by the caller (avtp.h), not a property
 * of this endpoint type. This module therefore operates at the ACF level
 * only (acf.h's rcp_acf_encode_abb()/_encode_gbb() and their decode
 * counterparts). Like ep_i2c.h/ep_uart.h, this endpoint type addresses
 * exactly one ADC channel per byte_bus_id -- there is no channel selector
 * on the wire, and the ACF header's evt field is always encoded/decoded as
 * 0.
 *
 * ── Request-driven sampling only: no autonomous free-running capture ──────
 *
 * This module never itself owns a timer, thread, or background sampling
 * loop -- every raw sample this module's averaging pipeline consumes is
 * supplied by the caller (e.g. read synchronously from real ADC hardware
 * at the moment a read request arrives), matching the specification's own
 * request-driven sampling model referenced by name only. A cyclic sampling
 * cadence is therefore always a caller-level concern, achieved either by
 * the requester re-issuing read requests on its own schedule
 * ("self-triggering") or by an external hardware trigger source gating
 * when the RC Server itself decides to run one averaging pipeline and
 * answer -- both are out of this pure protocol-core module's scope, which
 * models only the averaging math and the read request/response wire
 * codec, exactly like every prior endpoint type's own scope boundary.
 *
 * ── The three-layer sampling model, and the multi-value response ───────────
 *
 * An ADC response carries N measurement values, not one. The three
 * configurable stages that produce them, applied in this fixed order
 * (extraction §5.9), are:
 *
 *   1. rcp_ep_adc_average_interval(): adc_samples_per_avg_interval raw
 *      samples are averaged (arithmetic mean, ignoring any individual
 *      sample that itself timed out -- see below) into one
 *      rcp_ep_adc_avg_value_t per averaging interval.
 *   2. adc_avg_intervals_per_request such per-interval averages are
 *      captured per measurement cycle (by the caller, into the avg_values
 *      array rcp_ep_adc_collect_response_values() below takes).
 *   3. adc_combine_avg_values -- a COUNT, not a mode selector: the number
 *      of averaged output values to be placed in one response.
 *      rcp_ep_adc_collect_response_values() packs that many averaged
 *      values, in capture order, into the array
 *      rcp_ep_adc_encode_response() then encodes as the response payload.
 *
 * Stage 3 is deliberately *not* a second arithmetic reduction. An earlier
 * revision of this module modelled adc_combine_avg_values as a four-way
 * AVERAGE/MIN/MAX/LATEST mode enum collapsing every averaged value into
 * one 2-octet response payload; that has no basis in the register table,
 * which defines the field as the number of output values to be combined
 * into one response, and it made every response exactly one value wide
 * where a conforming peer expects N. The count relationship the
 * specification states for the request side is the same one
 * rcp_ep_adc_response_value_count() expresses: a response carries as many
 * measurement values as half the request's read_size.
 *
 * The three documented cadence cases follow directly from the two counts
 * and need no code of their own here (this module models the codec and
 * the arithmetic, never the scheduling): when adc_combine_avg_values
 * exceeds adc_avg_intervals_per_request, several request executions feed
 * one response (whose transaction_num is that of the request that
 * produced the response's first averaged value); when they are equal,
 * there is one response per execution; when it is smaller, one execution
 * yields several responses.
 *
 * Each stage is its own small, pure, directly-testable function operating
 * on caller-supplied arrays -- this module never itself owns sample
 * storage, matching every prior endpoint type's "consume already-gathered,
 * caller-prepared inputs" convention.
 *
 * ── Measurement timeout: reusing PWM_IN_NO_SIGNAL ───────────────────────────
 *
 * A raw sample that failed to complete within its applicable timeout
 * window is represented, by this module's own design choice, using the
 * exact same sentinel value ep_pwm.h's PWM_IN endpoint uses for its own
 * measurement-timeout case: RCP_EP_PWM_IN_NO_SIGNAL (see ep_pwm.h) --
 * reused here rather than duplicated, so a "no valid measurement" value
 * means the same numeric thing across both endpoint types sharing this
 * milestone. rcp_ep_adc_average_interval() excludes every NO_SIGNAL
 * sample from its arithmetic mean, reporting NO_SIGNAL itself (rather than
 * a mean computed from a smaller-than-expected sample set silently) only
 * when *every* sample in that interval timed out. There is no second
 * exclusion rule one layer up: because stage 3 is a packing count rather
 * than a reduction, an averaging interval that produced NO_SIGNAL is
 * reported verbatim as that response value's own contents -- the same
 * fail-safe-over-silent-substitution philosophy this project applies
 * throughout. A peer therefore sees exactly which of the N values in a
 * response had no valid measurement behind it, instead of that fact being
 * averaged away.
 *
 * ── The last-sample-of-the-first-response-value capture-moment rule ────────
 *
 * A timed response's message_timestamp is the moment the LAST sample that
 * fed the FIRST averaged value carried in that response was captured
 * (extraction §5.9.2) -- the end of that first averaging window, not its
 * start. Two functions express that rule between them:
 *
 *   - rcp_ep_adc_average_interval() sets an interval's
 *     rcp_ep_adc_avg_value_t.timestamp to that interval's *last* raw
 *     sample that actually contributed to the average, i.e. the last one
 *     not excluded as NO_SIGNAL. (An earlier revision used
 *     samples[0].timestamp, the interval's *first* sample -- the opposite
 *     end of the window, and wrong by a full averaging interval for every
 *     interval longer than one sample.) When every sample in an interval
 *     timed out, so that none was "used", the last sample's timestamp is
 *     reported, since that is still the moment the interval closed.
 *   - rcp_ep_adc_capture_moment_timestamp() then selects the *first*
 *     averaged value carried in the response: avg_values[0].timestamp.
 *
 * As with every prior endpoint type's own
 * timed-response convention, the resulting timestamp value is still only
 * caller-supplied input to rcp_ep_adc_encode_response()'s own timed/
 * untimed choice, never read from a clock by this module itself.
 *
 * ── Functional configuration ────────────────────────────────────────────────
 *
 * rcp_ep_adc_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that
 * module's documented convention, same as every prior endpoint type) and
 * adds this endpoint's three sampling-pipeline parameters:
 * adc_samples_per_avg_interval, adc_avg_intervals_per_request, and
 * adc_combine_avg_values (a count of output values per response). rcp_ep_adc_functional_cfg_writable() is,
 * likewise, a thin, named wrapper over server.h's
 * rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W), and every
 * rcp_ep_adc_set_*() mutator consults it before ever touching cfg --
 * reusing, never duplicating, server.h's/regmap.h's existing authorization
 * logic, per the roadmap's explicit instruction (the same rule every
 * prior endpoint type's own setters already follow).
 */
#ifndef RCP_EP_ADC_H
#define RCP_EP_ADC_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/ep_pwm.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/lifecycle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Response geometry: measurement values are 2 octets each ────────────────── */

/* The octet width of one measurement value in a response payload. A
 * response's payload is exactly value_count * RCP_EP_ADC_VALUE_LEN octets
 * long -- N values, never one (see the file header). */
#define RCP_EP_ADC_VALUE_LEN ((size_t)2u)

/* The largest number of measurement values one response can carry, i.e.
 * the largest value_count rcp_ep_adc_encode_response() accepts. Derived
 * from acf.h's own payload ceiling for the narrower (GBB) message form,
 * so the same bound holds whether the response is timed or not. */
#define RCP_EP_ADC_MAX_VALUES ((size_t)(RCP_ACF_MAX_PAYLOAD / RCP_EP_ADC_VALUE_LEN))

/* The number of measurement values a response to a request carrying
 * read_size is expected to contain: half the read_size, since each value
 * occupies RCP_EP_ADC_VALUE_LEN octets (extraction §5.9.3). Returns 0 for
 * an odd read_size, which cannot describe a whole number of values. */
size_t rcp_ep_adc_response_value_count(uint16_t read_size);

/* ── Layer 1: adc_samples_per_avg_interval ───────────────────────────────────── */

/* One raw ADC sample: value is RCP_EP_PWM_IN_NO_SIGNAL (ep_pwm.h) iff this
 * particular sample did not complete within its timeout window; timestamp
 * is the moment this sample was captured (or attempted). */
typedef struct {
    uint16_t value;
    uint64_t timestamp;
} rcp_ep_adc_sample_t;

/* One averaging interval's result: value is the arithmetic mean (rounded
 * down) of every samples[i].value that is not RCP_EP_PWM_IN_NO_SIGNAL, or
 * RCP_EP_PWM_IN_NO_SIGNAL itself iff every sample in this interval timed
 * out (or sample_count == 0); timestamp is the capture moment of the LAST
 * sample that contributed to that mean -- or, when none did, of the last
 * sample in the interval (0 iff sample_count == 0). See the file header's
 * capture-moment note. */
typedef struct {
    uint16_t value;
    uint64_t timestamp;
} rcp_ep_adc_avg_value_t;

/* Computes one averaging interval's rcp_ep_adc_avg_value_t from
 * sample_count raw samples -- layer 1 of the three-layer averaging model
 * (see the file header). samples may be NULL iff sample_count == 0. */
rcp_ep_adc_avg_value_t rcp_ep_adc_average_interval(const rcp_ep_adc_sample_t *samples,
                                                    size_t sample_count);

/* ── Layers 2/3: adc_avg_intervals_per_request + adc_combine_avg_values ─────── */

/* Packs the first value_count layer-1 results into out_values, in capture
 * order and verbatim (a NO_SIGNAL interval is carried through as
 * NO_SIGNAL, never averaged away) -- layers 2/3 of the sampling model,
 * where adc_combine_avg_values is the output-value COUNT this function's
 * value_count parameter carries (see the file header). Returns the number
 * of values actually written, min(avg_count, value_count); out_values
 * entries beyond that are left untouched, so a caller that asked for more
 * values than it has averages yet knows exactly how many it still owes.
 * avg_values may be NULL iff avg_count == 0; out_values may be NULL iff
 * value_count == 0. */
size_t rcp_ep_adc_collect_response_values(const rcp_ep_adc_avg_value_t *avg_values,
                                           size_t avg_count,
                                           uint16_t *out_values, size_t value_count);

/* The last-sample-of-the-first-response-value capture-moment rule
 * (extraction §5.9.2) -- see the file header. Given the same avg_values
 * array rcp_ep_adc_collect_response_values() packed, returns
 * avg_values[0].timestamp: the moment the last sample feeding the
 * response's first measurement value was captured (0 iff avg_count == 0).
 * avg_values may be NULL iff avg_count == 0. */
uint64_t rcp_ep_adc_capture_moment_timestamp(const rcp_ep_adc_avg_value_t *avg_values,
                                              size_t avg_count);

/* ── Functional config ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    uint16_t                       adc_samples_per_avg_interval;
    uint16_t                       adc_avg_intervals_per_request;
    uint8_t                        adc_combine_avg_values; /* COUNT of output
                                                               values per
                                                               response -- not a
                                                               mode selector; see
                                                               the file header */
} rcp_ep_adc_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, and
 * adc_samples_per_avg_interval, adc_avg_intervals_per_request and
 * adc_combine_avg_values all 0). */
void rcp_ep_adc_functional_cfg_init(rcp_ep_adc_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (server.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_adc_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->adc_samples_per_avg_interval to samples_per_interval iff
 * rcp_ep_adc_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_adc_set_samples_per_avg_interval(rcp_ep_adc_functional_cfg_t *cfg,
                                              uint16_t samples_per_interval,
                                              rcp_lifecycle_state_t state,
                                              rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_adc_set_samples_per_avg_interval(),
 * for cfg->adc_avg_intervals_per_request. */
bool rcp_ep_adc_set_avg_intervals_per_request(rcp_ep_adc_functional_cfg_t *cfg,
                                               uint16_t intervals_per_request,
                                               rcp_lifecycle_state_t state,
                                               rcp_lifecycle_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_adc_set_samples_per_avg_interval(),
 * for cfg->adc_combine_avg_values -- the number of averaged output values
 * to place in one response (see the file header). Every value the field's
 * one-octet width can hold is a legal count, so there is no separate
 * validity predicate for it. */
bool rcp_ep_adc_set_combine_avg_values(rcp_ep_adc_functional_cfg_t *cfg,
                                        uint8_t combine_avg_values,
                                        rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_ADC_OK                  = 0,
    RCP_EP_ADC_ERR_SHORT_FRAME     = 1,
    RCP_EP_ADC_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_ADC_ERR_WRONG_BUS       = 3,
    RCP_EP_ADC_ERR_WRONG_OP        = 4,
    RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN = 5,
    /* The response carries more measurement values than the caller's
     * out_values array can hold -- see rcp_ep_adc_decode_response(). */
    RCP_EP_ADC_ERR_TOO_MANY_VALUES = 6,
    /* evt[2:0] is not 0b000, TC18 §13.5 Table 30's only legal value for a
     * plain (non-configuration) request in ADC's endpoint-type row --
     * caller shall respond with error code UNSUPPORTED_CMD (see
     * rcp_acf_evt_row2_is_plain()). */
    RCP_EP_ADC_ERR_BAD_EVT         = 7,
} rcp_ep_adc_errc_t;

/* Human-readable message for an rcp_ep_adc_errc_t value. Never returns
 * NULL. */
const char *rcp_ep_adc_strerror(rcp_ep_adc_errc_t e);

/* ── Read request ──────────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB read request addressed to byte_bus_id, with no
 * payload -- a request carries no byte_msg_payload of its own; how many
 * measurement values it asks for is carried by read_size alone
 * (extraction §5.9.3), which the endpoint answers with
 * rcp_ep_adc_response_value_count(read_size) values. Returns a zeroed
 * rcp_bytes_t (data=NULL) on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_adc_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint16_t read_size,
                                            uint8_t transaction_num);

/* Decodes and validates an ACF-level ADC read request from b[0..len).
 * Fails with RCP_EP_ADC_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_ADC_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_ADC_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_ADC_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_READ; RCP_EP_ADC_ERR_BAD_EVT if its evt[2:0] is not 0b000
 * (rcp_acf_evt_row2_is_plain(), TC18 §13.5 Table 30 -- the caller shall
 * respond with error code UNSUPPORTED_CMD). On RCP_EP_ADC_OK,
 * *out_read_size and *out_transaction_num are populated. */
rcp_ep_adc_errc_t rcp_ep_adc_decode_read_request(const uint8_t *b, size_t len,
                                                  rcp_byte_bus_id_t expected_bus_id,
                                                  uint16_t *out_read_size,
                                                  uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes an ADC response carrying values[0..value_count) -- the
 * measurement values rcp_ep_adc_collect_response_values() packed, each
 * possibly RCP_EP_PWM_IN_NO_SIGNAL (see the file header) -- as a
 * value_count * RCP_EP_ADC_VALUE_LEN octet big-endian payload, echoing
 * transaction_num and reporting 2 * value_count as the header's read_size.
 * Encoded as ACF_ABB when timed is false; as ACF_GBB (with
 * message_timestamp set to timestamp -- see
 * rcp_ep_adc_capture_moment_timestamp() -- mtv = RCP_ACF_MTV_VALID) when
 * timed is true. Returns a zeroed rcp_bytes_t (data=NULL) if value_count
 * is 0 or exceeds RCP_EP_ADC_MAX_VALUES, or on allocation failure. Caller
 * frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_adc_encode_response(rcp_byte_bus_id_t byte_bus_id, const uint16_t *values,
                                       size_t value_count, uint8_t transaction_num, bool timed,
                                       uint64_t timestamp);

/* Decodes an ADC response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoder
 * above, since a response's encoding depends on the responding endpoint's
 * own timed/untimed choice). Fails with RCP_EP_ADC_ERR_SHORT_FRAME (frame
 * too short for the applicable fixed header or its declared payload),
 * RCP_EP_ADC_ERR_WRONG_BUS (byte_bus_id != expected_bus_id),
 * RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN (payload absent, or present but not a
 * whole number of RCP_EP_ADC_VALUE_LEN-octet values), or
 * RCP_EP_ADC_ERR_TOO_MANY_VALUES (the payload holds more values than
 * max_values). On RCP_EP_ADC_OK, out_values[0..*out_value_count) and
 * *out_transaction_num are populated; *out_timed and *out_timestamp report
 * whether the message was ACF_GBB with a valid (rcp_acf_gbb_is_timed())
 * timestamp, and that timestamp's value (0 when !*out_timed). */
rcp_ep_adc_errc_t rcp_ep_adc_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              uint16_t *out_values, size_t max_values,
                                              size_t *out_value_count, bool *out_timed,
                                              uint64_t *out_timestamp,
                                              uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_ADC_H */
