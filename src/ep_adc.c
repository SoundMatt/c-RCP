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

//cfusa:req REQ-ADC-037
rcp_ep_adc_cadence_case_t rcp_ep_adc_cadence_case(uint16_t avg_intervals_per_request,
                                                   uint8_t combine_avg_values)
{
    if ((uint32_t)combine_avg_values > (uint32_t)avg_intervals_per_request) {
        return RCP_EP_ADC_CADENCE_ACCUMULATE;
    }
    if ((uint32_t)combine_avg_values < (uint32_t)avg_intervals_per_request) {
        return RCP_EP_ADC_CADENCE_FAN_OUT;
    }
    return RCP_EP_ADC_CADENCE_ONE_TO_ONE;
}

//cfusa:req REQ-ADC-037
bool rcp_ep_adc_cadence_response_ready(size_t pending_value_count, uint8_t combine_avg_values)
{
    return pending_value_count >= (size_t)combine_avg_values;
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
     * via the memset above; likewise ep_status/base_clk_divider/
     * sample_interval/resolution/trigger_min/trigger_max. */
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

/* ── Trigger outputs (Table 50), REQ-ADC-031 ─────────────────────────────── */

//cfusa:req REQ-ADC-031
void rcp_ep_adc_trigger_state_init(rcp_ep_adc_trigger_state_t *s)
{
    s->has_previous   = false;
    s->previous_value = 0;
}

//cfusa:req REQ-ADC-031
uint8_t rcp_ep_adc_trigger_evaluate(rcp_ep_adc_trigger_state_t *s, uint16_t value,
                                     uint16_t trigger_min, uint16_t trigger_max,
                                     bool measurement_finished)
{
    uint8_t fired = 0;

    if (s->has_previous) {
        if (s->previous_value >= trigger_min && value < trigger_min) {
            fired |= RCP_EP_ADC_TRIGGER_BELOW_MIN;
        }
        if (s->previous_value <= trigger_min && value > trigger_min) {
            fired |= RCP_EP_ADC_TRIGGER_ABOVE_MIN;
        }
        if (s->previous_value >= trigger_max && value < trigger_max) {
            fired |= RCP_EP_ADC_TRIGGER_BELOW_MAX;
        }
        if (s->previous_value <= trigger_max && value > trigger_max) {
            fired |= RCP_EP_ADC_TRIGGER_ABOVE_MAX;
        }
    }

    if (measurement_finished) fired |= RCP_EP_ADC_TRIGGER_MEASUREMENT_FINISHED;

    s->previous_value = value;
    s->has_previous    = true;

    return fired;
}

/* ── The EP_func register block (evt[2:0] == 111b) ─────────────────────────── */

/* The EP-common enable&clr (0x0002) and options (0x0003) octets, packed
 * from / unpacked into the flags regmap.h's shared functional-config
 * prefix already models -- same bit positions as ep_pwm.c's/ep_gpio.c's/
 * ep_spi.c's/ep_i2c.c's/ep_uart.c's/ep_lin.c's own copies, since Table
 * 32's common entries are shared across every endpoint type. Only the
 * bits regmap.h models are represented; see ep_pwm.c's identical note
 * for why the rest read back as 0. */
#define ADC_ENABLE_CLR_BIT_ENABLE ((uint8_t)(1u << 0))
#define ADC_ENABLE_CLR_BIT_CLEAR  ((uint8_t)(1u << 4))
#define ADC_OPTIONS_BIT_REQ_CRC   ((uint8_t)(1u << 0))
#define ADC_OPTIONS_BIT_RESP_TS   ((uint8_t)(1u << 3))
#define ADC_OPTIONS_BIT_SUPPRESS  ((uint8_t)(1u << 7))

//cfusa:req REQ-ADC-038
//cfusa:req REQ-ADC-040
void rcp_ep_adc_render_registers(const rcp_ep_adc_functional_cfg_t *cfg,
                                  uint8_t out[RCP_EP_ADC_EP_FUNC_LEN])
{
    uint8_t enable_clr = 0u;
    uint8_t options    = 0u;

    if (cfg->common.ep_enable) enable_clr |= ADC_ENABLE_CLR_BIT_ENABLE;
    if (cfg->common.ep_clear_req_storage) enable_clr |= ADC_ENABLE_CLR_BIT_CLEAR;
    if (cfg->common.ep_req_crc_enable) options |= ADC_OPTIONS_BIT_REQ_CRC;
    if (cfg->common.ep_response_ts_enable) options |= ADC_OPTIONS_BIT_RESP_TS;
    if (cfg->common.ep_suppress_response) options |= ADC_OPTIONS_BIT_SUPPRESS;

    out[RCP_EP_ADC_REG_EP_LEN]        = (uint8_t)RCP_EP_ADC_EP_FUNC_LEN;
    out[RCP_EP_ADC_REG_RESERVED_01]   = 0u;
    out[RCP_EP_ADC_REG_EP_ENABLE_CLR] = enable_clr;
    out[RCP_EP_ADC_REG_EP_OPTIONS]    = options;
    put_u16(&out[RCP_EP_ADC_REG_BASE_CLK], 0u); /* no real clock source
                                                    modelled -- see the file
                                                    header */
    put_u16(&out[RCP_EP_ADC_REG_EP_STATUS], cfg->ep_status);
    out[RCP_EP_ADC_REG_BASE_CLK_DIVIDER] = cfg->base_clk_divider;
    out[RCP_EP_ADC_REG_SAMPLE_INTERVAL]  = cfg->sample_interval;
    /* Truncated to the low octet -- see the file header's note on these
     * two fields' own wider uint16_t width. */
    out[RCP_EP_ADC_REG_AVG_INTERVALS]   = (uint8_t)(cfg->adc_avg_intervals_per_request & 0xFFu);
    out[RCP_EP_ADC_REG_SAMPLES_PER_AVG] = (uint8_t)(cfg->adc_samples_per_avg_interval & 0xFFu);
    out[RCP_EP_ADC_REG_COMBINE_AVG]     = cfg->adc_combine_avg_values;
    out[RCP_EP_ADC_REG_RESOLUTION]      = cfg->resolution;
    put_u16(&out[RCP_EP_ADC_REG_TRIGGER_MIN], cfg->trigger_min);
    put_u16(&out[RCP_EP_ADC_REG_TRIGGER_MAX], cfg->trigger_max);
}

/* The inverse of render: adopts every R/W register from an already
 * patched block image. The read-only offsets (EP_LEN, the reserved
 * octet, base_clk) are deliberately not read back -- apply_reconfig()
 * re-renders them from cfg before patching, so a write covering them is a
 * no-op. */
static void parse_registers(rcp_ep_adc_functional_cfg_t *cfg,
                             const uint8_t in[RCP_EP_ADC_EP_FUNC_LEN])
{
    uint8_t enable_clr = in[RCP_EP_ADC_REG_EP_ENABLE_CLR];
    uint8_t options    = in[RCP_EP_ADC_REG_EP_OPTIONS];

    cfg->common.ep_enable             = (enable_clr & ADC_ENABLE_CLR_BIT_ENABLE) != 0u;
    cfg->common.ep_clear_req_storage  = (enable_clr & ADC_ENABLE_CLR_BIT_CLEAR) != 0u;
    cfg->common.ep_req_crc_enable     = (options & ADC_OPTIONS_BIT_REQ_CRC) != 0u;
    cfg->common.ep_response_ts_enable = (options & ADC_OPTIONS_BIT_RESP_TS) != 0u;
    cfg->common.ep_suppress_response  = (options & ADC_OPTIONS_BIT_SUPPRESS) != 0u;

    cfg->ep_status        = get_u16(&in[RCP_EP_ADC_REG_EP_STATUS]);
    cfg->base_clk_divider = in[RCP_EP_ADC_REG_BASE_CLK_DIVIDER];
    cfg->sample_interval  = in[RCP_EP_ADC_REG_SAMPLE_INTERVAL];
    cfg->adc_avg_intervals_per_request   = in[RCP_EP_ADC_REG_AVG_INTERVALS];
    cfg->adc_samples_per_avg_interval    = in[RCP_EP_ADC_REG_SAMPLES_PER_AVG];
    cfg->adc_combine_avg_values          = in[RCP_EP_ADC_REG_COMBINE_AVG];
    cfg->resolution        = in[RCP_EP_ADC_REG_RESOLUTION];
    cfg->trigger_min       = get_u16(&in[RCP_EP_ADC_REG_TRIGGER_MIN]);
    cfg->trigger_max       = get_u16(&in[RCP_EP_ADC_REG_TRIGGER_MAX]);
}

/* True iff the octet at relative offset addr belongs to a read-only
 * register of the block -- EP_LEN, the reserved octet, and both octets of
 * base_clk. */
static bool reg_offset_read_only(uint16_t addr)
{
    return addr == RCP_EP_ADC_REG_EP_LEN ||
           addr == RCP_EP_ADC_REG_RESERVED_01 ||
           addr == RCP_EP_ADC_REG_BASE_CLK ||
           addr == (uint16_t)(RCP_EP_ADC_REG_BASE_CLK + 1u);
}

//cfusa:req REQ-ADC-039
const char *rcp_ep_adc_reconfig_strerror(rcp_ep_adc_reconfig_errc_t e)
{
    switch (e) {
    case RCP_EP_ADC_RECONFIG_OK:
        return "rcp/ep_adc: ADC configuration write applied";
    case RCP_EP_ADC_RECONFIG_ERR_SHORT:
        return "rcp/ep_adc: ADC configuration write has no address and data";
    case RCP_EP_ADC_RECONFIG_ERR_OUT_OF_RANGE:
        return "rcp/ep_adc: ADC configuration write extends past the EP_func block";
    default:
        return "rcp/ep_adc: ADC unknown configuration-write error";
    }
}

//cfusa:req REQ-ADC-038
//cfusa:req REQ-ADC-039
//cfusa:req REQ-ADC-040
rcp_ep_adc_reconfig_errc_t rcp_ep_adc_apply_reconfig(rcp_ep_adc_functional_cfg_t *cfg,
                                                      const uint8_t *payload, size_t payload_len)
{
    uint8_t  block[RCP_EP_ADC_EP_FUNC_LEN];
    uint16_t start_address;
    size_t   data_len;
    size_t   i;

    if (payload_len <= RCP_EP_ADC_RECONFIG_ADDR_LEN) {
        return RCP_EP_ADC_RECONFIG_ERR_SHORT;
    }

    start_address = get_u16(payload);
    data_len      = payload_len - RCP_EP_ADC_RECONFIG_ADDR_LEN;

    /* "Any payload whose length plus the start address exceeds EP_LEN is
     * to be ignored" -- the whole write, not just its overhanging tail
     * (§12.7.1). */
    if ((size_t)start_address + data_len > (size_t)RCP_EP_ADC_EP_FUNC_LEN) {
        return RCP_EP_ADC_RECONFIG_ERR_OUT_OF_RANGE;
    }

    /* Patch the block's current image at octet granularity, then adopt it
     * wholesale -- so a write covering only part of a multi-octet
     * register updates exactly the octets it addresses and leaves that
     * register's other octets alone. */
    rcp_ep_adc_render_registers(cfg, block);
    for (i = 0; i < data_len; i++) {
        uint16_t addr = (uint16_t)(start_address + i);

        if (reg_offset_read_only(addr)) continue; /* write ignored */
        block[addr] = payload[RCP_EP_ADC_RECONFIG_ADDR_LEN + i];
    }
    parse_registers(cfg, block);

    return RCP_EP_ADC_RECONFIG_OK;
}

//cfusa:req REQ-ADC-038
rcp_bytes_t rcp_ep_adc_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint16_t start_address, const uint8_t *data,
                                                size_t data_len, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr   = {0};
    rcp_bytes_t                 empty = {0};
    uint8_t                     *payload;
    size_t                       payload_len;
    rcp_bytes_t                  frame;

    if (data_len == 0 || data == NULL) return empty;

    payload_len = RCP_EP_ADC_RECONFIG_ADDR_LEN + data_len;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return empty;

    payload = (uint8_t *)malloc(payload_len);
    if (!payload) return empty;

    put_u16(payload, start_address);
    memcpy(payload + RCP_EP_ADC_RECONFIG_ADDR_LEN, data, data_len);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE; /* §12.7.1 Figure 18: "the data
                                                of the byte_msg_payload from
                                                a write request is written
                                                into" the EP_func block --
                                                matches PWM_OUT's/GPIO's/
                                                SPI's/I2C's/UART's/LIN's
                                                own encode_reconfig_request(). */
    hdr.evt             = 0x7u; /* evt[2:0] = 111b */
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    free(payload);
    return frame;
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
    case RCP_EP_ADC_ERR_BAD_EVT:         return "rcp/ep_adc: evt[2:0] is not 0b000";
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
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_ADC_ERR_BAD_EVT;

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
