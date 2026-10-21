/* SPDX-License-Identifier: MPL-2.0 */
/* Issue #618, Test plan T2: the pure, allocation-free ingress stages
 * (include/rcp/ingress.h) -- AVTPDU parse, the member iterator, the
 * once-per-frame timing decision and the TC18-11.2.1-005 fixed-field
 * check. The composed pipeline (rcp_mock_server_receive_avtpdu()) is
 * tested in test_mock.c / test_tc18_gaps_e2e.c. */
#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/ingress.h>
#include <rcp/request.h>

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMac[6] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};

/* Encodes a minimal ACF_ABB request: 8-octet header, payload_len octets
 * of payload. */
static rcp_bytes_t abb(rcp_byte_bus_id_t bus, uint8_t tn, size_t payload_len)
{
    rcp_acf_byte_message_info_t h;
    uint8_t                     pl[64];

    memset(&h, 0, sizeof(h));
    memset(pl, 0x5A, sizeof(pl));
    h.byte_bus_id     = bus;
    h.transaction_num = tn;
    h.op              = (uint8_t)RCP_ACF_OP_WRITE;
    return rcp_acf_encode_abb(&h, pl, payload_len);
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

//cfusa:test REQ-INGRESS-002
static void test_ingress_strerror_unique_nonempty(void)
{
    const rcp_ingress_errc_t codes[] = {RCP_INGRESS_OK, RCP_INGRESS_ERR_SHORT_FRAME,
                                        RCP_INGRESS_ERR_BAD_SUBTYPE, RCP_INGRESS_ERR_BAD_VERSION};
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *s = rcp_ingress_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) TEST_ASSERT_TRUE(strcmp(s, rcp_ingress_strerror(codes[j])) != 0);
    }
    TEST_ASSERT_NOT_NULL(rcp_ingress_strerror((rcp_ingress_errc_t)99));
}

/* ── Parse ─────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-INGRESS-002
static void test_parse_good_ntscf(void)
{
    rcp_avtp_ntscf_header_t h = {0};
    rcp_ingress_avtpdu_t    f;
    rcp_bytes_t             m = abb(3, 9, 4);
    rcp_bytes_t             pdu;

    h.sv           = 1;
    h.sequence_num = 0x42;
    h.stream_id    = rcp_stream_id_make(kMac, 0x0102);
    pdu            = rcp_avtp_encode_ntscf(&h, m.data, m.len);
    TEST_ASSERT_NOT_NULL(pdu.data);

    TEST_ASSERT_EQUAL(RCP_INGRESS_OK, rcp_ingress_parse_avtpdu(pdu.data, pdu.len, &f));
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_NTSCF, f.subtype);
    TEST_ASSERT_EQUAL_HEX8(0x80u, f.header_octet1);
    TEST_ASSERT_EQUAL_UINT8(1u, f.sv);
    TEST_ASSERT_EQUAL_UINT8(0u, f.version);
    TEST_ASSERT_EQUAL_UINT8(0u, f.tv);
    TEST_ASSERT_EQUAL_UINT8(0u, f.tu);
    TEST_ASSERT_EQUAL_UINT8(0x42u, f.sequence_num);
    TEST_ASSERT_TRUE(f.stream_id == rcp_stream_id_to_u64(h.stream_id));
    TEST_ASSERT_EQUAL_UINT32(0u, f.avtp_timestamp);
    TEST_ASSERT_FALSE(f.tscf_reserved_all_zero);
    TEST_ASSERT_EQUAL_UINT(m.len, f.payload_len);
    TEST_ASSERT_TRUE(f.payload == pdu.data + RCP_AVTP_NTSCF_HEADER_LEN);

    rcp_bytes_free(&pdu);
    rcp_bytes_free(&m);
}

//cfusa:test REQ-INGRESS-002
static void test_parse_good_tscf_keeps_header_octet1_byte_for_byte(void)
{
    rcp_avtp_tscf_header_t h = {0};
    rcp_ingress_avtpdu_t   f;
    rcp_bytes_t            m = abb(3, 9, 4);
    rcp_bytes_t            pdu;

    h.sv             = 1;
    h.mr             = 1;
    h.tv             = 1;
    h.tu             = 1;
    h.sequence_num   = 7;
    h.stream_id      = rcp_stream_id_make(kMac, 0x0203);
    h.avtp_timestamp = 0xCAFEBABEu;
    pdu              = rcp_avtp_encode_tscf(&h, m.data, m.len);
    TEST_ASSERT_NOT_NULL(pdu.data);

    TEST_ASSERT_EQUAL(RCP_INGRESS_OK, rcp_ingress_parse_avtpdu(pdu.data, pdu.len, &f));
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_TSCF, f.subtype);
    TEST_ASSERT_EQUAL_HEX8(pdu.data[1], f.header_octet1);
    TEST_ASSERT_EQUAL_HEX8(0x89u, f.header_octet1); /* sv | mr | tv */
    TEST_ASSERT_EQUAL_UINT8(1u, f.mr);
    TEST_ASSERT_EQUAL_UINT8(1u, f.tv);
    TEST_ASSERT_EQUAL_UINT8(1u, f.tu);
    TEST_ASSERT_EQUAL_UINT8(7u, f.sequence_num);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABEu, f.avtp_timestamp);
    TEST_ASSERT_TRUE(f.tscf_reserved_all_zero); /* conformant encoder */
    TEST_ASSERT_EQUAL_UINT(m.len, f.payload_len);

    /* Non-zero reserved bytes on the wire: flag false. */
    pdu.data[17] = 0x01u;
    TEST_ASSERT_EQUAL(RCP_INGRESS_OK, rcp_ingress_parse_avtpdu(pdu.data, pdu.len, &f));
    TEST_ASSERT_FALSE(f.tscf_reserved_all_zero);

    rcp_bytes_free(&pdu);
    rcp_bytes_free(&m);
}

