#include "rcp/mdns.h"

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rcp_mdns_discoverer_destroy(rcp_mdns_discoverer_t *d)
{
    if (!d) return;
    d->vt->destroy(d);
}

void rcp_mdns_announcer_destroy(rcp_mdns_announcer_t *a)
{
    if (!a) return;
    a->vt->destroy(a);
}

//cfusa:req REQ-MDNS-006
size_t rcp_mdns_make_instance_name(rcp_zone_t zone, const char *hostname, char *buf, size_t buf_len)
{
    int n;
    if (buf_len == 0) return 0;
    n = snprintf(buf, buf_len, "%s.%s._rcp._udp.local", rcp_zone_string(zone), hostname ? hostname : "");
    if (n < 0) return 0;
    return ((size_t)n < buf_len) ? (size_t)n : buf_len - 1;
}

/* ── StaticDiscoverer ──────────────────────────────────────────────────────── */

typedef struct {
    rcp_zone_t  zone;
    char       *host;          /* owned copy */
    uint16_t    port;
    char       *instance_name; /* owned copy */
} static_zone_entry_t;

typedef struct {
    rcp_mdns_discoverer_t  base;
    rcp_mutex_t            mu; /* protects stopped (start()/stop() may race) */
    bool                   stopped;
    static_zone_entry_t   *zones;
    size_t                 count;
} static_discoverer_t;

static char *dup_cstr(const char *s)
{
    size_t len;
    char *copy;
    if (!s) return NULL;
    len = strlen(s);
    copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

//cfusa:req REQ-MDNS-001
//cfusa:req REQ-MDNS-002
//cfusa:req REQ-MDNS-003
//cfusa:req REQ-MDNS-004
//cfusa:req REQ-MDNS-005
static int static_disc_start(rcp_mdns_discoverer_t *self, rcp_mdns_discovery_callback_fn cb, void *user_data)
{
    static_discoverer_t *d = (static_discoverer_t *)self;
    size_t i;

    rcp_mutex_lock(&d->mu);
    d->stopped = false;
    rcp_mutex_unlock(&d->mu);

    for (i = 0; i < d->count; i++) {
        bool stopped_now;
        rcp_mdns_discovery_event_t ev;

        rcp_mutex_lock(&d->mu);
        stopped_now = d->stopped;
        rcp_mutex_unlock(&d->mu);
        if (stopped_now) break;

        ev.event             = RCP_MDNS_EVENT_ADDED;
        ev.info.zone          = d->zones[i].zone;
        ev.info.host          = d->zones[i].host;
        ev.info.port          = d->zones[i].port;
        ev.info.instance_name = d->zones[i].instance_name;
        cb(&ev, user_data);
    }
    return RCP_OK;
}

static void static_disc_stop(rcp_mdns_discoverer_t *self)
{
    static_discoverer_t *d = (static_discoverer_t *)self;
    rcp_mutex_lock(&d->mu);
    d->stopped = true;
    rcp_mutex_unlock(&d->mu);
}

static void static_disc_destroy(rcp_mdns_discoverer_t *self)
{
    static_discoverer_t *d = (static_discoverer_t *)self;
    size_t i;
    for (i = 0; i < d->count; i++) {
        free(d->zones[i].host);
        free(d->zones[i].instance_name);
    }
    free(d->zones);
    rcp_mutex_destroy(&d->mu);
    free(d);
}

static const rcp_mdns_discoverer_vtable_t static_disc_vtable = {
    static_disc_start,
    static_disc_stop,
    static_disc_destroy,
};

rcp_mdns_discoverer_t *rcp_mdns_static_discoverer_new(const rcp_mdns_zone_info_t *zones, size_t count)
{
    static_discoverer_t *d = (static_discoverer_t *)calloc(1, sizeof(*d));
    size_t i;
    if (!d) return NULL;

    d->base.vt = &static_disc_vtable;
    rcp_mutex_init(&d->mu);

    if (count > 0) {
        static_zone_entry_t *entries =
            (static_zone_entry_t *)calloc(count, sizeof(*entries));
        if (!entries) {
            rcp_mutex_destroy(&d->mu);
            free(d);
            return NULL;
        }
        d->zones = entries;
    }
    d->count = count;
    for (i = 0; i < count; i++) {
        d->zones[i].zone          = zones[i].zone;
        d->zones[i].host          = dup_cstr(zones[i].host);
        d->zones[i].port          = zones[i].port;
        d->zones[i].instance_name = dup_cstr(zones[i].instance_name);
    }
    return &d->base;
}
