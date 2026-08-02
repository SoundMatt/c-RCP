/* SPDX-License-Identifier: MPL-2.0 */
/*
 * mdns.h -- optional mDNS/DNS-SD (RFC 6762 + RFC 6763) discovery
 * convenience for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 21, "Satellite Package Rework", milestone 82,
 * "Optional discovery convenience").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: mDNS/DNS-SD service
 * discovery is exactly as useful under TC18 as it was before -- what
 * changes is the identity a discovered record carries. The legacy shape
 * keyed everything off rcp_zone_t (retired by ROADMAP.md's Protocol
 * Replacement Notice along with rcp_command_t/rcp_response_t/
 * rcp_controller_t); rcp_mdns_server_info_t now instead carries an
 * avtp.h rcp_stream_id_t, the same 48-bit-MAC-plus-unique-suffix server
 * identity discovery.h's rcp_discovery_result_t.server_stream_id already
 * uses (milestone 63) -- so an mDNS-advertised record and a
 * natively-discovered one describe the same kind of thing and can
 * plausibly feed a shared client-side consumer later. host/port/
 * instance_name are unchanged in kind: they are transport-level
 * (IEEE1722-over-UDP/IP, spec Annex J) service-record fields, not a
 * protocol-model concern that TC18 redefines.
 *
 * ── Optional, and never a substitute for discovery.h ──────────────────────
 *
 * This module is scoped strictly to the IEEE1722-over-UDP/IP transport
 * variant, where a conventional mDNS responder is a reasonable thing to
 * have running anyway. It sits *beside* discovery.h's native broadcast
 * discovery (ROADMAP.md Phase 15, milestone 63) -- request/response
 * frames exchanged over NTSCF/ACF_ABB, addressed by rcp_stream_id_t, the
 * only discovery path this specification treats as mandatory. Native
 * Ethernet and CAN(FD/XL)-as-transport deployments (avtp.h's
 * rcp_avtp_transport_t) have no mDNS responder to speak of and must use
 * discovery.h regardless. Nothing in this file is ever a dependency of
 * discovery.h, and discovery.h has no dependency on this file either --
 * a caller may use one, the other, or both (e.g. mDNS for a first fast
 * hint of host:port, discovery.h's own request/response exchange for the
 * authoritative device-recognition fields) but must never treat this
 * module as a replacement for discovery.h's own mandatory path.
 *
 * Provides the abstract rcp_mdns_discoverer_t/rcp_mdns_announcer_t
 * interfaces and a StaticDiscoverer for testing. A full mDNS responder
 * (Avahi-compatible) requires platform APIs beyond this library's scope --
 * wrap avahi-client/systemd-resolved (Linux) or dns_sd (macOS) with these
 * interfaces.
 */
#ifndef RCP_MDNS_H
#define RCP_MDNS_H

#include "rcp/rcp.h"
#include "rcp/avtp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ownership: host/instance_name are borrowed for the duration of the
 * discovery callback (or the announce() call) only — implementations must
 * not retain the pointers past that call. rcp_mdns_static_discoverer_new()
 * takes its own internal copies of any server_info passed to it. */
typedef struct {
    rcp_stream_id_t server_stream_id;
    const char     *host;
    uint16_t        port;
    const char     *instance_name; /* e.g. "0021bd7f3a01-0001.myhost._rcp-tc18._udp.local" */
} rcp_mdns_server_info_t;

typedef enum {
    RCP_MDNS_EVENT_ADDED   = 0,
    RCP_MDNS_EVENT_REMOVED = 1,
} rcp_mdns_event_type_t;

typedef struct {
    rcp_mdns_event_type_t  event;
    rcp_mdns_server_info_t info;
} rcp_mdns_discovery_event_t;

/* ── Discoverer — abstract interface for mDNS-based server discovery ────────── */

typedef struct rcp_mdns_discoverer rcp_mdns_discoverer_t;

typedef void (*rcp_mdns_discovery_callback_fn)(const rcp_mdns_discovery_event_t *ev, void *user_data);

typedef struct {
    /* Begins discovery; calls cb for each event until stop() is called or
     * the implementation runs out of events (e.g. StaticDiscoverer). */
    int  (*start)(rcp_mdns_discoverer_t *self, rcp_mdns_discovery_callback_fn cb, void *user_data);
    void (*stop)(rcp_mdns_discoverer_t *self);
    void (*destroy)(rcp_mdns_discoverer_t *self);
} rcp_mdns_discoverer_vtable_t;

struct rcp_mdns_discoverer {
    const rcp_mdns_discoverer_vtable_t *vt;
};

static inline int rcp_mdns_discoverer_start(rcp_mdns_discoverer_t *d,
                                             rcp_mdns_discovery_callback_fn cb, void *user_data)
{
    return d->vt->start(d, cb, user_data);
}

static inline void rcp_mdns_discoverer_stop(rcp_mdns_discoverer_t *d)
{
    d->vt->stop(d);
}

void rcp_mdns_discoverer_destroy(rcp_mdns_discoverer_t *d);

/* ── StaticDiscoverer — emits a fixed set of records immediately on start() ── */

/* Suitable for testing and static configuration. Deep-copies
 * records[0..count) (including host/instance_name strings) — the caller
 * retains ownership of the input array. */
rcp_mdns_discoverer_t *rcp_mdns_static_discoverer_new(const rcp_mdns_server_info_t *records, size_t count);

/* ── Announcer — registers/withdraws a service record in the local mDNS responder ── */
/*
 * Abstract interface only — no concrete implementation ships here (mirrors
 * cpp-RCP: platform mDNS responders are out of this library's scope). Tests
 * and platform integrations provide their own implementation.
 */

//cfusa:req REQ-MDNS-007
typedef struct rcp_mdns_announcer rcp_mdns_announcer_t;

typedef struct {
    int  (*announce)(rcp_mdns_announcer_t *self, const rcp_mdns_server_info_t *info);
    void (*withdraw)(rcp_mdns_announcer_t *self, rcp_stream_id_t server_stream_id);
    void (*destroy)(rcp_mdns_announcer_t *self);
} rcp_mdns_announcer_vtable_t;

struct rcp_mdns_announcer {
    const rcp_mdns_announcer_vtable_t *vt;
};

static inline int rcp_mdns_announcer_announce(rcp_mdns_announcer_t *a, const rcp_mdns_server_info_t *info)
{
    return a->vt->announce(a, info);
}

//cfusa:req REQ-MDNS-008
static inline void rcp_mdns_announcer_withdraw(rcp_mdns_announcer_t *a, rcp_stream_id_t server_stream_id)
{
    a->vt->withdraw(a, server_stream_id);
}

void rcp_mdns_announcer_destroy(rcp_mdns_announcer_t *a);

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Writes "<server_stream_id as 16 lowercase hex digits>.<hostname>._rcp-tc18._udp.local\0"
 * into buf (up to buf_len bytes) and returns the string length excluding
 * the NUL (0 on failure). The service type ("_rcp-tc18._udp") deliberately
 * differs from any prior legacy naming so the two are never confused on
 * the wire; it names this module's own scope (TC18 RCP over
 * IEEE1722-over-UDP/IP), not a value defined by the specification. */
size_t rcp_mdns_make_instance_name(rcp_stream_id_t server_stream_id, const char *hostname, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_MDNS_H */