/* NTSCF's octet 1 low bits are ntscf_data_length[10:8]: header_octet1 is
 * still the raw byte (what the CRC covers), not a re-packed sv/version. */
//cfusa:test REQ-INGRESS-002
static void test_parse_ntscf_header_octet1_carries_length_bits(void)
{
    rcp_avtp_ntscf_header_t h = {0};
    rcp_ingress_avtpdu_t    f;
    uint8_t                 payload[300];
    rcp_bytes_t             pdu;

    memset(payload, 0, sizeof(payload));
    h.sv = 1;
    pdu  = rcp_avtp_encode_ntscf(&h, payload, sizeof(payload));
    TEST_ASSERT_NOT_NULL(pdu.data);
    TEST_ASSERT_EQUAL(RCP_INGRESS_OK, rcp_ingress_parse_avtpdu(pdu.data, pdu.len, &f));
    TEST_ASSERT_EQUAL_HEX8(0x81u, f.header_octet1);
    rcp_bytes_free(&pdu);
}

//cfusa:test REQ-INGRESS-002
static void test_parse_rejects_short_bad_subtype_bad_version_and_overlong_length(void)
{
    uint8_t              ntscf[RCP_AVTP_NTSCF_HEADER_LEN] = {0};
    uint8_t              tscf[RCP_AVTP_TSCF_HEADER_LEN]   = {0};
    rcp_ingress_avtpdu_t f;

    memset(&f, 0xA5, sizeof(f));
    ntscf[0] = RCP_AVTP_SUBTYPE_NTSCF;
    ntscf[1] = 0x80u;
    tscf[0]  = RCP_AVTP_SUBTYPE_TSCF;
    tscf[1]  = 0x80u;

    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_SHORT_FRAME, rcp_ingress_parse_avtpdu(ntscf, 0, &f));
    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_SHORT_FRAME, rcp_ingress_parse_avtpdu(ntscf, 5, &f));
    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_SHORT_FRAME, rcp_ingress_parse_avtpdu(tscf, 10, &f));

    /* stream_data_length / ntscf_data_length past the end of the buffer */
    ntscf[2] = 4u;
    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_SHORT_FRAME, rcp_ingress_parse_avtpdu(ntscf, sizeof(ntscf), &f));
    ntscf[2] = 0u;
    tscf[21] = 4u;
    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_SHORT_FRAME, rcp_ingress_parse_avtpdu(tscf, sizeof(tscf), &f));
    tscf[21] = 0u;

    ntscf[0] = 0x00u;
    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_BAD_SUBTYPE, rcp_ingress_parse_avtpdu(ntscf, sizeof(ntscf), &f));
    ntscf[0] = RCP_AVTP_SUBTYPE_NTSCF;

    ntscf[1] = 0x90u; /* version 1 */
    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_BAD_VERSION, rcp_ingress_parse_avtpdu(ntscf, sizeof(ntscf), &f));
    tscf[1] = 0xF0u; /* version 7 */
    TEST_ASSERT_EQUAL(RCP_INGRESS_ERR_BAD_VERSION, rcp_ingress_parse_avtpdu(tscf, sizeof(tscf), &f));

    /* *out untouched on every error */
    TEST_ASSERT_EQUAL_HEX8(0xA5u, f.subtype);

    /* boundary: the same NTSCF header with version 0 parses */
    ntscf[1] = 0x80u;
    TEST_ASSERT_EQUAL(RCP_INGRESS_OK, rcp_ingress_parse_avtpdu(ntscf, sizeof(ntscf), &f));
    TEST_ASSERT_EQUAL_UINT(0u, f.payload_len);
}

