/*
 * End-to-end communication protection (ISO 26262 Part 7 E2E profile).
 *
 * Three layers of defence:
 *   1. Sequence counter — per-controller monotonically incrementing uint32.
 *   2. CRC-16/CCITT-FALSE checksum — computed over seq + original payload.
 *   3. Replay guard — sliding window rejects previously seen sequence numbers.
 *
 * Sender: wrap any rcp_controller_t with rcp_e2e_controller_new().
 * Receiver: call rcp_e2e_unwrap() then rcp_e2e_replay_guard_check().
 */
#ifndef RCP_E2E_H
#define RCP_E2E_H

#include "rcp/rcp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Byte overhead prepended by rcp_e2e_wrap(). Layout: [0:4] seq_num uint32
 * big-endian, [4:6] CRC-16 uint16 big-endian. */
#define RCP_E2E_HEADER_LEN 6u

/* Size of the ReplayGuard's sliding bitmap window. */
#define RCP_E2E_REPLAY_WINDOW_SIZE 32u

typedef enum {
    RCP_E2E_OK               = 0,
    RCP_E2E_ERR_CRC_MISMATCH = 1,
    RCP_E2E_ERR_SHORT_FRAME  = 2,
    RCP_E2E_ERR_REPLAY       = 3,
} rcp_e2e_errc_t;

/* Never returns NULL. */
const char *rcp_e2e_strerror(rcp_e2e_errc_t e);

/* Prepends a RCP_E2E_HEADER_LEN-byte E2E header (seq_num + CRC-16) to
 * payload. Returns an owned rcp_bytes_t (data=NULL, len=0 on allocation
 * failure) — the caller must eventually call rcp_bytes_free() on it.
 * payload may be NULL iff payload_len==0. */
rcp_bytes_t rcp_e2e_wrap(uint32_t seq_num, const uint8_t *payload, size_t payload_len);

/* Validates the CRC and extracts seq_num + the original payload from frame.
 * On success (RCP_E2E_OK), *payload_out is an owned rcp_bytes_t the caller
 * must eventually call rcp_bytes_free() on; on failure *payload_out is
 * zeroed and *seq_num_out is unspecified. */
rcp_e2e_errc_t rcp_e2e_unwrap(const uint8_t *frame, size_t frame_len,
                              uint32_t *seq_num_out, rcp_bytes_t *payload_out);

/* Implements a bitmap sliding window to detect duplicate or replayed
 * sequence numbers (ISO 26262 Part 7 E2E counter protection). The window
 * covers [high_water - RCP_E2E_REPLAY_WINDOW_SIZE + 1, high_water];
 * sequence numbers older than the window floor are unconditionally
 * rejected. Thread-safe. */
typedef struct rcp_e2e_replay_guard rcp_e2e_replay_guard_t;

/* Returns NULL on allocation failure. */
rcp_e2e_replay_guard_t *rcp_e2e_replay_guard_new(void);

/* Returns RCP_E2E_OK if seq_num is fresh and within the valid window, or
 * RCP_E2E_ERR_REPLAY if it is a duplicate or too old. The very first call
 * on a fresh guard always bootstraps and accepts, regardless of seq_num
 * (including 0). */
rcp_e2e_errc_t rcp_e2e_replay_guard_check(rcp_e2e_replay_guard_t *g, uint32_t seq_num);

void rcp_e2e_replay_guard_destroy(rcp_e2e_replay_guard_t *g);

/* Wraps inner (retains it) and automatically applies E2E protection
 * (sequence counter + CRC-16) to every command payload on send(). Returned
 * with refcount 1; release with rcp_controller_release(), which also
 * releases this wrapper's reference to inner. */
rcp_controller_t *rcp_e2e_controller_new(rcp_controller_t *inner);

#ifdef __cplusplus
}
#endif

#endif /* RCP_E2E_H */
