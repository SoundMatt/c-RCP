/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_adc.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy -- see ep_gpio.c/ep_pwm.c for
 * the same house convention) ────────────────────────────────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* ── Response geometry ─────────────────────────────────────────────────────── */

//cfusa:req REQ-ADC-001
size_t rcp_ep_adc_response_value_count(uint16_t read_size)
{
    /* A response carries as many measurement values as half the request's
     * read_size -- each value is RCP_EP_ADC_VALUE_LEN octets wide
     * (extraction §5.9.3). An odd read_size cannot describe a whole
     * number of values, so it describes none. */
    if ((read_size % (uint16_t)RCP_EP_ADC_VALUE_LEN) != 0u) return 0u;
    return (size_t)read_size / RCP_EP_ADC_VALUE_LEN;
}

/* ── Layer 1: adc_samples_per_avg_interval ───────────────────────────────────── */

//cfusa:req REQ-ADC-002
//cfusa:req REQ-ADC-003
//cfusa:req REQ-ADC-004
//cfusa:req REQ-ADC-005
rcp_ep_adc_avg_value_t rcp_ep_adc_average_interval(const rcp_ep_adc_sample_t *samples,
                                                    size_t sample_count)
{
    rcp_ep_adc_avg_value_t result;
    uint64_t                sum;
    size_t                   counted;
    size_t                   i;

    if (sample_count == 0) {
        result.value     = RCP_EP_PWM_IN_NO_SIGNAL;
        result.timestamp = 0u;
        return result;
    }

    sum     = 0u;
    counted = 0u;
    for (i = 0; i < sample_count; i++) {
        if (samples[i].value == RCP_EP_PWM_IN_NO_SIGNAL) continue;
        sum += samples[i].value;
        counted++;
    }

    /* The capture moment of the LAST sample that actually fed the mean --
     * the end of the averaging window, not its start (extraction §5.9.2).
     * When no sample was usable at all, the interval's last sample still
     * marks the moment the window closed, so that is reported instead of
     * a value from an arbitrary earlier point. */
    result.timestamp = samples[sample_count - 1].timestamp;
    for (i = sample_count; i > 0; i--) {
        if (samples[i - 1].value == RCP_EP_PWM_IN_NO_SIGNAL) continue;
        result.timestamp = samples[i - 1].timestamp;
        break;
    }

    result.value = (counted == 0) ? RCP_EP_PWM_IN_NO_SIGNAL
                                  : (uint16_t)(sum / counted);
    return result;
}

/* ── Layers 2/3: adc_avg_intervals_per_request + adc_combine_avg_values ─────── */

//cfusa:req REQ-ADC-006
//cfusa:req REQ-ADC-007
//cfusa:req REQ-ADC-008
//cfusa:req REQ-ADC-009
//cfusa:req REQ-ADC-010
//cfusa:req REQ-ADC-011
size_t rcp_ep_adc_collect_response_values(const rcp_ep_adc_avg_value_t *avg_values,
                                           size_t avg_count,
                                           uint16_t *out_values, size_t value_count)
{
    size_t n;
    size_t i;

    /* adc_combine_avg_values is the COUNT of averaged output values a
     * response carries, so this stage packs rather than reduces: each
     * averaged value goes out verbatim, in capture order, NO_SIGNAL
     * included (see the file header). */
    n = (avg_count < value_count) ? avg_count : value_count;
    for (i = 0; i < n; i++) {
        out_values[i] = avg_values[i].value;
    }
    return n;
}

//cfusa:req REQ-ADC-012
//cfusa:req REQ-ADC-013
uint64_t rcp_ep_adc_capture_moment_timestamp(const rcp_ep_adc_avg_value_t *avg_values,
                                              size_t avg_count)
{
    if (avg_count == 0) return 0u;
    return avg_values[0].timestamp;
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-ADC-014
void rcp_ep_adc_functional_cfg_init(rcp_ep_adc_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* adc_samples_per_avg_interval, adc_avg_intervals_per_request and
     * adc_combine_avg_values (an output-value count) are all already 0
     * via the memset above. */
}

//cfusa:req REQ-ADC-015
//cfusa:req REQ-ADC-016
//cfusa:req REQ-ADC-017
bool rcp_ep_adc_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-ADC-018
//cfusa:req REQ-ADC-019
bool rcp_ep_adc_set_samples_per_avg_interval(rcp_ep_adc_functional_cfg_t *cfg,
                                              uint16_t samples_per_interval,
                                              rcp_lifecycle_state_t state,
                                              rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_adc_functional_cfg_writable(state, writer)) return false;

    cfg->adc_samples_per_avg_interval = samples_per_interval;
    return true;
}

//cfusa:req REQ-ADC-020
//cfusa:req REQ-ADC-021
bool rcp_ep_adc_set_avg_intervals_per_request(rcp_ep_adc_functional_cfg_t *cfg,
                                               uint16_t intervals_per_request,
                                               rcp_lifecycle_state_t state,
                                               rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_adc_functional_cfg_writable(state, writer)) return false;

    cfg->adc_avg_intervals_per_request = intervals_per_request;
    return true;
}

//cfusa:req REQ-ADC-022
//cfusa:req REQ-ADC-023
bool rcp_ep_adc_set_combine_avg_values(rcp_ep_adc_functional_cfg_t *cfg,
                                        uint8_t combine_avg_values,
                                        rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_adc_functional_cfg_writable(state, writer)) return false;

    cfg->adc_combine_avg_values = combine_avg_values;
    return true;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-ADC-024
