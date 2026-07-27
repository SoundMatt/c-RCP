/*
 * Zone controller OTA firmware update.
 *
 * rcp_firmware_session_t manages a multi-command exchange:
 *   initiate -> transfer (N chunks) -> verify -> activate -> reset
 *
 * Rollback is available via rcp_firmware_session_rollback() if activation
 * fails. Uses RCP_CMD_UPDATE (added this milestone) as the command type for
 * every step; the first payload byte is a subcommand selector (0x01
 * Initiate, 0x02 Transfer, 0x03 Verify, 0x04 Activate, 0x05 Rollback),
 * matching cpp-RCP's own wire encoding.
 */
#ifndef RCP_FIRMWARE_H
#define RCP_FIRMWARE_H

#include "rcp/rcp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t   chunk_size;        /* bytes per Transfer command (default: 4096) */
    int      retries;           /* per-chunk retry count (default: 3) */
    uint64_t chunk_timeout_ms;  /* default: 500 */
    uint64_t verify_timeout_ms; /* default: 5000 */
} rcp_fw_config_t;

/* { chunk_size = 4096, retries = 3, chunk_timeout_ms = 500, verify_timeout_ms = 5000 }. */
rcp_fw_config_t rcp_fw_default_config(void);

typedef enum {
    RCP_FW_STATE_IDLE         = 0,
    RCP_FW_STATE_INITIATED    = 1,
    RCP_FW_STATE_TRANSFERRING = 2,
    RCP_FW_STATE_VERIFYING    = 3,
    RCP_FW_STATE_ACTIVATED    = 4,
    RCP_FW_STATE_FAILED       = 5,
} rcp_fw_state_t;

/* Firmware-specific error codes, offset by 100 so they never collide with
 * the generic rcp_errc_t values (0-8) that session functions may also
 * return directly when the underlying rcp_controller_send() itself fails.
 * Pass values below 100 to rcp_strerror(); pass these to rcp_fw_strerror(). */
typedef enum {
    RCP_FW_ERR_BAD_STATE       = 100,
    RCP_FW_ERR_VERIFY_FAILED   = 101,
    RCP_FW_ERR_TRANSFER_ERROR  = 102,
    RCP_FW_ERR_ROLLBACK_FAILED = 103,
} rcp_fw_errc_t;

/* Human-readable message for a rcp_fw_errc_t value (100-103 only). Never
 * returns NULL. */
const char *rcp_fw_strerror(rcp_fw_errc_t e);

typedef struct {
    size_t bytes_sent;
    size_t total_bytes;
    size_t chunk_index;
    size_t total_chunks;
} rcp_fw_progress_t;

typedef void (*rcp_fw_progress_fn)(const rcp_fw_progress_t *p, void *user_data);

typedef struct rcp_firmware_session rcp_firmware_session_t;

/* Creates a session over ctrl (retains it). Returns NULL on allocation
 * failure. */
rcp_firmware_session_t *rcp_firmware_session_new(rcp_controller_t *ctrl, rcp_fw_config_t cfg);

rcp_fw_state_t rcp_firmware_session_state(rcp_firmware_session_t *s);

/* Starts an OTA session on the zone controller. Returns RCP_FW_ERR_BAD_STATE
 * if the session is not currently RCP_FW_STATE_IDLE, without sending a
 * command. On success, transitions to RCP_FW_STATE_INITIATED. */
int rcp_firmware_session_initiate(rcp_firmware_session_t *s, const rcp_context_t *ctx, const char *version);

/* Sends the firmware image in cfg.chunk_size chunks, retrying each chunk up
 * to cfg.retries times with a fresh cfg.chunk_timeout_ms deadline per
 * attempt (the ctx parameter is accepted for API-shape parity with the
 * other session functions but is not itself used to bound the transfer,
 * matching cpp-RCP's own transfer()). Returns RCP_FW_ERR_BAD_STATE if the
 * session is not currently RCP_FW_STATE_INITIATED. progress_cb, if
 * non-NULL, is invoked once per chunk sent. */
int rcp_firmware_session_transfer(rcp_firmware_session_t *s, const rcp_context_t *ctx,
                                   const uint8_t *image, size_t image_len,
                                   rcp_fw_progress_fn progress_cb, void *progress_user_data);

/* Triggers the zone controller to validate the received image, using
 * cfg.verify_timeout_ms rather than ctx (matching cpp-RCP's own verify(),
 * which likewise ignores its ctx parameter). Returns RCP_FW_ERR_BAD_STATE
 * if the session is not currently RCP_FW_STATE_TRANSFERRING. */
int rcp_firmware_session_verify(rcp_firmware_session_t *s, const rcp_context_t *ctx);

/* Instructs the zone controller to boot the new image on next reset.
 * Returns RCP_FW_ERR_BAD_STATE if the session is not currently
 * RCP_FW_STATE_VERIFYING. */
int rcp_firmware_session_activate(rcp_firmware_session_t *s, const rcp_context_t *ctx);

/* Asks the zone controller to revert to the previous image. Unlike every
 * other session function, this may be called from any state. On success,
 * transitions to RCP_FW_STATE_IDLE. */
int rcp_firmware_session_rollback(rcp_firmware_session_t *s, const rcp_context_t *ctx);

/* Releases s's reference to its controller and frees s. Call exactly once. */
void rcp_firmware_session_destroy(rcp_firmware_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* RCP_FIRMWARE_H */
