#include "rcp/adapt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

/* ── Meta parsers (§15.7.5) ────────────────────────────────────────────────── */

static rcp_priority_t priority_from_meta(const relay_message_t *msg)
{
    const char *v = relay_message_get_meta(msg, "rcp.priority");
    if (!v) return RCP_PRIORITY_NORMAL;
    if (strcmp(v, "high") == 0)     return RCP_PRIORITY_HIGH;
    if (strcmp(v, "critical") == 0) return RCP_PRIORITY_CRITICAL;
    return RCP_PRIORITY_NORMAL;
}

static rcp_command_type_t cmd_type_from_meta(const relay_message_t *msg)
{
    const char *v = relay_message_get_meta(msg, "rcp.cmd_type");
    if (!v) return RCP_CMD_NOOP;
    if (strcmp(v, "set") == 0)      return RCP_CMD_SET;
    if (strcmp(v, "get") == 0)      return RCP_CMD_GET;
    if (strcmp(v, "reset") == 0)    return RCP_CMD_RESET;
    if (strcmp(v, "watchdog") == 0) return RCP_CMD_WATCHDOG;
    if (strcmp(v, "sleep") == 0)    return RCP_CMD_SLEEP;
    if (strcmp(v, "wake") == 0)     return RCP_CMD_WAKE;
    return RCP_CMD_NOOP;
}

/* ── ToMessage / FromMessage (§15.7.5) ─────────────────────────────────────── */

//cfusa:req REQ-RELAY-005
relay_message_t rcp_status_to_message(const rcp_status_t *s)
{
    relay_message_t msg;

    relay_message_init(&msg);
    msg.protocol     = RELAY_PROTOCOL_RCP;
    msg.timestamp_ms = rcp_wallclock_ms();
    msg.seq          = s->seq;
    relay_message_set_id(&msg, rcp_zone_string(s->zone));
    msg.payload = relay_bytes_dup(s->payload.data, s->payload.len);
    relay_message_set_meta(&msg, "rcp.healthy", s->healthy ? "true" : "false");
    return msg;
}

//cfusa:req REQ-RELAY-006
relay_message_t rcp_response_to_message(const rcp_response_t *r)
{
    relay_message_t msg;
    char status_buf[16];

    relay_message_init(&msg);
    msg.protocol     = RELAY_PROTOCOL_RCP;
    msg.timestamp_ms = rcp_wallclock_ms();
    relay_message_set_id(&msg, rcp_zone_string(r->zone));
    msg.payload = relay_bytes_dup(r->payload.data, r->payload.len);
    snprintf(status_buf, sizeof(status_buf), "%d", (int)r->status);
    relay_message_set_meta(&msg, "rcp.status", status_buf);
    return msg;
}

//cfusa:req REQ-RELAY-007
rcp_command_t rcp_message_to_command(const relay_message_t *msg)
{
    rcp_command_t cmd;

    cmd.id       = 0;
    cmd.zone     = rcp_zone_from_string(msg->id);
    cmd.type     = cmd_type_from_meta(msg);
    cmd.priority = priority_from_meta(msg);
    cmd.payload.data = msg->payload.data;
    cmd.payload.len  = msg->payload.len;
    return cmd;
}

/* ── RcpCallerAdapter — implements rcp_relay_caller_t over rcp_controller_t ── */

typedef struct {
    rcp_relay_caller_t base;
    rcp_controller_t  *ctrl; /* retained */
} rcp_adapter_t;

static relay_protocol_t adapter_protocol(rcp_relay_caller_t *self)
{
    (void)self;
    return RELAY_PROTOCOL_RCP;
}

/* send maps relay_message_t -> rcp_command_t, discards the response (§10.6). */
//cfusa:req REQ-RELAY-008
static int adapter_send(rcp_relay_caller_t *self, const relay_context_t *ctx,
                         const relay_message_t *msg)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    rcp_command_t cmd = rcp_message_to_command(msg);
    rcp_response_t resp = {0};
    int ec;

    if (!a->ctrl) return RCP_ERR_CLOSED;
    ec = rcp_controller_send(a->ctrl, ctx, &cmd, &resp);
    rcp_response_free(&resp);
    return ec;
}

/* call maps relay_message_t -> rcp_command_t -> relay_message_t (§10.2). */
//cfusa:req REQ-RELAY-009
static int adapter_call(rcp_relay_caller_t *self, const relay_context_t *ctx,
                         const relay_message_t *req, relay_message_t *out)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    rcp_command_t cmd = rcp_message_to_command(req);
    rcp_response_t resp = {0};
    int ec;

    if (!a->ctrl) return RCP_ERR_CLOSED;
    ec = rcp_controller_send(a->ctrl, ctx, &cmd, &resp);
    if (ec != RCP_OK) return ec;
    *out = rcp_response_to_message(&resp);
    rcp_response_free(&resp);
    return RCP_OK;
}

