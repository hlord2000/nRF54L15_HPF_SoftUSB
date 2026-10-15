#ifndef SOFTUSB_H
#define SOFTUSB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No allocation, timing, MMIO, or hardware side effects. All lengths are bytes
 * except state_count/capacity in the line codec (one physical bit cell/state).
 * API accepts complete PID bytes, never bare PID nibbles. */
#define SOFTUSB_LS_MAX_PAYLOAD 8u
#define SOFTUSB_FS_MAX_PAYLOAD 1023u
#define SOFTUSB_MAX_PACKET_BYTES (SOFTUSB_FS_MAX_PAYLOAD + 3u)
#define SOFTUSB_LS_MAX_PACKET_BYTES (SOFTUSB_LS_MAX_PAYLOAD + 3u)
/* Conservative bound: SYNC + all packet bits + stuffing + 3 EOP cells. */
#define SOFTUSB_MAX_LINE_STATES (8u + SOFTUSB_MAX_PACKET_BYTES * 8u + \
                                (SOFTUSB_MAX_PACKET_BYTES * 8u + 1u) / 6u + 3u)
#define SOFTUSB_LS_MAX_LINE_STATES (8u + SOFTUSB_LS_MAX_PACKET_BYTES * 8u + \
                                   (SOFTUSB_LS_MAX_PACKET_BYTES * 8u + 1u) / 6u + 3u)

typedef enum {
    SOFTUSB_SE0 = 0, SOFTUSB_DP = 1, SOFTUSB_DM = 2, SOFTUSB_SE1 = 3
} softusb_line_state;

typedef enum {
    SOFTUSB_OK = 0, SOFTUSB_ERR_ARGUMENT, SOFTUSB_ERR_CAPACITY,
    SOFTUSB_ERR_PID, SOFTUSB_ERR_LENGTH, SOFTUSB_ERR_CRC,
    SOFTUSB_ERR_SYNC, SOFTUSB_ERR_STUFF, SOFTUSB_ERR_EOP,
    SOFTUSB_ERR_LINE, SOFTUSB_ERR_UNSUPPORTED, SOFTUSB_ERR_STATE
} softusb_status;

typedef enum {
    SOFTUSB_PID_OUT = 0xe1, SOFTUSB_PID_IN = 0x69,
    SOFTUSB_PID_SOF = 0xa5, SOFTUSB_PID_SETUP = 0x2d,
    SOFTUSB_PID_DATA0 = 0xc3, SOFTUSB_PID_DATA1 = 0x4b,
    SOFTUSB_PID_ACK = 0xd2, SOFTUSB_PID_NAK = 0x5a,
    SOFTUSB_PID_STALL = 0x1e
} softusb_pid;

typedef enum {
    SOFTUSB_PACKET_TOKEN, SOFTUSB_PACKET_DATA,
    SOFTUSB_PACKET_HANDSHAKE, SOFTUSB_PACKET_SOF
} softusb_packet_kind;

typedef struct {
    softusb_packet_kind kind;
    uint8_t pid;
    uint8_t address, endpoint;
    uint16_t frame;
    /* Borrowed view into input packet; valid only while that buffer is valid. */
    const uint8_t *payload;
    size_t payload_len;
} softusb_packet;

uint8_t softusb_pid_byte(uint8_t nibble); /* low four bits, complemented high */
bool softusb_pid_valid(uint8_t pid); /* complement only; not supported-PID test */
uint8_t softusb_crc5(uint16_t bits11); /* 11 LSb-first bits, complemented CRC */
uint16_t softusb_crc16(const uint8_t *data, size_t len); /* len=0 permits NULL */
const char *softusb_status_string(softusb_status status);

/* Builders output PID/body/CRC, without SYNC/EOP. On error *out_len=0;
 * output contents are unspecified. Input and output buffers must not overlap. */
softusb_status softusb_build_token(uint8_t pid, uint8_t address, uint8_t endpoint,
                                  uint8_t *out, size_t capacity, size_t *out_len);
softusb_status softusb_build_sof(uint16_t frame, uint8_t *out, size_t capacity,
                                size_t *out_len);
softusb_status softusb_build_data(bool low_speed, uint8_t pid,
                                 const uint8_t *payload, size_t payload_len,
                                 uint8_t *out, size_t capacity, size_t *out_len);
