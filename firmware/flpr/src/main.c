/*
 * SoftUSB FLPR engine: USB 1.1 low-speed device link layer.
 *
 * Runs with interrupts locked in a polling loop. Handles packet RX/TX via the
 * HRT engines, token decoding, address/endpoint filtering, data toggles and
 * ACK/NAK/STALL on its own, so every handshake meets the 6.5 bit-time
 * turnaround. The app core only exchanges payloads through struct su_ipc.
 */
#include <zephyr/kernel.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_oscillators.h>
#include <hal/nrf_vpr_csr.h>
#include <hal/nrf_vpr_csr_vio.h>
#include <hal/nrf_vpr_csr_vtim.h>
#include <hal/nrf_vpr_csr_vevif.h>
#include "softusb_ipc.h"
#include "hrt/hrt.h"

#define PID_OUT 0xE1
#define PID_IN 0x69
#define PID_SETUP 0x2D
#define PID_DATA0 0xC3
#define PID_DATA1 0x4B
#define PID_ACK 0xD2
#define PID_NAK 0x5A
#define PID_STALL 0x1E

#define BIT_CYCLES 85u
/* Idle poll slice between app-mailbox checks. */
#define IDLE_SLICE_CYCLES (BIT_CYCLES * 64u)
/* Host data packet / handshake must start within 7.5 bit times (+ slack). */
#define RESP_TIMEOUT_CYCLES (BIT_CYCLES * 18u)
/* Minimum inter-packet gap before we drive: 2 bit times, plus margin. */
#define TURNAROUND_CYCLES (BIT_CYCLES * 2u + 40u)
/* SE0 longer than this is a bus reset (keep-alive EOP is 2 bits = 1.33 us). */
#define SILENCE_CYCLES (128000u * 3u)

static volatile struct su_ipc *const ipc = SU_IPC;

static uint8_t rx[16];
static uint8_t txb[16];
static uint8_t addr;
static int pending_addr = -1;
static uint8_t in_tog[SU_NUM_EP], out_tog[SU_NUM_EP];
static uint32_t in_tr_seen[SU_NUM_EP], out_tr_seen[SU_NUM_EP];
static uint32_t addr_req_seen;
static uint32_t ctrl_seen;
static uint32_t connected;

/* CRC16 (USB, reflected 0xA001) table; residual over data+crc is 0xB001. */
static uint16_t crc16_tab[256];

static void crc16_init(void)
{
	for (uint32_t i = 0; i < 256u; i++) {
		uint32_t c = i;

		for (int k = 0; k < 8; k++) {
			c = (c & 1u) ? ((c >> 1) ^ 0xA001u) : (c >> 1);
		}
		crc16_tab[i] = (uint16_t)c;
	}
}

static uint32_t crc16(const uint8_t *p, uint32_t n)
{
	uint32_t c = 0xFFFFu;

	while (n--) {
		c = (c >> 8) ^ crc16_tab[(c ^ *p++) & 0xFFu];
	}
	return c;
}

/* CRC5 over the 11 token bits, compared with the received (inverted) CRC. */
static bool token_crc_ok(uint32_t b1, uint32_t b2)
{
	uint32_t data = b1 | ((b2 & 7u) << 8);
	uint32_t c = 0x1Fu;

	for (int i = 0; i < 11; i++) {
		uint32_t fb = (c ^ data) & 1u;

		c >>= 1;
		data >>= 1;
		if (fb) {
			c ^= 0x14u; /* reflected 0x05 */
		}
	}
	return ((~c) & 0x1Fu) == (b2 >> 3);
}

static void notify_app(void)
{
	ipc->event_seq = ipc->event_seq + 1u;
	nrf_vpr_csr_vevif_events_trigger(BIT(SU_VEVIF_EVENT));
	/* nRF54L errata 16: wait for the event to latch, then clear it. */
	for (int i = 0; i < 64 && !(nrf_vpr_csr_vevif_events_get() & BIT(SU_VEVIF_EVENT)); i++) {
	}
	nrf_vpr_csr_vevif_events_set(0);
}

static void handshake(uint8_t pid, uint32_t t_eop)
{
	txb[0] = pid;
	su_tx(txb, 1u, t_eop + TURNAROUND_CYCLES);
	ipc->st_tx_pkts++;
}

