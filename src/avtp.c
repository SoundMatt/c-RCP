#include "rcp/avtp.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-AVTP-015
const char *rcp_avtp_strerror(rcp_avtp_errc_t e)
{
    switch (e) {
    case RCP_AVTP_OK:              return "rcp/avtp: success";
    case RCP_AVTP_ERR_SHORT_FRAME: return "rcp/avtp: frame too short";
    case RCP_AVTP_ERR_BAD_SUBTYPE: return "rcp/avtp: unexpected AVTP subtype";
    default:                       return "rcp/avtp: unknown error";
    }
}

/* ── Byte-order helpers (this TU's own copy, matching wire.c's/e2e.c's
 * house convention of not sharing a byte-order util across modules) ────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
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
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_stream_id(uint8_t *p, rcp_stream_id_t id)
{
    memcpy(p, id.mac, sizeof(id.mac));
    put_u16(&p[6], id.unique_id);
}

static rcp_stream_id_t get_stream_id(const uint8_t *p)
{
    rcp_stream_id_t id;
    memcpy(id.mac, p, sizeof(id.mac));
    id.unique_id = get_u16(&p[6]);
    return id;
}

/* ── stream_id / byte_bus_id addressing ───────────────────────────────────── */

//cfusa:req REQ-AVTP-010
rcp_stream_id_t rcp_stream_id_make(const uint8_t mac[6], uint16_t unique_id)
{
    rcp_stream_id_t id;
    memcpy(id.mac, mac, sizeof(id.mac));
    id.unique_id = unique_id;
    return id;
}

//cfusa:req REQ-AVTP-010
uint64_t rcp_stream_id_to_u64(rcp_stream_id_t id)
{
    uint64_t mac48 = ((uint64_t)id.mac[0] << 40) | ((uint64_t)id.mac[1] << 32) |
                      ((uint64_t)id.mac[2] << 24) | ((uint64_t)id.mac[3] << 16) |
                      ((uint64_t)id.mac[4] << 8)  |  (uint64_t)id.mac[5];
    return (mac48 << 16) | (uint64_t)id.unique_id;
}

//cfusa:req REQ-AVTP-010
rcp_stream_id_t rcp_stream_id_from_u64(uint64_t v)
{
    rcp_stream_id_t id;
    uint64_t mac48 = v >> 16;

    id.mac[0] = (uint8_t)((mac48 >> 40) & 0xFFu);
    id.mac[1] = (uint8_t)((mac48 >> 32) & 0xFFu);
    id.mac[2] = (uint8_t)((mac48 >> 24) & 0xFFu);
    id.mac[3] = (uint8_t)((mac48 >> 16) & 0xFFu);
    id.mac[4] = (uint8_t)((mac48 >> 8) & 0xFFu);
    id.mac[5] = (uint8_t)(mac48 & 0xFFu);
    id.unique_id = (uint16_t)(v & 0xFFFFu);
    return id;
}

//cfusa:req REQ-AVTP-011
bool rcp_stream_id_equal(rcp_stream_id_t a, rcp_stream_id_t b)
{
    return memcmp(a.mac, b.mac, sizeof(a.mac)) == 0 && a.unique_id == b.unique_id;
}

//cfusa:req REQ-AVTP-012
bool rcp_avtp_addr_equal(rcp_avtp_addr_t a, rcp_avtp_addr_t b)
{
    return rcp_stream_id_equal(a.stream_id, b.stream_id) && a.byte_bus_id == b.byte_bus_id;
}

