/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/ep_mdio.h"

#include <stdlib.h>
#include <string.h>

/* ── Wire-layout constants (this module's own choice; see the file header) ── */

#define ADDR_PREFIX_LEN         ((size_t)5u)  /* clause(1) + prtad(1) + devad(1) + regad(2 BE) */
#define READ_REQUEST_PAYLOAD_LEN ((size_t)(ADDR_PREFIX_LEN + 2u)) /* + word_count(2 BE) */

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/avtp.c's/
 * ep_can.c's house convention of not sharing a byte-order util across
 * modules) ──────────────────────────────────────────────────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* ── Addressing: Clause-22 MMD / Clause-45 MMS ───────────────────────────── */

//cfusa:req REQ-MDIO-001
bool rcp_ep_mdio_addr_valid(rcp_ep_mdio_addr_t addr)
{
    if (addr.prtad > RCP_EP_MDIO_PRTAD_MAX) return false;

    switch (addr.clause) {
    case RCP_EP_MDIO_CLAUSE_22:
        return addr.devad == 0u && addr.regad <= RCP_EP_MDIO_CLAUSE22_REGAD_MAX;
    case RCP_EP_MDIO_CLAUSE_45: return addr.devad <= RCP_EP_MDIO_DEVAD_MAX;
    default:                    return false;
    }
}

//cfusa:req REQ-MDIO-002
uint16_t rcp_ep_mdio_burst_next_regad(rcp_ep_mdio_clause_t clause, uint16_t regad)
{
    switch (clause) {
    case RCP_EP_MDIO_CLAUSE_22:
        return (uint16_t)((regad + 1u) & RCP_EP_MDIO_CLAUSE22_REGAD_MAX);
    case RCP_EP_MDIO_CLAUSE_45: return (uint16_t)(regad + 1u); /* wraps at 16 bits naturally */
    default:                    return regad;
    }
}

/* ── Register-word packing ─────────────────────────────────────────────────── */

//cfusa:req REQ-MDIO-003
void rcp_ep_mdio_word_encode(uint16_t word, uint8_t out[2])
{
    put_u16(out, word);
}

//cfusa:req REQ-MDIO-004
uint16_t rcp_ep_mdio_word_decode(const uint8_t in[2])
{
    return get_u16(in);
}

//cfusa:req REQ-MDIO-005
size_t rcp_ep_mdio_pack_len(size_t word_count)
{
    return word_count * 2u;
}

//cfusa:req REQ-MDIO-006
rcp_bytes_t rcp_ep_mdio_pack_words(const uint16_t *words, size_t word_count)
{
    rcp_bytes_t out = {0};
    uint8_t     *b;
    size_t       i;

    if (word_count == 0u) return out;

    b = (uint8_t *)malloc(rcp_ep_mdio_pack_len(word_count));
    if (!b) return out;

    for (i = 0; i < word_count; i++) rcp_ep_mdio_word_encode(words[i], &b[2u * i]);

    out.data = b;
    out.len  = rcp_ep_mdio_pack_len(word_count);
    return out;
}

//cfusa:req REQ-MDIO-007
bool rcp_ep_mdio_word_count_of(size_t byte_len, size_t *out_word_count)
{
    if ((byte_len & (size_t)1u) != 0u) return false;

    *out_word_count = byte_len / 2u;
    return true;
}

//cfusa:req REQ-MDIO-008
uint16_t rcp_ep_mdio_unpack_word_at(const uint8_t *data, size_t word_index)
{
    return rcp_ep_mdio_word_decode(&data[2u * word_index]);
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:req REQ-MDIO-009
void rcp_ep_mdio_functional_cfg_init(rcp_ep_mdio_functional_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg->common);
}

//cfusa:req REQ-MDIO-010
bool rcp_ep_mdio_functional_cfg_writable(rcp_lifecycle_state_t state,
                                          rcp_lifecycle_writer_ctx_t writer)
{
    return rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, writer);
}

/* ── Error codes ───────────────────────────────────────────────────────────── */

