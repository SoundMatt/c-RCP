/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-L2-001
//cfusa:req REQ-L2-002
//cfusa:req REQ-L2-003
//cfusa:req REQ-L2-004
//cfusa:req REQ-L2-005
//cfusa:req REQ-L2-006
//cfusa:req REQ-L2-007
//cfusa:req REQ-L2-008
//cfusa:req REQ-L2-009
//cfusa:req REQ-L2-010
/*
 * l2.h -- native Ethernet (IEEE 1722, EtherType 0x22F0) transport for the
 * TC18 Remote Control Protocol wire layer. Finally delivers the native-
 * Ethernet carrier ROADMAP.md milestone 59 (v0.59.0) originally named as
 * one of three concrete `rcp_avtp_transport_t` (avtp.h) backends this
 * vtable was purpose-built to admit -- alongside IEEE1722-over-UDP/IP
 * (`udp.h`) and CAN(FD/XL)-as-network (still unimplemented) -- but that
 * milestone 78 (v0.78.0, "Transport satellites") silently dropped when it
 * reworked `udp.c`/`shmem.c`/`tsn.c` without ever adding this one. See
 * ROADMAP.md's own new milestone entry for this addition.
 *
 * Structurally parallel to udp.h/udp.c: implements the same
 * `rcp_avtp_transport_t` vtable (`send`/`recv`/`close`/`destroy`), with
 * the same "flag-then-real-teardown" close()/destroy() split, the same
 * poll-rather-than-block recv() so close() reliably unblocks a
 * concurrent in-progress recv() without touching the fd out from under
 * it, and the same non-Linux stub convention udp.c's own Windows stub
 * uses (every operation fails cleanly; ok() reports false).
 *
 * ── Wire frame ───────────────────────────────────────────────────────────
 *
 * destination MAC (6 octets) + source MAC (6 octets) + EtherType 0x22F0
 * (2 octets, big-endian) + the AVTPDU bytes directly. UNLIKE udp.h's own
 * Annex J encapsulation, there is NO 4-byte encapsulation sequence number
 * here -- that field is specific to the UDP/IP carrier; TC18 §10.1 itself
 * says IEEE 1722 "can be used as a layer-2 protocol, which is independent
 * from the physical layer below", and the public secondary sources udp.h's
 * own file header cites for the UDP encapsulation format describe it as a
 * UDP-only addition, never appearing in the native-Ethernet framing this
 * module implements. `rcp_l2_frame_encode()`/`_decode()` (below) are the
 * pure, socket-free codec for this framing -- no privilege, no Linux
 * requirement, unit-testable everywhere.
 *
 * The source MAC is never supplied by the caller: `rcp_l2_avtp_transport_
 * new()` reads it itself from the given network interface (Linux
 * `SIOCGIFHWADDR` ioctl) once, at construction time, exactly the way a
 * real Ethernet NIC driver would stamp its own hardware address into
 * every frame it emits -- a caller-supplied source MAC would risk
 * spoofing a different device's address by accident. The destination MAC
 * (unicast or multicast) IS caller-supplied, symmetrically with how
 * `rcp_udp_avtp_transport_dial()` takes a caller-supplied host: this
 * project does not implement -- and was explicitly told not to invent --
 * the IEEE 1722 base standard's own algorithm for deriving/allocating a
 * multicast destination MAC from a stream_id; a caller that needs
 * multicast delivery must compute that address itself and pass it in.
 *
 * ── Linux-only ───────────────────────────────────────────────────────────
 *
 * Raw Ethernet framing (`AF_PACKET`/`SOCK_RAW`) is a Linux-specific
 * socket API; this module has no equivalent on any other platform this
 * project targets. On any non-Linux build (`#if !defined(__linux__)`,
 * matching how udp.c splits `RCP_UDP_POSIX` from its Windows stub, just
 * on a narrower platform test), every construction/operation follows
 * udp.c's own Windows-stub convention exactly: `rcp_l2_avtp_transport_
 * new()` returns a non-NULL transport whose `ok()` is false, and whose
 * `send()`/`recv()` both return `RCP_ERR_CLOSED`.
 *
 * ── Privilege requirement ────────────────────────────────────────────────
 *
 * Opening an `AF_PACKET`/`SOCK_RAW` socket requires `CAP_NET_RAW` (or
 * running as root) on Linux. Without it, `socket()` itself fails and
 * `rcp_l2_avtp_transport_new()` returns a transport whose `ok()` is
 * false -- the same "construction never returns NULL just because the
 * underlying resource couldn't be opened, check ok() instead" contract
 * udp.c's own dial()/bind() use.
 */
