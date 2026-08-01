/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_gpio.h"

#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/discovery.c's
 * house convention of not sharing a byte-order util across modules) ────── */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* ── Pin addressing ────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-002
bool rcp_ep_gpio_pin_index_valid(uint8_t pin_index)
{
    return pin_index < RCP_EP_GPIO_MAX_PINS;
}

//cfusa:req REQ-GPIO-003
uint32_t rcp_ep_gpio_pin_mask(uint8_t pin_index)
{
    if (!rcp_ep_gpio_pin_index_valid(pin_index)) return 0;
    return (uint32_t)1u << pin_index;
}

//cfusa:req REQ-GPIO-004
bool rcp_ep_gpio_pin_get(uint32_t bitmask, uint8_t pin_index)
{
    return (bitmask & rcp_ep_gpio_pin_mask(pin_index)) != 0;
}

/* ── evt[2:0]: the eight write-semantics variants ──────────────────────────── */

//cfusa:req REQ-GPIO-005
bool rcp_ep_gpio_write_semantics_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_GPIO_WRITE_RECONFIG;
}

//cfusa:req REQ-GPIO-006
//cfusa:req REQ-GPIO-007
//cfusa:req REQ-GPIO-008
//cfusa:req REQ-GPIO-009
//cfusa:req REQ-GPIO-010
//cfusa:req REQ-GPIO-011
//cfusa:req REQ-GPIO-012
uint32_t rcp_ep_gpio_apply_write(uint32_t current, uint32_t request,
                                  rcp_ep_gpio_write_semantics_t evt)
{
    switch (evt) {
    case RCP_EP_GPIO_WRITE_REPLACE: return request;
    case RCP_EP_GPIO_WRITE_OR:      return current | request;
    case RCP_EP_GPIO_WRITE_AND:     return current & request;
    case RCP_EP_GPIO_WRITE_XOR:     return current ^ request;
    case RCP_EP_GPIO_WRITE_ADD:
        /* Saturate at 0xFFFFFFFF rather than wrapping -- see the file
         * header. (current + request) itself cannot overflow a uint64_t,
         * so the sum is computed widened and then clamped. */
        {
            uint64_t sum = (uint64_t)current + (uint64_t)request;
            return (sum > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)sum;
        }
    case RCP_EP_GPIO_WRITE_SUB:
        /* The subtraction's operand order is normatively "request minus
         * current": the requested payload value is the minuend and the
         * current interface status the subtrahend (extraction §4.5 Group
         * C, the evt[2:0]=110b row -- one row covering GPIO and PWM_OUT
         * jointly, so the identical rule ep_pwm.c applies). The row's
         * parenthetical remark about decreasing a PWM duty cycle is an
         * illustrative note, not a second definition of the operand
         * order. Saturates at 0x00000000 rather than wrapping. */
        return (current > request) ? 0u : (request - current);
    case RCP_EP_GPIO_WRITE_RESERVED4:
        return current; /* documented no-op; see the file header */
    case RCP_EP_GPIO_WRITE_RECONFIG:
    default:
        /* Not a data-write semantic -- callers must route evt == RECONFIG
         * to rcp_ep_gpio_apply_reconfig() instead (see the file header).
         * Fail safe for a caller that violates that contract: leave the
         * register unchanged rather than perform an unintended write. */
        return current;
    }
}

//cfusa:req REQ-GPIO-013
void rcp_ep_gpio_apply_reconfig(uint8_t pins[RCP_EP_GPIO_MAX_PINS], uint32_t reconfig_mask)
{
    uint8_t i;

    for (i = 0; i < RCP_EP_GPIO_MAX_PINS; i++) {
        if ((reconfig_mask & rcp_ep_gpio_pin_mask(i)) == 0) continue;

        if ((pins[i] & RCP_REGMAP_PIN_PROP_OUTPUT) != 0) {
            pins[i] = (uint8_t)((pins[i] & ~(unsigned)RCP_REGMAP_PIN_PROP_OUTPUT) |
                                 RCP_REGMAP_PIN_PROP_INPUT);
        } else {
            pins[i] = (uint8_t)((pins[i] & ~(unsigned)RCP_REGMAP_PIN_PROP_INPUT) |
                                 RCP_REGMAP_PIN_PROP_OUTPUT);
        }
    }
}

/* ── Per-pin trigger signals ────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-014
//cfusa:req REQ-GPIO-015
//cfusa:req REQ-GPIO-016
//cfusa:req REQ-GPIO-017
bool rcp_ep_gpio_trigger_fires(rcp_ep_gpio_trigger_t trigger, bool prev_level, bool new_level)
{
    switch (trigger) {
    case RCP_EP_GPIO_TRIGGER_ANY_CHANGE: return prev_level != new_level;
    case RCP_EP_GPIO_TRIGGER_RISING:     return !prev_level && new_level;
    case RCP_EP_GPIO_TRIGGER_FALLING:    return prev_level && !new_level;
    case RCP_EP_GPIO_TRIGGER_NONE:
    default:                             return false;
    }
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-018
void rcp_ep_gpio_functional_cfg_init(rcp_ep_gpio_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* Every pins[i].trigger is already RCP_EP_GPIO_TRIGGER_NONE (0) and
     * pin_property already 0 via the memset above. */
}

