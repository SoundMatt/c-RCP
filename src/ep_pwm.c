#include "rcp/ep_pwm.h"

#include <string.h>

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/ep_gpio.c's
 * house convention of not sharing a byte-order util across modules) ────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void put_pwm_value(uint8_t *p, rcp_ep_pwm_value_t v)
{
    put_u16(p, v.period);
    put_u16(p + 2, v.active_duration);
}

static rcp_ep_pwm_value_t get_pwm_value(const uint8_t *p)
{
    rcp_ep_pwm_value_t v;

    v.period          = get_u16(p);
    v.active_duration = get_u16(p + 2);
    return v;
}

/* Saturating 16-bit add/subtract shared by every RCP_EP_PWM_OUT_WRITE_ADD/
 * _SUB field application below -- see the file header. */
static uint16_t saturating_add_u16(uint16_t current, uint16_t request)
{
    uint32_t sum = (uint32_t)current + (uint32_t)request;
    return (sum > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)sum;
}

static uint16_t saturating_sub_u16(uint16_t current, uint16_t request)
{
    return (request > current) ? (uint16_t)0u : (uint16_t)(current - request);
}

static uint16_t apply_write_field(uint16_t current, uint16_t request,
                                   rcp_ep_pwm_out_write_semantics_t evt)
{
    switch (evt) {
    case RCP_EP_PWM_OUT_WRITE_REPLACE: return request;
    case RCP_EP_PWM_OUT_WRITE_OR:      return (uint16_t)(current | request);
    case RCP_EP_PWM_OUT_WRITE_AND:     return (uint16_t)(current & request);
    case RCP_EP_PWM_OUT_WRITE_XOR:     return (uint16_t)(current ^ request);
    case RCP_EP_PWM_OUT_WRITE_ADD:     return saturating_add_u16(current, request);
    case RCP_EP_PWM_OUT_WRITE_SUB:     return saturating_sub_u16(current, request);
    case RCP_EP_PWM_OUT_WRITE_RESERVED6:
        return current; /* documented no-op; see the file header */
    case RCP_EP_PWM_OUT_WRITE_RECONFIG:
    default:
        /* Not a data-write semantic -- callers must route evt == RECONFIG
         * to rcp_ep_pwm_out_apply_reconfig() instead. Fail safe for a
         * caller that violates that contract. */
        return current;
    }
}

/* ── PWM_OUT: evt[2:0] write semantics ─────────────────────────────────────── */

//cfusa:req REQ-PWM-001
bool rcp_ep_pwm_out_write_semantics_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_PWM_OUT_WRITE_RECONFIG;
}

//cfusa:req REQ-PWM-002
//cfusa:req REQ-PWM-003
//cfusa:req REQ-PWM-004
//cfusa:req REQ-PWM-005
//cfusa:req REQ-PWM-006
//cfusa:req REQ-PWM-007
//cfusa:req REQ-PWM-008
//cfusa:req REQ-PWM-009
rcp_ep_pwm_value_t rcp_ep_pwm_out_apply_write(rcp_ep_pwm_value_t current,
                                               rcp_ep_pwm_value_t request,
                                               rcp_ep_pwm_out_write_semantics_t evt)
{
    rcp_ep_pwm_value_t result;

    result.period          = apply_write_field(current.period, request.period, evt);
    result.active_duration = apply_write_field(current.active_duration, request.active_duration, evt);
    return result;
}

//cfusa:req REQ-PWM-010
//cfusa:req REQ-PWM-011
void rcp_ep_pwm_out_apply_reconfig(bool *enabled, uint32_t reconfig_word)
{
    if ((reconfig_word & 0x1u) != 0) {
        *enabled = !*enabled;
    }
}

