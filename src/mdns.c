/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/mdns.h"
#include "rcp/alloc.h"

#include "platform.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-MDNS-010
void rcp_mdns_discoverer_destroy(rcp_mdns_discoverer_t *d)
{
    if (!d) return;
    d->vt->destroy(d);
}

//cfusa:req REQ-MDNS-009
void rcp_mdns_announcer_destroy(rcp_mdns_announcer_t *a)
{
    if (!a) return;
    a->vt->destroy(a);
}

//cfusa:req REQ-MDNS-006
size_t rcp_mdns_make_instance_name(rcp_stream_id_t server_stream_id, const char *hostname, char *buf, size_t buf_len)
{
    int n;
    if (buf_len == 0) return 0;
    n = snprintf(buf, buf_len, "%016" PRIx64 ".%s._rcp-tc18._udp.local",
                 rcp_stream_id_to_u64(server_stream_id), hostname ? hostname : "");
    if (n < 0) return 0;
    return ((size_t)n < buf_len) ? (size_t)n : buf_len - 1;
}

/* ── StaticDiscoverer ──────────────────────────────────────────────────────── */

typedef struct {
    rcp_stream_id_t server_stream_id;
    char           *host;          /* owned copy */
    uint16_t        port;
    char           *instance_name; /* owned copy */
} static_record_entry_t;

typedef struct {
    rcp_mdns_discoverer_t   base;
    rcp_mutex_t             mu; /* protects stopped (start()/stop() may race) */
    bool                    stopped;
    static_record_entry_t  *records;
    size_t                  count;
} static_discoverer_t;

static char *dup_cstr(const char *s)
{
    size_t len;
    char *copy;
    if (!s) return NULL;
    len = strlen(s);
    copy = (char *)rcp_malloc(len + 1);
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

        ev.event                     = RCP_MDNS_EVENT_ADDED;
        ev.info.server_stream_id     = d->records[i].server_stream_id;
        ev.info.host                 = d->records[i].host;
        ev.info.port                 = d->records[i].port;
        ev.info.instance_name        = d->records[i].instance_name;
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
        rcp_free(d->records[i].host);
        d->records[i].host = NULL;
        rcp_free(d->records[i].instance_name);
        d->records[i].instance_name = NULL;
    }
    rcp_free(d->records);
    d->records = NULL;
    rcp_mutex_destroy(&d->mu);
    rcp_free(d);
    d = NULL;
}

static const rcp_mdns_discoverer_vtable_t static_disc_vtable = {
    static_disc_start,
    static_disc_stop,
    static_disc_destroy,
};

//cfusa:req REQ-MDNS-011
rcp_mdns_discoverer_t *rcp_mdns_static_discoverer_new(const rcp_mdns_server_info_t *records, size_t count)
{
    static_discoverer_t *d = (static_discoverer_t *)rcp_calloc(1, sizeof(*d));
    size_t i;
    if (!d) return NULL;

    d->base.vt = &static_disc_vtable;
    rcp_mutex_init(&d->mu);

    if (count > 0) {
        static_record_entry_t *entries =
            (static_record_entry_t *)rcp_calloc(count, sizeof(*entries));
        if (!entries) {
            rcp_mutex_destroy(&d->mu);
            rcp_free(d);
            d = NULL;
            return NULL;
        }
        d->records = entries;
    }
    d->count = count;
    for (i = 0; i < count; i++) {
        d->records[i].server_stream_id = records[i].server_stream_id;
        d->records[i].host             = dup_cstr(records[i].host);
        d->records[i].port             = records[i].port;
        d->records[i].instance_name    = dup_cstr(records[i].instance_name);
    }
    return &d->base;
}
