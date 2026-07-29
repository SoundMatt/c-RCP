//cfusa:req REQ-UDP-001
//cfusa:req REQ-UDP-002
//cfusa:req REQ-UDP-003
//cfusa:req REQ-UDP-004
//cfusa:req REQ-UDP-005
//cfusa:req REQ-UDP-006
//cfusa:req REQ-UDP-007
//cfusa:req REQ-UDP-008
//cfusa:req REQ-UDP-009
//cfusa:req REQ-UDP-010
//cfusa:req REQ-UDP-011
//cfusa:req REQ-UDP-012
//cfusa:req REQ-UDP-013
//cfusa:req REQ-UDP-014
/*
 * udp.h -- IEEE1722-over-UDP/IP (spec Annex J) transport for the TC18
 * Remote Control Protocol wire layer (ROADMAP.md Phase 21, "Satellite
 * Package Rework", milestone 78, "Transport satellites").
 *
 * REPLACEs the pre-TC18 length-framed Command/Response UDP transport
 * (rcp_udp_zone_server_t / rcp_udp_controller_t, wire.h's own frame
 * format) with an rcp_avtp_transport_t (avtp.h, milestone 59)
 * implementation: this module's only remaining job is moving
 * already-framed AVTPDUs (as produced by avtp.c's own
 * rcp_avtp_encode_ntscf()/_tscf()) across a UDP/IP association, exactly
 * per the "transport independence" contract avtp.h's own file header
 * describes. Every request/response correlation, addressing, and
 * dispatch concern the old rcp_udp_controller_t/rcp_udp_zone_server_t
 * pair used to own (pending-request rendezvous by id, Subscribe/
 * Unsubscribe control frames, Zone-addressed responses) belongs one
 * layer up from here now -- to whichever future module drives an
 * rcp_avtp_transport_t against lifecycle.h/regmap.h/server.h (see
 * ROADMAP.md's own Phase 21 sequencing) -- so none of that machinery
 * survives the REPLACE. What *is* reused, per ROADMAP.md's own
 * milestone-78 text ("POSIX socket/thread plumbing reused as a starting
 * point where it still fits"): the POSIX socket create/bind/connect/
 * getsockname helpers the predecessor module already had, and the
 * cross-platform mutex seam from platform.h.
 *
 * Two construction modes, matching a client dialing a known RC Server
 * and an RC Server binding a local association:
 *
 *   - rcp_udp_avtp_transport_dial(): connects a UDP socket to a fixed
 *     peer (host:port). send()/recv() go through that connected socket,
 *     so the kernel itself discards any datagram not actually from that
 *     peer -- the usual "connected UDP" hardening.
 *   - rcp_udp_avtp_transport_bind(): binds a UDP socket to a local
 *     addr:port (addr NULL/"" -> INADDR_ANY; port 0 -> an OS-assigned
 *     ephemeral port, see rcp_udp_avtp_transport_port()). This transport
 *     starts with no known peer; its first successfully received
 *     datagram records the sender's address as the peer send()
 *     subsequently targets (see rcp_udp_avtp_transport_bind()'s own
 *     send() behavior below). This is a deliberate, documented
 *     simplification for a first cut at a UDP AVTP transport -- one
 *     learned peer per bound socket, not per-stream_id multi-peer
 *     routing; multiplexing many RC Clients behind a single bound RC
 *     Server association is tracked as future work, not assumed solved
 *     here.
 *
 * recv() polls the underlying socket in short slices rather than
 * blocking directly on it, so that close() -- called from a different
 * thread than whichever one is inside recv() -- reliably unblocks that
 * call within one poll slice on every platform this project targets,
 * instead of relying on shutdown()-interrupts-a-blocking-recv semantics,
 * which are not portable (POSIX leaves that implementation-defined for
 * datagram sockets, and Windows' own semantics differ again). close()
 * itself never touches the underlying socket -- it only raises a flag
 * send()/recv() both check first -- so a close() racing a recv() already
 * inside select()/recvfrom() on the same fd can never invalidate that
 * fd out from under it; the fd is only actually closed once this
 * transport's own destroy() runs (i.e. once nothing holds a reference to
 * it any more, per avtp.h's own refcounting contract), the same
 * "flag-then-real-teardown" split avtp.c's own loopback transport uses
 * for its own close()/destroy() pair.
 *
 * On Windows: stub, matching the predecessor module's own documented
 * scope gap -- every operation fails (ok() false, send()/recv() return
 * RCP_ERR_CLOSED) rather than attempting a real winsock implementation.
 * See ROADMAP.md.
 */
#ifndef RCP_UDP_H
#define RCP_UDP_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Connects a UDP socket to host:port and returns an rcp_avtp_transport_t
 * wrapping it, with refcount 1 (release with rcp_avtp_transport_release()).
 * Never returns NULL except on allocation failure -- check
 * rcp_udp_avtp_transport_ok() to distinguish a successful connect from a
 * socket/connect failure (mirrors the predecessor module's own
 * rcp_udp_controller_ok() convention). time_sync_supported is recorded on
 * the returned transport's own base.time_sync_supported field (avtp.h). */
rcp_avtp_transport_t *rcp_udp_avtp_transport_dial(const char *host, uint16_t port,
                                                   bool time_sync_supported);

/* Binds a UDP socket to addr:port and returns an rcp_avtp_transport_t
 * wrapping it, same refcount/ownership/ok() conventions as
 * rcp_udp_avtp_transport_dial(). Its send() targets whichever peer
 * address its own recv() most recently learned (see the file header);
 * before any datagram has ever been received, send() returns
 * RCP_ERR_BUSY rather than silently discarding the frame or guessing a
 * destination. */
rcp_avtp_transport_t *rcp_udp_avtp_transport_bind(const char *addr, uint16_t port,
                                                   bool time_sync_supported);

/* True iff t is a transport returned by this module's own dial()/bind()
 * and its underlying socket was created (and, for dial(), connected; for
 * bind(), bound) successfully. Behavior is undefined if t was not
 * returned by this module's own dial()/bind(). */
bool rcp_udp_avtp_transport_ok(rcp_avtp_transport_t *t);

/* The local port t's socket is actually bound to (useful after passing
 * port 0 to rcp_udp_avtp_transport_bind()/_dial() to let the OS assign
 * one). Returns 0 if t is not ok(). Behavior is undefined if t was not
 * returned by this module's own dial()/bind(). */
uint16_t rcp_udp_avtp_transport_port(rcp_avtp_transport_t *t);

/* Writes "host:port\0" into buf (up to buf_len bytes) for t's own bound
 * local address and returns the string length excluding the NUL; returns
 * 0 (buf untouched) if t is not ok() or buf_len == 0. Behavior is
 * undefined if t was not returned by this module's own dial()/bind(). */
size_t rcp_udp_avtp_transport_addr_string(rcp_avtp_transport_t *t, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_UDP_H */
