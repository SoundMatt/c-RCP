/*
 * MQTT protocol bridge interface stub (SG-006).
 *
 * This is a compile-time interface stub: no MQTT client library (e.g.
 * Eclipse Paho) is linked, so every RPC-facing call returns
 * RCP_ERR_NOT_SUPPORTED rather than publishing to an unconfigured broker
 * -- mirrors cpp-RCP's own mqttbr.hpp, which ships the same stub absent a
 * linked MQTT backend. Wiring in a real MQTT backend is future work (see
 * ROADMAP.md).
 */
#ifndef RCP_MQTTBR_H
#define RCP_MQTTBR_H

#include "rcp/rcp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *broker_url;   /* default: "tcp://localhost:1883" */
    const char *topic_prefix; /* default: "rcp" */
    int         qos;           /* default: 1 */
    uint64_t    timeout_ms;    /* default: 1000 */
} rcp_mqtt_config_t;

/* { broker_url = "tcp://localhost:1883", topic_prefix = "rcp", qos = 1,
 * timeout_ms = 1000 }. */
rcp_mqtt_config_t rcp_mqtt_default_config(void);

/* Bridges RCP commands for zone to MQTT topics described by cfg. Every
 * operation on the returned controller currently returns
 * RCP_ERR_NOT_SUPPORTED (no backend). Returned with refcount 1; release
 * with rcp_controller_release(). */
rcp_controller_t *rcp_mqtt_controller_new(rcp_zone_t zone, rcp_mqtt_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_MQTTBR_H */
