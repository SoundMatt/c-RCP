/*
 * Per-zone priority queue that serialises concurrent command senders while
 * honouring RCP_PRIORITY_CRITICAL > RCP_PRIORITY_HIGH > RCP_PRIORITY_NORMAL.
 *
 * Commands at equal priority are dispatched FIFO. Critical commands always
 * pre-empt queued Normal and High commands, ensuring watchdog kicks and
 * safety-critical actuation are never head-of-line blocked by lower-priority
 * traffic.
 *
 * A single dispatch thread issues inner send() calls one at a time, so the
 * inner controller never sees concurrent sends.
 *
 * Deviation from cpp-RCP: cpp-RCP's close() flips its closed flag and
 * returns immediately without waiting for the dispatch thread to drain (only
 * its destructor joins). This port's rcp_controller_close() joins the
 * dispatch thread before returning, matching the stronger close()-blocks-
 * until-drained guarantee already established by every other background-
 * thread module in this port (watchdog, deadline, powerstate).
 */
#ifndef RCP_PRIOQUEUE_H
#define RCP_PRIOQUEUE_H

#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wraps inner (retains it) and serialises send() calls through a priority
 * queue dispatched by a single background thread. Returned with refcount 1;
 * release with rcp_controller_release(), which also releases this wrapper's
 * reference to inner. */
rcp_controller_t *rcp_prioqueue_controller_new(rcp_controller_t *inner);

#ifdef __cplusplus
}
#endif

#endif /* RCP_PRIOQUEUE_H */