//cfusa:req REQ-MDIO-011
const char *rcp_ep_mdio_strerror(rcp_ep_mdio_errc_t e)
{
    switch (e) {
    case RCP_EP_MDIO_OK:                 return "rcp/ep_mdio: success";
    case RCP_EP_MDIO_ERR_SHORT_FRAME:    return "rcp/ep_mdio: frame too short";
    case RCP_EP_MDIO_ERR_BAD_MSG_TYPE:   return "rcp/ep_mdio: unexpected ACF message type";
    case RCP_EP_MDIO_ERR_WRONG_BUS:      return "rcp/ep_mdio: wrong byte_bus_id";
    case RCP_EP_MDIO_ERR_WRONG_OP:       return "rcp/ep_mdio: wrong ACF op";
    case RCP_EP_MDIO_ERR_BAD_ADDR:       return "rcp/ep_mdio: invalid MDIO address";
    case RCP_EP_MDIO_ERR_BAD_WORD_COUNT: return "rcp/ep_mdio: invalid register-word count";
    case RCP_EP_MDIO_ERR_ALLOC:          return "rcp/ep_mdio: allocation failure";
    default:                             return "rcp/ep_mdio: unknown error";
    }
}

/* ── Address-prefix helpers (internal) ───────────────────────────────────── */

static void put_addr_prefix(uint8_t *p, rcp_ep_mdio_addr_t addr)
{
    p[0] = (uint8_t)addr.clause;
    p[1] = addr.prtad;
    p[2] = addr.devad;
    put_u16(&p[3], addr.regad);
}

static rcp_ep_mdio_addr_t get_addr_prefix(const uint8_t *p)
{
    rcp_ep_mdio_addr_t addr;

    addr.clause = (rcp_ep_mdio_clause_t)p[0];
    addr.prtad  = p[1];
    addr.devad  = p[2];
    addr.regad  = get_u16(&p[3]);
    return addr;
}

/* ── Read request/response ─────────────────────────────────────────────────── */

//cfusa:req REQ-MDIO-012
rcp_bytes_t rcp_ep_mdio_encode_read_request(rcp_byte_bus_id_t byte_bus_id,
                                             rcp_ep_mdio_addr_t addr, size_t word_count,
                                             uint8_t transaction_num)
{
    rcp_bytes_t                 frame = {0};
    rcp_acf_byte_message_info_t hdr   = {0};
    uint8_t                     payload[READ_REQUEST_PAYLOAD_LEN];

    if (!rcp_ep_mdio_addr_valid(addr)) return frame;
    if (word_count == 0u || word_count > RCP_EP_MDIO_MAX_BURST_WORDS) return frame;

    put_addr_prefix(payload, addr);
    put_u16(&payload[ADDR_PREFIX_LEN], (uint16_t)word_count);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_READ;
    hdr.evt             = 0;
    hdr.transaction_num = transaction_num;

    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

//cfusa:req REQ-MDIO-013
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    rcp_ep_mdio_addr_t *out_addr,
                                                    size_t *out_word_count,
                                                    uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    rcp_ep_mdio_addr_t           addr;
    uint16_t                     word_count;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_MDIO_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_MDIO_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_MDIO_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_READ) return RCP_EP_MDIO_ERR_WRONG_OP;
    if (payload_len < READ_REQUEST_PAYLOAD_LEN) return RCP_EP_MDIO_ERR_SHORT_FRAME;

    addr = get_addr_prefix(payload);
    if (!rcp_ep_mdio_addr_valid(addr)) return RCP_EP_MDIO_ERR_BAD_ADDR;

    word_count = get_u16(&payload[ADDR_PREFIX_LEN]);
    if (word_count == 0u || (size_t)word_count > RCP_EP_MDIO_MAX_BURST_WORDS)
        return RCP_EP_MDIO_ERR_BAD_WORD_COUNT;

    *out_addr           = addr;
    *out_word_count      = (size_t)word_count;
    *out_transaction_num = hdr.transaction_num;
    return RCP_EP_MDIO_OK;
}

//cfusa:req REQ-MDIO-014
rcp_bytes_t rcp_ep_mdio_encode_read_response(rcp_byte_bus_id_t byte_bus_id,
                                              const uint16_t *words, size_t word_count,
                                              uint8_t transaction_num, bool timed,
                                              uint64_t timestamp)
{
    rcp_bytes_t frame   = {0};
    rcp_bytes_t payload;

    if (word_count > RCP_EP_MDIO_MAX_BURST_WORDS) return frame;

    payload = rcp_ep_mdio_pack_words(words, word_count);
    if (word_count > 0u && !payload.data) return frame;

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_READ;
        hdr.info.evt             = 0;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        frame = rcp_acf_encode_gbb(&hdr, payload.data, payload.len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_READ;
        hdr.evt             = 0;
        hdr.transaction_num = transaction_num;

        frame = rcp_acf_encode_abb(&hdr, payload.data, payload.len);
    }

    rcp_bytes_free(&payload);
    return frame;
}

