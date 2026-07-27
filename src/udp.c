/* Must precede any system header: exposes the POSIX declarations used below
 * on glibc under strict -std=c99 (see clock.c for the same fix). */
#define _POSIX_C_SOURCE 200809L

#include "rcp/udp.h"
#include "rcp/wire.h"

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#define RCP_UDP_POSIX 1
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(RCP_UDP_POSIX)

/* ── ZoneServer ────────────────────────────────────────────────────────────── */

struct rcp_udp_zone_server {
    rcp_zone_t          zone;
    int                 fd;
    bool                closed;
    bool                healthy;
    uint32_t            seq;
    rcp_mutex_t         mu; /* protects handler/user_data/subs/seq/healthy/closed */
    rcp_udp_handler_fn  handler;
    void               *user_data;
    struct sockaddr_in *subs;
    size_t              subs_len;
    size_t              subs_cap;
    rcp_thread_t        serve_thread;
    bool                serve_thread_started;
};

//cfusa:req REQ-UDP-018
static bool same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static bool zserv_subs_append(rcp_udp_zone_server_t *srv, const struct sockaddr_in *addr)
{
    size_t i;
    for (i = 0; i < srv->subs_len; i++) {
        if (same_addr(&srv->subs[i], addr)) return true; /* already subscribed */
    }
    if (srv->subs_len == srv->subs_cap) {
        size_t new_cap = (srv->subs_cap == 0) ? 4 : srv->subs_cap * 2;
        struct sockaddr_in *grown =
            (struct sockaddr_in *)realloc(srv->subs, new_cap * sizeof(*grown));
        if (!grown) return false;
        srv->subs     = grown;
        srv->subs_cap = new_cap;
    }
    srv->subs[srv->subs_len++] = *addr;
    return true;
}

//cfusa:req REQ-UDP-018
static void zserv_subs_remove(rcp_udp_zone_server_t *srv, const struct sockaddr_in *addr)
{
    size_t i;
    for (i = 0; i < srv->subs_len; i++) {
        if (same_addr(&srv->subs[i], addr)) {
            srv->subs[i] = srv->subs[srv->subs_len - 1];
            srv->subs_len--;
            return;
        }
    }
}

//cfusa:req REQ-CTRL-001
//cfusa:req REQ-CTRL-002
//cfusa:req REQ-CTRL-016
static void zserv_serve_loop(void *arg)
{
    rcp_udp_zone_server_t *srv = (rcp_udp_zone_server_t *)arg;
    size_t buf_cap = RCP_WIRE_HEADER_LEN + RCP_WIRE_MAX_PAYLOAD;
    uint8_t *buf = (uint8_t *)malloc(buf_cap);
    if (!buf) return;

    for (;;) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        ssize_t n = recvfrom(srv->fd, buf, buf_cap, 0, (struct sockaddr *)&client, &clen);
        if (n <= 0) break;
        if ((size_t)n < RCP_WIRE_HEADER_LEN) continue;

        switch (buf[3]) {
        case RCP_WIRE_TYPE_COMMAND: {
            rcp_command_t cmd;
            rcp_response_t resp;
            rcp_bytes_t frame;
            if (rcp_wire_decode_command(buf, (size_t)n, &cmd) != RCP_WIRE_OK) break;

            memset(&resp, 0, sizeof(resp));
            rcp_mutex_lock(&srv->mu);
            if (srv->handler) {
                srv->handler(&cmd, &resp, srv->user_data);
            } else {
                resp.command_id = cmd.id;
                resp.zone       = srv->zone;
                resp.status     = RCP_RESPONSE_OK;
            }
            rcp_mutex_unlock(&srv->mu);
            rcp_bytes_free(&cmd.payload);

            frame = rcp_wire_encode_response(&resp);
            (void)sendto(srv->fd, frame.data, frame.len, 0, (struct sockaddr *)&client, clen);
            rcp_bytes_free(&frame);
            rcp_response_free(&resp);
            break;
        }
        case RCP_WIRE_TYPE_SUBSCRIBE:
            rcp_mutex_lock(&srv->mu);
            (void)zserv_subs_append(srv, &client);
            rcp_mutex_unlock(&srv->mu);
            break;
        case RCP_WIRE_TYPE_UNSUBSCRIBE:
            rcp_mutex_lock(&srv->mu);
            zserv_subs_remove(srv, &client);
            rcp_mutex_unlock(&srv->mu);
            break;
        default:
            break;
        }
    }
    free(buf);
}

