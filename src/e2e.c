#include "rcp/e2e.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-E2E-009
const char *rcp_e2e_strerror(rcp_e2e_errc_t e)
{
    switch (e) {
    case RCP_E2E_OK:               return "e2e: ok";
    case RCP_E2E_ERR_CRC_MISMATCH: return "e2e: CRC-16 mismatch -- payload corrupted";
    case RCP_E2E_ERR_SHORT_FRAME:  return "e2e: frame too short for E2E header";
    case RCP_E2E_ERR_REPLAY:       return "e2e: replayed sequence number detected";
    default:                       return "e2e: unknown error";
    }
}

/* ── CRC-16/CCITT-FALSE ────────────────────────────────────────────────────── */

//cfusa:req REQ-E2E-003
static uint16_t crc16_update(uint16_t crc, uint8_t b)
{
    static const uint16_t poly = 0x1021;
    int i;

    crc = (uint16_t)(crc ^ ((uint16_t)b << 8));
    for (i = 0; i < 8; i++) {
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ poly) : (uint16_t)(crc << 1);
    }
    return crc;
}

//cfusa:req REQ-E2E-003
//cfusa:req REQ-E2E-004
static uint16_t crc16(const uint8_t *prefix, size_t prefix_len, const uint8_t *data, size_t data_len)
{
    uint16_t crc = 0xFFFFu;
    size_t i;

    for (i = 0; i < prefix_len; i++) crc = crc16_update(crc, prefix[i]);
    for (i = 0; i < data_len; i++)   crc = crc16_update(crc, data[i]);
    return crc;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* ── wrap / unwrap ─────────────────────────────────────────────────────────── */

//cfusa:req REQ-E2E-001
//cfusa:req REQ-E2E-002
rcp_bytes_t rcp_e2e_wrap(uint32_t seq_num, const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};
    uint8_t *data;
    uint16_t crc;

    data = (uint8_t *)malloc(RCP_E2E_HEADER_LEN + payload_len);
    if (!data) return frame;

    put_u32(data, seq_num);
    if (payload_len > 0) memcpy(data + RCP_E2E_HEADER_LEN, payload, payload_len);
    crc = crc16(data, 4, payload, payload_len);
    put_u16(data + 4, crc);

    frame.data = data;
    frame.len  = RCP_E2E_HEADER_LEN + payload_len;
    return frame;
}

//cfusa:req REQ-E2E-004
//cfusa:req REQ-E2E-005
rcp_e2e_errc_t rcp_e2e_unwrap(const uint8_t *frame, size_t frame_len,
                              uint32_t *seq_num_out, rcp_bytes_t *payload_out)
{
    uint16_t got_crc;
    uint16_t want_crc;
    const uint8_t *pl;
    size_t pl_len;

    payload_out->data = NULL;
    payload_out->len  = 0;

    if (frame_len < RCP_E2E_HEADER_LEN) return RCP_E2E_ERR_SHORT_FRAME;

    *seq_num_out = get_u32(frame);
    got_crc      = get_u16(frame + 4);
    pl           = frame + RCP_E2E_HEADER_LEN;
    pl_len       = frame_len - RCP_E2E_HEADER_LEN;
    want_crc     = crc16(frame, 4, pl, pl_len);
    if (got_crc != want_crc) return RCP_E2E_ERR_CRC_MISMATCH;

    *payload_out = rcp_bytes_dup(pl, pl_len);
    return RCP_E2E_OK;
}

/* ── ReplayGuard ───────────────────────────────────────────────────────────── */

struct rcp_e2e_replay_guard {
    rcp_mutex_t mu;
    bool        bitmap[RCP_E2E_REPLAY_WINDOW_SIZE];
    uint32_t    high_water;
    bool        initialized;
};

