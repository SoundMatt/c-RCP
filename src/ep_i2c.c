/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_i2c.h"

#include <string.h>

/* ── i2c_mode: bus-speed presets ────────────────────────────────────────────── */

//cfusa:req REQ-I2C-001
bool rcp_ep_i2c_mode_valid(uint8_t v)
{
    return v <= (uint8_t)RCP_EP_I2C_MODE_HIGH_SPEED;
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-I2C-002
void rcp_ep_i2c_functional_cfg_init(rcp_ep_i2c_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
    /* i2c_mode is already RCP_EP_I2C_MODE_STANDARD (0) via the memset
     * above. */
}

//cfusa:req REQ-I2C-003
//cfusa:req REQ-I2C-004
//cfusa:req REQ-I2C-005
bool rcp_ep_i2c_functional_cfg_writable(rcp_lifecycle_state_t state,
                                        rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

//cfusa:req REQ-I2C-006
//cfusa:req REQ-I2C-007
//cfusa:req REQ-I2C-008
bool rcp_ep_i2c_set_mode(rcp_ep_i2c_functional_cfg_t *cfg, rcp_ep_i2c_mode_t mode,
                          rcp_lifecycle_state_t state, rcp_lifecycle_writer_ctx_t writer)
{
    if (!rcp_ep_i2c_mode_valid((uint8_t)mode)) return false;
    if (!rcp_ep_i2c_functional_cfg_writable(state, writer)) return false;

    cfg->i2c_mode = (uint8_t)mode;
    return true;
}

/* ── Transfer direction ────────────────────────────────────────────────────── */

//cfusa:req REQ-I2C-017
bool rcp_ep_i2c_dir_valid(rcp_ep_i2c_dir_t d)
{
    return d == RCP_EP_I2C_DIR_WRITE || d == RCP_EP_I2C_DIR_READ;
}

/* The RCP-level op sense each direction carries. The read sense (wire
 * op=0) is the one that asks for a payload-bearing response; the write
 * sense (wire op=1) asks only for a payload-less success confirmation.
 * See ep_i2c.h's file header for why an I2C transfer -- unlike a LIN
 * command or an SPI transfer -- is genuinely either-directional and so
 * cannot carry a constant op. */
static uint8_t dir_to_op(rcp_ep_i2c_dir_t d)
{
    return (uint8_t)(d == RCP_EP_I2C_DIR_READ ? RCP_ACF_OP_READ : RCP_ACF_OP_WRITE);
}

static rcp_ep_i2c_dir_t op_to_dir(uint8_t op)
{
    /* A decoded header's op is only ever RCP_ACF_OP_READ or
     * RCP_ACF_OP_WRITE (acf.h) -- there is no third wire state. */
    return op == (uint8_t)RCP_ACF_OP_READ ? RCP_EP_I2C_DIR_READ : RCP_EP_I2C_DIR_WRITE;
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-I2C-009
const char *rcp_ep_i2c_strerror(rcp_ep_i2c_errc_t e)
{
    switch (e) {
    case RCP_EP_I2C_OK:               return "rcp/ep_i2c: success";
    case RCP_EP_I2C_ERR_SHORT_FRAME:  return "rcp/ep_i2c: frame too short";
    case RCP_EP_I2C_ERR_BAD_MSG_TYPE: return "rcp/ep_i2c: unexpected ACF message type";
    case RCP_EP_I2C_ERR_WRONG_BUS:    return "rcp/ep_i2c: wrong byte_bus_id";
    case RCP_EP_I2C_ERR_WRONG_OP:     return "rcp/ep_i2c: wrong ACF op";
    case RCP_EP_I2C_ERR_BAD_EVT:      return "rcp/ep_i2c: evt[2:0] is not 0b000";
    default:                          return "rcp/ep_i2c: unknown error";
    }
}

/* ── Transfer request ──────────────────────────────────────────────────────── */

//cfusa:req REQ-I2C-010
//cfusa:req REQ-I2C-018
rcp_bytes_t rcp_ep_i2c_encode_transfer_request(rcp_byte_bus_id_t byte_bus_id,
                                                rcp_ep_i2c_dir_t direction,
                                                const uint8_t *tx_data, size_t tx_len,
                                                uint16_t read_size,
                                                uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr  = {0};
    rcp_bytes_t                 fail = {0};

    if (!rcp_ep_i2c_dir_valid(direction)) return fail;
    if (read_size > RCP_EP_I2C_MAX_READ_SIZE) return fail;
    /* In the write sense that header slot is a segment_num, not a
     * read_size, so a caller asking for octets back on a write request is
     * asking for something the wire cannot express -- see ep_i2c.h. */
    if (direction == RCP_EP_I2C_DIR_WRITE && read_size != 0u) return fail;

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = dir_to_op(direction);
    hdr.evt             = 0; /* no channel selector -- see the file header */
    hdr.transaction_num = transaction_num;
    hdr.read_size_or_segment_num =
        (direction == RCP_EP_I2C_DIR_READ) ? read_size : (uint16_t)0u;

    return rcp_acf_encode_abb(&hdr, tx_data, tx_len);
}

//cfusa:req REQ-I2C-011
//cfusa:req REQ-I2C-012
rcp_ep_i2c_errc_t rcp_ep_i2c_decode_transfer_request(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      rcp_ep_i2c_dir_t *out_direction,
                                                      const uint8_t **out_tx_data,
                                                      size_t *out_tx_len,
                                                      uint16_t *out_read_size,
                                                      uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    rcp_ep_i2c_dir_t             direction;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_I2C_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_I2C_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_I2C_ERR_WRONG_BUS;
    if (!rcp_acf_evt_row2_is_plain(hdr.evt)) return RCP_EP_I2C_ERR_BAD_EVT;

    /* Both op senses are valid on an I2C transfer request -- see the file
     * header. The direction is reported, not rejected. */
    direction = op_to_dir(hdr.op);

    /* payload (address byte(s) plus data) is round-tripped verbatim, byte
     * for byte, with no protocol-level parsing of any kind -- see the file
     * header. */
    *out_direction       = direction;
    *out_tx_data         = payload;
    *out_tx_len          = payload_len;
    /* Only meaningful in the read sense; in the write sense that slot is
     * a segment_num this module does not interpret. */
    *out_read_size       = (direction == RCP_EP_I2C_DIR_READ) ? hdr.read_size_or_segment_num
                                                              : (uint16_t)0u;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_I2C_OK;
}

/* ── Response ───────────────────────────────────────────────────────────────── */

//cfusa:req REQ-I2C-013
rcp_bytes_t rcp_ep_i2c_encode_response(rcp_byte_bus_id_t byte_bus_id,
                                        rcp_ep_i2c_dir_t direction, const uint8_t *rx_data,
                                        size_t rx_len, uint8_t transaction_num, bool timed,
                                        uint64_t timestamp)
{
    rcp_bytes_t fail = {0};

    if (!rcp_ep_i2c_dir_valid(direction)) return fail;
    /* A write response confirms execution and carries no byte_msg_payload;
     * only a read response has one -- see ep_i2c.h's file header. */
    if (direction == RCP_EP_I2C_DIR_WRITE && rx_len != 0u) return fail;

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = dir_to_op(direction);
        hdr.info.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.info.evt             = 0;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        return rcp_acf_encode_gbb(&hdr, rx_data, rx_len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = dir_to_op(direction);
        hdr.rsp             = 1; /* TC18.txt:1885 -- rsp=1b identifies a response */
        hdr.evt             = 0;
        hdr.transaction_num = transaction_num;

        return rcp_acf_encode_abb(&hdr, rx_data, rx_len);
    }
}

//cfusa:req REQ-I2C-014
//cfusa:req REQ-I2C-015
//cfusa:req REQ-I2C-016
rcp_ep_i2c_errc_t rcp_ep_i2c_decode_response(const uint8_t *b, size_t len,
                                              rcp_byte_bus_id_t expected_bus_id,
                                              rcp_ep_i2c_dir_t *out_direction,
                                              const uint8_t **out_rx_data, size_t *out_rx_len,
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
    bool                         timed;
    uint64_t                     timestamp;
    rcp_ep_i2c_dir_t             direction;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_I2C_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_I2C_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_I2C_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        direction       = op_to_dir(gbb_hdr.info.op);
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_I2C_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_I2C_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        direction       = op_to_dir(abb_hdr.op);
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_I2C_ERR_WRONG_BUS;

    *out_direction       = direction;
    *out_rx_data         = payload;
    *out_rx_len          = payload_len;
    *out_timed           = timed;
    *out_timestamp       = timestamp;
    *out_transaction_num = transaction_num;
    return RCP_EP_I2C_OK;
}
