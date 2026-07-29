#include "rcp/grpcbridge.h"

//cfusa:req REQ-GRPC-002
rcp_grpc_config_t rcp_grpc_default_config(void)
{
    rcp_grpc_config_t c;
    c.server_address = NULL;
    c.max_retries     = 3;
    c.rpc_timeout_ms  = 1000;
    return c;
}

//cfusa:req REQ-GRPC-001
int rcp_grpc_bridge_send(rcp_grpc_config_t cfg, rcp_avtp_addr_t addr,
                          uint8_t request_type,
                          const uint8_t *payload, size_t payload_len,
                          rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