#ifndef RCP_L2_H
#define RCP_L2_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IEEE 1722 EtherType, per the public IEEE 1722-2016 base standard (not
 * TC18-specific) -- the same value avtp.h's own file header and TC18
 * §10.1 (quoted in this header's own file comment) both reference. */
#define RCP_L2_ETHERTYPE ((uint16_t)0x22F0u)

/* Fixed Ethernet header length in octets this module's own wire frame
 * uses: destination MAC (6) + source MAC (6) + EtherType (2). */
#define RCP_L2_HEADER_LEN ((size_t)14u)

/* ── Pure frame codec (no socket, no privilege, no Linux requirement) ────── */

/* Heap-allocates a frame: dst_mac(6) + src_mac(6) + RCP_L2_ETHERTYPE
 * (big-endian, 2 octets) + avtpdu[0..avtpdu_len) (avtpdu may be NULL iff
 * avtpdu_len == 0). Returns a zeroed rcp_bytes_t (data=NULL) on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_l2_frame_encode(const uint8_t dst_mac[6], const uint8_t src_mac[6],
                                 const uint8_t *avtpdu, size_t avtpdu_len);

/* Decodes frame[0..frame_len) produced by rcp_l2_frame_encode() (or a
 * real Ethernet frame with the same layout): writes the destination/
 * source MAC into out_dst_mac/out_src_mac and points *out_avtpdu at the
 * AVTPDU bytes that follow (borrowed from frame, not copied -- matching
 * avtp.h's own decode_ntscf()/_tscf() and udp.h's own
 * rcp_udp_annexj_unwrap() borrowing convention; frame must outlive
 * *out_avtpdu's use). Returns false (out params left untouched) if
 * frame_len < RCP_L2_HEADER_LEN, or if the EtherType field is not
 * RCP_L2_ETHERTYPE -- this decoder is deliberately specific to this
 * project's own AVTP-over-Ethernet frames, not a general Ethernet
 * decoder that happens to also report EtherType. */
bool rcp_l2_frame_decode(const uint8_t *frame, size_t frame_len,
                          uint8_t out_dst_mac[6], uint8_t out_src_mac[6],
                          const uint8_t **out_avtpdu, size_t *out_avtpdu_len);

/* True iff mac is a unicast address: the I/G (individual/group) bit --
 * the least-significant bit of the first octet -- is 0. False for any
 * multicast address, including the all-ones broadcast address
 * (ff:ff:ff:ff:ff:ff), which is itself a special case of multicast under
 * this same bit test -- standard IEEE 802.3 addressing, not a TC18-
 * specific rule. lifecycle.h's rcp_lifecycle_writer_ctx_t is the
 * intended caller: TC18 §12.3.1.1/.2/.3 (REQ-LIFECYCLE-027) requires a
 * write request be accepted only when its frame's destination MAC is
 * unicast, and this is the primitive an integrator uses to classify a
 * frame's destination MAC (obtained from rcp_l2_frame_decode()'s own
 * out_dst_mac, or the transport-specific equivalent for a non-l2.h
 * carrier) before constructing that writer context. */
bool rcp_l2_mac_is_unicast(const uint8_t mac[6]);

/* ── Transport ─────────────────────────────────────────────────────────────── */

/* Opens a raw Ethernet socket on network interface ifname, reads that
 * interface's own hardware address as this transport's source MAC (see
 * this header's own file comment), and returns an rcp_avtp_transport_t
 * whose send() targets dst_mac and whose recv() accepts any AVTP-over-
 * Ethernet frame arriving on ifname regardless of its own source --
 * with refcount 1 (release with rcp_avtp_transport_release()). Never
 * returns NULL except on allocation failure -- check
 * rcp_l2_avtp_transport_ok() to distinguish a successful open from a
 * socket/bind/ioctl failure (mirrors udp.h's own rcp_udp_avtp_transport_
 * ok() convention). time_sync_supported is recorded on the returned
 * transport's own base.time_sync_supported field (avtp.h). Linux only;
 * see this header's own file comment for the non-Linux stub and the
 * CAP_NET_RAW privilege requirement. */
rcp_avtp_transport_t *rcp_l2_avtp_transport_new(const char *ifname, const uint8_t dst_mac[6],
                                                 bool time_sync_supported);

/* True iff t is a transport returned by this module's own new() and its
 * underlying raw socket was created, bound to its interface, and had its
 * interface's own hardware address successfully read, all successfully.
 * Behavior is undefined if t was not returned by this module's own
 * new(). */
bool rcp_l2_avtp_transport_ok(rcp_avtp_transport_t *t);

/* Writes t's own source MAC (the network interface's hardware address,
 * read once at construction time -- see this header's own file comment)
 * into out_mac[6] and returns true, or leaves out_mac untouched and
 * returns false if t is not ok(). Behavior is undefined if t was not
 * returned by this module's own new(). */
bool rcp_l2_avtp_transport_local_mac(rcp_avtp_transport_t *t, uint8_t out_mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* RCP_L2_H */