typedef struct {
    rcp_status_channel_t    *status_ch; /* retained; released by the thread */
    relay_message_channel_t *out;       /* retained; released by the thread */
    relay_backpressure_t     bp;
} subscribe_thread_ctx_t;

/* Runs in its own detached thread per subscription (§10.5): reads Status
 * updates from status_ch and pushes the mapped Message onto out until
 * status_ch closes, then closes out and releases both channel references. */
static void subscribe_thread_fn(void *arg)
{
    subscribe_thread_ctx_t *tctx = (subscribe_thread_ctx_t *)arg;
    rcp_status_t st;

    while (rcp_status_channel_recv(tctx->status_ch, &st)) {
        relay_message_t msg = rcp_status_to_message(&st);
        rcp_status_free(&st);

        switch (tctx->bp) {
        case RELAY_BACKPRESSURE_DROP_NEWEST:
            relay_message_channel_push(tctx->out, &msg);
            break;
        case RELAY_BACKPRESSURE_DROP_OLDEST:
            if (!relay_message_channel_push(tctx->out, &msg)) {
                relay_message_t discard;
                if (relay_message_channel_try_recv(tctx->out, &discard)) {
                    relay_message_free(&discard);
                }
                relay_message_channel_push(tctx->out, &msg);
            }
            break;
        case RELAY_BACKPRESSURE_BLOCK:
            while (!relay_message_channel_push(tctx->out, &msg) &&
                   !relay_message_channel_is_closed(tctx->out)) {
                rcp_sleep_ms(1);
            }
            break;
        }
        /* relay_message_channel_push() deep-copies; our copy is ours either
         * way, whether it was accepted, dropped, or the channel had closed
         * out from under us. */
        relay_message_free(&msg);
    }

    relay_message_channel_close(tctx->out);
    rcp_status_channel_release(tctx->status_ch);
    relay_message_channel_release(tctx->out);
    free(tctx);
}

/* subscribe wraps rcp_controller_subscribe, forwarding Status as
 * relay_message_t via a background thread per §10.5. */
//cfusa:req REQ-RELAY-010
//cfusa:req REQ-RELAY-011
static int adapter_subscribe(rcp_relay_caller_t *self, const relay_subscriber_options_t *opts,
                              relay_message_channel_t **out)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    rcp_context_t ctx = rcp_context_background();
    rcp_status_channel_t *status_ch = NULL;
    relay_message_channel_t *chan;
    subscribe_thread_ctx_t *tctx;
    int ec;

    if (!a->ctrl) return RCP_ERR_CLOSED;

    ec = rcp_controller_subscribe(a->ctrl, &ctx, &status_ch);
    if (ec != RCP_OK) return ec;

    chan = relay_message_channel_new(opts->channel_depth);
    if (!chan) {
        rcp_status_channel_release(status_ch);
        return RCP_ERR_BUSY;
    }

    tctx = (subscribe_thread_ctx_t *)malloc(sizeof(*tctx));
    if (!tctx) {
        relay_message_channel_release(chan);
        rcp_status_channel_release(status_ch);
        return RCP_ERR_BUSY;
    }
    tctx->status_ch = status_ch; /* transfers the reference from subscribe() above */
    tctx->out       = relay_message_channel_retain(chan);
    tctx->bp        = opts->back_pressure;

    if (rcp_thread_start_detached(subscribe_thread_fn, tctx) != 0) {
        relay_message_channel_release(tctx->out);
        rcp_status_channel_release(tctx->status_ch);
        free(tctx);
        relay_message_channel_release(chan);
        return RCP_ERR_BUSY;
    }

    *out = chan; /* caller's reference */
    return RCP_OK;
}

static int adapter_close(rcp_relay_caller_t *self)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    if (!a->ctrl) return RCP_OK;
    return rcp_controller_close(a->ctrl);
}

static void adapter_destroy(rcp_relay_caller_t *self)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    rcp_controller_release(a->ctrl);
    free(a);
}

static const rcp_relay_caller_vtable_t adapter_vtable = {
    adapter_protocol,
    adapter_send,
    adapter_call,
    adapter_subscribe,
    adapter_close,
    adapter_destroy,
};

/* ── Adapt() (§10.3) ──────────────────────────────────────────────────────── */

//cfusa:req REQ-RELAY-012
rcp_relay_caller_t *rcp_adapt(rcp_controller_t *ctrl)
{
    rcp_adapter_t *a = (rcp_adapter_t *)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->base.vt       = &adapter_vtable;
    a->base.refcount = 1;
    a->ctrl          = rcp_controller_retain(ctrl);
    return &a->base;
}
