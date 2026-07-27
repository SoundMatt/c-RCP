#include "rcp/mqttbr.h"

#include <stdlib.h>

rcp_mqtt_config_t rcp_mqtt_default_config(void)
{
    rcp_mqtt_config_t c;
    c.broker_url   = "tcp://localhost:1883";
    c.topic_prefix = "rcp";
    c.qos          = 1;
    c.timeout_ms   = 1000;
    return c;
}

/* ── Controller stub ───────────────────────────────────────────────────────── */

typedef struct {
    rcp_controller_t base;
    rcp_zone_t        zone;
} mqtt_controller_t;

static rcp_zone_t mqtt_ctrl_zone(rcp_controller_t *self)
{
    return ((mqtt_controller_t *)self)->zone;
}

//cfusa:req REQ-MQTT-001
static int mqtt_ctrl_send(rcp_controller_t *self, const rcp_context_t *ctx,
                           const rcp_command_t *cmd, rcp_response_t *out)
{
    (void)self; (void)ctx; (void)cmd; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-MQTT-003
static int mqtt_ctrl_subscribe(rcp_controller_t *self, const rcp_context_t *ctx, rcp_status_channel_t **out)
{
    (void)self; (void)ctx; (void)out;
    return RCP_ERR_NOT_SUPPORTED;
}

//cfusa:req REQ-MQTT-004
static int mqtt_ctrl_close(rcp_controller_t *self)
{
    (void)self;
    return RCP_OK;
}

static void mqtt_ctrl_destroy(rcp_controller_t *self)
{
    free(self);
}

static const rcp_controller_vtable_t mqtt_controller_vtable = {
    mqtt_ctrl_zone,
    mqtt_ctrl_send,
    mqtt_ctrl_subscribe,
    mqtt_ctrl_close,
    mqtt_ctrl_destroy,
    NULL, /* loan: not supported */
    NULL, /* send_loaned: not supported */
};

//cfusa:req REQ-MQTT-002
rcp_controller_t *rcp_mqtt_controller_new(rcp_zone_t zone, rcp_mqtt_config_t cfg)
{
    mqtt_controller_t *c = (mqtt_controller_t *)calloc(1, sizeof(*c));
    (void)cfg;
    if (!c) return NULL;
    c->base.vt       = &mqtt_controller_vtable;
    c->base.refcount = 1;
    c->zone          = zone;
    return &c->base;
}