/* ── Member iterator ───────────────────────────────────────────────────────── */

/* Counts members of a payload made of n back-to-back 8-octet ABBs. */
static size_t count_members(size_t n, bool *out_malformed)
{
    uint8_t                   buf[2048];
    rcp_ingress_member_iter_t it;
    const uint8_t            *m;
    size_t                    mlen;
    size_t                    i;
    size_t                    seen = 0;

    TEST_ASSERT_TRUE(n * 8u <= sizeof(buf));
    for (i = 0; i < n; i++) {
        rcp_bytes_t one = abb((rcp_byte_bus_id_t)(i & 0x7FFu), (uint8_t)i, 0);
        TEST_ASSERT_EQUAL_UINT(8u, one.len);
        memcpy(&buf[i * 8u], one.data, 8u);
        rcp_bytes_free(&one);
    }
    rcp_ingress_member_iter_init(&it, n ? buf : NULL, n * 8u);
    while (rcp_ingress_member_next(&it, &m, &mlen)) {
        TEST_ASSERT_EQUAL_UINT(8u, mlen);
        TEST_ASSERT_TRUE(m == &buf[seen * 8u]);
        seen++;
    }
    TEST_ASSERT_EQUAL_UINT(seen, it.index);
    *out_malformed = it.malformed;
    return seen;
}

/* 0, 1, 32, 33 and 250 members (250 x 8 octets fits one NTSCF AVTPDU's
 * 2047-octet data length): no fixed cap anywhere. */
//cfusa:test REQ-INGRESS-008
static void test_member_iterator_has_no_member_cap(void)
{
    const size_t counts[] = {0u, 1u, 32u, 33u, 250u};
    size_t       i;

    for (i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        bool malformed = true;
        TEST_ASSERT_EQUAL_UINT(counts[i], count_members(counts[i], &malformed));
        TEST_ASSERT_FALSE(malformed);
    }
}

