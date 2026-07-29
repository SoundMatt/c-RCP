#include "rcp/mqttbr.h"

//cfusa:req REQ-MQTT-002
rcp_mqtt_config_t rcp_mqtt_default_config(void)
{
    rcp_mqtt_config_t c;
    c.broker_url   = "tcp://localhost:1883";
    c.topic_prefix = "rcp";
    c.qos          = 1;
    c.timeout_ms   = 1000;
    return c;
}

//cfusa:req REQ-MQTT-001
int rcp_mqtt_bridge_send(rcp_mqtt_config_t cfg, rcp_avtp_addr_t addr,
                         uint8_t request_type,
                         const uint8_t *payload, size_t payload_len,
                         rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
