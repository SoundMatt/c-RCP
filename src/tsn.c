/* SPDX-License-Identifier: MPL-2.0 */
/* SO_PRIORITY is a glibc extension, not exposed by <sys/socket.h> under
 * strict -std=c99 unless _DEFAULT_SOURCE is defined first — must be the
 * literal first thing in the translation unit, before any include (even
 * "rcp/tsn.h", which pulls in <stdint.h> and could lock in feature-test
 * macros via glibc's <features.h> before we get a chance to set this). */
#define _DEFAULT_SOURCE

#include "rcp/tsn.h"

#include "rcp/acf.h"
#include "rcp/request_compound.h"
#include "rcp/alloc.h"

#include <stdlib.h>

#if defined(__linux__)
#  include <sys/socket.h>
#  define RCP_TSN_SO_PRIORITY 1
#endif

//cfusa:req REQ-TSN-001
rcp_tsn_pcp_map_t rcp_tsn_default_pcp_map(void)
{
    rcp_tsn_pcp_map_t m;
    int               k;

    for (k = (int)RCP_SCHED_KIND_STANDARD; k <= (int)RCP_SCHED_KIND_CANCELLATION; k++) {
        m.pcp[k] = rcp_sched_kind_rank((rcp_sched_kind_t)k);
    }
    return m;
}

//cfusa:req REQ-TSN-002
uint8_t rcp_tsn_pcp_for(const rcp_tsn_pcp_map_t *m, rcp_sched_kind_t kind)
{
    if (kind < RCP_SCHED_KIND_STANDARD || kind > RCP_SCHED_KIND_CANCELLATION) {
        kind = RCP_SCHED_KIND_STANDARD;
    }
    return m->pcp[kind];
}

//cfusa:req REQ-TSN-008
rcp_tsn_config_t rcp_tsn_default_config(void)
{
    rcp_tsn_config_t c;
    c.pcp_map  = rcp_tsn_default_pcp_map();
    c.vlan_id  = 0;
    c.cycle_ns = 0;
    return c;
}

/* ── Frame classification ──────────────────────────────────────────────────── */

//cfusa:req REQ-TSN-003
rcp_sched_kind_t rcp_tsn_classify_frame(const uint8_t *frame, size_t frame_len)
{
    uint8_t                 subtype;
    rcp_avtp_ntscf_header_t nhdr;
    rcp_avtp_tscf_header_t  thdr;
    const uint8_t          *payload     = NULL;
    size_t                   payload_len = 0;
    uint8_t                  acf_type;
    uint8_t                  request_type;
    rcp_compound_errc_t       perr;

    if (rcp_avtp_peek_subtype(frame, frame_len, &subtype) != RCP_AVTP_OK) {
        return RCP_SCHED_KIND_STANDARD;
    }

    if (subtype == RCP_AVTP_SUBTYPE_NTSCF) {
        if (rcp_avtp_decode_ntscf(frame, frame_len, &nhdr, &payload, &payload_len) != RCP_AVTP_OK) {
            return RCP_SCHED_KIND_STANDARD;
        }
    } else if (subtype == RCP_AVTP_SUBTYPE_TSCF) {
        if (rcp_avtp_decode_tscf(frame, frame_len, &thdr, &payload, &payload_len) != RCP_AVTP_OK) {
            return RCP_SCHED_KIND_STANDARD;
        }
    } else {
        return RCP_SCHED_KIND_STANDARD;
    }

    if (payload_len == 0 || rcp_acf_peek_msg_type(payload, payload_len, &acf_type) != RCP_ACF_OK) {
        return RCP_SCHED_KIND_STANDARD;
    }
    if (acf_type != RCP_ACF_MSG_TYPE_GBB) {
        return RCP_SCHED_KIND_STANDARD; /* ACF_ABB never carries the repurposing trick */
    }

    perr = rcp_compound_peek_request_type(payload, payload_len, &request_type);
    if (perr != RCP_COMPOUND_OK) {
        return RCP_SCHED_KIND_STANDARD; /* not repurposed, or malformed: fail-safe Standard */
    }

    return rcp_sched_classify(true, request_type);
}

/* ── Transport wrapper ─────────────────────────────────────────────────────── */

typedef struct {
    rcp_avtp_transport_t  base;
    rcp_avtp_transport_t *inner; /* retained */
    int                    fd;
    rcp_tsn_config_t        cfg;
} tsn_transport_t;

//cfusa:req REQ-TSN-004
static int tsn_send(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len)
{
    tsn_transport_t *t = (tsn_transport_t *)self;

#if defined(RCP_TSN_SO_PRIORITY)
    if (t->fd >= 0) {
        rcp_sched_kind_t kind = rcp_tsn_classify_frame(frame, frame_len);
        int              pcp  = (int)rcp_tsn_pcp_for(&t->cfg.pcp_map, kind);
        (void)setsockopt(t->fd, SOL_SOCKET, SO_PRIORITY, &pcp, sizeof(pcp));
    }
#endif

    return rcp_avtp_transport_send(t->inner, frame, frame_len);
}

//cfusa:req REQ-TSN-005
static int tsn_recv(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                     uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    tsn_transport_t *t = (tsn_transport_t *)self;
    return rcp_avtp_transport_recv(t->inner, ctx, buf, buf_cap, out_len);
}

//cfusa:req REQ-TSN-006
static int tsn_close(rcp_avtp_transport_t *self)
{
    tsn_transport_t *t = (tsn_transport_t *)self;
    return rcp_avtp_transport_close(t->inner);
}

static void tsn_destroy(rcp_avtp_transport_t *self)
{
    tsn_transport_t *t = (tsn_transport_t *)self;
    rcp_avtp_transport_release(t->inner);
    rcp_free(t);
    t = NULL;
}

static const rcp_avtp_transport_vtable_t tsn_vtable = {
    tsn_send,
    tsn_recv,
    tsn_close,
    tsn_destroy,
};

//cfusa:req REQ-TSN-007
rcp_avtp_transport_t *rcp_tsn_avtp_transport_new(rcp_avtp_transport_t *inner, int socket_fd,
                                                  rcp_tsn_config_t cfg)
{
    tsn_transport_t *t = (tsn_transport_t *)rcp_calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->base.vt                  = &tsn_vtable;
    t->base.refcount             = 1;
    t->base.time_sync_supported = inner->time_sync_supported;
    t->inner                    = rcp_avtp_transport_retain(inner);
    t->fd                       = socket_fd;
    t->cfg                      = cfg;
    return &t->base;
}
