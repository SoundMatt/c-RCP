/* SPDX-License-Identifier: MPL-2.0 */
/* Real, privileged, Linux-only round-trip verification for the native-
 * Ethernet AVTPDU transport (rcp_l2_avtp_transport_t, rcp/l2.h) -- moves
 * an actual AVTPDU across a real pair of raw AF_PACKET sockets bound to
 * two ends of a real veth pair, asserting byte-for-byte equality on
 * both legs. Deliberately NOT wired into ctest/Unity: it needs
 * CAP_NET_RAW (or root) and a pre-existing `veth0`/`veth1` pair (see
 * .github/workflows/ci.yml's own "L2 transport (veth)" job, which
 * creates that pair with `sudo ip link add veth0 type veth peer name
 * veth1` before invoking this binary directly, also under sudo) -- ctest
 * itself runs everywhere, unprivileged, so this can't be one of its own
 * tests (tests/test_l2.c already covers the frame codec logic itself,
 * unprivileged, on every platform).
 *
 * Broadcast (ff:ff:ff:ff:ff:ff) is used as the destination MAC on both
 * sides rather than either interface's own real hardware address: this
 * avoids this program needing to first query each veth end's own MAC
 * out-of-band before it can address the other, and broadcast is exactly
 * one of the two destination-address kinds l2.h's own file header
 * documents as valid caller-supplied values (unicast or multicast; a
 * broadcast frame is delivered to every listener on the segment,
 * including this program's own receiving socket on the peer interface).
 *
 * Exit code 0 on a verified round trip in both directions; nonzero (with
 * a diagnostic on stderr) on any failure, including "L2 transport not
 * available/privileged" -- which is itself a real failure for this
 * specific program's own job (unlike tests/test_l2.c's own unit tests,
 * which gracefully IGNORE that case because they're meant to run
 * everywhere, not just under this job's own guaranteed privileges).
 *
 * Also exercises close()-unblocks-a-concurrent-in-progress-recv() (REQ-
 * L2-008) for real, over the real raw socket: nothing in tests/test_l2.c
 * covers this, since it needs an actual ok() transport to block a real
 * recv() call on in the first place, which that unprivileged, cross-
 * platform unit test file cannot assume it has. */
//cfusa:test REQ-L2-006
//cfusa:test REQ-L2-007
//cfusa:test REQ-L2-008
#include <rcp/avtp.h>
#include <rcp/l2.h>
#include <rcp/rcp.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const uint8_t k_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static rcp_bytes_t build_ntscf_frame(uint8_t seed)
{
    uint8_t                  mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, seed};
    rcp_avtp_ntscf_header_t hdr    = {0};
    uint8_t                  payload[8];
    size_t                     i;

    hdr.sv           = 1;
    hdr.version       = 0;
    hdr.sequence_num  = seed;
    hdr.stream_id     = rcp_stream_id_make(mac, (uint16_t)(1000u + seed));

    for (i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(seed + i);

    return rcp_avtp_encode_ntscf(&hdr, payload, sizeof(payload));
}

/* Sends frame on tx, receives on rx (5s timeout), and asserts the
 * received bytes match frame exactly. Returns 0 on success, 1 (with a
 * stderr diagnostic) on any mismatch/error. */
static int roundtrip_one_way(const char *label, rcp_avtp_transport_t *tx,
                              rcp_avtp_transport_t *rx, uint8_t seed)
{
    rcp_bytes_t   frame = build_ntscf_frame(seed);
    rcp_context_t  ctx;
    uint8_t          buf[512];
    size_t             out_len = 0;
    int                  rc;

    if (!frame.data) {
        fprintf(stderr, "[%s] build_ntscf_frame() allocation failure\n", label);
        return 1;
    }

    rc = rcp_avtp_transport_send(tx, frame.data, frame.len);
    if (rc != RCP_OK) {
        fprintf(stderr, "[%s] send() failed: rc=%d (%s)\n", label, rc, rcp_strerror((rcp_errc_t)rc));
        rcp_bytes_free(&frame);
        return 1;
    }

    ctx = rcp_context_with_timeout_ms(5000);
    rc  = rcp_avtp_transport_recv(rx, &ctx, buf, sizeof(buf), &out_len);
    if (rc != RCP_OK) {
        fprintf(stderr, "[%s] recv() failed: rc=%d (%s)\n", label, rc, rcp_strerror((rcp_errc_t)rc));
        rcp_bytes_free(&frame);
        return 1;
    }

    if (out_len != frame.len || memcmp(buf, frame.data, frame.len) != 0) {
        fprintf(stderr, "[%s] byte mismatch: sent %zu bytes, received %zu bytes\n", label,
                frame.len, out_len);
        rcp_bytes_free(&frame);
        return 1;
    }

    printf("[%s] OK: %zu bytes round-tripped byte-for-byte\n", label, frame.len);
    rcp_bytes_free(&frame);
    return 0;
}

