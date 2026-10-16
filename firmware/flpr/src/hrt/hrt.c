/*
 * Hard-real-time LS bit engines (HPF "HRT" file, frozen to hrt-nrf54l15.s).
 *
 * Bit timing comes from VTIM counter 0 in RELOAD mode: WAIT0 stalls until the
 * counter reaches zero, so the instruction right after it (OUT write or IN
 * read) happens at a fixed offset from the counter event regardless of the
 * code path. One LS bit is 128 MHz / 1.5 MHz = 85.333 cycles, produced by the
 * reload pattern 85, 85, 86 (TOP 84, 84, 85), i.e. exactly 1.5 Mbit/s.
 *
 * LS line states on the VIO pins: J = D- high, K = D+ high, SE0 = both low.
 */
#include "hrt.h"
#include <hal/nrf_vpr_csr.h>

#define CSR_STR2(x) #x
#define CSR_STR(x) CSR_STR2(x)

volatile uint32_t su_last_activity;
static uint32_t time_acc;
static uint32_t time_last16;

static inline uint32_t cnt1(void)
{
	uint32_t v;

	__asm__ volatile("csrr %0, " CSR_STR(VPRCSR_NORDIC_CNT1) : "=r"(v));
	return v & 0xFFFFu;
}

uint32_t su_now(void)
{
	uint32_t c = cnt1();

	time_acc += (time_last16 - c) & 0xFFFFu;
	time_last16 = c;
	return time_acc;
}

void su_time_init(void)
{
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_CNTMODE1) ", %0" : : "r"(1u)); /* WRAP */
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_CNT1) ", %0" : : "r"(0xFFFFu));
	time_last16 = cnt1();
	time_acc = 0;
}

#define DP SU_HRT_DP
#define DM SU_HRT_DM
#define SU_HRT_DP (1u << 4) /* P2.00 = VIO4 */
#define SU_HRT_DM (1u << 0) /* P2.01 = VIO0 */
#define LINE_J DM
#define LINE_K DP
#define LINE_JK (DP | DM)

#define TOP_BASE 84u
/* Cycles from the K->J edge of SYNC bit 0 to the middle of bit 1. */
#define RX_FIRST_COUNT 36u
#define EDGE_POLLS 100u   /* ~5 cycles each: ~3 bit times */
#define EOP_POLLS 150u


static inline uint32_t vio_in(void)
{
	uint32_t v;

	__asm__ volatile("csrr %0, " CSR_STR(VPRCSR_NORDIC_IN) : "=r"(v));
	return v;
}

/* Wait for the counter event, then sample: fixed latency. */
static inline uint32_t wait_in(void)
{
	uint32_t v;

	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_WAIT0) ", zero\n\t"
			 "csrr %0, " CSR_STR(VPRCSR_NORDIC_IN)
			 : "=r"(v) : : "memory");
	return v;
}

/* Wait for the counter event, then drive: fixed latency. */
static inline void wait_out(uint32_t level)
{
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_WAIT0) ", zero\n\t"
			 "csrw " CSR_STR(VPRCSR_NORDIC_OUT) ", %0"
			 : : "r"(level) : "memory");
}

static inline void wait_only(void)
{
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_WAIT0) ", zero" : : : "memory");
}

static inline void set_top(uint32_t top)
{
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_CNTTOP) ", %0" : : "r"(top));
}

static inline void set_cnt0(uint32_t v)
{
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_CNT0) ", %0" : : "r"(v));
}

static inline void set_out(uint32_t v)
{
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_OUT) ", %0" : : "r"(v));
}

static inline void set_dir(uint32_t v)
{
	__asm__ volatile("csrw " CSR_STR(VPRCSR_NORDIC_DIR) ", %0" : : "r"(v));
}

/* Next reload value in the 84, 84, 85 pattern. */
static inline uint32_t next_top(uint32_t *phase)
{
	uint32_t p = *phase;

	*phase = (p == 0u) ? 2u : (p - 1u);
	return TOP_BASE + (p == 0u ? 1u : 0u);
}

