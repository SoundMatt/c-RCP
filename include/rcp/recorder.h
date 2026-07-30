/* SPDX-License-Identifier: MPL-2.0 */
/*
 * recorder.h -- Binary record and replay of raw wire traffic for the TC18
 * Remote Control Protocol wire layer (ROADMAP.md Phase 21, "Satellite
 * Package Rework", milestone 80, "Generic decorators, batch 1").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: "append a timestamped
 * entry for every request/response that crosses this point, then be able
 * to replay them later with the original timing" is a strictly more
 * general, still-useful idea under TC18 than it was before. What changes
 * is the shape of what gets captured: the old rcp_recorder_entry_t held a
 * whole rcp_command_t/rcp_response_t pair, a shape retired along with
 * rcp_controller_t (ROADMAP.md's Protocol Replacement Notice). This
 * module now captures raw ACF messages/AVTPDUs -- exactly the
 * already-framed byte buffers every Phase 16+ endpoint module
 * (ep_gpio.h and siblings) and discovery.h already produce/consume, and
 * the same shape mock.h's own RC-Server/endpoint test double dispatches
 * on (see mock.h's file header) -- tagged with the avtp.h
 * rcp_avtp_addr_t (stream_id + byte_bus_id) they belong to and whether
 * they were inbound (received) or outbound (about to be sent). This is a
 * new on-the-wire capture format, not a byte-for-byte match of the old
 * Command/Response-pair one, per this milestone's own roadmap scope.
 *
 * There is no longer a single generic rcp_controller_t::send() choke
 * point to wrap automatically. This module drops the old
 * RecordingController vtable wrapper entirely: rcp_recorder_capture() is
 * now the whole interception point, called directly by the caller at
 * whichever call site actually touches a raw ACF/AVTPDU buffer --
 * matching the caller-driven shape milestone 79's watchdog.c/deadline.c/
 * powerstate.c already established. Playback drops its own dependency on
 * a single generic target controller for the same reason:
 * rcp_playback_run_all() now drives a caller-supplied delivery callback
 * instead, leaving it up to the caller to decide what "replaying" a
 * captured frame means for whichever endpoint/transport it belongs to.
 *
 * Deviation from cpp-RCP, unchanged from this module's pre-rebind
 * version: entry timestamps use rcp_monotonic_ms() (millisecond
 * resolution) rather than cpp-RCP's wall-clock std::chrono::system_clock
 * (nanosecond resolution) -- a monotonic clock can never run backward
 * (unlike wall-clock time, which NTP adjustment can), so it is strictly
 * better suited to the "timestamps never decrease" guarantee this module
 * documents. The on-disk binary log format remains this port's own
 * (millisecond timestamps, native integer byte order), a local
 * debug/tooling artifact read back only by this same library, not a
 * cross-language wire format.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_RECORDER_H
#define RCP_RECORDER_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t         timestamp_ms;
    rcp_avtp_addr_t   addr;    /* endpoint this frame is associated with */
    bool               inbound; /* true = received from the wire, false = about to be sent */
    rcp_bytes_t         frame;   /* raw ACF message (or AVTPDU) bytes; owned by the Record */
} rcp_recorder_entry_t;

typedef struct rcp_recorder rcp_recorder_t;

/* Returns NULL on allocation failure. */
rcp_recorder_t *rcp_recorder_new(void);

/* Frees every entry's frame copy and the entries array itself, then
 * frees r. Call exactly once. */
void rcp_recorder_destroy(rcp_recorder_t *r);

size_t rcp_recorder_size(rcp_recorder_t *r);

/* Fills out[0..min(count,cap)) with a copy of every entry recorded so far,
 * in order, and returns the total count (which may exceed cap; callers
 * needing all of them should re-call with a larger buffer sized to the
 * returned count). Each returned entry's frame.data points into r's own
 * storage (valid only as long as r itself is alive) -- callers must not
 * free it. */
size_t rcp_recorder_entries(rcp_recorder_t *r, rcp_recorder_entry_t *out, size_t cap);

/* Appends one entry to r: a copy of frame[0..frame_len) (frame may be
 * NULL iff frame_len == 0), tagged with addr, inbound, and
 * timestamp_ms. Thread-safe with concurrent rcp_recorder_capture() calls.
 * Returns false on allocation failure (nothing recorded). */
bool rcp_recorder_capture(rcp_recorder_t *r, uint64_t timestamp_ms, rcp_avtp_addr_t addr,
                           bool inbound, const uint8_t *frame, size_t frame_len);

/* Serialises r to a binary file at path (truncating any existing file).
 * Returns RCP_OK on success, RCP_ERR_BUSY if the file could not be opened
 * for writing (no dedicated I/O-error code exists in rcp_errc_t). */
int rcp_recorder_write_binary(rcp_recorder_t *r, const char *path);

typedef struct {
    double speed_factor; /* 2.0 = 2x faster, 0.0 = no delays (default: 1.0) */
} rcp_playback_config_t;

/* { speed_factor = 1.0 }. */
rcp_playback_config_t rcp_playback_default_config(void);

/* Invoked once per recorded entry, in order, during rcp_playback_run_all().
 * entry is only valid for the duration of the call. user_data is the
 * opaque pointer passed to rcp_playback_run_all(). */
typedef void (*rcp_playback_deliver_fn)(const rcp_recorder_entry_t *entry, void *user_data);

/* Replays every entry in rec, synchronously, invoking deliver once per
 * entry (in order) and pausing between deliveries to respect the
 * original recorded timing (adjusted by cfg.speed_factor) -- it is up to
 * deliver to decide what "replaying" a captured frame means for
 * whichever endpoint/transport it belongs to; this module sends no wire
 * traffic and owns no transport itself. Takes an internal snapshot of
 * rec's entries before starting playback, so it is safe to call even
 * while another thread is concurrently calling rcp_recorder_capture() on
 * rec. Always returns RCP_OK; deliver has no return value to report a
 * per-entry failure with. */
int rcp_playback_run_all(rcp_recorder_t *rec, rcp_playback_deliver_fn deliver, void *user_data,
                          rcp_playback_config_t cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_RECORDER_H */