//cfusa:req REQ-MDIO-015
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_read_response(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     const uint8_t **out_words_data,
                                                     size_t *out_word_count, bool *out_timed,
                                                     uint64_t *out_timestamp,
                                                     uint8_t *out_transaction_num)
{
    uint8_t                      msg_type;
    rcp_acf_errc_t               acf_rc;
    rcp_acf_byte_message_info_t  abb_hdr;
    rcp_acf_gbb_header_t         gbb_hdr;
    const uint8_t                *payload;
    size_t                       payload_len;
    rcp_byte_bus_id_t            bus_id;
    uint8_t                      transaction_num;
    bool                         timed;
    uint64_t                     timestamp;
    size_t                       word_count;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_MDIO_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_MDIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_MDIO_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_MDIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_MDIO_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_MDIO_ERR_WRONG_BUS;

    if (!rcp_ep_mdio_word_count_of(payload_len, &word_count)) return RCP_EP_MDIO_ERR_BAD_WORD_COUNT;
    if (word_count > RCP_EP_MDIO_MAX_BURST_WORDS) return RCP_EP_MDIO_ERR_BAD_WORD_COUNT;

    *out_words_data       = payload;
    *out_word_count       = word_count;
    *out_timed            = timed;
    *out_timestamp        = timestamp;
    *out_transaction_num  = transaction_num;
    return RCP_EP_MDIO_OK;
}

/* ── Write request/response ────────────────────────────────────────────────── */

//cfusa:req REQ-MDIO-016
rcp_bytes_t rcp_ep_mdio_encode_write_request(rcp_byte_bus_id_t byte_bus_id,
                                              rcp_ep_mdio_addr_t addr, const uint16_t *words,
                                              size_t word_count, uint8_t transaction_num)
{
    rcp_bytes_t                 frame = {0};
    rcp_acf_byte_message_info_t hdr   = {0};
    rcp_bytes_t                 words_bytes;
    uint8_t                     *payload;
    size_t                       payload_len;

    if (!rcp_ep_mdio_addr_valid(addr)) return frame;
    if (word_count == 0u || word_count > RCP_EP_MDIO_MAX_BURST_WORDS) return frame;

    words_bytes = rcp_ep_mdio_pack_words(words, word_count);
    if (!words_bytes.data) return frame;

    payload_len = ADDR_PREFIX_LEN + words_bytes.len;
    payload     = (uint8_t *)malloc(payload_len);
    if (!payload) {
        rcp_bytes_free(&words_bytes);
        return frame;
    }

    put_addr_prefix(payload, addr);
    memcpy(&payload[ADDR_PREFIX_LEN], words_bytes.data, words_bytes.len);
    rcp_bytes_free(&words_bytes);

    hdr.byte_bus_id     = byte_bus_id;
    hdr.op              = RCP_ACF_OP_WRITE;
    hdr.evt             = 0;
    hdr.transaction_num = transaction_num;

    frame = rcp_acf_encode_abb(&hdr, payload, payload_len);
    free(payload);
    return frame;
}

//cfusa:req REQ-MDIO-017
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_write_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     rcp_ep_mdio_addr_t *out_addr,
                                                     const uint8_t **out_words_data,
                                                     size_t *out_word_count,
                                                     uint8_t *out_transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_acf_errc_t               acf_rc;
    rcp_ep_mdio_addr_t           addr;
    size_t                       words_len;
    size_t                       word_count;

    acf_rc = rcp_acf_decode_abb(b, len, &hdr, &payload, &payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_MDIO_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_EP_MDIO_ERR_BAD_MSG_TYPE;

    if (hdr.byte_bus_id != expected_bus_id) return RCP_EP_MDIO_ERR_WRONG_BUS;
    if (hdr.op != RCP_ACF_OP_WRITE) return RCP_EP_MDIO_ERR_WRONG_OP;
    if (payload_len < ADDR_PREFIX_LEN) return RCP_EP_MDIO_ERR_SHORT_FRAME;

    addr = get_addr_prefix(payload);
    if (!rcp_ep_mdio_addr_valid(addr)) return RCP_EP_MDIO_ERR_BAD_ADDR;

    words_len = payload_len - ADDR_PREFIX_LEN;
    if (!rcp_ep_mdio_word_count_of(words_len, &word_count)) return RCP_EP_MDIO_ERR_BAD_WORD_COUNT;
    if (word_count == 0u || word_count > RCP_EP_MDIO_MAX_BURST_WORDS)
        return RCP_EP_MDIO_ERR_BAD_WORD_COUNT;

    *out_addr             = addr;
    *out_words_data       = &payload[ADDR_PREFIX_LEN];
    *out_word_count       = word_count;
    *out_transaction_num  = hdr.transaction_num;
    return RCP_EP_MDIO_OK;
}

