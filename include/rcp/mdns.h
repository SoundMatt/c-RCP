/*
 * mDNS/DNS-SD zone controller discovery (RFC 6762 + RFC 6763).
 *
 * Provides the abstract rcp_mdns_discoverer_t/rcp_mdns_announcer_t
 * interfaces and a StaticDiscoverer for testing. A full mDNS responder
 * (Avahi-compatible) requires platform APIs beyond this library's scope —
 * wrap avahi-client/systemd-resolved (Linux) or dns_sd (macOS) with these
 * interfaces.
 */
#ifndef RCP_MDNS_H
#define RCP_MDNS_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ownership: host/instance_name are borrowed for the duration of the
 * discovery callback (or the announce() call) only — implementations must
 * not retain the pointers past that call. rcp_mdns_static_discoverer_new()
 * takes its own internal copies of any zone_info passed to it. */
typedef struct {
    rcp_zone_t  zone;
    const char *host;
    uint16_t    port;
    const char *instance_name; /* e.g. "front-left.myhost._rcp._udp.local" */
} rcp_mdns_zone_info_t;

typedef enum {
    RCP_MDNS_EVENT_ADDED   = 0,
    RCP_MDNS_EVENT_REMOVED = 1,
} rcp_mdns_event_type_t;

typedef struct {
    rcp_mdns_event_type_t event;
    rcp_mdns_zone_info_t  info;
} rcp_mdns_discovery_event_t;

/* ── Discoverer — abstract interface for mDNS-based zone discovery ──────────── */

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

/* ── StaticDiscoverer — emits a fixed set of zones immediately on start() ───── */

/* Suitable for testing and static configuration. Deep-copies zones[0..count)
 * (including host/instance_name strings) — the caller retains ownership of
 * the input array. */
rcp_mdns_discoverer_t *rcp_mdns_static_discoverer_new(const rcp_mdns_zone_info_t *zones, size_t count);

/* ── Announcer — registers/withdraws a zone record in the local mDNS responder ── */
/*
 * Abstract interface only — no concrete implementation ships here (mirrors
 * cpp-RCP: platform mDNS responders are out of this library's scope). Tests
 * and platform integrations provide their own implementation.
 */

typedef struct rcp_mdns_announcer rcp_mdns_announcer_t;

typedef struct {
    int  (*announce)(rcp_mdns_announcer_t *self, const rcp_mdns_zone_info_t *info);
    void (*withdraw)(rcp_mdns_announcer_t *self, rcp_zone_t zone);
    void (*destroy)(rcp_mdns_announcer_t *self);
} rcp_mdns_announcer_vtable_t;

struct rcp_mdns_announcer {
    const rcp_mdns_announcer_vtable_t *vt;
};

static inline int rcp_mdns_announcer_announce(rcp_mdns_announcer_t *a, const rcp_mdns_zone_info_t *info)
{
    return a->vt->announce(a, info);
}

static inline void rcp_mdns_announcer_withdraw(rcp_mdns_announcer_t *a, rcp_zone_t zone)
{
    a->vt->withdraw(a, zone);
}

void rcp_mdns_announcer_destroy(rcp_mdns_announcer_t *a);

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Writes "<zone-name>.<hostname>._rcp._udp.local\0" into buf (up to buf_len
 * bytes) and returns the string length excluding the NUL (0 on failure). */
size_t rcp_mdns_make_instance_name(rcp_zone_t zone, const char *hostname, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_MDNS_H */