//cfusa:test REQ-INGRESS-008
static void test_member_iterator_flags_malformed_tails(void)
{
    rcp_bytes_t               a = abb(1, 1, 4); /* 12 octets */
    uint8_t                   buf[64];
    rcp_ingress_member_iter_t it;
    const uint8_t            *m;
    size_t                    mlen;

    /* truncated tail: one whole member, then 5 octets of a second */
    memcpy(buf, a.data, a.len);
    memcpy(&buf[a.len], a.data, 5u);
    rcp_ingress_member_iter_init(&it, buf, a.len + 5u);
    TEST_ASSERT_TRUE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_EQUAL_UINT(a.len, mlen);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_TRUE(it.malformed);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen)); /* sticky */

    /* a single trailing octet */
    rcp_ingress_member_iter_init(&it, buf, a.len + 1u);
    TEST_ASSERT_TRUE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_TRUE(it.malformed);

    /* unknown acf_msg_type */
    memcpy(buf, a.data, a.len);
    buf[0] = (uint8_t)(0x01u << 1);
    rcp_ingress_member_iter_init(&it, buf, a.len);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_TRUE(it.malformed);

    /* fewer octets than the ABB header */
    memcpy(buf, a.data, a.len);
    rcp_ingress_member_iter_init(&it, buf, 6u);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_TRUE(it.malformed);

    /* declared acf_msg_length (1 quadlet) shorter than the header */
    buf[1] = 1u;
    rcp_ingress_member_iter_init(&it, buf, a.len);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_TRUE(it.malformed);

    /* declared length past the end of the payload */
    buf[1] = 4u;
    rcp_ingress_member_iter_init(&it, buf, a.len);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_TRUE(it.malformed);

    rcp_bytes_free(&a);
}

//cfusa:test REQ-INGRESS-008
static void test_member_iterator_walks_gbb_members(void)
{
    rcp_acf_gbb_header_t      g;
    rcp_bytes_t               gm;
    rcp_bytes_t               am = abb(2, 2, 0);
    uint8_t                   buf[64];
    rcp_ingress_member_iter_t it;
    const uint8_t            *m;
    size_t                    mlen;
    const uint8_t             pl[3] = {1, 2, 3};

    memset(&g, 0, sizeof(g));
    g.info.byte_bus_id = 4;
    gm                 = rcp_acf_encode_gbb(&g, pl, sizeof(pl));
    TEST_ASSERT_NOT_NULL(gm.data);
    memcpy(buf, gm.data, gm.len);
    memcpy(&buf[gm.len], am.data, am.len);

    rcp_ingress_member_iter_init(&it, buf, gm.len + am.len);
    TEST_ASSERT_TRUE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_EQUAL_UINT(gm.len, mlen);
    TEST_ASSERT_TRUE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_EQUAL_UINT(am.len, mlen);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_FALSE(it.malformed);

    /* a GBB member cut inside its 16-octet header */
    rcp_ingress_member_iter_init(&it, buf, 12u);
    TEST_ASSERT_FALSE(rcp_ingress_member_next(&it, &m, &mlen));
    TEST_ASSERT_TRUE(it.malformed);

    rcp_bytes_free(&gm);
    rcp_bytes_free(&am);
}

/* ── resolve_timing ────────────────────────────────────────────────────────── */

/* Independent reference model of ingress.h's documented precedence. */
static rcp_ingress_timing_t model_timing(bool tscf, bool ts, rcp_avtp_tscf_fallback_t up,
                                         rcp_avtp_tscf_reserved_zero_policy_t rz, bool rzero,
                                         bool tv, uint8_t *sub, bool *etv)
{
    *sub = (uint8_t)(tscf ? RCP_AVTP_SUBTYPE_TSCF : RCP_AVTP_SUBTYPE_NTSCF);
    *etv = false;
    if (!tscf) return RCP_INGRESS_TIMING_IMMEDIATE;
    if (!ts) {
        return up == RCP_AVTP_TSCF_FALLBACK_DROP ? RCP_INGRESS_TIMING_DROP
                                                 : RCP_INGRESS_TIMING_IMMEDIATE;
    }
    if (rzero && rz == RCP_AVTP_TSCF_RESERVED_ZERO_DROP) return RCP_INGRESS_TIMING_DROP;
    if (rzero && rz == RCP_AVTP_TSCF_RESERVED_ZERO_AS_NTSCF) {
        *sub = RCP_AVTP_SUBTYPE_NTSCF;
        return RCP_INGRESS_TIMING_IMMEDIATE;
    }
    if (!tv) return RCP_INGRESS_TIMING_IMMEDIATE;
    *etv = true;
    return RCP_INGRESS_TIMING_PRESENTATION;
}

