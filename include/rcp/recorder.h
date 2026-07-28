/*
 * Binary record and replay of RCP traffic.
 *
 * rcp_recorder_controller_new() wraps any rcp_controller_t and appends a
 * timestamped entry to a rcp_recorder_t for every Command/Response pair.
 * rcp_playback_run_all() reads a Record and drives a target controller
 * with the recorded commands, using the recorded inter-entry timing
 * (scaled by speed_factor).
 *
 * Deviation from cpp-RCP: entry timestamps use rcp_monotonic_ms()
 * (millisecond resolution) rather than cpp-RCP's wall-clock
 * std::chrono::system_clock (nanosecond resolution). This is a genuine
 * improvement, not just reduced precision: a monotonic clock can never
 * run backward (unlike wall-clock time, which NTP adjustment can), so it
 * is strictly better suited to the "timestamps never decrease" guarantee
 * this module documents.
 *
 * The on-disk binary log format is this port's own (millisecond
 * timestamps, native integer byte order) rather than a byte-for-byte
 * match of cpp-RCP's log format -- the format is a local debug/tooling
 * artifact read back only by this same library's own
 * rcp_recorder_write_binary(), not a cross-language wire format, so exact
 * compatibility was never a functional requirement.
 */
#ifndef RCP_RECORDER_H
#define RCP_RECORDER_H

#include "rcp/rcp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t         timestamp_ms;
    rcp_command_t   cmd;   /* payload is owned by the Record; do not free */
    rcp_response_t  resp;  /* payload is owned by the Record; do not free */
    int              error; /* the rcp_errc_t from the recorded send() */
} rcp_recorder_entry_t;

typedef struct rcp_recorder rcp_recorder_t;

/* Returns NULL on allocation failure. */
rcp_recorder_t *rcp_recorder_new(void);

/* Frees every entry's payload copies and the entries array itself, then
 * frees r. Call exactly once. */
void rcp_recorder_destroy(rcp_recorder_t *r);

size_t rcp_recorder_size(rcp_recorder_t *r);

/* Fills out[0..min(count,cap)) with a copy of every entry recorded so far,
 * in order, and returns the total count (which may exceed cap; callers
 * needing all of them should re-call with a larger buffer sized to the
 * returned count), matching rcp_registry_controllers()'s convention. Each
 * returned entry's cmd.payload/resp.payload point into r's own storage
 * (valid only as long as r itself is alive) -- callers must not free
 * them. */
size_t rcp_recorder_entries(rcp_recorder_t *r, rcp_recorder_entry_t *out, size_t cap);

/* Serialises r to a binary file at path (truncating any existing file).
 * Returns RCP_OK on success, RCP_ERR_BUSY if the file could not be opened
 * for writing (no dedicated I/O-error code exists in rcp_errc_t). */
int rcp_recorder_write_binary(rcp_recorder_t *r, const char *path);

/* Wraps inner (retains it) and appends one entry to rec (retains it, via
 * a raw pointer -- rec must outlive ctrl, matching cpp-RCP's own
 * std::shared_ptr<Record> sharing without this port needing to add
 * refcounting to rcp_recorder_t itself, since only one recording controller
 * ever writes to a given Record in every real usage of this module)
 * around every send(). Returned with refcount 1; release with
 * rcp_controller_release(), which also releases this wrapper's reference
 * to inner. */
rcp_controller_t *rcp_recorder_controller_new(rcp_controller_t *inner, rcp_recorder_t *rec);

typedef struct {
    double speed_factor; /* 2.0 = 2x faster, 0.0 = no delays (default: 1.0) */
} rcp_playback_config_t;

/* { speed_factor = 1.0 }. */
rcp_playback_config_t rcp_playback_default_config(void);

/* Replays every entry in rec against target, synchronously, pausing
 * between entries to respect the original recorded timing (adjusted by
 * cfg.speed_factor). Takes an internal snapshot of rec's entries before
 * starting playback, so it is safe to call even while another thread is
 * concurrently appending to rec (a genuine hardening over cpp-RCP's own
 * run_all(), which iterates Record::entries() with no synchronization
 * against a concurrent append()). Always returns RCP_OK; per-entry send()
 * failures are not surfaced (matching cpp-RCP's own run_all(), which
 * discards each entry's send() result). */
int rcp_playback_run_all(rcp_controller_t *target, rcp_recorder_t *rec, const rcp_context_t *ctx,
                          rcp_playback_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RECORDER_H */
