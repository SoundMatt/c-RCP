#include "rcp/tls.h"

#include <stdlib.h>

//cfusa:req REQ-TLS-001
//cfusa:req REQ-TLS-002
//cfusa:req REQ-TLS-003
//cfusa:req REQ-TLS-004
rcp_tls_config_t rcp_tls_config_default(void)
{
    rcp_tls_config_t c;
    c.cert_file   = NULL;
    c.key_file    = NULL;
    c.ca_file     = NULL;
    c.verify_peer = true;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} tls_controller_t;

static rcp_zone_t tls_zone(rcp_controller_t *self)
{
    return ((tls_controller_t *)self)->zone;
}

//cfusa:req REQ-TLS-005
static int tls_send(rcp_controller_t *self, const rcp_context_t *ctx,
                     const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-TLS-006
static int tls_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-TLS-009
static int tls_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void tls_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t tls_controller_vtable = {
    tls_zone,
    tls_send,
    tls_subscribe,
    tls_close,
    tls_destroy,
};

rcp_controller_t *rcp_tls_controller_new(rcp_zone_t zone, const char *server_host,
                                          uint16_t server_port, rcp_tls_config_t config)
{
    tls_controller_t *c = (tls_controller_t *)calloc(1, sizeof(*c));
    (void)server_host; (void)server_port; (void)config;
    if (!c) return NULL;
    c->base.vt       = &tls_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}

/* ── ZoneServer stub ───────────────────────────────────────────────────────── */

struct rcp_tls_zone_server {
    int unused;
};

rcp_tls_zone_server_t *rcp_tls_zone_server_new(rcp_zone_t zone, const char *addr,
                                                uint16_t port, rcp_tls_config_t config)
{
    (void)zone; (void)addr; (void)port; (void)config;
    return (rcp_tls_zone_server_t *)calloc(1, sizeof(rcp_tls_zone_server_t));
}

bool rcp_tls_zone_server_ok(const rcp_tls_zone_server_t *srv)
{
    (void)srv;
    return false;
}

void rcp_tls_zone_server_set_handler(rcp_tls_zone_server_t *srv, rcp_tls_handler_fn handler, void *user_data)
{
    (void)srv; (void)handler; (void)user_data;
}

void rcp_tls_zone_server_set_healthy(rcp_tls_zone_server_t *srv, bool healthy)
{
    (void)srv; (void)healthy;
}

void rcp_tls_zone_server_publish(rcp_tls_zone_server_t *srv, const uint8_t *payload, size_t len)
{
    (void)srv; (void)payload; (void)len;
}

void rcp_tls_zone_server_close(rcp_tls_zone_server_t *srv)
{
    (void)srv;
}

void rcp_tls_zone_server_destroy(rcp_tls_zone_server_t *srv)
{
    free(srv);
}

/* ── Registry stub ─────────────────────────────────────────────────────────── */

typedef struct {
    rcp_registry_t base;
} tls_registry_t;

//cfusa:req REQ-TLS-010
static int tls_reg_register(rcp_registry_t *self, rcp_controller_t *ctrl)
{
    (void)self; (void)ctrl;
    return RCP_ERR_NOT_SUPPORTED;
}

static int tls_reg_deregister(rcp_registry_t *self, rcp_zone_t zone)
{
    (void)self; (void)zone;
    return RCP_ERR_NOT_SUPPORTED;
}

static int tls_reg_lookup(rcp_registry_t *self, rcp_zone_t zone, rcp_controller_t **out)
{
    (void)self; (void)zone; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

static size_t tls_reg_controllers(rcp_registry_t *self, rcp_controller_t **out, size_t cap)
{
    (void)self; (void)out; (void)cap;
    return 0;
}

static int tls_reg_close(rcp_registry_t *self)
{
    (void)self;
    return RCP_OK;
}

static void tls_reg_destroy(rcp_registry_t *self)
{
    free(self);
}

static const rcp_registry_vtable_t tls_registry_vtable = {
    tls_reg_register,
    tls_reg_deregister,
    tls_reg_lookup,
    tls_reg_controllers,
    tls_reg_close,
    tls_reg_destroy,
};

rcp_registry_t *rcp_tls_registry_new(void)
{
    tls_registry_t *r = (tls_registry_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->base.vt = &tls_registry_vtable;
    return &r->base;
}

//cfusa:req REQ-TLS-007
//cfusa:req REQ-TLS-008
int rcp_tls_registry_dial(rcp_registry_t *reg, rcp_zone_t zone, const char *server_host,
                           uint16_t server_port, rcp_tls_config_t config)
{
    (void)reg; (void)zone; (void)server_host; (void)server_port; (void)config;
    return RCP_ERR_NOT_SUPPORTED;
}