/* Full truth table: subtype x time_sync x unsupported_policy x
 * reserved_zero_policy x reserved_all_zero x tv x tu (tu never changes
 * the outcome -- REQ-AVTP-023). */
//cfusa:test REQ-INGRESS-003
static void test_resolve_timing_truth_table(void)
{
    unsigned bits;

    for (bits = 0; bits < (1u << 6) * 3u; bits++) {
        bool                                 tscf  = (bits & 1u) != 0;
        bool                                 ts    = (bits & 2u) != 0;
        rcp_avtp_tscf_fallback_t             up    = (bits & 4u) ? RCP_AVTP_TSCF_FALLBACK_IGNORE
                                                                 : RCP_AVTP_TSCF_FALLBACK_DROP;
        bool                                 rzero = (bits & 8u) != 0;
        bool                                 tv    = (bits & 16u) != 0;
        uint8_t                              tu    = (uint8_t)((bits & 32u) ? 1u : 0u);
        rcp_avtp_tscf_reserved_zero_policy_t rz =
            (rcp_avtp_tscf_reserved_zero_policy_t)(bits >> 6);
        rcp_ingress_avtpdu_t                 f;
        uint8_t                              sub = 0, msub = 0;
        bool                                 etv = true, metv = true;
        rcp_ingress_timing_t                 got, want;

        memset(&f, 0, sizeof(f));
        f.subtype                = (uint8_t)(tscf ? RCP_AVTP_SUBTYPE_TSCF : RCP_AVTP_SUBTYPE_NTSCF);
        f.tscf_reserved_all_zero = rzero;
        f.tv                     = (uint8_t)(tv ? 1u : 0u);
        f.tu                     = tu;

        got  = rcp_ingress_resolve_timing(&f, ts, up, rz, &sub, &etv);
        want = model_timing(tscf, ts, up, rz, rzero, tv, &msub, &metv);
        TEST_ASSERT_EQUAL_MESSAGE(want, got, "timing");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(msub, sub, "effective subtype");
        TEST_ASSERT_EQUAL_MESSAGE(metv, etv, "effective tv");
    }
}

static rcp_ingress_timing_t timing_of(uint8_t subtype, bool ts, rcp_avtp_tscf_fallback_t up,
                                      rcp_avtp_tscf_reserved_zero_policy_t rz, bool rzero,
                                      uint8_t tv)
{
    rcp_ingress_avtpdu_t f;
    uint8_t              sub;
    bool                 etv;

    memset(&f, 0, sizeof(f));
    f.subtype                = subtype;
    f.tscf_reserved_all_zero = rzero;
    f.tv                     = tv;
    return rcp_ingress_resolve_timing(&f, ts, up, rz, &sub, &etv);
}

/* MC/DC independence pairs: each condition flipped alone changes the
 * outcome. */
//cfusa:test REQ-INGRESS-003
static void test_resolve_timing_mcdc_pairs(void)
{
    const rcp_avtp_tscf_fallback_t D = RCP_AVTP_TSCF_FALLBACK_DROP;
    const rcp_avtp_tscf_fallback_t I = RCP_AVTP_TSCF_FALLBACK_IGNORE;
    const rcp_avtp_tscf_reserved_zero_policy_t TG = RCP_AVTP_TSCF_RESERVED_ZERO_TIME_GATE;
    const rcp_avtp_tscf_reserved_zero_policy_t AN = RCP_AVTP_TSCF_RESERVED_ZERO_AS_NTSCF;
    const rcp_avtp_tscf_reserved_zero_policy_t DR = RCP_AVTP_TSCF_RESERVED_ZERO_DROP;

    /* subtype */
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_PRESENTATION, timing_of(RCP_AVTP_SUBTYPE_TSCF, true, D, TG, false, 1));
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_IMMEDIATE, timing_of(RCP_AVTP_SUBTYPE_NTSCF, true, D, TG, false, 1));
    /* time_sync_supported */
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_DROP, timing_of(RCP_AVTP_SUBTYPE_TSCF, false, D, TG, false, 1));
    /* unsupported_policy */
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_IMMEDIATE, timing_of(RCP_AVTP_SUBTYPE_TSCF, false, I, TG, false, 1));
    /* reserved_all_zero (under DROP), and the policy values */
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_DROP, timing_of(RCP_AVTP_SUBTYPE_TSCF, true, D, DR, true, 1));
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_PRESENTATION, timing_of(RCP_AVTP_SUBTYPE_TSCF, true, D, DR, false, 1));
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_IMMEDIATE, timing_of(RCP_AVTP_SUBTYPE_TSCF, true, D, AN, true, 1));
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_PRESENTATION, timing_of(RCP_AVTP_SUBTYPE_TSCF, true, D, TG, true, 1));
    /* tv */
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_IMMEDIATE, timing_of(RCP_AVTP_SUBTYPE_TSCF, true, D, TG, true, 0));
}