/* ── PWM_OUT: triggers ──────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-012
//cfusa:req REQ-PWM-013
//cfusa:req REQ-PWM-014
//cfusa:req REQ-PWM-015
bool rcp_ep_pwm_out_trigger_fires(rcp_ep_pwm_out_trigger_t trigger, rcp_ep_pwm_out_event_t event)
{
    switch (trigger) {
    case RCP_EP_PWM_OUT_TRIGGER_CYCLE_START: return event == RCP_EP_PWM_OUT_EVENT_CYCLE_START;
    case RCP_EP_PWM_OUT_TRIGGER_MID_PULSE:   return event == RCP_EP_PWM_OUT_EVENT_MID_PULSE;
    case RCP_EP_PWM_OUT_TRIGGER_DONE:        return event == RCP_EP_PWM_OUT_EVENT_DONE;
    case RCP_EP_PWM_OUT_TRIGGER_NONE:
    default:                                 return false;
    }
}

/* ── PWM_OUT: functional config ─────────────────────────────────────────────── */

//cfusa:req REQ-PWM-016
void rcp_ep_pwm_out_functional_cfg_init(rcp_ep_pwm_out_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* cfg->trigger is already RCP_EP_PWM_OUT_TRIGGER_NONE (0) and
     * cfg->enabled already false via the memset above. */
}

