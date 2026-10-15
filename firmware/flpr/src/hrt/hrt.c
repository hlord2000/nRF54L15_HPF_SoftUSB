#include "hrt.h"
#include <hal/nrf_vpr_csr.h>

/* CNTTOP is combined; CNT1 is stopped and unused. OUT upper bits are ignored.
 * No shifter: VIO0 is DM, and automatic-clock mode would corrupt DM.
 * Constant instruction path for all states (J/K/SE0). Counter reloads anchor
 * subsequent edges; descriptor n supplies the duration of cell n. TOP for the
 * upcoming cell is installed before waiting for the current cell boundary.
 * .option norvc makes disassembly/cycle review unambiguous.
 */
#define START \
    ".option push\n.option norvc\n" \
    "lw t0, 0(%[p])\n" \
    "srli t1, t0, 16\n" \
    "csrw %[top], t1\n" \
    "csrw %[cnt], t1\n" \
    "csrw %[out], t0\n"
#define NEXT \
    "addi %[n], %[n], -1\n" \
    "beqz %[n], 2f\n" \
    "addi %[p], %[p], 4\n" \
    "lw t0, 0(%[p])\n" \
    "srli t1, t0, 16\n" \
    "csrw %[top], t1\n" \
    "csrw %[wait], zero\n" \
    "csrw %[out], t0\n" \
    "j 1b\n"
#define END \
    "2: csrw %[wait], zero\n" \
    "csrw %[dir], zero\n" \
    "csrw %[cnt], zero\n" \
    ".option pop\n"
#define CSRS \
    [top] "i" (VPRCSR_NORDIC_CNTTOP), [cnt] "i" (VPRCSR_NORDIC_CNT0), \
    [out] "i" (VPRCSR_NORDIC_OUT), [wait] "i" (VPRCSR_NORDIC_WAIT0), \
    [dir] "i" (VPRCSR_NORDIC_DIR)

void bench_hrt_tx(const volatile uint32_t *cells, uint32_t count)
{
    __asm__ volatile(START "1:\n" NEXT END
        : [p] "+r" (cells), [n] "+r" (count)
        : CSRS : "t0", "t1", "memory");
}

/* LS only. Raw independent input samples after a fixed 16 instruction settling
 * interval, NOT an asynchronous USB receiver or proof of physical loopback.
 * Input pads P2.03/04 must actually be wired by the user. */
void bench_hrt_tx_sample(const volatile uint32_t *cells, uint32_t count,
                         volatile uint16_t *samples)
{
    __asm__ volatile(START
        "1: .rept 16\nnop\n.endr\n"
        "csrr t1, %[in]\n"
        "sh t1, 0(%[s])\n"
        "addi %[s], %[s], 2\n"
        NEXT END
        : [p] "+r" (cells), [n] "+r" (count), [s] "+r" (samples)
        : CSRS, [in] "i" (VPRCSR_NORDIC_IN)
        : "t0", "t1", "memory");
}