static void pins_init(void)
{
	nrf_vpr_csr_vio_dir_set(0);
	nrf_vpr_csr_vio_out_set(SU_VIO_DM);
	for (uint32_t pin = SU_PIN_DP; pin <= SU_PIN_DM; pin++) {
		uint32_t abs_pin = NRF_GPIO_PIN_MAP(2, pin);

		nrf_gpio_cfg(abs_pin, NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT,
			     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
		nrf_gpio_pin_control_select(abs_pin, NRF_GPIO_PIN_SEL_VPR);
	}
}

static void set_pullup(bool on)
{
	/* The internal D- pull-up signals a low-speed attach (measured to work
	 * on this host; nominally weaker than the spec's 1.5 kOhm).
	 */
	nrf_gpio_cfg(NRF_GPIO_PIN_MAP(2, SU_PIN_DM), NRF_GPIO_PIN_DIR_INPUT,
		     NRF_GPIO_PIN_INPUT_CONNECT,
		     on ? NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_NOPULL,
		     NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
	nrf_gpio_pin_control_select(NRF_GPIO_PIN_MAP(2, SU_PIN_DM), NRF_GPIO_PIN_SEL_VPR);
}

static void bus_reset_state(void)
{
	addr = 0;
	pending_addr = -1;
	for (uint32_t i = 0; i < SU_NUM_EP; i++) {
		in_tog[i] = 0;
		out_tog[i] = 0;
		/* Drop staged IN data; the stack restarts transfers after reset. */
		ipc->in[i].done = ipc->in[i].req;
	}
	ipc->address = 0;
}

static void service_app(void)
{
	if (ipc->ctrl_seq == ctrl_seen || ipc->app_magic != SU_MAGIC) {
		return;
	}
	ctrl_seen = ipc->ctrl_seq;

	uint32_t c = ipc->connect;

	if (c != connected) {
		connected = c;
		set_pullup(c != 0u);
		bus_reset_state();
	}
	uint32_t s = ipc->addr_req_seq;

	if (s != addr_req_seen) {
		addr_req_seen = s;
		addr = (uint8_t)(ipc->addr_req & 0x7Fu);
		ipc->address = addr;
	}
	for (uint32_t i = 0; i < SU_NUM_EP; i++) {
		if (ipc->in_toggle_reset[i] != in_tr_seen[i]) {
			in_tr_seen[i] = ipc->in_toggle_reset[i];
			in_tog[i] = 0;
		}
		if (ipc->out_toggle_reset[i] != out_tr_seen[i]) {
			out_tr_seen[i] = ipc->out_toggle_reset[i];
			out_tog[i] = 0;
		}
	}
}

static bool ep0_stalled(void)
{
	return ipc->ep0_stall_gen == ipc->setup_seq + 1u;
}

static void handle_in(uint32_t ep, uint32_t t_eop)
{
	volatile struct su_in_ep *e = &ipc->in[ep];

	if (ep != 0u && !(ipc->ep_in_enabled & BIT(ep))) {
		return; /* not configured: no response */
	}
	if ((ep == 0u && ep0_stalled()) || (ep != 0u && (ipc->ep_in_halt & BIT(ep)))) {
		handshake(PID_STALL, t_eop);
		ipc->st_stalls++;
		return;
	}
	uint32_t req = e->req;

	if (req != e->done && ep == 0u && e->gen != ipc->setup_seq) {
		e->done = req; /* staged for an older control transfer */
		ipc->st_stale_drop++;
	}
	if (req == e->done) {
		handshake(PID_NAK, t_eop);
		ipc->st_naks++;
		return;
	}

	uint32_t len = e->len;

	txb[0] = in_tog[ep] ? PID_DATA1 : PID_DATA0;
	for (uint32_t i = 0; i < len + 2u; i++) {
		txb[1u + i] = e->data[i];
	}
	su_tx(txb, len + 3u, t_eop + TURNAROUND_CYCLES);
	ipc->st_tx_pkts++;

	uint32_t t2;
	int r = su_rx(rx, sizeof(rx), RESP_TIMEOUT_CYCLES, &t2);

	if (r == 2 && rx[0] == 0x80u && rx[1] == PID_ACK) {
		in_tog[ep] ^= 1u;
		e->done = req;
		ipc->st_in_acked++;
		if (ep == 0u && len == 0u && pending_addr >= 0) {
			/* Status stage of SET_ADDRESS done: switch address now. */
			addr = (uint8_t)pending_addr;
			pending_addr = -1;
			ipc->address = addr;
		}
		notify_app();
	} else {
		ipc->st_in_noack++; /* host retries the IN; same toggle */
	}
}

static void handle_out(uint32_t ep, bool setup, uint32_t t_eop)
{
	uint32_t t2;
	int r = su_rx(rx, sizeof(rx), RESP_TIMEOUT_CYCLES, &t2);

	if (r <= 0) {
		ipc->st_data_timeout++;
		return;
	}
	if (r < 4 || rx[0] != 0x80u || (rx[1] != PID_DATA0 && rx[1] != PID_DATA1)) {
		ipc->st_rx_bad_pid++;
		return;
	}
	uint32_t len = (uint32_t)r - 4u;

	if (len > SU_MPS || crc16(&rx[2], len + 2u) != 0xB001u) {
		ipc->st_rx_crc++;
		ipc->dbg_len = (uint32_t)r;
		for (uint32_t i = 0; i < 16u; i++) {
			ipc->dbg_raw[i] = rx[i];
		}
		return; /* no handshake: host retries */
	}

	if (setup) {
		if (ep != 0u || rx[1] != PID_DATA0 || len != 8u) {
			return;
		}
		handshake(PID_ACK, t2);
		for (uint32_t i = 0; i < 8u; i++) {
			ipc->setup[i] = rx[2u + i];
		}
		in_tog[0] = 1u;
		out_tog[0] = 1u;
		ipc->in[0].done = ipc->in[0].req;
		if (rx[2] == 0x00u && rx[3] == 0x05u) {
			pending_addr = rx[4] & 0x7F; /* SET_ADDRESS */
		} else {
			pending_addr = -1;
		}
		ipc->st_setups++;
		su_store(&ipc->setup_seq, ipc->setup_seq + 1u);
		notify_app();
		return;
	}

	if (ep != 0u && !(ipc->ep_out_enabled & BIT(ep))) {
		return;
	}
	if ((ep == 0u && ep0_stalled()) || (ep != 0u && (ipc->ep_out_halt & BIT(ep)))) {
		handshake(PID_STALL, t2);
		ipc->st_stalls++;
		return;
	}
	uint32_t tog = (rx[1] == PID_DATA1) ? 1u : 0u;

	if (tog != out_tog[ep]) {
		handshake(PID_ACK, t2); /* retransmission of data we already have */
		ipc->st_toggle_dup++;
		return;
	}
	volatile struct su_out_ep *o = &ipc->out[ep];

	if (o->full != o->ack) {
		handshake(PID_NAK, t2);
		ipc->st_naks++;
		return;
	}
	handshake(PID_ACK, t2);
	out_tog[ep] ^= 1u;
	for (uint32_t i = 0; i < len; i++) {
		o->data[i] = rx[2u + i];
	}
	o->len = len;
	o->gen = ipc->setup_seq;
	su_store(&o->full, o->full + 1u);
	notify_app();
}

static void handle_packet(int n, uint32_t t_eop)
{
	ipc->st_rx_pkts++;
	if (n < 2 || rx[0] != 0x80u) {
		ipc->st_rx_bad_sync++;
		return;
	}
	uint32_t pid = rx[1];

	if (((pid ^ (pid >> 4)) & 0xFu) != 0xFu) {
		ipc->st_rx_bad_pid++;
		return;
	}
	if (pid != PID_IN && pid != PID_OUT && pid != PID_SETUP) {
		return; /* stray data/handshake: not ours to answer */
	}
	if (n != 4 || !token_crc_ok(rx[2], rx[3])) {
		ipc->st_rx_crc++;
		return;
	}
	uint32_t a = rx[2] & 0x7Fu;
	uint32_t ep = ((rx[3] & 7u) << 1) | (rx[2] >> 7);

	if (a != addr || ep >= SU_NUM_EP) {
		return;
	}
	ipc->st_tokens_mine++;
	uint32_t i = ipc->last_token_idx;

	ipc->last_tokens[i & 7u] = (pid << 16) | (a << 8) | ep;
	ipc->last_token_idx = i + 1u;

	if (pid == PID_IN) {
		handle_in(ep, t_eop);
	} else {
		handle_out(ep, pid == PID_SETUP, t_eop);
	}
}

/*
 * Bus reset / suspend / resume without a usable idle level: with the weak
 * internal pull-up, idle J and SE0 both read 0,0. Any host activity (driven
 * J or K) at least every 1 ms (keep-alive) means the bus is running. Silence
 * longer than SILENCE_CYCLES is either a reset (SE0 >= 10 ms) or a suspend;
 * a suspend ends with a long resume K, a reset with ordinary traffic. So the
 * first ordinary packet or keep-alive after silence is reported as a reset.
 */
static bool silent;

static void bus_reset(void)
{
	bus_reset_state();
	ipc->st_resets++;
	su_store(&ipc->reset_seq, ipc->reset_seq + 1u);
	notify_app();
}

int main(void)
{
	nrf_vpr_csr_rtperiph_enable_set(true);

	const nrf_vpr_csr_vio_config_t vio_config = {
		.clk_polarity = false, .stop_cnt = false, .input_sel = false,
	};
	const nrf_vpr_csr_vio_mode_out_t out_mode = {
		.mode = NRF_VPR_CSR_VIO_SHIFT_NONE, .frame_width = 1,
	};

	nrf_vpr_csr_vio_config_set(&vio_config);
	nrf_vpr_csr_vio_mode_out_set(&out_mode);
	nrf_vpr_csr_vio_mode_in_set(NRF_VPR_CSR_VIO_MODE_IN_CONTINUOUS);
	nrf_vpr_csr_vtim_count_mode_set(0, NRF_VPR_CSR_VTIM_COUNT_RELOAD);
	su_time_init();
	pins_init();
	crc16_init();

	/* Calibrate: cycles per WAIT0 period with TOP=84 (expect 85). */
	{
		uint32_t t0, t1;

		nrf_csr_write(VPRCSR_NORDIC_CNTTOP, 84);
		nrf_csr_write(VPRCSR_NORDIC_CNT0, 84);
		nrf_csr_write(VPRCSR_NORDIC_WAIT0, 0);
		t0 = su_now();
		for (int i = 0; i < 300; i++) {
			nrf_csr_write(VPRCSR_NORDIC_WAIT0, 0);
		}
		t1 = su_now();
		ipc->cal_mcycle_per_300 = t1 - t0;
	}

	(void)irq_lock();

	ipc->st_core_hz =
		(nrf_oscillators_pll_freq_get(NRF_OSCILLATORS) == NRF_OSCILLATORS_PLL_FREQ_128M)
			? 128000000u : 0u;
	connected = 0;
	ctrl_seen = ipc->ctrl_seq - 1u; /* force one full read once the app is up */
	addr_req_seen = ipc->addr_req_seq;
	for (uint32_t i = 0; i < SU_NUM_EP; i++) {
		in_tr_seen[i] = ipc->in_toggle_reset[i];
		out_tr_seen[i] = ipc->out_toggle_reset[i];
		ipc->in[i].done = ipc->in[i].req;
		ipc->out[i].full = ipc->out[i].ack;
	}
	for (volatile uint32_t *p = &ipc->st_rx_pkts; p <= &ipc->last_token_idx; p++) {
		*p = 0;
	}
	ipc->reset_seq = 0;
	ipc->setup_seq = 0;
	ipc->event_seq = 0;
	ipc->address = 0;
	ipc->magic = SU_MAGIC;
	ipc->abi = SU_ABI;
	su_store(&ipc->flpr_ready, SU_MAGIC);

	for (;;) {
		ipc->dbg_mcycle = su_now();
		service_app();
		if (!connected) {
			continue;
		}
		uint32_t t_eop;
		int r = su_rx(rx, sizeof(rx), IDLE_SLICE_CYCLES, &t_eop);

		if (r == SU_RX_NOEDGE) {
			silent = false; /* long K: resume signalling, not a reset */
			continue;
		}
		if (!silent && (su_now() - su_last_activity) > SILENCE_CYCLES) {
			silent = true;
		}
		if (r == SU_RX_NONE) {
			if (silent && (su_now() - su_last_activity) < SILENCE_CYCLES) {
				silent = false; /* keep-alive after silence */
				bus_reset();
			}
			continue;
		}
		if (silent) {
			silent = false;
			bus_reset();
		}
		if (r < 0) {
			if (r == SU_RX_STUFF) {
				ipc->st_rx_stuff++;
			} else {
				ipc->st_rx_overflow++;
			}
			continue;
		}
		handle_packet(r, t_eop);
	}
}