//cfusa:req REQ-PWM-017
//cfusa:req REQ-PWM-018
//cfusa:req REQ-PWM-019
bool rcp_ep_pwm_out_functional_cfg_writable(rcp_server_lifecycle_t state,
                                            rcp_server_writer_ctx_t writer)
{
    return rcp_server_field_writable(state, RCP_SERVER_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-PWM-020
//cfusa:req REQ-PWM-021
bool rcp_ep_pwm_out_set_trigger(rcp_ep_pwm_out_functional_cfg_t *cfg,
                                 rcp_ep_pwm_out_trigger_t trigger,
                                 rcp_server_lifecycle_t state, rcp_server_writer_ctx_t writer)
{
    if (!rcp_ep_pwm_out_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

//cfusa:req REQ-PWM-022
//cfusa:req REQ-PWM-023
bool rcp_ep_pwm_out_set_enabled(rcp_ep_pwm_out_functional_cfg_t *cfg, bool enabled,
                                 rcp_server_lifecycle_t state, rcp_server_writer_ctx_t writer)
{
    if (!rcp_ep_pwm_out_functional_cfg_writable(state, writer)) return false;

    cfg->enabled = enabled;
    return true;
}

/* ── PWM_OUT: error codes ──────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-024
const char *rcp_ep_pwm_out_strerror(rcp_ep_pwm_out_errc_t e)
{
    switch (e) {
    case RCP_EP_PWM_OUT_OK:                  return "rcp/ep_pwm: PWM_OUT success";
    case RCP_EP_PWM_OUT_ERR_SHORT_FRAME:     return "rcp/ep_pwm: PWM_OUT frame too short";
    case RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE:    return "rcp/ep_pwm: PWM_OUT unexpected ACF message type";
    case RCP_EP_PWM_OUT_ERR_WRONG_BUS:       return "rcp/ep_pwm: PWM_OUT wrong byte_bus_id";
    case RCP_EP_PWM_OUT_ERR_WRONG_OP:        return "rcp/ep_pwm: PWM_OUT wrong ACF op";
    case RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN: return "rcp/ep_pwm: PWM_OUT unexpected payload length";
    default:                                 return "rcp/ep_pwm: PWM_OUT unknown error";
    }
}

/* ── PWM_OUT: read request ─────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-025
rcp_bytes_t rcp_ep_pwm_out_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                                uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-PWM-025
//cfusa:req REQ-PWM-026
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_read_request(const uint8_t *b, size_t len,
                                                          rcp_byte_bus_id_t expected_bus_id,
                                                          uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_PWM_OUT_ERR_WRONG_OP;

    (void)payload;
    (void)payload_len; /* a read request carries no payload -- see ep_pwm.h */

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_PWM_OUT_OK;
}

/* ── PWM_OUT: write request ────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-027
rcp_bytes_t rcp_ep_pwm_out_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                                 rcp_ep_pwm_value_t value,
                                                 rcp_ep_pwm_out_write_semantics_t evt,
                                                 uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    uint8_t                      payload[RCP_EP_PWM_PAYLOAD_LEN];

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = (uint8_t)((unsigned)evt & 0x7u);
    hdr.transaction_num = transaction_num;

    put_pwm_value(payload, value);

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-PWM-027
//cfusa:req REQ-PWM-028
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_write_request(const uint8_t *b, size_t len,
                                                           rcp_byte_bus_id_t expected_bus_id,
                                                           rcp_ep_pwm_value_t *out_value,
                                                           uint8_t *out_evt,
                                                           uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_PWM_OUT_ERR_WRONG_OP;
    if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN;

    *out_value           = get_pwm_value(payload);
    *out_evt             = (uint8_t)(hdr.evt & 0x7u);
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_PWM_OUT_OK;
}

/* ── PWM_OUT: response ─────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-029
//cfusa:req REQ-PWM-030
rcp_bytes_t rcp_ep_pwm_out_encode_response(rcp_byte_bus_id_t byte_bus_id, rcp_ep_pwm_value_t value,
                                            uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    uint8_t payload[RCP_EP_PWM_PAYLOAD_LEN];

    put_pwm_value(payload, value);

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

//cfusa:req REQ-PWM-029
//cfusa:req REQ-PWM-030
//cfusa:req REQ-PWM-031
rcp_ep_pwm_out_errc_t rcp_ep_pwm_out_decode_response(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      rcp_ep_pwm_value_t *out_value,
                                                      bool *out_timed, uint64_t *out_timestamp,
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

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_OUT_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_OUT_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_OUT_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_OUT_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = false;
        *out_timestamp = 0u;
    }

    *out_transaction_num = transaction_num;
    return RCP_EP_PWM_OUT_OK;
}

/* ── PWM_IN: triggers ───────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-032
//cfusa:req REQ-PWM-033
//cfusa:req REQ-PWM-034
bool rcp_ep_pwm_in_trigger_fires(rcp_ep_pwm_in_trigger_t trigger, bool prev_level, bool new_level)
{
    switch (trigger) {
    case RCP_EP_PWM_IN_TRIGGER_RISING:  return !prev_level && new_level;
    case RCP_EP_PWM_IN_TRIGGER_FALLING: return prev_level && !new_level;
    case RCP_EP_PWM_IN_TRIGGER_NONE:
    default:                            return false;
    }
}

/* ── PWM_IN: functional config ─────────────────────────────────────────────── */

//cfusa:req REQ-PWM-035
void rcp_ep_pwm_in_functional_cfg_init(rcp_ep_pwm_in_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* cfg->trigger is already RCP_EP_PWM_IN_TRIGGER_NONE (0) via the
     * memset above. */
}

//cfusa:req REQ-PWM-036
//cfusa:req REQ-PWM-037
//cfusa:req REQ-PWM-038
bool rcp_ep_pwm_in_functional_cfg_writable(rcp_server_lifecycle_t state,
                                           rcp_server_writer_ctx_t writer)
{
    return rcp_server_field_writable(state, RCP_SERVER_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-PWM-039
//cfusa:req REQ-PWM-040
bool rcp_ep_pwm_in_set_trigger(rcp_ep_pwm_in_functional_cfg_t *cfg,
                                rcp_ep_pwm_in_trigger_t trigger, rcp_server_lifecycle_t state,
                                rcp_server_writer_ctx_t writer)
{
    if (!rcp_ep_pwm_in_functional_cfg_writable(state, writer)) return false;

    cfg->trigger = (uint8_t)trigger;
    return true;
}

/* ── PWM_IN: error codes ───────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-041
const char *rcp_ep_pwm_in_strerror(rcp_ep_pwm_in_errc_t e)
{
    switch (e) {
    case RCP_EP_PWM_IN_OK:                  return "rcp/ep_pwm: PWM_IN success";
    case RCP_EP_PWM_IN_ERR_SHORT_FRAME:     return "rcp/ep_pwm: PWM_IN frame too short";
    case RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE:    return "rcp/ep_pwm: PWM_IN unexpected ACF message type";
    case RCP_EP_PWM_IN_ERR_WRONG_BUS:       return "rcp/ep_pwm: PWM_IN wrong byte_bus_id";
    case RCP_EP_PWM_IN_ERR_WRONG_OP:        return "rcp/ep_pwm: PWM_IN wrong ACF op";
    case RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN: return "rcp/ep_pwm: PWM_IN unexpected payload length";
    default:                                return "rcp/ep_pwm: PWM_IN unknown error";
    }
}

/* ── PWM_IN: read request ──────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-042
rcp_bytes_t rcp_ep_pwm_in_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                               uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, NULL, 0);
}

//cfusa:req REQ-PWM-042
//cfusa:req REQ-PWM-043
rcp_ep_pwm_in_errc_t rcp_ep_pwm_in_decode_read_request(const uint8_t *b, size_t len,
                                                        rcp_byte_bus_id_t expected_bus_id,
                                                        uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_PWM_IN_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_PWM_IN_ERR_WRONG_OP;

    (void)payload;
    (void)payload_len;

    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_PWM_IN_OK;
}

/* ── PWM_IN: response ──────────────────────────────────────────────────────── */

//cfusa:req REQ-PWM-044
//cfusa:req REQ-PWM-045
//cfusa:req REQ-PWM-047
rcp_bytes_t rcp_ep_pwm_in_encode_response(rcp_byte_bus_id_t byte_bus_id, rcp_ep_pwm_value_t value,
                                           uint8_t transaction_num, bool timed, uint64_t timestamp)
{
    uint8_t payload[RCP_EP_PWM_PAYLOAD_LEN];

    put_pwm_value(payload, value);

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

//cfusa:req REQ-PWM-044
//cfusa:req REQ-PWM-045
//cfusa:req REQ-PWM-046
//cfusa:req REQ-PWM-047
rcp_ep_pwm_in_errc_t rcp_ep_pwm_in_decode_response(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    rcp_ep_pwm_value_t *out_value, bool *out_timed,
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

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_IN_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = rcp_acf_gbb_is_timed(&gbb_hdr);
        *out_timestamp = *out_timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_PWM_IN_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_PWM_IN_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;

        if (bus_id != expected_bus_id) return RCP_EP_PWM_IN_ERR_WRONG_BUS;
        if (payload_len != RCP_EP_PWM_PAYLOAD_LEN) return RCP_EP_PWM_IN_ERR_BAD_PAYLOAD_LEN;

        *out_value     = get_pwm_value(payload);
        *out_timed     = false;
        *out_timestamp = 0u;
    }

    *out_transaction_num = transaction_num;
    return RCP_EP_PWM_IN_OK;
}

/* ── Compound-wait's numeric ≥/≤ comparison modes against PWM_IN ────────────── */

//cfusa:req REQ-PWM-048
bool rcp_ep_pwm_in_compound_wait_mode_valid(uint8_t v)
{
    return v >= (uint8_t)RCP_EP_PWM_IN_CMP_PERIOD_GE && v <= (uint8_t)RCP_EP_PWM_IN_CMP_DUTY_LE;
}

//cfusa:req REQ-PWM-049
//cfusa:req REQ-PWM-050
//cfusa:req REQ-PWM-051
//cfusa:req REQ-PWM-052
//cfusa:req REQ-PWM-053
//cfusa:req REQ-PWM-054
bool rcp_ep_pwm_in_compound_wait_compare(rcp_ep_pwm_value_t captured,
                                          rcp_ep_pwm_in_compound_wait_mode_t mode,
                                          uint16_t threshold)
{
    switch (mode) {
    case RCP_EP_PWM_IN_CMP_PERIOD_GE:
        if (captured.period == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.period >= threshold;
    case RCP_EP_PWM_IN_CMP_PERIOD_LE:
        if (captured.period == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.period <= threshold;
    case RCP_EP_PWM_IN_CMP_DUTY_GE:
        if (captured.active_duration == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.active_duration >= threshold;
    case RCP_EP_PWM_IN_CMP_DUTY_LE:
        if (captured.active_duration == RCP_EP_PWM_IN_NO_SIGNAL) return false;
        return captured.active_duration <= threshold;
    default:
        return false;
    }
}