//cfusa:req REQ-GPIO-019
//cfusa:req REQ-GPIO-020
//cfusa:req REQ-GPIO-021
bool rcp_ep_gpio_functional_cfg_writable(rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-GPIO-022
//cfusa:req REQ-GPIO-023
bool rcp_ep_gpio_set_pin_property(rcp_ep_gpio_functional_cfg_t *cfg, uint8_t pin_index,
                                   uint8_t pin_property, rcp_lifecycle_state_t state,
                                   rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_gpio_pin_index_valid(pin_index)) return false;
    if (!rcp_ep_gpio_functional_cfg_writable(state, writer)) return false;

    cfg->pins[pin_index].pin_property = pin_property;
    return true;
}

//cfusa:req REQ-GPIO-024
//cfusa:req REQ-GPIO-025
bool rcp_ep_gpio_set_pin_trigger(rcp_ep_gpio_functional_cfg_t *cfg, uint8_t pin_index,
                                  rcp_ep_gpio_trigger_t trigger, rcp_lifecycle_state_t state,
                                  rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_gpio_pin_index_valid(pin_index)) return false;
    if (!rcp_ep_gpio_functional_cfg_writable(state, writer)) return false;

    cfg->pins[pin_index].trigger = (uint8_t)trigger;
    return true;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-001
const char *rcp_ep_gpio_strerror(rcp_ep_gpio_errc_t e)
{
    switch (e) {
    case RCP_EP_GPIO_OK:                  return "rcp/ep_gpio: success";
    case RCP_EP_GPIO_ERR_SHORT_FRAME:     return "rcp/ep_gpio: frame too short";
    case RCP_EP_GPIO_ERR_BAD_MSG_TYPE:    return "rcp/ep_gpio: unexpected ACF message type";
    case RCP_EP_GPIO_ERR_WRONG_BUS:       return "rcp/ep_gpio: wrong byte_bus_id";
    case RCP_EP_GPIO_ERR_WRONG_OP:        return "rcp/ep_gpio: wrong ACF op";
    case RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN: return "rcp/ep_gpio: unexpected payload length";
    default:                              return "rcp/ep_gpio: unknown error";
    }
}

/* ── Read request ──────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-026
rcp_bytes_t rcp_ep_gpio_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id      = byte_bus_id;
    hdr.op               = RCP_ACF_OP_READ;
    hdr.transaction_num  = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-GPIO-026
//cfusa:req REQ-GPIO-027
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_GPIO_ERR_WRONG_OP;

    (void)payload;
    (void)payload_len; /* a read request carries no payload -- see ep_gpio.h */

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_GPIO_OK;
}

/* ── Write request ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-028
rcp_bytes_t rcp_ep_gpio_encode_write_request(rcp_byte_bus_id_t byte_bus_id, uint32_t bitmask,
                                              rcp_ep_gpio_write_semantics_t evt,
                                              uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                      payload[RCP_EP_GPIO_PAYLOAD_LEN];

    hdr.byte_bus_id      = byte_bus_id;
    hdr.op               = RCP_ACF_OP_WRITE;
    hdr.evt              = (uint8_t)((unsigned)evt & 0x7u);
    hdr.transaction_num  = transaction_num;

    put_u32(payload, bitmask);

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-GPIO-028
//cfusa:req REQ-GPIO-029
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_write_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     uint32_t *out_bitmask, uint8_t *out_evt,
                                                     uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_GPIO_ERR_WRONG_OP;
    if (payload_len != RCP_EP_GPIO_PAYLOAD_LEN) return RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN;

    *out_bitmask         = get_u32(payload);
    *out_evt             = (uint8_t)(hdr.evt & 0x7u);
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_GPIO_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-GPIO-030
//cfusa:req REQ-GPIO-031
rcp_bytes_t rcp_ep_gpio_encode_response(rcp_byte_bus_id_t byte_bus_id, uint32_t bitmask,
                                         uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN];

    put_u32(payload, bitmask);

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, payload, sizeof(payload));
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
    }
}

//cfusa:req REQ-GPIO-030
//cfusa:req REQ-GPIO-031
//cfusa:req REQ-GPIO-032
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_response(const uint8_t *b, size_t len,
                                                rcp_byte_bus_id_t expected_bus_id,
                                                uint32_t *out_bitmask, bool *out_timed,
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

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_GPIO_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_GPIO_PAYLOAD_LEN) return RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN;

        *out_bitmask = get_u32(payload);
        *out_timed   = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_GPIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_GPIO_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_GPIO_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_GPIO_PAYLOAD_LEN) return RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN;

        *out_bitmask   = get_u32(payload);
        *out_timed     = false;
        *out_timestamp = 0u;
    }

    *out_transaction_num = transaction_num;
    return RCP_EP_GPIO_OK;
}
