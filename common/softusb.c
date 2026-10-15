#include "softusb.h"
#include <string.h>

static bool token_pid(uint8_t pid)
{
    return pid == SOFTUSB_PID_OUT || pid == SOFTUSB_PID_IN || pid == SOFTUSB_PID_SETUP;
}
static bool data_pid(uint8_t pid)
{
    return pid == SOFTUSB_PID_DATA0 || pid == SOFTUSB_PID_DATA1;
}
static bool handshake_pid(uint8_t pid)
{
    return pid == SOFTUSB_PID_ACK || pid == SOFTUSB_PID_NAK || pid == SOFTUSB_PID_STALL;
}
uint8_t softusb_pid_byte(uint8_t nibble)
{
    nibble &= 15u;
    return (uint8_t)(nibble | ((nibble ^ 15u) << 4));
}
bool softusb_pid_valid(uint8_t pid)
{
    return ((pid >> 4) ^ (pid & 15u)) == 15u;
}
uint8_t softusb_crc5(uint16_t bits11)
{
    uint8_t crc = 31u;
    unsigned i;
    for (i = 0; i < 11; ++i) {
        uint8_t mix = (uint8_t)(((unsigned)crc ^ ((unsigned)bits11 >> i)) & 1u);
        crc >>= 1;
        if (mix) crc ^= 0x14u;
    }
    return (uint8_t)(crc ^ 31u);
}
uint16_t softusb_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xffffu;
    size_t i;
    unsigned bit;
    /* NULL is only valid for the empty sequence, as documented. */
    for (i = 0; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xa001u : 0u));
    }
    return (uint16_t)(crc ^ 0xffffu);
}
const char *softusb_status_string(softusb_status status)
{
    switch (status) {
    case SOFTUSB_OK: return "ok";
    case SOFTUSB_ERR_ARGUMENT: return "argument";
    case SOFTUSB_ERR_CAPACITY: return "capacity";
    case SOFTUSB_ERR_PID: return "pid";
    case SOFTUSB_ERR_LENGTH: return "length";
    case SOFTUSB_ERR_CRC: return "crc";
    case SOFTUSB_ERR_SYNC: return "sync";
    case SOFTUSB_ERR_STUFF: return "stuff";
    case SOFTUSB_ERR_EOP: return "eop";
    case SOFTUSB_ERR_LINE: return "line";
    case SOFTUSB_ERR_UNSUPPORTED: return "unsupported";
    case SOFTUSB_ERR_STATE: return "state";
    default: return "unknown";
    }
}
static softusb_status output_check(uint8_t *out, size_t capacity,
                                   size_t needed, size_t *out_len)
{
    if (!out_len) return SOFTUSB_ERR_ARGUMENT;
    *out_len = 0;
    if (!out) return SOFTUSB_ERR_ARGUMENT;
    if (capacity < needed) return SOFTUSB_ERR_CAPACITY;
    return SOFTUSB_OK;
}
softusb_status softusb_build_token(uint8_t pid, uint8_t address, uint8_t endpoint,
                                  uint8_t *out, size_t capacity, size_t *out_len)
{
    uint16_t field;
    softusb_status s = output_check(out, capacity, 3, out_len);
    if (s != SOFTUSB_OK) return s;
    if (!token_pid(pid)) return SOFTUSB_ERR_PID;
    if (address > 127 || endpoint > 15) return SOFTUSB_ERR_ARGUMENT;
    field = (uint16_t)(address | ((uint16_t)endpoint << 7));
    out[0] = pid;
    out[1] = (uint8_t)field;
    out[2] = (uint8_t)((field >> 8) | (softusb_crc5(field) << 3));
    *out_len = 3;
    return SOFTUSB_OK;
}
softusb_status softusb_build_sof(uint16_t frame, uint8_t *out, size_t capacity,
                                size_t *out_len)
{
    softusb_status s = output_check(out, capacity, 3, out_len);
    if (s != SOFTUSB_OK) return s;
    if (frame > 2047) return SOFTUSB_ERR_ARGUMENT;
    out[0] = SOFTUSB_PID_SOF;
    out[1] = (uint8_t)frame;
    out[2] = (uint8_t)((frame >> 8) | (softusb_crc5(frame) << 3));
    *out_len = 3;
    return SOFTUSB_OK;
}
softusb_status softusb_build_data(bool low_speed, uint8_t pid,
                                 const uint8_t *payload, size_t payload_len,
                                 uint8_t *out, size_t capacity, size_t *out_len)
{
    uint16_t crc;
    softusb_status s;
    if (!out_len) return SOFTUSB_ERR_ARGUMENT;
    *out_len = 0;
    if (!data_pid(pid)) return SOFTUSB_ERR_PID;
    if (payload_len > (low_speed ? SOFTUSB_LS_MAX_PAYLOAD : SOFTUSB_FS_MAX_PAYLOAD))
        return SOFTUSB_ERR_LENGTH;
    if (payload_len && !payload) return SOFTUSB_ERR_ARGUMENT;
    s = output_check(out, capacity, payload_len + 3u, out_len);
    if (s != SOFTUSB_OK) return s;
    crc = softusb_crc16(payload, payload_len);
    out[0] = pid;
    if (payload_len) memcpy(out + 1, payload, payload_len);
    out[payload_len + 1] = (uint8_t)crc;
    out[payload_len + 2] = (uint8_t)(crc >> 8);
    *out_len = payload_len + 3u;
    return SOFTUSB_OK;
}
softusb_status softusb_build_handshake(uint8_t pid, uint8_t *out,
                                      size_t capacity, size_t *out_len)
{
    softusb_status s = output_check(out, capacity, 1, out_len);
    if (s != SOFTUSB_OK) return s;
    if (!handshake_pid(pid)) return SOFTUSB_ERR_PID;
    out[0] = pid;
    *out_len = 1;
    return SOFTUSB_OK;
}
softusb_status softusb_parse_packet(bool low_speed, const uint8_t *bytes,
                                   size_t len, softusb_packet *packet)
{
    softusb_packet parsed;
    uint16_t field, received;
    uint8_t pid;
    if (!packet) return SOFTUSB_ERR_ARGUMENT;
    memset(packet, 0, sizeof(*packet));
    if (!bytes) return SOFTUSB_ERR_ARGUMENT;
    if (!len) return SOFTUSB_ERR_LENGTH;
    pid = bytes[0];
    if (!softusb_pid_valid(pid)) return SOFTUSB_ERR_PID;
    memset(&parsed, 0, sizeof(parsed));
    parsed.pid = pid;
    if (token_pid(pid) || pid == SOFTUSB_PID_SOF) {
        if (len != 3) return SOFTUSB_ERR_LENGTH;
        field = (uint16_t)(bytes[1] | ((uint16_t)(bytes[2] & 7u) << 8));
        if ((bytes[2] >> 3) != softusb_crc5(field)) return SOFTUSB_ERR_CRC;
        if (pid == SOFTUSB_PID_SOF) {
            parsed.kind = SOFTUSB_PACKET_SOF;
            parsed.frame = field;
        } else {
            parsed.kind = SOFTUSB_PACKET_TOKEN;
            parsed.address = (uint8_t)(field & 127u);
            parsed.endpoint = (uint8_t)(field >> 7);
        }
    } else if (data_pid(pid)) {
        if (len < 3 || len - 3u > (low_speed ? SOFTUSB_LS_MAX_PAYLOAD : SOFTUSB_FS_MAX_PAYLOAD))
            return SOFTUSB_ERR_LENGTH;
        received = (uint16_t)(bytes[len - 2] | ((uint16_t)bytes[len - 1] << 8));
        if (received != softusb_crc16(bytes + 1, len - 3u)) return SOFTUSB_ERR_CRC;
        parsed.kind = SOFTUSB_PACKET_DATA;
        parsed.payload = bytes + 1;
        parsed.payload_len = len - 3u;
    } else if (handshake_pid(pid)) {
        if (len != 1) return SOFTUSB_ERR_LENGTH;
        parsed.kind = SOFTUSB_PACKET_HANDSHAKE;
    } else {
        return SOFTUSB_ERR_UNSUPPORTED;
    }
    *packet = parsed;
    return SOFTUSB_OK;
}