const char *rcp_ep_adc_strerror(rcp_ep_adc_errc_t e)
{
    switch (e) {
    case RCP_EP_ADC_OK:                  return "rcp/ep_adc: success";
    case RCP_EP_ADC_ERR_SHORT_FRAME:     return "rcp/ep_adc: frame too short";
    case RCP_EP_ADC_ERR_BAD_MSG_TYPE:    return "rcp/ep_adc: unexpected ACF message type";
    case RCP_EP_ADC_ERR_WRONG_BUS:       return "rcp/ep_adc: wrong byte_bus_id";
    case RCP_EP_ADC_ERR_WRONG_OP:        return "rcp/ep_adc: wrong ACF op";
    case RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN: return "rcp/ep_adc: unexpected payload length";
    case RCP_EP_ADC_ERR_TOO_MANY_VALUES: return "rcp/ep_adc: more measurement values than the caller can hold";
    default:                             return "rcp/ep_adc: unknown error";
    }
}

/* ── Read request ──────────────────────────────────────────────────────────── */

//cfusa:req REQ-ADC-025
rcp_bytes_t rcp_ep_adc_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint16_t read_size,
                                            uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id              = byte_bus_id;
    hdr.op                       = RCP_ACF_OP_READ;
    /* How many measurement values the response is to carry is conveyed by
     * read_size alone -- the request has no payload of its own
     * (extraction §5.9.3). */
    hdr.read_size_or_segment_num = read_size;
    hdr.transaction_num          = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-ADC-025
//cfusa:req REQ-ADC-026
rcp_ep_adc_errc_t rcp_ep_adc_decode_read_request(const uint8_t *b, size_t len,
                                                  rcp_byte_bus_id_t expected_bus_id,
                                                  uint16_t *out_read_size,
                                                  uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ADC_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_ADC_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_ADC_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_ADC_ERR_WRONG_OP;

    (void)payload;
    (void)payload_len; /* a read request carries no payload -- see ep_adc.h */

    *out_read_size       = hdr.read_size_or_segment_num;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_ADC_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-ADC-027
//cfusa:req REQ-ADC-028
//cfusa:req REQ-ADC-030
rcp_bytes_t rcp_ep_adc_encode_response(rcp_byte_bus_id_t byte_bus_id, const uint16_t *values,
                                       size_t value_count, uint8_t transaction_num, bool timed,
                                       uint64_t timestamp)
{
    rcp_bytes_t empty = {0};
    uint8_t    *payload;
    size_t      payload_len;
    size_t      i;
    rcp_bytes_t frame;

    /* A response carries N measurement values, not one -- see the file
     * header. */
    if (values == NULL || value_count == 0 || value_count > RCP_EP_ADC_MAX_VALUES) return empty;

    payload_len = value_count * RCP_EP_ADC_VALUE_LEN;
    payload     = (uint8_t *)malloc(payload_len);
    if (!payload) return empty;

    for (i = 0; i < value_count; i++) {
        put_u16(&payload[i * RCP_EP_ADC_VALUE_LEN], values[i]);
    }

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id              = byte_bus_id;
        hdr.info.op                       = RCP_ACF_OP_READ;
        hdr.info.rsp                      = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.mtv                      = RCP_ACF_MTV_VALID;
        hdr.info.read_size_or_segment_num = (uint16_t)payload_len;
        hdr.info.transaction_num          = transaction_num;
        hdr.message_timestamp             = timestamp;

        frame = rcp_acf_encode_gbb(&hdr, payload, payload_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id              = byte_bus_id;
        hdr.op                       = RCP_ACF_OP_READ;
        hdr.rsp                      = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.read_size_or_segment_num = (uint16_t)payload_len;
        hdr.transaction_num          = transaction_num;

        frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    }

    free(payload);
    return frame;
}

//cfusa:req REQ-ADC-027
//cfusa:req REQ-ADC-028
//cfusa:req REQ-ADC-029
//cfusa:req REQ-ADC-030
rcp_ep_adc_errc_t rcp_ep_adc_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              uint16_t *out_values, size_t max_values,
                                              size_t *out_value_count, bool *out_timed,
                                              uint64_t *out_timestamp,
                                              uint8_t *out_transaction_num)
{
    uint8_t                      msg_type;
    rcp_acf_errc_t               acf_rc;
    rcp_acf_byte_message_info_t  abb_hdr;
    rcp_acf_gbb_header_t         gbb_hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_byte_bus_id_t            bus_id;
    uint8_t                      transaction_num;
    size_t                       value_count;
    size_t                       i;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_ADC_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ADC_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_ADC_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        *out_timed      = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp  = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ADC_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_ADC_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        *out_timed      = false;
        *out_timestamp  = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_ADC_ERR_WRONG_BUS;

    /* A response's payload is a whole number of 2-octet measurement
     * values, and never empty -- see the file header. */
    if (payload_len == 0 || (payload_len % RCP_EP_ADC_VALUE_LEN) != 0) {
        return RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN;
    }

    value_count = payload_len / RCP_EP_ADC_VALUE_LEN;
    if (value_count > max_values) return RCP_EP_ADC_ERR_TOO_MANY_VALUES;

    for (i = 0; i < value_count; i++) {
        out_values[i] = get_u16(&payload[i * RCP_EP_ADC_VALUE_LEN]);
    }

    *out_value_count     = value_count;
    *out_transaction_num = transaction_num;
    return RCP_EP_ADC_OK;
}