//cfusa:req REQ-MDIO-018
rcp_bytes_t rcp_ep_mdio_encode_write_response(rcp_byte_bus_id_t byte_bus_id,
                                               const uint16_t *accepted_words,
                                               size_t accepted_word_count,
                                               uint8_t transaction_num, bool timed,
                                               uint64_t timestamp)
{
    rcp_bytes_t frame   = {0};
    rcp_bytes_t payload;

    if (accepted_word_count > RCP_EP_MDIO_MAX_BURST_WORDS) return frame;

    payload = rcp_ep_mdio_pack_words(accepted_words, accepted_word_count);
    if (accepted_word_count > 0u && !payload.data) return frame;

    if (timed) {
        rcp_acf_gbb_header_t hdr = {0};

        hdr.info.byte_bus_id     = byte_bus_id;
        hdr.info.op              = RCP_ACF_OP_WRITE;
        hdr.info.evt             = 0;
        hdr.info.mtv             = RCP_ACF_MTV_VALID;
        hdr.info.transaction_num = transaction_num;
        hdr.message_timestamp    = timestamp;

        frame = rcp_acf_encode_gbb(&hdr, payload.data, payload.len);
    } else {
        rcp_acf_byte_message_info_t hdr = {0};

        hdr.byte_bus_id     = byte_bus_id;
        hdr.op              = RCP_ACF_OP_WRITE;
        hdr.evt             = 0;
        hdr.transaction_num = transaction_num;

        frame = rcp_acf_encode_abb(&hdr, payload.data, payload.len);
    }

    rcp_bytes_free(&payload);
    return frame;
}

//cfusa:req REQ-MDIO-019
rcp_ep_mdio_errc_t rcp_ep_mdio_decode_write_response(const uint8_t *b, size_t len,
                                                      rcp_byte_bus_id_t expected_bus_id,
                                                      const uint8_t **out_words_data,
                                                      size_t *out_word_count, bool *out_timed,
                                                      uint64_t *out_timestamp,
                                                      uint8_t *out_transaction_num)
{
    uint8_t                      msg_type;
    rcp_acf_errc_t               acf_rc;
    rcp_acf_byte_message_info_t  abb_hdr;
    rcp_acf_gbb_header_t         gbb_hdr;
    const uint8_t                *payload;
    size_t                       payload_len;
    rcp_byte_bus_id_t            bus_id;
    uint8_t                      transaction_num;
    bool                         timed;
    uint64_t                     timestamp;
    size_t                       word_count;

    if (rcp_acf_peek_msg_type(b, len, &msg_type) != RCP_ACF_OK) return RCP_EP_MDIO_ERR_SHORT_FRAME;

    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        acf_rc = rcp_acf_decode_gbb(b, len, &gbb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_MDIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_MDIO_ERR_BAD_MSG_TYPE;

        bus_id          = gbb_hdr.info.byte_bus_id;
        transaction_num = gbb_hdr.info.transaction_num;
        timed           = rcp_acf_gbb_is_timed(&gbb_hdr);
        timestamp       = timed ? gbb_hdr.message_timestamp : 0u;
    } else {
        acf_rc = rcp_acf_decode_abb(b, len, &abb_hdr, &payload, &payload_len);
        if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_EP_MDIO_ERR_SHORT_FRAME;
        if (acf_rc != RCP_ACF_OK) return RCP_EP_MDIO_ERR_BAD_MSG_TYPE;

        bus_id          = abb_hdr.byte_bus_id;
        transaction_num = abb_hdr.transaction_num;
        timed           = false;
        timestamp       = 0u;
    }

    if (bus_id != expected_bus_id) return RCP_EP_MDIO_ERR_WRONG_BUS;

    if (!rcp_ep_mdio_word_count_of(payload_len, &word_count)) return RCP_EP_MDIO_ERR_BAD_WORD_COUNT;
    if (word_count > RCP_EP_MDIO_MAX_BURST_WORDS) return RCP_EP_MDIO_ERR_BAD_WORD_COUNT;

    *out_words_data       = payload;
    *out_word_count       = word_count;
    *out_timed            = timed;
    *out_timestamp        = timestamp;
    *out_transaction_num  = transaction_num;
    return RCP_EP_MDIO_OK;
}