rcp_udp_zone_server_t *rcp_udp_zone_server_new(rcp_zone_t zone, const char *addr, uint16_t port)
{
    rcp_udp_zone_server_t *srv = (rcp_udp_zone_server_t *)calloc(1, sizeof(*srv));
    struct sockaddr_in sa;
    if (!srv) return NULL;

    srv->zone = zone;
    srv->fd   = -1;
    rcp_mutex_init(&srv->mu);

    srv->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (srv->fd < 0) return srv; /* rcp_udp_zone_server_ok() == false */

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    if (!addr || addr[0] == '\0') {
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        close(srv->fd);
        srv->fd = -1;
        return srv;
    }
    if (bind(srv->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(srv->fd);
        srv->fd = -1;
        return srv;
    }

    srv->healthy = true;
    if (rcp_thread_start(&srv->serve_thread, zserv_serve_loop, srv) == 0) {
        srv->serve_thread_started = true;
    }
    return srv;
}

bool rcp_udp_zone_server_ok(const rcp_udp_zone_server_t *srv)
{
    return srv->fd >= 0;
}

//cfusa:req REQ-UDP-016
size_t rcp_udp_zone_server_addr_string(const rcp_udp_zone_server_t *srv, char *buf, size_t buf_len)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    char ipbuf[INET_ADDRSTRLEN];
    int n;

    if (srv->fd < 0 || buf_len == 0) return 0;
    if (getsockname(srv->fd, (struct sockaddr *)&sa, &len) < 0) return 0;
    if (!inet_ntop(AF_INET, &sa.sin_addr, ipbuf, sizeof(ipbuf))) return 0;

    n = snprintf(buf, buf_len, "%s:%u", ipbuf, (unsigned)ntohs(sa.sin_port));
    if (n < 0) return 0;
    return ((size_t)n < buf_len) ? (size_t)n : buf_len - 1;
}

uint16_t rcp_udp_zone_server_port(const rcp_udp_zone_server_t *srv)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (srv->fd < 0 || getsockname(srv->fd, (struct sockaddr *)&sa, &len) < 0) return 0;
    return ntohs(sa.sin_port);
}

void rcp_udp_zone_server_set_handler(rcp_udp_zone_server_t *srv, rcp_udp_handler_fn handler, void *user_data)
{
    rcp_mutex_lock(&srv->mu);
    srv->handler   = handler;
    srv->user_data = user_data;
    rcp_mutex_unlock(&srv->mu);
}

//cfusa:req REQ-UDP-017
void rcp_udp_zone_server_set_healthy(rcp_udp_zone_server_t *srv, bool healthy)
{
    rcp_mutex_lock(&srv->mu);
    srv->healthy = healthy;
    rcp_mutex_unlock(&srv->mu);
}

//cfusa:req REQ-CTRL-006
//cfusa:req REQ-CTRL-010
//cfusa:req REQ-CTRL-020
//cfusa:req REQ-CTRL-021
//cfusa:req REQ-CTRL-022
//cfusa:req REQ-CTRL-027
void rcp_udp_zone_server_publish(rcp_udp_zone_server_t *srv, const uint8_t *payload, size_t len)
{
    rcp_status_t st;
    rcp_bytes_t frame;
    struct sockaddr_in *snapshot = NULL;
    size_t snapshot_len = 0;
    size_t i;

    memset(&st, 0, sizeof(st));

    rcp_mutex_lock(&srv->mu);
    srv->seq++;
    st.zone         = srv->zone;
    st.seq          = srv->seq;
    st.healthy      = srv->healthy;
    st.payload.data = (uint8_t *)(uintptr_t)payload; /* read-only: encode copies it below */
    st.payload.len  = len;

    if (srv->subs_len > 0) {
        size_t n = srv->subs_len;
        snapshot = (struct sockaddr_in *)malloc(n * sizeof(*snapshot));
        if (snapshot) {
            memcpy(snapshot, srv->subs, n * sizeof(*snapshot));
            snapshot_len = n;
        }
    }
    rcp_mutex_unlock(&srv->mu);

    frame = rcp_wire_encode_status(&st);
    for (i = 0; i < snapshot_len; i++) {
        (void)sendto(srv->fd, frame.data, frame.len, 0,
                      (struct sockaddr *)&snapshot[i], sizeof(snapshot[i]));
    }
    rcp_bytes_free(&frame);
    free(snapshot);
}

void rcp_udp_zone_server_close(rcp_udp_zone_server_t *srv)
{
    bool was_open;

    rcp_mutex_lock(&srv->mu);
    was_open = !srv->closed;
    if (was_open) srv->closed = true;
    rcp_mutex_unlock(&srv->mu);

    if (!was_open) return;

    if (srv->fd >= 0) {
        shutdown(srv->fd, SHUT_RDWR);
        close(srv->fd);
        srv->fd = -1;
    }
    if (srv->serve_thread_started) {
        rcp_thread_join(srv->serve_thread);
        srv->serve_thread_started = false;
    }
}

void rcp_udp_zone_server_destroy(rcp_udp_zone_server_t *srv)
{
    if (!srv) return;
    rcp_udp_zone_server_close(srv);
    rcp_mutex_destroy(&srv->mu);
    free(srv->subs);
    free(srv);
}

