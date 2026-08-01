/* SPDX-License-Identifier: MPL-2.0 */
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
//cfusa:req REQ-UDP-015
//cfusa:req REQ-UDP-016
//cfusa:req REQ-UDP-017
//cfusa:req REQ-UDP-018
//cfusa:req REQ-UDP-019
/*
 * udp.h -- IEEE1722-over-UDP/IP (spec Annex J) transport for the TC18
 * Remote Control Protocol wire layer (ROADMAP.md Phase 21, "Satellite
 * Package Rework", milestone 78, "Transport satellites"; Annex J
 * conformance -- encapsulation sequence number + standard control port --
 * added at ROADMAP.md milestone 101, alongside the new native-Ethernet
 * `l2.h`/`l2.c` transport that milestone also adds).
 *
 * ── Annex J conformance: encapsulation sequence number + control port ──
 *
 * PROVENANCE CAVEAT (read this before trusting any port number or wire
 * offset below): this project has no access to the paywalled IEEE
 * 1722-2016 standard text, including its own Annex J. Everything in this
 * section is instead cross-checked against two public secondary sources
 * that independently agree with each other: (1) a Wireshark issue
 * tracker discussion of the real Annex J framing, and (2) the COVESA
 * Open1722 open-source reference implementation's actual
 * `Avtp_Udp_t` header struct (`include/avtp/Udp.h`, BSD-3-Clause,
 * github.com/COVESA/Open1722). Treat this as a good-faith, cross-checked
 * reconstruction, not a primary-source-verified reading of the standard
 * itself.
 *
 * Both sources agree that when an AVTPDU is carried over UDP/IP (Annex
 * J), the UDP payload begins with a 4-byte (32-bit) "encapsulation
 * sequence number" field, THEN the AVTPDU itself -- this field does
 * *not* exist when the same AVTPDU is instead carried at layer 2 with
 * EtherType 0x22F0 (see the new `rcp/l2.h`); L2 and UDP genuinely have
 * different wire framing here, not just a different socket API. This
 * module's own `rcp_udp_annexj_wrap()`/`_unwrap()` are the pure,
 * socket-free codec for that framing: `_wrap()` prepends a
 * caller-supplied sequence number (big-endian, matching this project's
 * own AVTPDU byte-order convention -- see acf.c's/avtp.c's own
 * put_u64-style helpers) ahead of the AVTPDU bytes; `_unwrap()` reads it
 * back off and returns a pointer to the AVTPDU bytes that follow.
 * `rcp_udp_avtp_transport_dial()`/`_bind()`'s own send()/recv() apply
 * this codec automatically -- callers of the `rcp_avtp_transport_t`
 * vtable never see the raw encapsulated wire form, only the AVTPDU
 * itself, exactly as before this milestone. The sequence number a
 * transport most recently *received* is available via
 * `rcp_udp_avtp_transport_last_recv_seq()` for callers who want it, but
 * this module does not verify -- and explicitly does NOT claim to know
 * -- what a real TC18/Annex J peer's own intended receiver-side
 * semantics for that field are (e.g. whether/how a receiver is meant to
 * detect gaps or reordering from it); neither secondary source above
 * documents that, so no such behavior is invented here. Each transport
 * instance maintains its own monotonically-incrementing `uint32_t` send
 * counter (wrapping on overflow, never validated against anything the
 * peer sends).
 *
 * Both sources also agree on the standard destination UDP ports Annex J
 * defines: 17220 for "Continuous" (streaming/periodic) traffic and
 * 17221 for "Discrete" (control) traffic. RCP requests/responses/
 * acknowledgements are control-plane traffic, so 17221 --
 * `RCP_UDP_ANNEX_J_CONTROL_PORT` below -- is the applicable port for
 * RCP-over-UDP. The explicit-port `rcp_udp_avtp_transport_dial()`/
 * `_bind()` entry points are unchanged and keep accepting any caller-
 * supplied port (back-compat, and useful for tests that want an
 * OS-assigned ephemeral port via 0); `rcp_udp_avtp_transport_dial_
 * default_port()`/`_bind_default_port()` are new convenience wrappers
 * that fill in `RCP_UDP_ANNEX_J_CONTROL_PORT` for a caller who just
 * wants the spec-default control association. Port 0 was deliberately
 * left alone rather than overloaded to also mean "use the default
 * control port": `rcp_udp_avtp_transport_bind(addr, 0, ...)` already has
 * an existing, tested, different meaning ("let the OS assign an
 * ephemeral port", see `rcp_udp_avtp_transport_port()`), so silently
 * changing what 0 means there would be a breaking, ambiguous change --
 * a new, explicitly-named wrapper function was the safer, more
 * consistent-with-this-project's-own-`_default_config()`-style
 * evolution.
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

/* Standard IEEE1722-over-UDP/IP (Annex J) destination ports -- see this
 * header's own file comment for the public-secondary-source provenance
 * caveat these two values carry. RCP requests/responses/acknowledgements
 * are control-plane ("Discrete") traffic, so RCP_UDP_ANNEX_J_CONTROL_PORT
 * is the port this module's own *_default_port() convenience wrappers
 * (below) use; RCP_UDP_ANNEX_J_CONTINUOUS_PORT is provided for
 * completeness/documentation only -- nothing in this module targets it. */
