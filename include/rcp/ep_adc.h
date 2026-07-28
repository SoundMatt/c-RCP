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
 * ── The three-layer averaging model ─────────────────────────────────────────
 *
 * A single ADC response value is the result of three configurable
 * averaging stages, applied in this fixed order (extraction §5.9):
 *
 *   1. rcp_ep_adc_average_interval(): adc_samples_per_avg_interval raw
 *      samples are averaged (arithmetic mean, ignoring any individual
 *      sample that itself timed out -- see below) into one
 *      rcp_ep_adc_avg_value_t per averaging interval.
 *   2. adc_avg_intervals_per_request such per-interval averages are
 *      collected (by the caller, into the avg_values array
 *      rcp_ep_adc_combine_avg_values() below takes).
 *   3. rcp_ep_adc_combine_avg_values(): those avg_intervals_per_request
 *      averages are combined, per the endpoint's adc_combine_avg_values
 *      functional-config selector (rcp_ep_adc_combine_mode_t), into the
 *      single value a request's response ultimately reports.
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
 * when *every* sample in that interval timed out; rcp_ep_adc_combine_avg_values()
 * applies the identical exclusion rule one layer up, over per-interval
 * averages, for RCP_EP_ADC_COMBINE_AVERAGE/_MIN/_MAX -- except
 * RCP_EP_ADC_COMBINE_LATEST, which deliberately reports its single most
 * recent interval's value even when that value is itself NO_SIGNAL (a
 * "most recent reading" combine mode legitimately reports "no signal" when
 * the most recent interval genuinely detected nothing -- the same
 * fail-safe-over-silent-substitution philosophy this project applies
 * throughout, not a special case invented for its own sake).
 *
 * ── The first-sample-of-first-combined-value capture-moment rule ───────────
 *
 * Per rcp_ep_adc_average_interval()'s own contract, an interval's
 * rcp_ep_adc_avg_value_t.timestamp is always that interval's first raw
 * sample's timestamp (samples[0].timestamp), regardless of how many
 * samples within that interval are later averaged together or excluded as
 * NO_SIGNAL. rcp_ep_adc_capture_moment_timestamp() is this module's own
 * single, directly-testable expression of the resulting rule a response's
 * message_timestamp ultimately follows (extraction §5.9): given the same
 * avg_values array rcp_ep_adc_combine_avg_values() combined into a
 * request's reported value, the applicable timestamp is always
 * avg_values[0].timestamp -- i.e. the very first raw sample of the very
 * first averaging interval that fed the request, independent of which
 * combine mode selected the reported *value* itself (even
 * RCP_EP_ADC_COMBINE_LATEST, which reports its *last* interval's value,
 * still reports the *first* interval's capture moment as the applicable
 * timestamp -- value and timestamp are deliberately not required to trace
 * to the same interval). As with every prior endpoint type's own
 * timed-response convention, the resulting timestamp value is still only
 * caller-supplied input to rcp_ep_adc_encode_response()'s own timed/
 * untimed choice, never read from a clock by this module itself.
 *
 * ── Functional configuration ────────────────────────────────────────────────
 *
 * rcp_ep_adc_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that
 * module's documented convention, same as every prior endpoint type) and
 * adds this endpoint's three averaging-pipeline parameters:
 * adc_samples_per_avg_interval, adc_avg_intervals_per_request, and
 * adc_combine_avg_values. rcp_ep_adc_functional_cfg_writable() is,
 * likewise, a thin, named wrapper over server.h's
 * rcp_server_field_writable() (RCP_SERVER_FIELD_FUNCTIONAL_W), and every
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
#include "rcp/server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── adc_combine_avg_values: the layer-3 combine modes ──────────────────────── */

typedef enum {
    RCP_EP_ADC_COMBINE_AVERAGE = 0, /* arithmetic mean of every avg_value */
    RCP_EP_ADC_COMBINE_MIN     = 1, /* smallest avg_value */
    RCP_EP_ADC_COMBINE_MAX     = 2, /* largest avg_value */
    RCP_EP_ADC_COMBINE_LATEST  = 3, /* the most recent (last) avg_value --
                                        see the file header's NO_SIGNAL note */
} rcp_ep_adc_combine_mode_t;

/* True iff v is one of the four defined combine modes, i.e. v <= 3. */
bool rcp_ep_adc_combine_mode_valid(uint8_t v);

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
 * out (or sample_count == 0); timestamp is always samples[0].timestamp (0
 * iff sample_count == 0) -- see the file header's capture-moment note. */
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

/* Combines avg_count layer-1 results per combine_mode into the single
 * value a request's response ultimately reports -- layers 2/3 of the
 * three-layer averaging model (see the file header). Returns
 * RCP_EP_PWM_IN_NO_SIGNAL iff avg_count == 0, or (for every combine_mode
 * except RCP_EP_ADC_COMBINE_LATEST) iff every avg_values[i].value is
 * itself RCP_EP_PWM_IN_NO_SIGNAL; RCP_EP_ADC_COMBINE_LATEST instead always
 * returns avg_values[avg_count - 1].value verbatim, NO_SIGNAL or not (see
 * the file header). avg_values may be NULL iff avg_count == 0. An invalid
 * combine_mode is treated the same as RCP_EP_ADC_COMBINE_AVERAGE
 * (fail-safe default, mirroring this project's convention of never
 * fabricating behavior for undefined input by instead falling back to the
 * most conservative defined one). */
uint16_t rcp_ep_adc_combine_avg_values(const rcp_ep_adc_avg_value_t *avg_values, size_t avg_count,
                                        rcp_ep_adc_combine_mode_t combine_mode);

