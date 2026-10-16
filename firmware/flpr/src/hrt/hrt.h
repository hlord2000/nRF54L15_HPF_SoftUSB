#ifndef SU_HRT_H
#define SU_HRT_H
#include <stdint.h>

/* su_rx() results (> 0 is the byte count including the SYNC byte). */
#define SU_RX_NONE 0      /* no activity before the timeout */
#define SU_RX_STUFF (-2)  /* bit-stuffing violation */
#define SU_RX_OVF (-3)    /* longer than the buffer */
#define SU_RX_NOEDGE (-4) /* K never returned to J: glitch */

/*
 * Poll for the start of a packet for up to timeout_cycles, then receive it
 * into buf. Returns once the EOP has ended (line back to J), so the caller
 * can respond immediately. *t_eop gets the mcycle value when J returned.
 */
int su_rx(uint8_t *buf, uint32_t max, uint32_t timeout_cycles, uint32_t *t_eop);

/*
 * Transmit SYNC + n bytes + EOP, then release the pins. Starts no earlier
 * than mcycle == t_start (used to honour the 2 bit-time inter-packet gap).
 */
void su_tx(const uint8_t *buf, uint32_t n, uint32_t t_start);

/* mcycle of the last host-driven J or K seen (bus activity). */
extern volatile uint32_t su_last_activity;

/*
 * Free-running time in core cycles. mcycle does not count on this VPR, so
 * VTIM CNT1 runs in WRAP mode (16-bit, counting down) and this extends it to
 * 32 bits; it must be called at least every 512 us.
 */
uint32_t su_now(void);
void su_time_init(void);

#endif
