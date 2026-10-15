#include "softusb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SOFTUSB_ZTEST
#include <zephyr/ztest.h>
#endif

static unsigned checks;
#ifdef SOFTUSB_ZTEST
#define CHECK(x) do { ++checks; zassert_true((x), "%s:%d: %s", __FILE__, __LINE__, #x); } while (0)
#else
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#endif
#define OK(x) CHECK((x) == SOFTUSB_OK)

static void roundtrip(bool ls, const uint8_t *raw, size_t len)
{
    uint8_t states[SOFTUSB_MAX_LINE_STATES + 2];
    uint8_t copy[SOFTUSB_MAX_PACKET_BYTES + 2];
    size_t n = 99, m = 99;
    softusb_packet p;
    memset(states, 0x77, sizeof(states));
    memset(copy, 0x66, sizeof(copy));
    OK(softusb_encode(ls, raw, len, states + 1, sizeof(states) - 2, &n));
    CHECK(states[0] == 0x77 && states[n + 1] == 0x77);
    CHECK(n <= (ls ? SOFTUSB_LS_MAX_LINE_STATES : SOFTUSB_MAX_LINE_STATES));
    CHECK(states[n - 2] == SOFTUSB_SE0 && states[n - 1] == SOFTUSB_SE0);
    CHECK(states[n] == (ls ? SOFTUSB_DM : SOFTUSB_DP));
    OK(softusb_decode(ls, states + 1, n, copy + 1, sizeof(copy) - 2, &m));
    CHECK(m == len && memcmp(copy + 1, raw, len) == 0);
    CHECK(copy[0] == 0x66 && copy[m + 1] == 0x66);
    OK(softusb_parse_packet(ls, copy + 1, m, &p));
    /* Short buffers and exact-capacity buffers exercise both boundaries. */
    CHECK(softusb_encode(ls, raw, len, states + 1, n - 1, &m) == SOFTUSB_ERR_CAPACITY);
    CHECK(m == 0);
    OK(softusb_encode(ls, raw, len, states + 1, n, &m));
    CHECK(m == n);
    CHECK(softusb_decode(ls, states + 1, n, copy + 1, len - 1, &m) == SOFTUSB_ERR_CAPACITY);
    CHECK(m == 0 && copy[len] == raw[len - 1] && copy[len + 1] == 0x66);
    OK(softusb_decode(ls, states + 1, n, copy + 1, len, &m));
}
void softusb_test_codec(void)
{
    uint8_t raw[SOFTUSB_MAX_PACKET_BYTES], data[SOFTUSB_FS_MAX_PAYLOAD + 1];
    uint8_t states[SOFTUSB_MAX_LINE_STATES], copy[SOFTUSB_MAX_PACKET_BYTES];
    uint8_t pid;
    size_t n, m, len;
    unsigned i, k;
    softusb_packet p;
    static const uint8_t token_pids[] = {SOFTUSB_PID_OUT, SOFTUSB_PID_IN, SOFTUSB_PID_SETUP};
    static const uint8_t hs_pids[] = {SOFTUSB_PID_ACK, SOFTUSB_PID_NAK, SOFTUSB_PID_STALL};
    CHECK(SOFTUSB_SE0 == 0 && SOFTUSB_DP == 1 && SOFTUSB_DM == 2 && SOFTUSB_SE1 == 3);
    CHECK(softusb_crc5(0) == 2);
    CHECK(softusb_crc16(NULL, 0) == 0);
    CHECK(softusb_crc16((const uint8_t *)"123456789", 9) == 0xb4c8);
    for (i = 0; i < 256; ++i) {
        CHECK(softusb_pid_valid((uint8_t)i) == (((i >> 4) ^ (i & 15)) == 15));
        CHECK(softusb_pid_valid(softusb_pid_byte((uint8_t)i)));
    }
    for (i = 0; i < 2048; ++i) {
        for (k = 0; k < 3; ++k) {
            OK(softusb_build_token(token_pids[k], (uint8_t)(i & 127), (uint8_t)(i >> 7), raw, sizeof(raw), &n));
            OK(softusb_parse_packet(true, raw, n, &p));
            CHECK(p.address == (i & 127) && p.endpoint == (i >> 7) && p.pid == token_pids[k]);
            roundtrip(true, raw, n);
            roundtrip(false, raw, n);
            raw[2] ^= 8;
            CHECK(softusb_parse_packet(true, raw, n, &p) == SOFTUSB_ERR_CRC);
        }
        OK(softusb_build_sof((uint16_t)i, raw, sizeof(raw), &n));
        OK(softusb_parse_packet(false, raw, n, &p));
        CHECK(p.frame == i && p.kind == SOFTUSB_PACKET_SOF);
        roundtrip(false, raw, n);
    }
    for (k = 0; k < 3; ++k) {
        OK(softusb_build_handshake(hs_pids[k], raw, sizeof(raw), &n));
        roundtrip(true, raw, n);
        roundtrip(false, raw, n);
    }
    for (i = 0; i < 256; ++i) {
        memset(data, (int)i, sizeof(data));
        for (len = 0; len <= 8; ++len) {
            for (k = 0; k < 2; ++k) {
                pid = k ? SOFTUSB_PID_DATA1 : SOFTUSB_PID_DATA0;
                OK(softusb_build_data(true, pid, data, len, raw, sizeof(raw), &n));
                roundtrip(true, raw, n);
                roundtrip(false, raw, n);
                raw[n - 1] ^= 1;
                CHECK(softusb_parse_packet(true, raw, n, &p) == SOFTUSB_ERR_CRC);
            }
        }
    }
    memset(data, 0xff, sizeof(data));
    OK(softusb_build_data(false, SOFTUSB_PID_DATA1, data, 1023, raw, sizeof(raw), &n));
    roundtrip(false, raw, n);
    CHECK(softusb_parse_packet(true, raw, n, &p) == SOFTUSB_ERR_LENGTH);
    CHECK(softusb_build_data(false, SOFTUSB_PID_DATA0, data, 1024, raw, sizeof(raw), &n) == SOFTUSB_ERR_LENGTH && n == 0);
    CHECK(softusb_build_data(true, SOFTUSB_PID_DATA0, data, 9, raw, sizeof(raw), &n) == SOFTUSB_ERR_LENGTH && n == 0);
    OK(softusb_build_data(true, SOFTUSB_PID_DATA0, data, 8, raw, sizeof(raw), &n));
    OK(softusb_encode(true, raw, n, states, sizeof(states), &m));
    states[0] ^= 3;
    CHECK(softusb_decode(true, states, m, copy, sizeof(copy), &n) == SOFTUSB_ERR_SYNC && n == 0);
    states[0] ^= 3;
    states[m - 1] = SOFTUSB_DP;
    CHECK(softusb_decode(true, states, m, copy, sizeof(copy), &n) == SOFTUSB_ERR_EOP && n == 0);
    states[m - 1] = SOFTUSB_DM;
    states[12] = SOFTUSB_SE1;
    CHECK(softusb_decode(true, states, m, copy, sizeof(copy), &n) == SOFTUSB_ERR_LINE && n == 0);
    CHECK(softusb_build_token(SOFTUSB_PID_OUT, 128, 0, raw, sizeof(raw), &n) == SOFTUSB_ERR_ARGUMENT);
    CHECK(softusb_build_token(SOFTUSB_PID_OUT, 0, 16, raw, sizeof(raw), &n) == SOFTUSB_ERR_ARGUMENT);
    CHECK(softusb_build_sof(2048, raw, sizeof(raw), &n) == SOFTUSB_ERR_ARGUMENT);
    CHECK(softusb_parse_packet(true, NULL, 1, &p) == SOFTUSB_ERR_ARGUMENT);
    CHECK(softusb_parse_packet(true, raw, 0, &p) == SOFTUSB_ERR_LENGTH);
    CHECK(softusb_build_data(true, SOFTUSB_PID_DATA0, NULL, 1, raw, sizeof(raw), &n) == SOFTUSB_ERR_ARGUMENT);
    CHECK(softusb_encode(true, raw, 0, states, sizeof(states), NULL) == SOFTUSB_ERR_ARGUMENT);
    raw[0] = 0;
    CHECK(softusb_parse_packet(true, raw, 1, &p) == SOFTUSB_ERR_PID);
    raw[0] = softusb_pid_byte(7); /* high-speed/reserved in this codec */
    CHECK(softusb_parse_packet(true, raw, 1, &p) == SOFTUSB_ERR_UNSUPPORTED);
    raw[0] = SOFTUSB_PID_ACK;
    CHECK(softusb_parse_packet(true, raw, 2, &p) == SOFTUSB_ERR_LENGTH);
    CHECK(strcmp(softusb_status_string(SOFTUSB_ERR_STUFF), "stuff") == 0);
}
static softusb_response token(softusb_device *d, uint8_t pid, uint8_t addr, uint8_t ep)
{
    uint8_t b[3]; size_t n; softusb_response r;
    OK(softusb_build_token(pid, addr, ep, b, sizeof(b), &n));
    OK(softusb_device_receive(d, b, n, &r));
    return r;
}
static softusb_response data(softusb_device *d, uint8_t pid, const uint8_t *v, size_t len)
{
    uint8_t b[11]; size_t n; softusb_response r;
    OK(softusb_build_data(true, pid, v, len, b, sizeof(b), &n));
    OK(softusb_device_receive(d, b, n, &r));
    return r;
}
static softusb_response ack(softusb_device *d)
{
    const uint8_t b = SOFTUSB_PID_ACK; softusb_response r;
    OK(softusb_device_receive(d, &b, 1, &r)); return r;
}
void softusb_test_transactions(void)
{
    softusb_device d, before;
    softusb_response r, saved;
    softusb_packet p;
    uint8_t setup[8] = {0x80,6,0,1,0,0,18,0}, v[8] = {1,2,3,4,5,6,7,8}, out[8];
    uint8_t bad[11], ignored = SOFTUSB_PID_NAK;
    size_t len; bool is_setup;
    OK(softusb_device_init(&d, 0, 3));
    r = token(&d, SOFTUSB_PID_IN, 1, 0); CHECK(r.len == 0);
    r = token(&d, SOFTUSB_PID_IN, 0, 2); CHECK(r.len == 0);
    r = token(&d, SOFTUSB_PID_IN, 0, 0); CHECK(r.len == 1 && r.bytes[0] == SOFTUSB_PID_NAK);
    r = data(&d, SOFTUSB_PID_DATA0, v, 8); CHECK(!r.len && !r.out_accepted);
    r = token(&d, SOFTUSB_PID_OUT, 0, 1); CHECK(r.len == 0);
    r = data(&d, SOFTUSB_PID_DATA1, v, 8); CHECK(!r.len && !r.out_accepted);
    (void)token(&d, SOFTUSB_PID_OUT, 0, 1);
    r = data(&d, SOFTUSB_PID_DATA0, v, 8); CHECK(r.out_accepted && r.bytes[0] == SOFTUSB_PID_ACK && d.ep[1].out_data1);
    (void)token(&d, SOFTUSB_PID_OUT, 0, 1);
    r = data(&d, SOFTUSB_PID_DATA0, v, 8); CHECK(!r.out_accepted && r.bytes[0] == SOFTUSB_PID_ACK && d.ep[1].out_data1);
    (void)token(&d, SOFTUSB_PID_OUT, 0, 1);
    r = data(&d, SOFTUSB_PID_DATA1, v, 0); CHECK(!r.out_accepted && r.bytes[0] == SOFTUSB_PID_NAK && d.ep[1].out_data1);
    CHECK(softusb_device_take_out(&d, 1, out, 7, &len, &is_setup) == SOFTUSB_ERR_CAPACITY && len == 0 && d.ep[1].out_pending);
    OK(softusb_device_take_out(&d, 1, out, 8, &len, &is_setup));
    CHECK(len == 8 && !is_setup && memcmp(out, v, 8) == 0);
    (void)token(&d, SOFTUSB_PID_OUT, 0, 1);
    r = data(&d, SOFTUSB_PID_DATA1, v, 0); CHECK(r.out_accepted && !d.ep[1].out_data1);
    OK(softusb_device_take_out(&d, 1, NULL, 0, &len, &is_setup)); CHECK(len == 0);
    CHECK(softusb_device_take_out(&d, 1, out, 8, &len, &is_setup) == SOFTUSB_ERR_STATE);
    OK(softusb_device_queue_in(&d, 1, v, 8));
    CHECK(softusb_device_queue_in(&d, 1, v, 8) == SOFTUSB_ERR_STATE);
    r = token(&d, SOFTUSB_PID_IN, 0, 1); saved = r;
    CHECK(r.len == 11 && r.bytes[0] == SOFTUSB_PID_DATA0 && d.ep[1].in_pending && !d.ep[1].in_data1);
    r = token(&d, SOFTUSB_PID_IN, 0, 1); CHECK(r.len == saved.len && memcmp(r.bytes, saved.bytes, r.len) == 0);
    OK(softusb_device_receive(&d, &ignored, 1, &r)); CHECK(d.ep[1].in_pending);
    r = ack(&d); CHECK(!r.in_acked && d.ep[1].in_pending);
    r = token(&d, SOFTUSB_PID_IN, 0, 1); CHECK(r.bytes[0] == SOFTUSB_PID_DATA0);
    r = ack(&d); CHECK(r.in_acked && !d.ep[1].in_pending && d.ep[1].in_data1);
    r = ack(&d); CHECK(!r.in_acked && d.ep[1].in_data1);
    OK(softusb_device_queue_in(&d, 1, NULL, 0));
    r = token(&d, SOFTUSB_PID_IN, 0, 1); OK(softusb_parse_packet(true, r.bytes, r.len, &p));
    CHECK(p.pid == SOFTUSB_PID_DATA1 && p.payload_len == 0);
    (void)token(&d, SOFTUSB_PID_OUT, 2, 0); r = ack(&d); CHECK(!r.in_acked && d.ep[1].in_pending);
    OK(softusb_device_queue_in(&d, 0, v, 8));
    (void)token(&d, SOFTUSB_PID_SETUP, 0, 0);
    r = data(&d, SOFTUSB_PID_DATA0, setup, 8);
    CHECK(r.setup_accepted && r.out_accepted && r.bytes[0] == SOFTUSB_PID_ACK);
    CHECK(d.ep[0].in_data1 && d.ep[0].out_data1 && !d.ep[0].in_pending);
    CHECK(d.ep[1].in_pending); /* SETUP only resets EP0 */
    OK(softusb_device_take_out(&d, 0, out, 8, &len, &is_setup)); CHECK(is_setup && len == 8 && memcmp(out, setup, 8) == 0);
    (void)token(&d, SOFTUSB_PID_OUT, 0, 0);
    r = data(&d, SOFTUSB_PID_DATA1, v, 8); CHECK(r.out_accepted && !d.ep[0].out_data1);
    (void)token(&d, SOFTUSB_PID_SETUP, 0, 0);
    r = data(&d, SOFTUSB_PID_DATA0, setup, 8); CHECK(r.setup_accepted && d.ep[0].out_is_setup);
    /* Wrong SETUP PID/length or bad CRC cannot reset a queued IN/toggles. */
    OK(softusb_device_queue_in(&d, 0, v, 8));
    (void)token(&d, SOFTUSB_PID_SETUP, 0, 0);
    OK(softusb_build_data(true, SOFTUSB_PID_DATA1, setup, 8, bad, 11, &len));
    before = d;
    CHECK(softusb_device_receive(&d, bad, len, &r) == SOFTUSB_ERR_STATE);
    CHECK(d.ep[0].in_pending && memcmp(d.ep, before.ep, sizeof(d.ep)) == 0 && !r.len);
    (void)token(&d, SOFTUSB_PID_SETUP, 0, 0);
    OK(softusb_build_data(true, SOFTUSB_PID_DATA0, setup, 7, bad, 11, &len));
    CHECK(softusb_device_receive(&d, bad, len, &r) == SOFTUSB_ERR_LENGTH && !r.len && d.ep[0].in_pending);
    (void)token(&d, SOFTUSB_PID_OUT, 0, 1);
    OK(softusb_build_data(true, SOFTUSB_PID_DATA0, v, 8, bad, 11, &len)); bad[10] ^= 1;
    before = d;
    CHECK(softusb_device_receive(&d, bad, len, &r) == SOFTUSB_ERR_CRC && !r.len);
    CHECK(!d.token_pending && memcmp(d.ep, before.ep, sizeof(d.ep)) == 0);
    r = data(&d, SOFTUSB_PID_DATA0, v, 8); CHECK(!r.len && !r.out_accepted);
    (void)token(&d, SOFTUSB_PID_SETUP, 0, 1);
    r = data(&d, SOFTUSB_PID_DATA0, setup, 8); CHECK(!r.len && !r.setup_accepted);
    OK(softusb_device_set_address(&d, 37));
    r = token(&d, SOFTUSB_PID_IN, 0, 0); CHECK(!r.len);
    r = token(&d, SOFTUSB_PID_IN, 37, 0); CHECK(r.len == 11 && r.bytes[0] == SOFTUSB_PID_DATA1);
    CHECK(softusb_device_queue_in(&d, 2, v, 8) == SOFTUSB_ERR_ARGUMENT);
    CHECK(softusb_device_queue_in(&d, 16, v, 8) == SOFTUSB_ERR_ARGUMENT);
    CHECK(softusb_device_set_address(&d, 128) == SOFTUSB_ERR_ARGUMENT);
    OK(softusb_device_init(&d, 0, 1)); CHECK(!d.ep[0].in_data1 && !d.ep[0].out_seen);
}
#ifndef SOFTUSB_ZTEST
int main(void)
{
    softusb_test_codec(); softusb_test_transactions();
    printf("softusb native: %u checks passed\n", checks);
    return 0;
}
#endif