/* The first-sample-of-first-combined-value capture-moment rule (extraction
 * §5.9) -- see the file header. Given the same avg_values array
 * rcp_ep_adc_combine_avg_values() combined, returns avg_values[0].timestamp
 * (0 iff avg_count == 0), independent of combine_mode. avg_values may be
 * NULL iff avg_count == 0. */
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
    uint8_t                        adc_combine_avg_values; /* rcp_ep_adc_combine_mode_t */
} rcp_ep_adc_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false,
 * adc_samples_per_avg_interval/adc_avg_intervals_per_request both 0,
 * adc_combine_avg_values RCP_EP_ADC_COMBINE_AVERAGE (0)). */
void rcp_ep_adc_functional_cfg_init(rcp_ep_adc_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_server_field_writable()
 * (server.h) with kind RCP_SERVER_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_adc_functional_cfg_writable(rcp_server_lifecycle_t state,
                                        rcp_server_writer_ctx_t writer);

/* Sets cfg->adc_samples_per_avg_interval to samples_per_interval iff
 * rcp_ep_adc_functional_cfg_writable() authorizes the write for
 * state/writer; returns whether the write was applied. cfg is left
 * entirely unchanged when it returns false. */
bool rcp_ep_adc_set_samples_per_avg_interval(rcp_ep_adc_functional_cfg_t *cfg,
                                              uint16_t samples_per_interval,
                                              rcp_server_lifecycle_t state,
                                              rcp_server_writer_ctx_t writer);

/* Same authorization rule as rcp_ep_adc_set_samples_per_avg_interval(),
 * for cfg->adc_avg_intervals_per_request. */
bool rcp_ep_adc_set_avg_intervals_per_request(rcp_ep_adc_functional_cfg_t *cfg,
                                               uint16_t intervals_per_request,
                                               rcp_server_lifecycle_t state,
                                               rcp_server_writer_ctx_t writer);

/* Sets cfg->adc_combine_avg_values to combine_mode iff combine_mode is
 * rcp_ep_adc_combine_mode_valid() and rcp_ep_adc_functional_cfg_writable()
 * authorizes the write for state/writer; returns whether the write was
 * applied. cfg is left entirely unchanged when it returns false. */
bool rcp_ep_adc_set_combine_mode(rcp_ep_adc_functional_cfg_t *cfg,
                                  rcp_ep_adc_combine_mode_t combine_mode,
                                  rcp_server_lifecycle_t state, rcp_server_writer_ctx_t writer);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_ADC_OK                  = 0,
    RCP_EP_ADC_ERR_SHORT_FRAME     = 1,
    RCP_EP_ADC_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_ADC_ERR_WRONG_BUS       = 3,
    RCP_EP_ADC_ERR_WRONG_OP        = 4,
    RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN = 5,
} rcp_ep_adc_errc_t;

/* Human-readable message for an rcp_ep_adc_errc_t value. Never returns
 * NULL. */
const char *rcp_ep_adc_strerror(rcp_ep_adc_errc_t e);

/* ── Read request ──────────────────────────────────────────────────────────── */

/* The fixed payload length (octets) of an ADC response -- see the file
 * header. A read request itself carries no payload. */
#define RCP_EP_ADC_PAYLOAD_LEN ((size_t)2u)

/* Encodes an ACF_ABB read request addressed to byte_bus_id, with no
 * payload -- requests one full run of the three-layer averaging pipeline
 * (see the file header). Returns a zeroed rcp_bytes_t (data=NULL) on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_adc_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num);

/* Decodes and validates an ACF-level ADC read request from b[0..len).
 * Fails with RCP_EP_ADC_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_ADC_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_ADC_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_ADC_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_READ. On RCP_EP_ADC_OK, *out_transaction_num is populated. */
rcp_ep_adc_errc_t rcp_ep_adc_decode_read_request(const uint8_t *b, size_t len,
                                                  rcp_byte_bus_id_t expected_bus_id,
                                                  uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes an ADC response carrying value (the pipeline's final combined
 * result -- possibly RCP_EP_PWM_IN_NO_SIGNAL, see the file header) as its
 * RCP_EP_ADC_PAYLOAD_LEN big-endian payload, echoing transaction_num.
 * Encoded as ACF_ABB when timed is false; as ACF_GBB (with
 * message_timestamp set to timestamp -- see
 * rcp_ep_adc_capture_moment_timestamp() -- mtv = RCP_ACF_MTV_VALID) when
 * timed is true. Returns a zeroed rcp_bytes_t (data=NULL) on allocation
 * failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_adc_encode_response(rcp_byte_bus_id_t byte_bus_id, uint16_t value,
                                       uint8_t transaction_num, bool timed, uint64_t timestamp);

/* Decodes an ADC response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoder
 * above, since a response's encoding depends on the responding endpoint's
 * own timed/untimed choice). Fails with RCP_EP_ADC_ERR_SHORT_FRAME (frame
 * too short for the applicable fixed header or a full
 * RCP_EP_ADC_PAYLOAD_LEN payload), RCP_EP_ADC_ERR_WRONG_BUS (byte_bus_id
 * != expected_bus_id), or RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN (payload present
 * but not exactly RCP_EP_ADC_PAYLOAD_LEN octets). On RCP_EP_ADC_OK,
 * *out_value and *out_transaction_num are populated; *out_timed and
 * *out_timestamp report whether the message was ACF_GBB with a valid
 * (rcp_acf_gbb_is_timed()) timestamp, and that timestamp's value (0 when
 * !*out_timed). */
rcp_ep_adc_errc_t rcp_ep_adc_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              uint16_t *out_value, bool *out_timed,
                                              uint64_t *out_timestamp,
                                              uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_ADC_H */