softusb_status softusb_build_handshake(uint8_t pid, uint8_t *out,
                                      size_t capacity, size_t *out_len);
/* Parser validates complement, supported type, exact length, CRC and payload
 * speed limit. SOF is available at the codec level for either polarity; a real
 * LS bus uses keep-alives, not SOF. No transfer-type/endpoint-size inference. */
softusb_status softusb_parse_packet(bool low_speed, const uint8_t *bytes,
                                   size_t len, softusb_packet *packet);

/* Raw packet -> physical states: starts immediately at SYNC's first K cell,
 * ends with exactly SE0,SE0,J; no leading/trailing idle. LS J=DM, FS J=DP.
 * Byte values in states are the physical enum above. Bounded, no timing.
 * encode validates the packet before encoding. decode requires this exact
 * framing, validates stuffing (including a final stuffed zero), then packet.
 * decode writes packet bytes; call parse_packet for a borrowed structured view.
 * On any failure the output count is zero. Buffers must not overlap. */
softusb_status softusb_encode(bool low_speed, const uint8_t *packet,
                             size_t packet_len, uint8_t *states,
                             size_t capacity, size_t *state_count);
softusb_status softusb_decode(bool low_speed, const uint8_t *states,
                             size_t state_count, uint8_t *out,
                             size_t capacity, size_t *out_len);
/* Convenience TX: DATA0/1 payload=data; token payload={address,endpoint};
 * SOF payload={frame_low,frame_high} (11-bit); handshake payload_len=0.
 * Produces the same SYNC/EOP physical states as softusb_encode. */
softusb_status softusb_encode_packet(bool low_speed, uint8_t pid,
                                    const uint8_t *payload, size_t payload_len,
                                    uint8_t *states, size_t capacity,
                                    size_t *state_count);

/* LS device TRANSACTION SIMULATION only: no descriptors, enumeration, address
 * assignment protocol, timing, transfer scheduling, STALL, or hardware claims.
 * All 16 endpoints have one queued IN and one captured OUT slot. SETUP only
 * accepts EP0 DATA0 with eight bytes, supersedes EP0 slots and resets both EP0
 * next toggles to DATA1. Caller explicitly sets device address after status ACK.
 * Single-owner state: no concurrent access; copied structs are independent. */
#define SOFTUSB_ENDPOINTS 16u
typedef struct {
    uint8_t in_data[8], out_data[8];
    size_t in_len, out_len;
    bool in_pending, out_pending, out_is_setup;
    bool in_data1, out_data1, out_seen;
} softusb_endpoint;
typedef struct {
    uint8_t address;
    uint16_t endpoint_mask;
    softusb_endpoint ep[SOFTUSB_ENDPOINTS];
    bool token_pending, awaiting_ack;
    uint8_t token_pid, token_ep, ack_ep;
} softusb_device;
typedef struct {
    /* zero means silent; otherwise raw response includes PID/CRC, no SYNC. */
    uint8_t bytes[SOFTUSB_LS_MAX_PACKET_BYTES];
    size_t len;
    bool out_accepted, setup_accepted, in_acked;
    uint8_t endpoint;
} softusb_response;

softusb_status softusb_device_init(softusb_device *device, uint8_t address,
                                   uint16_t endpoint_mask);
softusb_status softusb_device_set_address(softusb_device *device, uint8_t address);
softusb_status softusb_device_queue_in(softusb_device *device, uint8_t endpoint,
                                      const uint8_t *data, size_t len);
/* CAPACITY leaves captured OUT pending for retry. On success consumes the slot. */
softusb_status softusb_device_take_out(softusb_device *device, uint8_t endpoint,
                                      uint8_t *out, size_t capacity,
                                      size_t *out_len, bool *is_setup);
/* Malformed packets return error, send nothing, clear transient token/ACK
 * association and leave accepted data/queues/toggles intact. Correctly formed
 * unrelated packets are silent and also break transient association. Duplicate
 * OUT data is ACKed without storing/toggling again. Busy OUT/empty IN -> NAK.
 * Queued IN and its PID persist across retries until immediately following ACK. */
softusb_status softusb_device_receive(softusb_device *device,
                                     const uint8_t *packet, size_t packet_len,
                                     softusb_response *response);

#ifdef __cplusplus
}
#endif
#endif