/* Count first: avoids partial writes on an undersized encode buffer. SYNC's
 * final logical one participates in the stuffing run across the PID boundary. */
softusb_status softusb_encode(bool low_speed, const uint8_t *packet,
                             size_t packet_len, uint8_t *states,
                             size_t capacity, size_t *state_count)
{
    static const uint8_t sync_bits[8] = {0,0,0,0,0,0,0,1};
    softusb_packet parsed;
    softusb_status s;
    size_t i, count = 11, pos = 0;
    unsigned b, ones = 1;
    uint8_t j = low_speed ? SOFTUSB_DM : SOFTUSB_DP;
    uint8_t level = j;
    if (!state_count) return SOFTUSB_ERR_ARGUMENT;
    *state_count = 0;
    if (!states) return SOFTUSB_ERR_ARGUMENT;
    s = softusb_parse_packet(low_speed, packet, packet_len, &parsed);
    if (s != SOFTUSB_OK) return s;
    for (i = 0; i < packet_len; ++i) {
        for (b = 0; b < 8; ++b) {
            ++count;
            if ((packet[i] >> b) & 1u) {
                if (++ones == 6) { ++count; ones = 0; }
            } else ones = 0;
        }
    }
    if (capacity < count) return SOFTUSB_ERR_CAPACITY;
    for (b = 0; b < 8; ++b) {
        if (!sync_bits[b]) level ^= 3u;
        states[pos++] = level;
    }
    ones = 1;
    for (i = 0; i < packet_len; ++i) {
        for (b = 0; b < 8; ++b) {
            bool bit = ((packet[i] >> b) & 1u) != 0;
            if (!bit) level ^= 3u;
            states[pos++] = level;
            ones = bit ? ones + 1u : 0u;
            if (ones == 6) {
                level ^= 3u;
                states[pos++] = level;
                ones = 0;
            }
        }
    }
    states[pos++] = SOFTUSB_SE0;
    states[pos++] = SOFTUSB_SE0;
    states[pos++] = j;
    *state_count = pos;
    return SOFTUSB_OK;
}
softusb_status softusb_decode(bool low_speed, const uint8_t *states,
                             size_t state_count, uint8_t *out,
                             size_t capacity, size_t *out_len)
{
    static const uint8_t sync_j[8] = {0,1,0,1,0,1,0,0};
    uint8_t j = low_speed ? SOFTUSB_DM : SOFTUSB_DP;
    uint8_t k = (uint8_t)(j ^ 3u), previous, byte = 0;
    unsigned ones = 1, bits = 0;
    size_t i, len = 0;
    softusb_packet parsed;
    softusb_status s;
    if (!out_len) return SOFTUSB_ERR_ARGUMENT;
    *out_len = 0;
    if (!states || !out) return SOFTUSB_ERR_ARGUMENT;
    if (state_count < 19) return SOFTUSB_ERR_LENGTH;
    if (state_count > (low_speed ? SOFTUSB_LS_MAX_LINE_STATES : SOFTUSB_MAX_LINE_STATES))
        return SOFTUSB_ERR_LENGTH;
    for (i = 0; i < 8; ++i)
        if (states[i] != (sync_j[i] ? j : k)) return SOFTUSB_ERR_SYNC;
    if (states[state_count - 3] != SOFTUSB_SE0 ||
        states[state_count - 2] != SOFTUSB_SE0 || states[state_count - 1] != j)
        return SOFTUSB_ERR_EOP;
    previous = k;
    for (i = 8; i < state_count - 3; ++i) {
        bool bit;
        if (states[i] != j && states[i] != k)
            return states[i] == SOFTUSB_SE0 ? SOFTUSB_ERR_EOP : SOFTUSB_ERR_LINE;
        bit = states[i] == previous;
        previous = states[i];
        if (ones == 6) {
            if (bit) return SOFTUSB_ERR_STUFF;
            ones = 0;
            continue;
        }
        ones = bit ? ones + 1u : 0u;
        if (bit) byte |= (uint8_t)(1u << bits);
        if (++bits == 8) {
            if (len >= capacity) return SOFTUSB_ERR_CAPACITY;
            out[len++] = byte;
            bits = 0;
            byte = 0;
        }
    }
    if (ones == 6) return SOFTUSB_ERR_STUFF;
    if (bits) return SOFTUSB_ERR_LENGTH;
    s = softusb_parse_packet(low_speed, out, len, &parsed);
    if (s != SOFTUSB_OK) return s;
    *out_len = len;
    return SOFTUSB_OK;
}
softusb_status softusb_encode_packet(bool low_speed, uint8_t pid,
                                    const uint8_t *payload, size_t payload_len,
                                    uint8_t *states, size_t capacity,
                                    size_t *state_count)
{
    uint8_t raw[SOFTUSB_MAX_PACKET_BYTES];
    size_t len = 0;
    softusb_status s;
    if (!state_count) return SOFTUSB_ERR_ARGUMENT;
    *state_count = 0;
    if (!states || (payload_len && !payload)) return SOFTUSB_ERR_ARGUMENT;
    if (data_pid(pid))
        s = softusb_build_data(low_speed, pid, payload, payload_len, raw, sizeof(raw), &len);
    else if (token_pid(pid)) {
        if (payload_len != 2) return SOFTUSB_ERR_LENGTH;
        s = softusb_build_token(pid, payload[0], payload[1], raw, sizeof(raw), &len);
    } else if (pid == SOFTUSB_PID_SOF) {
        if (payload_len != 2) return SOFTUSB_ERR_LENGTH;
        s = softusb_build_sof((uint16_t)(payload[0] | ((uint16_t)payload[1] << 8)), raw, sizeof(raw), &len);
    } else if (handshake_pid(pid)) {
        if (payload_len) return SOFTUSB_ERR_LENGTH;
        s = softusb_build_handshake(pid, raw, sizeof(raw), &len);
    } else return softusb_pid_valid(pid) ? SOFTUSB_ERR_UNSUPPORTED : SOFTUSB_ERR_PID;
    if (s != SOFTUSB_OK) return s;
    return softusb_encode(low_speed, raw, len, states, capacity, state_count);
}
