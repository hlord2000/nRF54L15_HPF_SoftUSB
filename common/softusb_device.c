#include "softusb.h"
#include <string.h>

static bool endpoint_enabled(const softusb_device *d, uint8_t endpoint)
{
    return endpoint < SOFTUSB_ENDPOINTS &&
           (d->endpoint_mask & (uint16_t)(1u << endpoint)) != 0;
}
softusb_status softusb_device_init(softusb_device *d, uint8_t address,
                                   uint16_t endpoint_mask)
{
    if (!d || address > 127) return SOFTUSB_ERR_ARGUMENT;
    memset(d, 0, sizeof(*d));
    d->address = address;
    d->endpoint_mask = endpoint_mask;
    return SOFTUSB_OK;
}
softusb_status softusb_device_set_address(softusb_device *d, uint8_t address)
{
    if (!d || address > 127) return SOFTUSB_ERR_ARGUMENT;
    d->address = address;
    d->token_pending = false;
    d->awaiting_ack = false;
    return SOFTUSB_OK;
}
softusb_status softusb_device_queue_in(softusb_device *d, uint8_t endpoint,
                                      const uint8_t *data, size_t len)
{
    softusb_endpoint *ep;
    if (!d || (len && !data)) return SOFTUSB_ERR_ARGUMENT;
    if (!endpoint_enabled(d, endpoint)) return SOFTUSB_ERR_ARGUMENT;
    if (len > SOFTUSB_LS_MAX_PAYLOAD) return SOFTUSB_ERR_LENGTH;
    ep = &d->ep[endpoint];
    if (ep->in_pending) return SOFTUSB_ERR_STATE;
    if (len) memcpy(ep->in_data, data, len);
    ep->in_len = len;
    ep->in_pending = true;
    return SOFTUSB_OK;
}
softusb_status softusb_device_take_out(softusb_device *d, uint8_t endpoint,
                                      uint8_t *out, size_t capacity,
                                      size_t *out_len, bool *is_setup)
{
    softusb_endpoint *ep;
    if (!out_len) return SOFTUSB_ERR_ARGUMENT;
    *out_len = 0;
    if (is_setup) *is_setup = false;
    if (!d || !is_setup || !endpoint_enabled(d, endpoint)) return SOFTUSB_ERR_ARGUMENT;
    ep = &d->ep[endpoint];
    if (!ep->out_pending) return SOFTUSB_ERR_STATE;
    if (capacity < ep->out_len) return SOFTUSB_ERR_CAPACITY;
    if (ep->out_len && !out) return SOFTUSB_ERR_ARGUMENT;
    if (ep->out_len) memcpy(out, ep->out_data, ep->out_len);
    *out_len = ep->out_len;
    *is_setup = ep->out_is_setup;
    ep->out_pending = false;
    return SOFTUSB_OK;
}
static void handshake(softusb_response *r, uint8_t pid)
{
    r->bytes[0] = pid;
    r->len = 1;
}
softusb_status softusb_device_receive(softusb_device *d,
                                     const uint8_t *bytes, size_t len,
                                     softusb_response *r)
{
    softusb_packet p;
    softusb_endpoint *ep;
    softusb_status s;
    bool had_token, had_ack;
    uint8_t token_pid, token_ep, ack_ep;
    if (!r) return SOFTUSB_ERR_ARGUMENT;
    memset(r, 0, sizeof(*r));
    if (!d) return SOFTUSB_ERR_ARGUMENT;
    had_token = d->token_pending;
    token_pid = d->token_pid;
    token_ep = d->token_ep;
    had_ack = d->awaiting_ack;
    ack_ep = d->ack_ep;
    d->token_pending = false;
    d->awaiting_ack = false;
    s = softusb_parse_packet(true, bytes, len, &p);
    if (s != SOFTUSB_OK) return s;
    if (p.kind == SOFTUSB_PACKET_TOKEN) {
        if (p.address != d->address || !endpoint_enabled(d, p.endpoint)) return SOFTUSB_OK;
        if (p.pid == SOFTUSB_PID_SETUP && p.endpoint != 0) return SOFTUSB_OK;
        r->endpoint = p.endpoint;
        ep = &d->ep[p.endpoint];
        if (p.pid == SOFTUSB_PID_IN) {
            if (!ep->in_pending) { handshake(r, SOFTUSB_PID_NAK); return SOFTUSB_OK; }
            s = softusb_build_data(true, ep->in_data1 ? SOFTUSB_PID_DATA1 : SOFTUSB_PID_DATA0,
                                   ep->in_data, ep->in_len, r->bytes, sizeof(r->bytes), &r->len);
            if (s == SOFTUSB_OK) {
                d->awaiting_ack = true;
                d->ack_ep = p.endpoint;
            }
            return s;
        }
        d->token_pending = true;
        d->token_pid = p.pid;
        d->token_ep = p.endpoint;
        return SOFTUSB_OK;
    }
    if (p.kind == SOFTUSB_PACKET_HANDSHAKE) {
        if (p.pid == SOFTUSB_PID_ACK && had_ack && endpoint_enabled(d, ack_ep)) {
            ep = &d->ep[ack_ep];
            if (ep->in_pending) {
                ep->in_pending = false;
                ep->in_data1 = !ep->in_data1;
                r->in_acked = true;
                r->endpoint = ack_ep;
            }
        }
        return SOFTUSB_OK;
    }
    if (p.kind != SOFTUSB_PACKET_DATA || !had_token || !endpoint_enabled(d, token_ep))
        return SOFTUSB_OK;
    ep = &d->ep[token_ep];
    r->endpoint = token_ep;
    if (token_pid == SOFTUSB_PID_SETUP) {
        if (p.pid != SOFTUSB_PID_DATA0) return SOFTUSB_ERR_STATE;
        if (p.payload_len != 8) return SOFTUSB_ERR_LENGTH;
        /* Only a validated SETUP DATA0 supersedes the previous EP0 transfer. */
        memset(ep, 0, sizeof(*ep));
        ep->in_data1 = true;
        ep->out_data1 = true;
        ep->out_is_setup = true;
        r->setup_accepted = true;
    } else {
        bool data1 = p.pid == SOFTUSB_PID_DATA1;
        if (data1 != ep->out_data1) {
            if (ep->out_seen) handshake(r, SOFTUSB_PID_ACK);
            return SOFTUSB_OK;
        }
        if (ep->out_pending) { handshake(r, SOFTUSB_PID_NAK); return SOFTUSB_OK; }
        ep->out_data1 = !ep->out_data1;
        ep->out_is_setup = false;
    }
    if (p.payload_len) memcpy(ep->out_data, p.payload, p.payload_len);
    ep->out_len = p.payload_len;
    ep->out_pending = true;
    ep->out_seen = true;
    r->out_accepted = true;
    handshake(r, SOFTUSB_PID_ACK);
    return SOFTUSB_OK;
}