/* ── Controller ────────────────────────────────────────────────────────────── */

/* A single in-flight send() call's rendezvous point with the read thread.
 * Refcounted (starts at 2: one ref for send()'s local pointer, one for its
 * membership in the controller's pending[] list) so neither side ever frees
 * it while the other might still be touching it — see the long comment in
 * udp_controller_send() for the exact handoff protocol this implements. */
typedef struct {
    uint32_t       id;
    rcp_mutex_t    mu;
    rcp_cond_t     cv;
    bool           ready;
    bool           cancelled;
    bool           consumed; /* true once send() has copied resp into its caller's *out */
    rcp_response_t resp;
    int            refcount;
} udp_pending_t;

static udp_pending_t *pending_new(uint32_t id)
{
    udp_pending_t *p = (udp_pending_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->id       = id;
    p->refcount = 2;
    rcp_mutex_init(&p->mu);
    rcp_cond_init(&p->cv);
    return p;
}

static void pending_release(udp_pending_t *p)
{
    if (!p) return;
    if (rcp_atomic_dec(&p->refcount) != 0) return;
    if (p->ready && !p->consumed) rcp_response_free(&p->resp);
    rcp_mutex_destroy(&p->mu);
    rcp_cond_destroy(&p->cv);
    free(p);
}

typedef struct rcp_udp_controller {
    rcp_controller_t       base;
    rcp_zone_t              zone;
    int                     fd;
    bool                    closed;
    uint32_t                next_id;
    rcp_mutex_t             mu; /* protects closed/next_id/pending[]/subs[] */
    udp_pending_t         **pending;
    size_t                  pending_len;
    size_t                  pending_cap;
    rcp_status_channel_t  **subs;
    size_t                  subs_len;
    size_t                  subs_cap;
    rcp_thread_t            read_thread;
    bool                    read_thread_started;
} rcp_udp_controller_t;

static bool pending_list_append(rcp_udp_controller_t *uc, udp_pending_t *p)
{
    if (uc->pending_len == uc->pending_cap) {
        size_t new_cap = (uc->pending_cap == 0) ? 8 : uc->pending_cap * 2;
        udp_pending_t **grown =
            (udp_pending_t **)realloc(uc->pending, new_cap * sizeof(*grown));
        if (!grown) return false;
        uc->pending     = grown;
        uc->pending_cap = new_cap;
    }
    uc->pending[uc->pending_len++] = p;
    return true;
}

/* Removes and returns the pending entry with the given id (caller must hold
 * uc->mu), or NULL if no entry with that id is currently in the list. */
static udp_pending_t *pending_list_take(rcp_udp_controller_t *uc, uint32_t id)
{
    size_t i;
    for (i = 0; i < uc->pending_len; i++) {
        if (uc->pending[i]->id == id) {
            udp_pending_t *p = uc->pending[i];
            uc->pending[i] = uc->pending[uc->pending_len - 1];
            uc->pending_len--;
            return p;
        }
    }
    return NULL;
}

static bool subs_append(rcp_udp_controller_t *uc, rcp_status_channel_t *ch)
{
    if (uc->subs_len == uc->subs_cap) {
        size_t new_cap = (uc->subs_cap == 0) ? 4 : uc->subs_cap * 2;
        rcp_status_channel_t **grown =
            (rcp_status_channel_t **)realloc(uc->subs, new_cap * sizeof(*grown));
        if (!grown) return false;
        uc->subs     = grown;
        uc->subs_cap = new_cap;
    }
    uc->subs[uc->subs_len++] = ch;
    return true;
}

//cfusa:req REQ-UDP-018
static void subs_remove(rcp_udp_controller_t *uc, rcp_status_channel_t *ch)
{
    size_t i;
    for (i = 0; i < uc->subs_len; i++) {
        if (uc->subs[i] == ch) {
            uc->subs[i] = uc->subs[uc->subs_len - 1];
            uc->subs_len--;
            return;
        }
    }
}

//cfusa:req REQ-UDP-015
static rcp_zone_t udp_controller_zone(rcp_controller_t *self)
{
    return ((rcp_udp_controller_t *)self)->zone;
}

/*
 * send() / the read thread rendezvous on a per-request udp_pending_t via two
 * independent races, both resolved under uc->mu:
 *   1. The read thread may decode a Response for this id and call
 *      pending_list_take() to claim it, then fill in resp/ready and signal.
 *   2. send() itself may give up (ctx deadline, or the controller closing)
 *      and call pending_list_take() to claim it first instead.
 * Whichever side's pending_list_take() call actually finds the entry (the
 * other finds nothing — removed already) owns releasing the "list"
 * reference, exactly once; send() additionally always releases its own
 * local reference once at the end. That accounts for exactly the 2
 * references pending_new() starts with, regardless of which side wins.
 */
//cfusa:req REQ-CTRL-001
//cfusa:req REQ-CTRL-002
//cfusa:req REQ-CTRL-003
//cfusa:req REQ-CTRL-004
//cfusa:req REQ-CTRL-013
//cfusa:req REQ-CTRL-014
//cfusa:req REQ-CTRL-015
//cfusa:req REQ-CTRL-016
//cfusa:req REQ-CTRL-018
//cfusa:req REQ-CTRL-023
//cfusa:req REQ-CTRL-024
//cfusa:req REQ-CTRL-025
//cfusa:req REQ-CTRL-026
//cfusa:req REQ-RESP-001
//cfusa:req REQ-RESP-002
//cfusa:req REQ-RESP-003
static int udp_controller_send(rcp_controller_t *self, const rcp_context_t *ctx,
                                const rcp_command_t *cmd, rcp_response_t *out)
{
    rcp_udp_controller_t *uc = (rcp_udp_controller_t *)self;
    rcp_command_t safe;
    rcp_bytes_t frame;
    udp_pending_t *p;
    uint32_t id;
    bool was_ready, was_cancelled;

    rcp_mutex_lock(&uc->mu);
    if (uc->closed) {
        rcp_mutex_unlock(&uc->mu);
        return RCP_ERR_CLOSED;
    }
    if (rcp_context_done(ctx)) {
        rcp_mutex_unlock(&uc->mu);
        return RCP_ERR_TIMEOUT;
    }
    if (cmd->zone != uc->zone) {
        rcp_mutex_unlock(&uc->mu);
        return RCP_ERR_ZONE_MISMATCH;
    }

    uc->next_id++;
    id = uc->next_id;

    p = pending_new(id);
    if (!p) {
        rcp_mutex_unlock(&uc->mu);
        return RCP_ERR_BUSY;
    }
    if (!pending_list_append(uc, p)) {
        rcp_mutex_unlock(&uc->mu);
        pending_release(p); /* drop the "list" ref we failed to establish */
        pending_release(p); /* drop our local ref too: never handed off */
        return RCP_ERR_BUSY;
    }
    rcp_mutex_unlock(&uc->mu);

    safe    = *cmd;
    safe.id = id;
    frame   = rcp_wire_encode_command(&safe);

    if (send(uc->fd, frame.data, frame.len, 0) < 0) {
        rcp_bytes_free(&frame);
        rcp_mutex_lock(&uc->mu);
        (void)pending_list_take(uc, id);
        rcp_mutex_unlock(&uc->mu);
        pending_release(p); /* the list reference we just took back */
        pending_release(p); /* our own local reference */
        return RCP_ERR_CLOSED;
    }
    rcp_bytes_free(&frame);

    rcp_mutex_lock(&p->mu);
    while (!p->ready && !p->cancelled) {
        if (ctx->has_deadline) {
            if (!rcp_cond_timedwait_until(&p->cv, &p->mu, ctx->deadline_ms)) break;
        } else {
            rcp_cond_wait(&p->cv, &p->mu);
        }
    }
    was_ready     = p->ready;
    was_cancelled = p->cancelled;
    if (was_ready) {
        *out        = p->resp;
        p->consumed = true;
    }
    rcp_mutex_unlock(&p->mu);

    rcp_mutex_lock(&uc->mu);
    {
        udp_pending_t *taken = pending_list_take(uc, id);
        rcp_mutex_unlock(&uc->mu);
        if (taken) pending_release(taken); /* we claimed the "list" reference */
    }
    pending_release(p); /* our own local reference, always */

    if (was_ready) return RCP_OK;
    if (was_cancelled) return RCP_ERR_CLOSED;
    return RCP_ERR_TIMEOUT;
}

typedef struct {
    rcp_udp_controller_t *uc; /* retained */
    rcp_status_channel_t  *ch; /* retained */
    rcp_context_t          ctx;
} udp_watcher_args_t;

static void udp_watcher_thread_fn(void *arg)
{
    udp_watcher_args_t *w = (udp_watcher_args_t *)arg;

    for (;;) {
        bool closed_now;
        rcp_mutex_lock(&w->uc->mu);
        closed_now = w->uc->closed;
        rcp_mutex_unlock(&w->uc->mu);

        if (closed_now) break;
        if (rcp_status_channel_is_closed(w->ch)) break;
        if (rcp_context_done(&w->ctx)) break;

        rcp_sleep_ms(1);
    }

    rcp_mutex_lock(&w->uc->mu);
    if (!w->uc->closed) {
        rcp_bytes_t frame;
        subs_remove(w->uc, w->ch);
        rcp_status_channel_release(w->ch); /* the subs list's reference */
        frame = rcp_wire_encode_control(RCP_WIRE_TYPE_UNSUBSCRIBE, w->uc->zone);
        (void)send(w->uc->fd, frame.data, frame.len, 0);
        rcp_bytes_free(&frame);
    }
    rcp_mutex_unlock(&w->uc->mu);

    rcp_status_channel_close(w->ch);
    rcp_status_channel_release(w->ch); /* this watcher's own reference */
    rcp_controller_release(&w->uc->base);
    free(w);
}

//cfusa:req REQ-CTRL-007
//cfusa:req REQ-CTRL-008
//cfusa:req REQ-CTRL-011
static int udp_controller_subscribe(rcp_controller_t *self, const rcp_context_t *ctx,
                                     rcp_status_channel_t **out)
{
    rcp_udp_controller_t *uc = (rcp_udp_controller_t *)self;
    rcp_status_channel_t *ch;
    rcp_bytes_t frame;
    udp_watcher_args_t *w;

    rcp_mutex_lock(&uc->mu);
    if (uc->closed) {
        rcp_mutex_unlock(&uc->mu);
        return RCP_ERR_CLOSED;
    }
    ch = rcp_status_channel_new(16);
    if (!ch) {
        rcp_mutex_unlock(&uc->mu);
        return RCP_ERR_BUSY;
    }
    if (!subs_append(uc, ch)) {
        rcp_mutex_unlock(&uc->mu);
        rcp_status_channel_release(ch);
        return RCP_ERR_BUSY;
    }
    rcp_mutex_unlock(&uc->mu);

    frame = rcp_wire_encode_control(RCP_WIRE_TYPE_SUBSCRIBE, uc->zone);
    (void)send(uc->fd, frame.data, frame.len, 0);
    rcp_bytes_free(&frame);

    *out = rcp_status_channel_retain(ch);

    w = (udp_watcher_args_t *)malloc(sizeof(*w));
    if (w) {
        w->uc  = (rcp_udp_controller_t *)rcp_controller_retain(&uc->base);
        w->ch  = rcp_status_channel_retain(ch);
        w->ctx = *ctx;
        if (rcp_thread_start_detached(udp_watcher_thread_fn, w) != 0) {
            rcp_controller_release(&w->uc->base);
            rcp_status_channel_release(w->ch);
            free(w);
        }
    }
    return RCP_OK;
}

static void udp_read_loop(void *arg)
{
    rcp_udp_controller_t *uc = (rcp_udp_controller_t *)arg;
    size_t buf_cap = RCP_WIRE_HEADER_LEN + RCP_WIRE_MAX_PAYLOAD;
    uint8_t *buf = (uint8_t *)malloc(buf_cap);
    if (!buf) return;

    for (;;) {
        ssize_t n = recv(uc->fd, buf, buf_cap, 0);
        if (n <= 0) break;
        if ((size_t)n < RCP_WIRE_HEADER_LEN) continue;

        switch (buf[3]) {
        case RCP_WIRE_TYPE_RESPONSE: {
            rcp_response_t resp;
            udp_pending_t *p;
            if (rcp_wire_decode_response(buf, (size_t)n, &resp) != RCP_WIRE_OK) break;

            rcp_mutex_lock(&uc->mu);
            p = pending_list_take(uc, resp.command_id);
            rcp_mutex_unlock(&uc->mu);

            if (p) {
                rcp_mutex_lock(&p->mu);
                p->resp  = resp; /* ownership transfers to p->resp */
                p->ready = true;
                rcp_cond_signal(&p->cv);
                rcp_mutex_unlock(&p->mu);
                pending_release(p); /* the "list" reference we just took */
            } else {
                rcp_response_free(&resp); /* no waiter left (timed out/cancelled) */
            }
            break;
        }
        case RCP_WIRE_TYPE_STATUS: {
            rcp_status_t st;
            rcp_status_channel_t **snapshot = NULL;
            size_t snapshot_len = 0;
            size_t i;
            if (rcp_wire_decode_status(buf, (size_t)n, &st) != RCP_WIRE_OK) break;

            rcp_mutex_lock(&uc->mu);
            if (uc->subs_len > 0) {
                size_t cnt = uc->subs_len;
                snapshot = (rcp_status_channel_t **)malloc(cnt * sizeof(*snapshot));
                if (snapshot) {
                    for (i = 0; i < cnt; i++) snapshot[i] = rcp_status_channel_retain(uc->subs[i]);
                    snapshot_len = cnt;
                }
            }
            rcp_mutex_unlock(&uc->mu);

            for (i = 0; i < snapshot_len; i++) {
                rcp_status_channel_push(snapshot[i], &st);
                rcp_status_channel_release(snapshot[i]);
            }
            free(snapshot);
            rcp_status_free(&st);
            break;
        }
        default:
            break;
        }
    }
    free(buf);
}

static int udp_controller_close(rcp_controller_t *self)
{
    rcp_udp_controller_t *uc = (rcp_udp_controller_t *)self;
    bool was_open;
    udp_pending_t **local_pending = NULL;
    size_t local_pending_len = 0;
    rcp_status_channel_t **local_subs = NULL;
    size_t local_subs_len = 0;
    size_t i;

    rcp_mutex_lock(&uc->mu);
    was_open = !uc->closed;
    if (was_open) {
        uc->closed          = true;
        local_pending        = uc->pending;
        local_pending_len    = uc->pending_len;
        uc->pending          = NULL;
        uc->pending_len      = 0;
        uc->pending_cap      = 0;
        local_subs           = uc->subs;
        local_subs_len       = uc->subs_len;
        uc->subs             = NULL;
        uc->subs_len         = 0;
        uc->subs_cap         = 0;
    }
    rcp_mutex_unlock(&uc->mu);

    if (!was_open) return RCP_OK;

    if (uc->fd >= 0) {
        shutdown(uc->fd, SHUT_RDWR);
        close(uc->fd);
        uc->fd = -1;
    }
    if (uc->read_thread_started) {
        rcp_thread_join(uc->read_thread);
        uc->read_thread_started = false;
    }

    for (i = 0; i < local_pending_len; i++) {
        udp_pending_t *p = local_pending[i];
        rcp_mutex_lock(&p->mu);
        p->cancelled = true;
        rcp_cond_broadcast(&p->cv);
        rcp_mutex_unlock(&p->mu);
        pending_release(p); /* the "list" reference this array held */
    }
    free(local_pending);

    for (i = 0; i < local_subs_len; i++) {
        rcp_status_channel_close(local_subs[i]);
        rcp_status_channel_release(local_subs[i]);
    }
    free(local_subs);

    return RCP_OK;
}

static void udp_controller_destroy(rcp_controller_t *self)
{
    rcp_udp_controller_t *uc = (rcp_udp_controller_t *)self;
    (void)udp_controller_close(self); /* idempotent; releases any remaining state */
    rcp_mutex_destroy(&uc->mu);
    free(uc->pending); /* NULL after close(); freeing NULL is a no-op */
    free(uc->subs);
    free(uc);
}

static const rcp_controller_vtable_t udp_controller_vtable = {
    udp_controller_zone,
    udp_controller_send,
    udp_controller_subscribe,
    udp_controller_close,
    udp_controller_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

rcp_controller_t *rcp_udp_controller_new(rcp_zone_t zone, const char *server_host, uint16_t server_port)
{
    rcp_udp_controller_t *uc = (rcp_udp_controller_t *)calloc(1, sizeof(*uc));
    struct sockaddr_in sa;
    if (!uc) return NULL;

    uc->base.vt       = &udp_controller_vtable;
    uc->base.refcount = 1;
    uc->zone          = zone;
    uc->fd            = -1;
    rcp_mutex_init(&uc->mu);

    uc->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (uc->fd < 0) return &uc->base;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(server_port);
    if (inet_pton(AF_INET, server_host, &sa.sin_addr) != 1) {
        close(uc->fd);
        uc->fd = -1;
        return &uc->base;
    }
    if (connect(uc->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(uc->fd);
        uc->fd = -1;
        return &uc->base;
    }

    if (rcp_thread_start(&uc->read_thread, udp_read_loop, uc) == 0) {
        uc->read_thread_started = true;
    }
    return &uc->base;
}

bool rcp_udp_controller_ok(rcp_controller_t *ctrl)
{
    return ((rcp_udp_controller_t *)ctrl)->fd >= 0;
}

/* ── Registry ──────────────────────────────────────────────────────────────── */

typedef struct {
    rcp_zone_t         zone;
    rcp_controller_t  *ctrl; /* registry holds one reference */
} udp_registry_entry_t;

typedef struct rcp_udp_registry {
    rcp_registry_t         base;
    rcp_mutex_t            mu;
    bool                   closed;
    udp_registry_entry_t  *entries;
    size_t                 len;
    size_t                 cap;
} rcp_udp_registry_t;

//cfusa:req REQ-REG-002
//cfusa:req REQ-REG-007
//cfusa:req REQ-REG-009
static int udp_registry_register(rcp_registry_t *self, rcp_controller_t *ctrl)
{
    rcp_udp_registry_t *ur = (rcp_udp_registry_t *)self;
    rcp_zone_t zone = rcp_controller_zone(ctrl);
    size_t i;

    rcp_mutex_lock(&ur->mu);
    if (ur->closed) {
        rcp_mutex_unlock(&ur->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < ur->len; i++) {
        if (ur->entries[i].zone == zone) {
            rcp_mutex_unlock(&ur->mu);
            return RCP_ERR_ALREADY_EXISTS;
        }
    }
    if (ur->len == ur->cap) {
        size_t new_cap = (ur->cap == 0) ? 8 : ur->cap * 2;
        udp_registry_entry_t *grown =
            (udp_registry_entry_t *)realloc(ur->entries, new_cap * sizeof(*grown));
        if (!grown) {
            rcp_mutex_unlock(&ur->mu);
            return RCP_ERR_BUSY;
        }
        ur->entries = grown;
        ur->cap     = new_cap;
    }
    ur->entries[ur->len].zone = zone;
    ur->entries[ur->len].ctrl = rcp_controller_retain(ctrl);
    ur->len++;
    rcp_mutex_unlock(&ur->mu);
    return RCP_OK;
}

//cfusa:req REQ-REG-003
//cfusa:req REQ-REG-008
//cfusa:req REQ-REG-012
static int udp_registry_deregister(rcp_registry_t *self, rcp_zone_t zone)
{
    rcp_udp_registry_t *ur = (rcp_udp_registry_t *)self;
    rcp_controller_t *found = NULL;
    size_t i;

    rcp_mutex_lock(&ur->mu);
    for (i = 0; i < ur->len; i++) {
        if (ur->entries[i].zone == zone) {
            found = ur->entries[i].ctrl;
            ur->entries[i] = ur->entries[ur->len - 1];
            ur->len--;
            break;
        }
    }
    rcp_mutex_unlock(&ur->mu);

    if (!found) return RCP_ERR_NOT_FOUND;

    rcp_controller_close(found);
    rcp_controller_release(found);
    return RCP_OK;
}

//cfusa:req REQ-REG-004
//cfusa:req REQ-REG-011
//cfusa:req REQ-REG-013
static int udp_registry_lookup(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out)
{
    rcp_udp_registry_t *ur = (rcp_udp_registry_t *)self;
    size_t i;

    rcp_mutex_lock(&ur->mu);
    if (ur->closed) {
        rcp_mutex_unlock(&ur->mu);
        return RCP_ERR_CLOSED;
    }
    for (i = 0; i < ur->len; i++) {
        if (ur->entries[i].zone == zone) {
            *out = rcp_controller_retain(ur->entries[i].ctrl);
            rcp_mutex_unlock(&ur->mu);
            return RCP_OK;
        }
    }
    rcp_mutex_unlock(&ur->mu);
    return RCP_ERR_NOT_FOUND;
}

//cfusa:req REQ-REG-006
static size_t udp_registry_controllers(rcp_registry_t *self, rcp_controller_t **out, size_t cap)
{
    rcp_udp_registry_t *ur = (rcp_udp_registry_t *)self;
    size_t i, n;

    rcp_mutex_lock(&ur->mu);
    n = ur->len;
    for (i = 0; i < n && i < cap; i++) {
        out[i] = rcp_controller_retain(ur->entries[i].ctrl);
    }
    rcp_mutex_unlock(&ur->mu);
    return n;
}

//cfusa:req REQ-REG-005
//cfusa:req REQ-REG-010
static int udp_registry_close(rcp_registry_t *self)
{
    rcp_udp_registry_t *ur = (rcp_udp_registry_t *)self;
    bool was_open;
    udp_registry_entry_t *local = NULL;
    size_t local_len = 0;
    size_t i;

    rcp_mutex_lock(&ur->mu);
    was_open = !ur->closed;
    if (was_open) {
        ur->closed  = true;
        local       = ur->entries;
        local_len   = ur->len;
        ur->entries = NULL;
        ur->len     = 0;
        ur->cap     = 0;
    }
    rcp_mutex_unlock(&ur->mu);

    if (!was_open) return RCP_OK;

    for (i = 0; i < local_len; i++) {
        rcp_controller_close(local[i].ctrl);
        rcp_controller_release(local[i].ctrl);
    }
    free(local);
    return RCP_OK;
}

static void udp_registry_destroy(rcp_registry_t *self)
{
    rcp_udp_registry_t *ur = (rcp_udp_registry_t *)self;
    (void)udp_registry_close(self);
    rcp_mutex_destroy(&ur->mu);
    free(ur->entries);
    free(ur);
}

static const rcp_registry_vtable_t udp_registry_vtable = {
    udp_registry_register,
    udp_registry_deregister,
    udp_registry_lookup,
    udp_registry_controllers,
    udp_registry_close,
    udp_registry_destroy,
};

//cfusa:req REQ-UDP-001
rcp_registry_t *rcp_udp_registry_new(void)
{
    rcp_udp_registry_t *ur = (rcp_udp_registry_t *)calloc(1, sizeof(*ur));
    if (!ur) return NULL;
    ur->base.vt = &udp_registry_vtable;
    rcp_mutex_init(&ur->mu);
    return &ur->base;
}

//cfusa:req REQ-UDP-019
int rcp_udp_registry_dial(rcp_registry_t *reg, rcp_zone_t zone, const char *server_host, uint16_t server_port)
{
    rcp_controller_t *ctrl = rcp_udp_controller_new(zone, server_host, server_port);
    int rc;

    if (!ctrl) return RCP_ERR_NOT_FOUND;
    if (!rcp_udp_controller_ok(ctrl)) {
        rcp_controller_release(ctrl);
        return RCP_ERR_NOT_FOUND;
    }
    rc = rcp_registry_register(reg, ctrl);
    rcp_controller_release(ctrl); /* register() took its own reference */
    return rc;
}

#else /* !RCP_UDP_POSIX — Windows stub: no BSD-sockets implementation yet */

struct rcp_udp_zone_server { int unused; };

rcp_udp_zone_server_t *rcp_udp_zone_server_new(rcp_zone_t zone, const char *addr, uint16_t port)
{
    (void)zone; (void)addr; (void)port;
    return (rcp_udp_zone_server_t *)calloc(1, sizeof(rcp_udp_zone_server_t));
}
bool rcp_udp_zone_server_ok(const rcp_udp_zone_server_t *srv) { (void)srv; return false; }
size_t rcp_udp_zone_server_addr_string(const rcp_udp_zone_server_t *srv, char *buf, size_t buf_len)
{ (void)srv; (void)buf; (void)buf_len; return 0; }
uint16_t rcp_udp_zone_server_port(const rcp_udp_zone_server_t *srv) { (void)srv; return 0; }
void rcp_udp_zone_server_set_handler(rcp_udp_zone_server_t *srv, rcp_udp_handler_fn handler, void *user_data)
{ (void)srv; (void)handler; (void)user_data; }
void rcp_udp_zone_server_set_healthy(rcp_udp_zone_server_t *srv, bool healthy) { (void)srv; (void)healthy; }
void rcp_udp_zone_server_publish(rcp_udp_zone_server_t *srv, const uint8_t *payload, size_t len)
{ (void)srv; (void)payload; (void)len; }
void rcp_udp_zone_server_close(rcp_udp_zone_server_t *srv) { (void)srv; }
void rcp_udp_zone_server_destroy(rcp_udp_zone_server_t *srv) { free(srv); }

typedef struct { rcp_controller_t base; rcp_zone_t zone; } udp_stub_controller_t;

static rcp_zone_t stub_zone(rcp_controller_t *self) { return ((udp_stub_controller_t *)self)->zone; }
static int stub_send(rcp_controller_t *self, const rcp_context_t *ctx, const rcp_command_t *cmd, rcp_response_t *out)
{ (void)self; (void)ctx; (void)cmd; (void)out; return RCP_ERR_CLOSED; }
static int stub_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{ (void)self; (void)ctx; (void)out; return RCP_ERR_CLOSED; }
static int stub_close(rcp_controller_t *self) { (void)self; return RCP_OK; }
static void stub_destroy(rcp_controller_t *self) { free(self); }

static const rcp_controller_vtable_t udp_stub_vtable = {
    stub_zone, stub_send, stub_subscribe, stub_close, stub_destroy, NULL, NULL,
};

rcp_controller_t *rcp_udp_controller_new(rcp_zone_t zone, const char *server_host, uint16_t server_port)
{
    udp_stub_controller_t *c;
    (void)server_host; (void)server_port;
    c = (udp_stub_controller_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->base.vt       = &udp_stub_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}

bool rcp_udp_controller_ok(rcp_controller_t *ctrl) { (void)ctrl; return false; }

typedef struct { rcp_registry_t base; } udp_stub_registry_t;

static int stub_reg_register(rcp_registry_t *self, rcp_controller_t *ctrl)
{ (void)self; (void)ctrl; return RCP_ERR_CLOSED; }
static int stub_reg_deregister(rcp_registry_t *self, rcp_zone_t zone)
{ (void)self; (void)zone; return RCP_ERR_CLOSED; }
static int stub_reg_lookup(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out)
{ (void)self; (void)zone; (void)out; return RCP_ERR_CLOSED; }
static size_t stub_reg_controllers(rcp_registry_t *self, rcp_controller_t **out, size_t cap)
{ (void)self; (void)out; (void)cap; return 0; }
static int stub_reg_close(rcp_registry_t *self) { (void)self; return RCP_OK; }
static void stub_reg_destroy(rcp_registry_t *self) { free(self); }

static const rcp_registry_vtable_t udp_stub_registry_vtable = {
    stub_reg_register, stub_reg_deregister, stub_reg_lookup,
    stub_reg_controllers, stub_reg_close, stub_reg_destroy,
};

rcp_registry_t *rcp_udp_registry_new(void)
{
    udp_stub_registry_t *r = (udp_stub_registry_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->base.vt = &udp_stub_registry_vtable;
    return &r->base;
}

int rcp_udp_registry_dial(rcp_registry_t *reg, rcp_zone_t zone, const char *server_host, uint16_t server_port)
{
    (void)reg; (void)zone; (void)server_host; (void)server_port;
    return RCP_ERR_NOT_FOUND;
}

#endif /* RCP_UDP_POSIX */