int su_rx(uint8_t *buf, uint32_t max, uint32_t timeout_cycles, uint32_t *t_eop)
{
	uint32_t t0 = su_now();
	uint32_t in;

	/* Wait for the first SYNC K (D+ high). Idle J held only by the weak
	 * internal pull-up reads as 0 on D- (below VIH), so idle and SE0 look
	 * alike; a host-driven J (end of every EOP, incl. keep-alives) reads 1
	 * and is recorded as bus activity. Time is checked every 8 polls to
	 * keep the poll tight.
	 */
	uint32_t act = 0u;
	uint32_t k = 8u;

	for (;;) {
		in = vio_in();
		if (in & DP) {
			break;
		}
		act |= in & DM;
		if (--k == 0u) {
			k = 8u;
			uint32_t t = su_now();

			if (act) {
				su_last_activity = t;
				act = 0u;
			}
			if ((t - t0) > timeout_cycles) {
				return SU_RX_NONE;
			}
		}
	}

	/* Align on the K->J edge that ends SYNC bit 0; polled tightly so the
	 * phase error is a few cycles even if the first K was seen late.
	 */
	k = EDGE_POLLS;
	while (vio_in() & DP) {
		if (--k == 0u) {
			su_last_activity = su_now();
			return SU_RX_NOEDGE;
		}
	}
	set_top(TOP_BASE);
	set_cnt0(RX_FIRST_COUNT);

	/* Bit 0 of SYNC (a 0: J->K) is already accounted for. */
	uint32_t prev = 1u;   /* level of bit 0: K */
	uint32_t ones = 0u;
	uint32_t byte = 0u;
	uint32_t nbits = 1u;
	uint32_t n = 0u;
	uint32_t phase = 2u;
	int ret;

	for (;;) {
		in = wait_in();
		set_top(next_top(&phase));
		if (!(in & (DP | DM))) {
			break; /* SE0: EOP */
		}
		uint32_t level = (in & DP) ? 1u : 0u;
		uint32_t bit = (level == prev) ? 1u : 0u;

		prev = level;
		if (ones == 6u) {
			if (bit) {
				ret = SU_RX_STUFF;
				goto out;
			}
			ones = 0u; /* stuffed 0 dropped */
			continue;
		}
		ones = bit ? (ones + 1u) : 0u;
		byte = (byte >> 1) | (bit << 7);
		if (++nbits == 8u) {
			if (n == max) {
				ret = SU_RX_OVF;
				goto out;
			}
			buf[n++] = (uint8_t)byte;
			nbits = 0u;
		}
	}
	ret = (int)n;
out:
	/* Let the EOP (or the rest of a bad packet) finish: the host drives J
	 * for one bit after SE0. If that is missed, stop after the timeout.
	 */
	k = EOP_POLLS;
	for (;;) {
		in = vio_in();
		if (((in & DM) && !(in & DP)) || --k == 0u) {
			break;
		}
	}
	*t_eop = su_now();
	su_last_activity = *t_eop;
	return ret;
}

void su_tx(const uint8_t *buf, uint32_t n, uint32_t t_start)
{
	while ((int32_t)(su_now() - t_start) < 0) {
	}

	/* Drive idle J first, then start the bit clock. */
	set_out(LINE_J);
	set_dir(DP | DM);
	set_top(TOP_BASE);
	set_cnt0(8u);

	uint32_t level = LINE_J;
	uint32_t ones = 0u;
	uint32_t phase = 2u;
	uint32_t byte = 0x80u; /* SYNC */
	uint32_t bits = 8u;

	for (;;) {
		if (byte & 1u) {
			ones++;
		} else {
			level ^= LINE_JK;
			ones = 0u;
		}
		byte >>= 1;
		wait_out(level);
		set_top(next_top(&phase));
		if (ones == 6u) {
			level ^= LINE_JK;
			ones = 0u;
			wait_out(level);
			set_top(next_top(&phase));
		}
		if (--bits == 0u) {
			if (n == 0u) {
				break;
			}
			byte = *buf++;
			n--;
			bits = 8u;
		}
	}

	/* EOP: SE0 for two bits, J for one bit, then release. */
	wait_out(0u);
	set_top(next_top(&phase));
	wait_only();
	set_top(next_top(&phase));
	wait_out(LINE_J);
	set_top(next_top(&phase));
	wait_only();
	set_dir(0u);
}
