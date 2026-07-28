#include "rcp/ep_adc.h"

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

/* ── adc_combine_avg_values: the layer-3 combine modes ──────────────────────── */

//cfusa:req REQ-ADC-001
bool rcp_ep_adc_combine_mode_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_ADC_COMBINE_LATEST;
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

    result.timestamp = samples[0].timestamp; /* always the interval's first
                                                  sample -- see the file
                                                  header */
    result.value     = (counted == 0) ? RCP_EP_PWM_IN_NO_SIGNAL
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
uint16_t rcp_ep_adc_combine_avg_values(const rcp_ep_adc_avg_value_t *avg_values, size_t avg_count,
                                        rcp_ep_adc_combine_mode_t combine_mode)
{
    size_t   i;
    size_t   counted;
    uint64_t sum;
    uint16_t best;

    if (avg_count == 0) return RCP_EP_PWM_IN_NO_SIGNAL;

    if (combine_mode == RCP_EP_ADC_COMBINE_LATEST) {
        return avg_values[avg_count - 1].value; /* NO_SIGNAL or not -- see
                                                     the file header */
    }

    if (combine_mode == RCP_EP_ADC_COMBINE_MIN || combine_mode == RCP_EP_ADC_COMBINE_MAX) {
        bool found = false;

        best = (combine_mode == RCP_EP_ADC_COMBINE_MIN) ? 0xFFFFu : 0x0000u;
        for (i = 0; i < avg_count; i++) {
            if (avg_values[i].value == RCP_EP_PWM_IN_NO_SIGNAL) continue;
            if (!found) {
                best  = avg_values[i].value;
                found = true;
                continue;
            }
            if (combine_mode == RCP_EP_ADC_COMBINE_MIN) {
                if (avg_values[i].value < best) best = avg_values[i].value;
            } else {
                if (avg_values[i].value > best) best = avg_values[i].value;
            }
        }
        return found ? best : RCP_EP_PWM_IN_NO_SIGNAL;
    }

    /* RCP_EP_ADC_COMBINE_AVERAGE, and the fail-safe fallback for any
     * out-of-range combine_mode value -- see the file header. */
    sum     = 0u;
    counted = 0u;
    for (i = 0; i < avg_count; i++) {
        if (avg_values[i].value == RCP_EP_PWM_IN_NO_SIGNAL) continue;
        sum += avg_values[i].value;
        counted++;
    }
    return (counted == 0) ? RCP_EP_PWM_IN_NO_SIGNAL : (uint16_t)(sum / counted);
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
    /* adc_samples_per_avg_interval/adc_avg_intervals_per_request are
     * already 0, and adc_combine_avg_values is already
     * RCP_EP_ADC_COMBINE_AVERAGE (0), via the memset above. */
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
bool rcp_ep_adc_set_combine_mode(rcp_ep_adc_functional_cfg_t *cfg,
                                  rcp_ep_adc_combine_mode_t combine_mode,
                                  rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_adc_combine_mode_valid((uint8_t)combine_mode)) return false;
    if (!rcp_ep_adc_functional_cfg_writable(state, writer)) return false;

    cfg->adc_combine_avg_values = (uint8_t)combine_mode;
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
    default:                             return "rcp/ep_adc: unknown error";
    }
}

/* ── Read request ──────────────────────────────────────────────────────────── */

//cfusa:req REQ-ADC-025
rcp_bytes_t rcp_ep_adc_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-ADC-025
//cfusa:req REQ-ADC-026
rcp_ep_adc_errc_t rcp_ep_adc_decode_read_request(const uint8_t *b, size_t len,
                                                  rcp_byte_bus_id_t expected_bus_id,
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

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_ADC_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-ADC-027
//cfusa:req REQ-ADC-028
//cfusa:req REQ-ADC-030
rcp_bytes_t rcp_ep_adc_encode_response(rcp_byte_bus_id_t byte_bus_id, uint16_t value,
                                       uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    uint8_t payload[RCP_EP_ADC_PAYLOAD_LEN];

    put_u16(payload, value);

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, payload, sizeof(payload));
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    }
}

//cfusa:req REQ-ADC-027
//cfusa:req REQ-ADC-028
//cfusa:req REQ-ADC-029
//cfusa:req REQ-ADC-030
rcp_ep_adc_errc_t rcp_ep_adc_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              uint16_t *out_value, bool *out_timed,
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

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_ADC_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ADC_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_ADC_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_ADC_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_ADC_PAYLOAD_LEN) return RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_u16(payload);
        *out_timed     = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_ADC_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_ADC_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_ADC_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_ADC_PAYLOAD_LEN) return RCP_EP_ADC_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_u16(payload);
        *out_timed     = false;
        *out_timestamp = 0u;
    }

    *out_transaction_num = transaction_num;
    return RCP_EP_ADC_OK;
}