/* The default reserved-zero policy is 0 = TIME_GATE, so a zero-initialized
 * caller (and every calloc()'d server) keeps the presentation-time gate
 * for a conformant (all-zero reserved bytes) frame. */
//cfusa:test REQ-INGRESS-004
static void test_resolve_timing_default_reserved_zero_policy_keeps_the_gate(void)
{
    rcp_avtp_tscf_reserved_zero_policy_t zero;
    rcp_ingress_avtpdu_t                 f;
    uint8_t                              sub = 0;
    bool                                 etv = false;

    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL(RCP_AVTP_TSCF_RESERVED_ZERO_TIME_GATE, zero);
    memset(&f, 0, sizeof(f));
    f.subtype                = RCP_AVTP_SUBTYPE_TSCF;
    f.tv                     = 1;
    f.tscf_reserved_all_zero = true;
    TEST_ASSERT_EQUAL(RCP_INGRESS_TIMING_PRESENTATION,
                      rcp_ingress_resolve_timing(&f, true, RCP_AVTP_TSCF_FALLBACK_DROP, zero, &sub,
                                                 &etv));
    TEST_ASSERT_TRUE(etv);
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_TSCF, sub);
}

/* ── Fixed fields ──────────────────────────────────────────────────────────── */

static void good_header(uint8_t out[8], uint8_t msg_type)
{
    rcp_acf_byte_message_info_t h;

    memset(&h, 0, sizeof(h));
    h.byte_bus_id = 5;
    h.op          = (uint8_t)RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(out, msg_type, 2u, &h);
}

/* Each TC18-11.2.1-005 fixed field set on its own is refused; the clean
 * header (the MC/DC baseline) passes. */
//cfusa:test REQ-INGRESS-010
static void test_fixed_fields_each_field_independently(void)
{
    uint8_t h[8];

    good_header(h, RCP_ACF_MSG_TYPE_ABB);
    TEST_ASSERT_TRUE(rcp_ingress_request_fixed_fields_valid(h, false));

    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[4] |= 0x02u; /* hs */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[6] |= 0x20u; /* err */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[6] |= 0x40u; /* rsp */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[2] |= 0x08u; /* rsv, octet 2 */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[2] |= 0x10u; /* rsv, octet 2 */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[4] |= 0x04u; /* rsv, octet 4 */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[4] |= 0x08u; /* rsv, octet 4 */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[2] |= 0x20u; /* ABB mtv */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
}

//cfusa:test REQ-INGRESS-010
static void test_fixed_fields_cs_and_gbb_mtv(void)
{
    uint8_t h[8];

    good_header(h, RCP_ACF_MSG_TYPE_ABB); h[4] |= 0x01u; /* cs */
    TEST_ASSERT_FALSE(rcp_ingress_request_fixed_fields_valid(h, false));
    TEST_ASSERT_TRUE(rcp_ingress_request_fixed_fields_valid(h, true));

    good_header(h, RCP_ACF_MSG_TYPE_GBB); h[2] |= 0x20u; /* GBB mtv is a real field */
    TEST_ASSERT_TRUE(rcp_ingress_request_fixed_fields_valid(h, false));
}