/* ── NTSCF ─────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-AVTP-001
//cfusa:req REQ-AVTP-003
rcp_bytes_t rcp_avtp_encode_ntscf(const rcp_avtp_ntscf_header_t *hdr,
                                   const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};
    size_t n;
    uint8_t *b;

    if (payload_len > RCP_AVTP_NTSCF_MAX_PAYLOAD) return frame;

    n = RCP_AVTP_NTSCF_HEADER_LEN + payload_len;
    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    b[0] = RCP_AVTP_SUBTYPE_NTSCF;
    b[1] = (uint8_t)(((hdr->sv & 0x1u) << 7) |
                      ((hdr->version & 0x7u) << 4) |
                      ((uint8_t)((payload_len >> 8) & 0x7u)));
    b[2] = (uint8_t)(payload_len & 0xFFu);
    b[3] = hdr->sequence_num;
    put_stream_id(&b[4], hdr->stream_id);

    if (payload_len > 0) memcpy(&b[RCP_AVTP_NTSCF_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-AVTP-003
//cfusa:req REQ-AVTP-005
//cfusa:req REQ-AVTP-007
//cfusa:req REQ-AVTP-009
rcp_avtp_errc_t rcp_avtp_decode_ntscf(const uint8_t *b, size_t len,
                                      rcp_avtp_ntscf_header_t *out_hdr,
                                      const uint8_t **out_payload, size_t *out_payload_len)
{
    uint16_t dlen;

    if (len < RCP_AVTP_NTSCF_HEADER_LEN) return RCP_AVTP_ERR_SHORT_FRAME;
    if (b[0] != RCP_AVTP_SUBTYPE_NTSCF) return RCP_AVTP_ERR_BAD_SUBTYPE;

    dlen = (uint16_t)(((uint16_t)(b[1] & 0x07u) << 8) | b[2]);
    if (len < RCP_AVTP_NTSCF_HEADER_LEN + (size_t)dlen) return RCP_AVTP_ERR_SHORT_FRAME;

    out_hdr->sv                = (uint8_t)((b[1] >> 7) & 0x1u);
    out_hdr->version           = (uint8_t)((b[1] >> 4) & 0x7u);
    out_hdr->ntscf_data_length = dlen;
    out_hdr->sequence_num      = b[3];
    out_hdr->stream_id         = get_stream_id(&b[4]);

    *out_payload     = &b[RCP_AVTP_NTSCF_HEADER_LEN];
    *out_payload_len = dlen;
    return RCP_AVTP_OK;
}

/* ── TSCF ──────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-AVTP-002
//cfusa:req REQ-AVTP-004
rcp_bytes_t rcp_avtp_encode_tscf(const rcp_avtp_tscf_header_t *hdr,
                                  const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};
    size_t n;
    uint8_t *b;

    if (payload_len > RCP_AVTP_TSCF_MAX_PAYLOAD) return frame;

    n = RCP_AVTP_TSCF_HEADER_LEN + payload_len;
    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    memset(b, 0, RCP_AVTP_TSCF_HEADER_LEN);

    b[0] = RCP_AVTP_SUBTYPE_TSCF;
    b[1] = (uint8_t)(((hdr->sv & 0x1u) << 7) |
                      ((hdr->version & 0x7u) << 4) |
                      ((hdr->mr & 0x1u) << 3) |
                      (hdr->tv & 0x1u));
    b[2] = hdr->sequence_num;
    b[3] = (uint8_t)(hdr->tu & 0x1u);
    put_stream_id(&b[4], hdr->stream_id);
    put_u32(&b[12], hdr->avtp_timestamp);
    /* bytes 16-19 reserved, left zeroed by the memset above */
    put_u16(&b[20], (uint16_t)payload_len);
    /* bytes 22-23 reserved, left zeroed by the memset above */

    if (payload_len > 0) memcpy(&b[RCP_AVTP_TSCF_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
    return frame;
}

//cfusa:req REQ-AVTP-004
//cfusa:req REQ-AVTP-006
//cfusa:req REQ-AVTP-008
//cfusa:req REQ-AVTP-009
rcp_avtp_errc_t rcp_avtp_decode_tscf(const uint8_t *b, size_t len,
                                     rcp_avtp_tscf_header_t *out_hdr,
                                     const uint8_t **out_payload, size_t *out_payload_len)
{
    uint16_t dlen;

    if (len < RCP_AVTP_TSCF_HEADER_LEN) return RCP_AVTP_ERR_SHORT_FRAME;
    if (b[0] != RCP_AVTP_SUBTYPE_TSCF) return RCP_AVTP_ERR_BAD_SUBTYPE;

    dlen = get_u16(&b[20]);
    if (len < RCP_AVTP_TSCF_HEADER_LEN + (size_t)dlen) return RCP_AVTP_ERR_SHORT_FRAME;

    out_hdr->sv             = (uint8_t)((b[1] >> 7) & 0x1u);
    out_hdr->version        = (uint8_t)((b[1] >> 4) & 0x7u);
    out_hdr->mr             = (uint8_t)((b[1] >> 3) & 0x1u);
    out_hdr->tv             = (uint8_t)(b[1] & 0x1u);
    out_hdr->sequence_num   = b[2];
    out_hdr->tu             = (uint8_t)(b[3] & 0x1u);
    out_hdr->stream_id      = get_stream_id(&b[4]);
    out_hdr->avtp_timestamp = get_u32(&b[12]);
    out_hdr->stream_data_length = dlen;

    *out_payload     = &b[RCP_AVTP_TSCF_HEADER_LEN];
    *out_payload_len = dlen;
    return RCP_AVTP_OK;
}

/* ── Subtype dispatch & the TSCF-without-time-sync drop rule ──────────────── */

//cfusa:req REQ-AVTP-013
rcp_avtp_errc_t rcp_avtp_peek_subtype(const uint8_t *b, size_t len, uint8_t *out_subtype)
{
    if (len < 1) return RCP_AVTP_ERR_SHORT_FRAME;
    *out_subtype = b[0];
    return RCP_AVTP_OK;
}

//cfusa:req REQ-AVTP-014
bool rcp_avtp_should_drop_tscf(bool server_time_sync_supported, uint8_t subtype)
{
    if (subtype != RCP_AVTP_SUBTYPE_TSCF) return false;
    return !server_time_sync_supported;
}

/* ── Transport base refcounting ────────────────────────────────────────────── */

//cfusa:req REQ-AVTP-016
rcp_avtp_transport_t *rcp_avtp_transport_retain(rcp_avtp_transport_t *t)
{
    if (t) rcp_atomic_inc(&t->refcount);
    return t;
}

//cfusa:req REQ-AVTP-016
void rcp_avtp_transport_release(rcp_avtp_transport_t *t)
{
    if (!t) return;
    if (rcp_atomic_dec(&t->refcount) == 0) {
        t->vt->destroy(t);
    }
}

/* ── Loopback transport ────────────────────────────────────────────────────── */

typedef struct rcp_avtp_loopback_transport {
    rcp_avtp_transport_t base; /* first member: allows rcp_avtp_transport_t* <-> this cast */

    rcp_mutex_t   mu;
    rcp_cond_t    cv;
    bool          closed;

    rcp_bytes_t  *items; /* circular buffer of owned frame copies, cap entries */
    size_t        head;
    size_t        count;
    size_t        cap;
} rcp_avtp_loopback_transport_t;

//cfusa:req REQ-AVTP-020
static int loopback_send(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len)
{
    rcp_avtp_loopback_transport_t *lb = (rcp_avtp_loopback_transport_t *)self;
    rcp_bytes_t *slot;
    size_t tail;

    rcp_mutex_lock(&lb->mu);
    if (lb->closed) {
        rcp_mutex_unlock(&lb->mu);
        return RCP_ERR_CLOSED;
    }
    if (lb->count >= lb->cap) {
        rcp_mutex_unlock(&lb->mu);
        return RCP_ERR_BUSY;
    }

    tail = (lb->head + lb->count) % lb->cap;
    slot = &lb->items[tail];
    *slot = rcp_bytes_dup(frame, frame_len);
    lb->count++;

    rcp_cond_signal(&lb->cv);
    rcp_mutex_unlock(&lb->mu);
    return RCP_OK;
}

//cfusa:req REQ-AVTP-017
//cfusa:req REQ-AVTP-018
static int loopback_recv(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                          uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    rcp_avtp_loopback_transport_t *lb = (rcp_avtp_loopback_transport_t *)self;
    rcp_bytes_t item;
    int rc = RCP_OK;

    rcp_mutex_lock(&lb->mu);
    while (lb->count == 0 && !lb->closed && !rcp_context_done(ctx)) {
        if (ctx->has_deadline) {
            (void)rcp_cond_timedwait_until(&lb->cv, &lb->mu, ctx->deadline_ms);
        } else {
            rcp_cond_wait(&lb->cv, &lb->mu);
        }
    }

    if (lb->count == 0) {
        rc = lb->closed ? RCP_ERR_CLOSED : RCP_ERR_TIMEOUT;
        rcp_mutex_unlock(&lb->mu);
        return rc;
    }

    item = lb->items[lb->head];
    if (item.len > buf_cap) {
        /* Leave the frame queued -- rejecting into too-small a buffer must
         * not silently drop it. */
        rcp_mutex_unlock(&lb->mu);
        return RCP_ERR_BUSY;
    }

    if (item.len > 0) memcpy(buf, item.data, item.len);
    *out_len = item.len;
    rcp_bytes_free(&lb->items[lb->head]);
    lb->head = (lb->head + 1) % lb->cap;
    lb->count--;

    rcp_mutex_unlock(&lb->mu);
    return RCP_OK;
}

//cfusa:req REQ-AVTP-019
static int loopback_close(rcp_avtp_transport_t *self)
{
    rcp_avtp_loopback_transport_t *lb = (rcp_avtp_loopback_transport_t *)self;

    rcp_mutex_lock(&lb->mu);
    lb->closed = true;
    rcp_cond_broadcast(&lb->cv);
    rcp_mutex_unlock(&lb->mu);
    return RCP_OK;
}

static void loopback_destroy(rcp_avtp_transport_t *self)
{
    rcp_avtp_loopback_transport_t *lb = (rcp_avtp_loopback_transport_t *)self;
    size_t i;

    for (i = 0; i < lb->count; i++) {
        rcp_bytes_free(&lb->items[(lb->head + i) % lb->cap]);
    }
    rcp_mutex_destroy(&lb->mu);
    rcp_cond_destroy(&lb->cv);
    free(lb->items);
    free(lb);
}

static const rcp_avtp_transport_vtable_t loopback_vtable = {
    loopback_send,
    loopback_recv,
    loopback_close,
    loopback_destroy,
};

//cfusa:req REQ-AVTP-016
//cfusa:req REQ-AVTP-017
rcp_avtp_transport_t *rcp_avtp_loopback_transport_new(bool time_sync_supported,
                                                       size_t queue_capacity)
{
    rcp_avtp_loopback_transport_t *lb;
    rcp_bytes_t *items;

    if (queue_capacity == 0) queue_capacity = 1;

    lb = (rcp_avtp_loopback_transport_t *)malloc(sizeof(*lb));
    if (!lb) return NULL;

    items = (rcp_bytes_t *)calloc(queue_capacity, sizeof(*items));
    if (!items) {
        free(lb);
        return NULL;
    }
    lb->items = items;

    lb->base.vt                  = &loopback_vtable;
    lb->base.refcount             = 1;
    lb->base.time_sync_supported = time_sync_supported;
    lb->head                     = 0;
    lb->count                    = 0;
    lb->cap                      = queue_capacity;
    lb->closed                   = false;

    rcp_mutex_init(&lb->mu);
    rcp_cond_init(&lb->cv);

    return &lb->base;
}
