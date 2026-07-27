#include "rcp/firmware.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

rcp_fw_config_t rcp_fw_default_config(void)
{
    rcp_fw_config_t c;
    c.chunk_size        = 4096;
    c.retries           = 3;
    c.chunk_timeout_ms  = 500;
    c.verify_timeout_ms = 5000;
    return c;
}

const char *rcp_fw_strerror(rcp_fw_errc_t e)
{
    switch (e) {
    case RCP_FW_ERR_BAD_STATE:       return "firmware: invalid session state";
    case RCP_FW_ERR_VERIFY_FAILED:   return "firmware: image verification failed";
    case RCP_FW_ERR_TRANSFER_ERROR:  return "firmware: chunk transfer error";
    case RCP_FW_ERR_ROLLBACK_FAILED: return "firmware: rollback failed";
    default:                         return "firmware: unknown error";
    }
}

struct rcp_firmware_session {
    rcp_controller_t *ctrl; /* retained */
    rcp_fw_config_t    cfg;
    rcp_mutex_t          mu; /* protects state */
    rcp_fw_state_t       state;
};

rcp_firmware_session_t *rcp_firmware_session_new(rcp_controller_t *ctrl, rcp_fw_config_t cfg)
{
    rcp_firmware_session_t *s = (rcp_firmware_session_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctrl  = rcp_controller_retain(ctrl);
    s->cfg   = cfg;
    s->state = RCP_FW_STATE_IDLE;
    rcp_mutex_init(&s->mu);
    return s;
}

//cfusa:req REQ-FW-001
rcp_fw_state_t rcp_firmware_session_state(rcp_firmware_session_t *s)
{
    rcp_fw_state_t st;
    rcp_mutex_lock(&s->mu);
    st = s->state;
    rcp_mutex_unlock(&s->mu);
    return st;
}

//cfusa:req REQ-FW-002
//cfusa:req REQ-FW-003
int rcp_firmware_session_initiate(rcp_firmware_session_t *s, const rcp_context_t *ctx, const char *version)
{
    rcp_command_t  cmd = {0};
    rcp_response_t resp = {0};
    size_t version_len = strlen(version);
    uint8_t *payload;
    int ec;

    rcp_mutex_lock(&s->mu);
    if (s->state != RCP_FW_STATE_IDLE) {
        rcp_mutex_unlock(&s->mu);
        return RCP_FW_ERR_BAD_STATE;
    }
    rcp_mutex_unlock(&s->mu);

    payload = (uint8_t *)malloc(1 + version_len);
    if (!payload) return RCP_ERR_BUSY;
    payload[0] = 0x01;
    if (version_len > 0) memcpy(payload + 1, version, version_len);

    cmd.zone         = rcp_controller_zone(s->ctrl);
    cmd.type         = RCP_CMD_UPDATE;
    cmd.priority     = RCP_PRIORITY_HIGH;
    cmd.payload.data = payload;
    cmd.payload.len  = 1 + version_len;

    ec = rcp_controller_send(s->ctrl, ctx, &cmd, &resp);
    free(payload);
    if (ec != RCP_OK) {
        rcp_response_free(&resp);
        return ec;
    }
    if (resp.status != RCP_RESPONSE_OK) {
        rcp_response_free(&resp);
        return RCP_FW_ERR_BAD_STATE;
    }
    rcp_response_free(&resp);

    rcp_mutex_lock(&s->mu);
    s->state = RCP_FW_STATE_INITIATED;
    rcp_mutex_unlock(&s->mu);
    return RCP_OK;
}

//cfusa:req REQ-FW-004
//cfusa:req REQ-FW-006
//cfusa:req REQ-FW-008
int rcp_firmware_session_transfer(rcp_firmware_session_t *s, const rcp_context_t *ctx,
                                   const uint8_t *image, size_t image_len,
                                   rcp_fw_progress_fn progress_cb, void *progress_user_data)
{
    size_t chunks;
    size_t i;

    (void)ctx; /* mirrors cpp-RCP's transfer(): each attempt uses its own
                * cfg.chunk_timeout_ms deadline instead of the caller's ctx */

    rcp_mutex_lock(&s->mu);
    if (s->state != RCP_FW_STATE_INITIATED) {
        rcp_mutex_unlock(&s->mu);
        return RCP_FW_ERR_BAD_STATE;
    }
    s->state = RCP_FW_STATE_TRANSFERRING;
    rcp_mutex_unlock(&s->mu);

    chunks = (image_len + s->cfg.chunk_size - 1) / s->cfg.chunk_size;

    for (i = 0; i < chunks; i++) {
        size_t   offset  = i * s->cfg.chunk_size;
        size_t   remain  = image_len - offset;
        size_t   len     = (s->cfg.chunk_size < remain) ? s->cfg.chunk_size : remain;
        uint8_t *payload = (uint8_t *)malloc(5 + len);
        int      attempt;
        int      ec = RCP_OK;
        bool     ok = false;

        if (!payload) {
            rcp_mutex_lock(&s->mu);
            s->state = RCP_FW_STATE_FAILED;
            rcp_mutex_unlock(&s->mu);
            return RCP_ERR_BUSY;
        }
        payload[0] = 0x02;
        payload[1] = (uint8_t)(i >> 24);
        payload[2] = (uint8_t)(i >> 16);
        payload[3] = (uint8_t)(i >> 8);
        payload[4] = (uint8_t)i;
        if (len > 0) memcpy(payload + 5, image + offset, len);

        for (attempt = 0; attempt <= s->cfg.retries; attempt++) {
            rcp_command_t  cmd = {0};
            rcp_response_t resp = {0};
            rcp_context_t  chunk_ctx = rcp_context_with_timeout_ms(s->cfg.chunk_timeout_ms);

            cmd.zone         = rcp_controller_zone(s->ctrl);
            cmd.type         = RCP_CMD_UPDATE;
            cmd.priority     = RCP_PRIORITY_HIGH;
            cmd.payload.data = payload;
            cmd.payload.len  = 5 + len;

            ec = rcp_controller_send(s->ctrl, &chunk_ctx, &cmd, &resp);
            if (ec == RCP_OK && resp.status == RCP_RESPONSE_OK) {
                rcp_response_free(&resp);
                ok = true;
                break;
            }
            rcp_response_free(&resp);
        }
        free(payload);

        if (!ok) {
            rcp_mutex_lock(&s->mu);
            s->state = RCP_FW_STATE_FAILED;
            rcp_mutex_unlock(&s->mu);
            return (ec != RCP_OK) ? ec : RCP_FW_ERR_TRANSFER_ERROR;
        }

        if (progress_cb) {
            rcp_fw_progress_t p;
            p.bytes_sent   = offset + len;
            p.total_bytes  = image_len;
            p.chunk_index  = i + 1;
            p.total_chunks = chunks;
            progress_cb(&p, progress_user_data);
        }
    }

    return RCP_OK;
}

//cfusa:req REQ-FW-005
int rcp_firmware_session_verify(rcp_firmware_session_t *s, const rcp_context_t *ctx)
{
    rcp_command_t  cmd = {0};
    rcp_response_t resp = {0};
    uint8_t        payload[1];
    rcp_context_t  vctx;
    int            ec;

    (void)ctx; /* mirrors cpp-RCP's verify(): uses cfg.verify_timeout_ms instead */

    rcp_mutex_lock(&s->mu);
    if (s->state != RCP_FW_STATE_TRANSFERRING) {
        rcp_mutex_unlock(&s->mu);
        return RCP_FW_ERR_BAD_STATE;
    }
    s->state = RCP_FW_STATE_VERIFYING;
    rcp_mutex_unlock(&s->mu);

    payload[0] = 0x03;
    vctx = rcp_context_with_timeout_ms(s->cfg.verify_timeout_ms);
    cmd.zone         = rcp_controller_zone(s->ctrl);
    cmd.type         = RCP_CMD_UPDATE;
    cmd.priority     = RCP_PRIORITY_HIGH;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    ec = rcp_controller_send(s->ctrl, &vctx, &cmd, &resp);
    if (ec != RCP_OK) {
        rcp_response_free(&resp);
        rcp_mutex_lock(&s->mu);
        s->state = RCP_FW_STATE_FAILED;
        rcp_mutex_unlock(&s->mu);
        return ec;
    }
    if (resp.status != RCP_RESPONSE_OK) {
        rcp_response_free(&resp);
        rcp_mutex_lock(&s->mu);
        s->state = RCP_FW_STATE_FAILED;
        rcp_mutex_unlock(&s->mu);
        return RCP_FW_ERR_VERIFY_FAILED;
    }
    rcp_response_free(&resp);
    return RCP_OK;
}

int rcp_firmware_session_activate(rcp_firmware_session_t *s, const rcp_context_t *ctx)
{
    rcp_command_t  cmd = {0};
    rcp_response_t resp = {0};
    uint8_t        payload[1];
    int            ec;

    rcp_mutex_lock(&s->mu);
    if (s->state != RCP_FW_STATE_VERIFYING) {
        rcp_mutex_unlock(&s->mu);
        return RCP_FW_ERR_BAD_STATE;
    }
    rcp_mutex_unlock(&s->mu);

    payload[0] = 0x04;
    cmd.zone         = rcp_controller_zone(s->ctrl);
    cmd.type         = RCP_CMD_UPDATE;
    cmd.priority     = RCP_PRIORITY_HIGH;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    ec = rcp_controller_send(s->ctrl, ctx, &cmd, &resp);
    if (ec != RCP_OK) {
        rcp_response_free(&resp);
        rcp_mutex_lock(&s->mu);
        s->state = RCP_FW_STATE_FAILED;
        rcp_mutex_unlock(&s->mu);
        return ec;
    }
    if (resp.status != RCP_RESPONSE_OK) {
        rcp_response_free(&resp);
        rcp_mutex_lock(&s->mu);
        s->state = RCP_FW_STATE_FAILED;
        rcp_mutex_unlock(&s->mu);
        return RCP_FW_ERR_VERIFY_FAILED; /* matches cpp-RCP's activate(), which reuses verify_failed here too */
    }
    rcp_response_free(&resp);

    rcp_mutex_lock(&s->mu);
    s->state = RCP_FW_STATE_ACTIVATED;
    rcp_mutex_unlock(&s->mu);
    return RCP_OK;
}

//cfusa:req REQ-FW-007
int rcp_firmware_session_rollback(rcp_firmware_session_t *s, const rcp_context_t *ctx)
{
    rcp_command_t  cmd = {0};
    rcp_response_t resp = {0};
    uint8_t        payload[1];
    int            ec;

    payload[0] = 0x05;
    cmd.zone         = rcp_controller_zone(s->ctrl);
    cmd.type         = RCP_CMD_UPDATE;
    cmd.priority     = RCP_PRIORITY_HIGH;
    cmd.payload.data = payload;
    cmd.payload.len  = sizeof(payload);

    ec = rcp_controller_send(s->ctrl, ctx, &cmd, &resp);
    if (ec == RCP_OK && resp.status == RCP_RESPONSE_OK) {
        rcp_response_free(&resp);
        rcp_mutex_lock(&s->mu);
        s->state = RCP_FW_STATE_IDLE;
        rcp_mutex_unlock(&s->mu);
        return RCP_OK;
    }
    rcp_response_free(&resp);
    return (ec != RCP_OK) ? ec : RCP_FW_ERR_ROLLBACK_FAILED;
}

void rcp_firmware_session_destroy(rcp_firmware_session_t *s)
{
    if (!s) return;
    rcp_controller_release(s->ctrl);
    rcp_mutex_destroy(&s->mu);
    free(s);
}
