/*
 * Shared-memory ABI between the app-core UDC driver and the FLPR LS engine.
 *
 * The FLPR answers every USB transaction on its own (ACK/NAK/STALL, data
 * toggles, address filtering) from buffers staged here; the app core only
 * moves payloads between this block and Zephyr's UDC buffers.
 *
 * Ownership is expressed with sequence counters, never with flags that both
 * sides write:
 *   IN  ep: app writes len/data/gen then bumps in[ep].req; FLPR bumps
 *           in[ep].done once the host ACKed (or the packet was dropped as stale).
 *           Pending <=> req != done. The app stages only when req == done.
 *   OUT ep: FLPR writes len/data/gen then bumps out[ep].full; app copies and
 *           sets out[ep].ack = full. FLPR NAKs while full != ack.
 * Every field has exactly one writer. No data cache on either core.
 */
#ifndef SOFTUSB_IPC_H
#define SOFTUSB_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/devicetree.h>

#define SU_MAGIC 0x53555342u /* "SUSB" */
#define SU_ABI 2u
#define SU_NUM_EP 4u
#define SU_MPS 8u

/* VIO bit masks (nRF54L15 P2 -> VIO map from HPF MSPI). */
#define SU_VIO_DP (1u << 4) /* P2.00 */
#define SU_VIO_DM (1u << 0) /* P2.01 */
#define SU_PIN_DP 0u        /* P2 pin numbers */
#define SU_PIN_DM 1u

/* VEVIF event the FLPR raises towards the app core (cpuapp_vevif_rx 20). */
#define SU_VEVIF_EVENT 20u

struct su_in_ep {
	uint32_t req;      /* app */
	uint32_t done;     /* FLPR */
	uint32_t len;      /* app: payload bytes 0..8 */
	uint32_t gen;      /* app: setup_seq this packet belongs to (EP0 only) */
	uint8_t data[12];  /* app: payload followed by CRC16 (LE) */
};

struct su_out_ep {
	uint32_t full;     /* FLPR */
	uint32_t ack;      /* app */
	uint32_t len;      /* FLPR */
	uint32_t gen;      /* FLPR: setup_seq when received */
	uint8_t data[12];  /* FLPR */
};

struct su_ipc {
	/* FLPR-written status. */
	uint32_t magic, abi, flpr_ready;
	uint32_t reset_seq;     /* bus resets seen */
	uint32_t setup_seq;     /* SETUP packets accepted */
	uint8_t setup[8];       /* last SETUP payload, valid when setup_seq moved */
	uint32_t address;       /* address in use */
	uint32_t event_seq;     /* bumped with every app-visible change */

	/* App-written control. FLPR ignores it until app_magic == SU_MAGIC, and
	 * only re-reads it when ctrl_seq changes (keeps the idle loop short).
	 */
	uint32_t app_magic;
	uint32_t ctrl_seq;
	uint32_t connect;       /* 1: internal pull-up on D- (attach) */
	uint32_t addr_req_seq;  /* bump to apply addr_req */
	uint32_t addr_req;
	uint32_t ep_in_enabled, ep_out_enabled;   /* bitmasks, EP0 always on */
	uint32_t ep_in_halt, ep_out_halt;         /* bitmasks, EP1.. */
	uint32_t ep0_stall_gen;  /* EP0 protocol stall while == setup_seq + 1 */
	uint32_t in_toggle_reset[SU_NUM_EP];   /* bump -> next IN DATA0 */
	uint32_t out_toggle_reset[SU_NUM_EP];  /* bump -> expect DATA0 */

	struct su_in_ep in[SU_NUM_EP];
	struct su_out_ep out[SU_NUM_EP];

	/* FLPR-written statistics. */
	uint32_t st_rx_pkts, st_rx_bad_sync, st_rx_bad_pid, st_rx_crc, st_rx_stuff;
	uint32_t st_rx_overflow, st_tokens_mine, st_tx_pkts, st_naks, st_stalls;
	uint32_t st_in_acked, st_in_noack, st_data_timeout, st_resets, st_setups;
	uint32_t st_toggle_dup, st_stale_drop, st_core_hz;
	uint32_t last_tokens[8]; /* ring of (pid<<16 | addr<<8 | ep) for debugging */
	uint32_t last_token_idx;
	uint32_t dbg_len;
	uint32_t cal_mcycle_per_300; /* mcycle delta over 300 WAIT0 periods, TOP=84 */
	uint32_t dbg_mcycle;          /* live mcycle, updated in the idle loop */
	uint8_t dbg_raw[16];
};

#define SU_IPC ((volatile struct su_ipc *)DT_REG_ADDR(DT_NODELABEL(softusb_ipc)))

_Static_assert(sizeof(struct su_ipc) <= DT_REG_SIZE(DT_NODELABEL(softusb_ipc)), "IPC size");

static inline uint32_t su_load(const volatile uint32_t *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void su_store(volatile uint32_t *p, uint32_t v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

#endif