#define RCP_UDP_ANNEX_J_CONTROL_PORT     ((uint16_t)17221u)
#define RCP_UDP_ANNEX_J_CONTINUOUS_PORT  ((uint16_t)17220u)

/* Width, in octets, of the Annex J encapsulation sequence number that
 * rcp_udp_annexj_wrap()/_unwrap() prepend to/strip from an AVTPDU. */
#define RCP_UDP_ANNEX_J_SEQ_LEN ((size_t)4u)

/* Pure, socket-free codec for Annex J's UDP encapsulation framing (see
 * this header's own file comment for the exact field layout and its
 * provenance caveat). No I/O, no allocation failure path beyond the one
 * documented below -- safe to unit-test on any platform, with no socket
 * or privilege requirement.
 *
 * rcp_udp_annexj_wrap() heap-allocates a buffer holding seq (4 octets,
 * big-endian) followed by avtpdu[0..avtpdu_len), and returns it as an
 * rcp_bytes_t (avtpdu may be NULL iff avtpdu_len == 0). Returns a zeroed
 * rcp_bytes_t (data=NULL) on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_udp_annexj_wrap(uint32_t seq, const uint8_t *avtpdu, size_t avtpdu_len);

/* rcp_udp_annexj_unwrap() reads datagram[0..datagram_len) as a
 * wrap()-produced buffer: writes the sequence number to *out_seq and
 * points *out_avtpdu at the AVTPDU bytes that follow (borrowed from
 * datagram, not copied -- matching avtp.h's own decode_ntscf()/_tscf()
 * borrowing convention; datagram must outlive *out_avtpdu's use).
 * Returns false (out params left untouched) if datagram_len is shorter
 * than RCP_UDP_ANNEX_J_SEQ_LEN -- there is no sequence number to read,
 * let alone an AVTPDU after it. */
bool rcp_udp_annexj_unwrap(const uint8_t *datagram, size_t datagram_len,
                            uint32_t *out_seq, const uint8_t **out_avtpdu,
                            size_t *out_avtpdu_len);

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

/* Convenience wrappers equivalent to calling rcp_udp_avtp_transport_dial()/
 * _bind() with port == RCP_UDP_ANNEX_J_CONTROL_PORT (17221) -- the
 * spec-default control-plane association, for a caller that has no reason
 * to pick a different port. See this header's own file comment for why
 * this is a separate wrapper rather than overloading port 0's existing,
 * different meaning. */
rcp_avtp_transport_t *rcp_udp_avtp_transport_dial_default_port(const char *host,
                                                                 bool time_sync_supported);
rcp_avtp_transport_t *rcp_udp_avtp_transport_bind_default_port(const char *addr,
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

/* The Annex J encapsulation sequence number carried by the most recently
 * successfully received datagram (see this header's own file comment for
 * the field's provenance and this module's explicit non-claim about its
 * intended receiver-side semantics). Returns 0 before any datagram has
 * ever been received -- indistinguishable from a real received sequence
 * number of 0; callers that need to tell the two apart must track
 * "have I received anything yet" themselves. Behavior is undefined if t
 * was not returned by this module's own dial()/bind(). */
uint32_t rcp_udp_avtp_transport_last_recv_seq(rcp_avtp_transport_t *t);

#ifdef __cplusplus
}
#endif

#endif /* RCP_UDP_H */