//cfusa:test REQ-INGRESS-010
static void test_cs_has_meaning_only_for_compound_and_chained(void)
{
    rcp_compound_step_t step;
    rcp_bytes_t         compound, chained, timed, wait, plain;

    memset(&step, 0, sizeof(step));
    compound = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND, 5, &step, 0, 1, NULL, 0);
    wait     = rcp_compound_encode_request(RCP_REQUEST_TYPE_COMPOUND_WAIT, 5, &step, 0, 1, NULL, 0);
    chained  = rcp_chained_encode_member(5, 0, 1, 2, NULL, 0);
    timed    = rcp_timed_encode_request(5, 1000u, 3, NULL, 0);
    plain    = abb(5, 4, 0);
    TEST_ASSERT_NOT_NULL(compound.data);
    TEST_ASSERT_NOT_NULL(wait.data);
    TEST_ASSERT_NOT_NULL(chained.data);
    TEST_ASSERT_NOT_NULL(timed.data);

    TEST_ASSERT_TRUE(rcp_ingress_cs_has_meaning(compound.data, compound.len));
    TEST_ASSERT_TRUE(rcp_ingress_cs_has_meaning(chained.data, chained.len));
    TEST_ASSERT_FALSE(rcp_ingress_cs_has_meaning(wait.data, wait.len));
    TEST_ASSERT_FALSE(rcp_ingress_cs_has_meaning(timed.data, timed.len));
    TEST_ASSERT_FALSE(rcp_ingress_cs_has_meaning(plain.data, plain.len));

    rcp_bytes_free(&compound);
    rcp_bytes_free(&wait);
    rcp_bytes_free(&chained);
    rcp_bytes_free(&timed);
    rcp_bytes_free(&plain);
}

//cfusa:test REQ-INGRESS-011
static void test_reserved_bits_zero_checks_both_fields(void)
{
    uint8_t h[8];

    good_header(h, RCP_ACF_MSG_TYPE_ABB);
    TEST_ASSERT_TRUE(rcp_acf_header_reserved_bits_zero(h));
    h[2] |= 0x08u;
    TEST_ASSERT_FALSE(rcp_acf_header_reserved_bits_zero(h));
    good_header(h, RCP_ACF_MSG_TYPE_ABB);
    h[4] |= 0x04u;
    TEST_ASSERT_FALSE(rcp_acf_header_reserved_bits_zero(h));
    /* neighbouring, non-reserved bits do not count */
    good_header(h, RCP_ACF_MSG_TYPE_ABB);
    h[2] |= 0x27u; /* mtv + bbid[10:8] */
    h[4] |= 0xF3u; /* evt + hs + cs */
    TEST_ASSERT_TRUE(rcp_acf_header_reserved_bits_zero(h));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ingress_strerror_unique_nonempty);
    RUN_TEST(test_parse_good_ntscf);
    RUN_TEST(test_parse_good_tscf_keeps_header_octet1_byte_for_byte);
    RUN_TEST(test_parse_ntscf_header_octet1_carries_length_bits);
    RUN_TEST(test_parse_rejects_short_bad_subtype_bad_version_and_overlong_length);
    RUN_TEST(test_member_iterator_has_no_member_cap);
    RUN_TEST(test_member_iterator_flags_malformed_tails);
    RUN_TEST(test_member_iterator_walks_gbb_members);
    RUN_TEST(test_resolve_timing_truth_table);
    RUN_TEST(test_resolve_timing_mcdc_pairs);
    RUN_TEST(test_resolve_timing_default_reserved_zero_policy_keeps_the_gate);
    RUN_TEST(test_fixed_fields_each_field_independently);
    RUN_TEST(test_fixed_fields_cs_and_gbb_mtv);
    RUN_TEST(test_cs_has_meaning_only_for_compound_and_chained);
    RUN_TEST(test_reserved_bits_zero_checks_both_fields);
    return UNITY_END();
}