rcp_e2e_replay_guard_t *rcp_e2e_replay_guard_new(void)
{
    rcp_e2e_replay_guard_t *g = (rcp_e2e_replay_guard_t *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    rcp_mutex_init(&g->mu);
    return g;
}

//cfusa:req REQ-E2E-006
//cfusa:req REQ-E2E-007
//cfusa:req REQ-E2E-008
rcp_e2e_errc_t rcp_e2e_replay_guard_check(rcp_e2e_replay_guard_t *g, uint32_t seq_num)
{
    rcp_e2e_errc_t result = RCP_E2E_OK;
    size_t slot;

    rcp_mutex_lock(&g->mu);

    if (!g->initialized) {
        /* Bootstrap -- accept the very first sequence number. */
        g->initialized = true;
        g->high_water   = seq_num;
        g->bitmap[seq_num % RCP_E2E_REPLAY_WINDOW_SIZE] = true;
        rcp_mutex_unlock(&g->mu);
        return RCP_E2E_OK;
    }

    if (seq_num > g->high_water) {
        /* Advance window: clear slots that are no longer in range. */
        uint32_t i;
        for (i = g->high_water + 1; i != seq_num + 1; i++) {
            g->bitmap[i % RCP_E2E_REPLAY_WINDOW_SIZE] = false;
        }
        g->high_water = seq_num;
    } else {
        uint32_t age = g->high_water - seq_num;
        if (age >= (uint32_t)RCP_E2E_REPLAY_WINDOW_SIZE) {
            rcp_mutex_unlock(&g->mu);
            return RCP_E2E_ERR_REPLAY; /* too old -- reject */
        }
    }

    slot = seq_num % RCP_E2E_REPLAY_WINDOW_SIZE;
    if (g->bitmap[slot]) {
        result = RCP_E2E_ERR_REPLAY; /* duplicate within window */
    } else {
        g->bitmap[slot] = true;
    }

    rcp_mutex_unlock(&g->mu);
    return result;
}

void rcp_e2e_replay_guard_destroy(rcp_e2e_replay_guard_t *g)
{
    if (!g) return;
    rcp_mutex_destroy(&g->mu);
    free(g);
}

/* ── Controller wrapper ────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t  base;
    rcp_controller_t *inner; /* retained */
    volatile int       seq;
} e2e_controller_t;

//cfusa:req REQ-E2E-010
static rcp_zone_t e2e_ctrl_zone(rcp_controller_t *self)
{
    return rcp_controller_zone(((e2e_controller_t *)self)->inner);
}

static int e2e_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                          const rcp_command_t *cmd, rcp_response_t *out)
{
    e2e_controller_t *ec = (e2e_controller_t *)self;
    uint32_t seq = (uint32_t)rcp_atomic_inc(&ec->seq);
    rcp_command_t protected_cmd = *cmd;
    rcp_bytes_t frame;
    int result;

    frame = rcp_e2e_wrap(seq, cmd->payload.data, cmd->payload.len);
    protected_cmd.payload = frame;

    result = rcp_controller_send(ec->inner, ctx, &protected_cmd, out);
    rcp_bytes_free(&frame);
    return result;
}

//cfusa:req REQ-E2E-011
static int e2e_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    e2e_controller_t *ec = (e2e_controller_t *)self;
    return rcp_controller_subscribe(ec->inner, ctx, out);
}

//cfusa:req REQ-E2E-012
static int e2e_ctrl_close(rcp_controller_t *self)
{
    e2e_controller_t *ec = (e2e_controller_t *)self;
    return rcp_controller_close(ec->inner);
}

static void e2e_ctrl_destroy(rcp_controller_t *self)
{
    e2e_controller_t *ec = (e2e_controller_t *)self;
    rcp_controller_release(ec->inner);
    free(ec);
}

static const rcp_controller_vtable_t e2e_controller_vtable = {
    e2e_ctrl_zone,
    e2e_ctrl_send,
    e2e_ctrl_subscribe,
    e2e_ctrl_close,
    e2e_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_e2e_controller_new(rcp_controller_t *inner)
{
    e2e_controller_t *ec = (e2e_controller_t *)calloc(1, sizeof(*ec));
    if (!ec) return NULL;
    ec->base.vt       = &e2e_controller_vtable;
    ec->base.refcount = 1;
    ec->inner         = rcp_controller_retain(inner);
    return &ec->base;
}