typedef struct {
    rcp_avtp_transport_t *t;
    int                     result;
} close_unblock_args_t;

static void *close_unblock_recv_thread(void *arg)
{
    close_unblock_args_t *a       = (close_unblock_args_t *)arg;
    rcp_context_t           ctx     = rcp_context_with_timeout_ms(5000);
    uint8_t                  buf[64];
    size_t                     out_len = 0;

    a->result = rcp_avtp_transport_recv(a->t, &ctx, buf, sizeof(buf), &out_len);
    return NULL;
}

/* REQ-L2-008: close() called from one thread must unblock a concurrent
 * in-progress recv() on another thread, reliably and without invalidating
 * the underlying fd out from under it -- the same contract udp.c's own
 * test_close_unblocks_in_progress_recv() (tests/test_udp.c) verifies for
 * the UDP transport, exercised here for real over a real raw socket. */
static int test_close_unblocks_in_progress_recv(rcp_avtp_transport_t *t)
{
    close_unblock_args_t args;
    pthread_t              th;
    int                      rc;

    args.t      = t;
    args.result = RCP_OK;
    if (pthread_create(&th, NULL, close_unblock_recv_thread, &args) != 0) {
        fprintf(stderr, "[close-unblocks-recv] pthread_create() failed\n");
        return 1;
    }

    /* Give the reader thread a moment to actually enter recv() and start
     * polling before close() runs -- a few poll slices' worth is enough
     * (mirrors test_udp.c's own identical busy-wait rationale). */
    usleep(60000);

    rc = rcp_avtp_transport_close(t);
    pthread_join(th, NULL);

    if (rc != RCP_OK) {
        fprintf(stderr, "[close-unblocks-recv] close() returned rc=%d, want RCP_OK\n", rc);
        return 1;
    }
    if (args.result != RCP_ERR_CLOSED) {
        fprintf(stderr,
                "[close-unblocks-recv] recv() returned rc=%d after close(), want RCP_ERR_CLOSED\n",
                args.result);
        return 1;
    }

    printf("[close-unblocks-recv] OK: concurrent recv() unblocked with RCP_ERR_CLOSED\n");
    return 0;
}

int main(void)
{
    rcp_avtp_transport_t *a; /* bound to veth0 */
    rcp_avtp_transport_t *b; /* bound to veth1 */
    int                     failures = 0;
    uint8_t                  a_mac[6];
    uint8_t                   b_mac[6];

    a = rcp_l2_avtp_transport_new("veth0", k_broadcast_mac, false);
    b = rcp_l2_avtp_transport_new("veth1", k_broadcast_mac, false);

    if (!rcp_l2_avtp_transport_ok(a) || !rcp_l2_avtp_transport_ok(b)) {
        fprintf(stderr,
                "L2 transport not available/privileged -- expected CAP_NET_RAW/root and a "
                "pre-existing veth0/veth1 pair (see ci.yml's own \"L2 transport (veth)\" job)\n");
        rcp_avtp_transport_release(a);
        rcp_avtp_transport_release(b);
        return 1;
    }

    if (!rcp_l2_avtp_transport_local_mac(a, a_mac) || !rcp_l2_avtp_transport_local_mac(b, b_mac)) {
        fprintf(stderr, "local_mac() failed on an ok() transport\n");
        rcp_avtp_transport_release(a);
        rcp_avtp_transport_release(b);
        return 1;
    }
    printf("veth0 (a) MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", a_mac[0], a_mac[1], a_mac[2],
           a_mac[3], a_mac[4], a_mac[5]);
    printf("veth1 (b) MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", b_mac[0], b_mac[1], b_mac[2],
           b_mac[3], b_mac[4], b_mac[5]);

    failures += roundtrip_one_way("veth0->veth1", a, b, 0x11);
    failures += roundtrip_one_way("veth1->veth0", b, a, 0x22);
    /* a is deliberately closed (not just released) here, before either
     * transport is torn down below: this exercises REQ-L2-008 against a
     * transport that has already done real send()/recv() traffic, not a
     * freshly-constructed one, closer to how a real caller would use it. */
    failures += test_close_unblocks_in_progress_recv(a);

    rcp_avtp_transport_release(a);
    rcp_avtp_transport_release(b);

    if (failures == 0) {
        printf("PASS: L2 transport round-tripped real frames byte-for-byte over a real veth "
               "pair in both directions\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d round-trip(s) failed\n", failures);
    return 1;
}
