#include <zephyr/kernel.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_oscillators.h>
#include <hal/nrf_vpr_csr.h>
#include <hal/nrf_vpr_csr_vio.h>
#include <hal/nrf_vpr_csr_vtim.h>
#include "bench_ipc.h"
#include "hrt/hrt.h"

static void release_pins(void)
{
    nrf_vpr_csr_vio_dir_set(0);
    nrf_vpr_csr_vtim_simple_counter_set(0, 0);
    nrf_vpr_csr_vtim_simple_counter_set(1, 0);
    nrf_vpr_csr_vio_out_set(0);
}

static void claim_inputs(void)
{
    /* CTRLSEL=VPR, not pin-number==VIO-index. Never drive during handoff.
     * S0S1 avoids extra-drive E0E1 output-enable delay; electrical qualification
     * remains pending. P2.03/04 always inputs, no pulls. */
    release_pins();
    for (unsigned pin = 0; pin < 5; ++pin) {
        unsigned abs_pin = NRF_GPIO_PIN_MAP(2, pin);
        nrf_gpio_cfg(abs_pin, NRF_GPIO_PIN_DIR_INPUT,
            NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_NOPULL,
            NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
        nrf_gpio_pin_control_select(abs_pin, NRF_GPIO_PIN_SEL_VPR);
    }
}

static uint32_t execute(volatile struct bench_ipc *ipc)
{
    if (ipc->magic != BENCH_MAGIC || ipc->abi != BENCH_ABI) {
        ipc->armed = 0;
        release_pins();
        return BENCH_BAD_ABI;
    }
    if (ipc->command == BENCH_CMD_DISARM) {
        release_pins();
        ipc->armed = 0;
        return BENCH_OK;
    }
    if (ipc->command == BENCH_CMD_ARM) {
        ipc->armed = 0;
        release_pins();
        if (ipc->key != BENCH_ARM_KEY) {
            return BENCH_NOT_ARMED;
        }
        claim_inputs();
        ipc->armed = 1;
        return BENCH_OK;
    }
    if (ipc->command != BENCH_CMD_TX) {
        return BENCH_BAD_COMMAND;
    }
    if (!ipc->armed || ipc->key != BENCH_ARM_KEY) {
        return BENCH_NOT_ARMED;
    }
    if (ipc->count == 0 || ipc->count > BENCH_CAPACITY) {
        return BENCH_BAD_LENGTH;
    }
    uint32_t flags = ipc->flags;
    if ((flags & ~(BENCH_SAMPLE | BENCH_FS_EXPERIMENTAL)) ||
        ((flags & BENCH_SAMPLE) && (flags & BENCH_FS_EXPERIMENTAL))) {
        return BENCH_BAD_FLAGS;
    }
    /* Verify actual PLL readback every request, not just devicetree's128MHz. */
    if (nrf_oscillators_pll_freq_get(NRF_OSCILLATORS) != NRF_OSCILLATORS_PLL_FREQ_128M) {
        ipc->core_hz = 0;
        return BENCH_BAD_FLAGS;
    }
    ipc->core_hz = 128000000;
    for (uint32_t i = 0; i < ipc->count; ++i) {
        uint32_t cell = ipc->cells[i];
        uint32_t out = cell & 0xffffu;
        uint32_t top = cell >> 16;
        uint32_t base = (flags & BENCH_FS_EXPERIMENTAL) ? 9 : 84;
        if ((out & ~BENCH_DRIVE) || !(out & BENCH_SYNC) ||
            ((out & (BENCH_DP | BENCH_DM)) == (BENCH_DP | BENCH_DM)) ||
            top < base || top > base + 1) {
            return BENCH_BAD_CELL;
        }
    }
    ipc->sample_count = 0;
    /* Prepare J while high-Z; sync goes high with first SYNC K write. */
    nrf_vpr_csr_vio_out_set((flags & BENCH_FS_EXPERIMENTAL) ? BENCH_DP : BENCH_DM);
    nrf_vpr_csr_vtim_count_mode_set(0, NRF_VPR_CSR_VTIM_COUNT_RELOAD);
    nrf_vpr_csr_vtim_count_mode_set(1, NRF_VPR_CSR_VTIM_COUNT_STOP);
    unsigned int irq_key = irq_lock();
    nrf_vpr_csr_vio_dir_set(BENCH_DRIVE);
    if (flags & BENCH_SAMPLE) {
        bench_hrt_tx_sample(ipc->cells, ipc->count, ipc->samples);
        ipc->sample_count = ipc->count;
    } else {
        bench_hrt_tx(ipc->cells, ipc->count);
    }
    release_pins();
    irq_unlock(irq_key);
    ipc->tx_count++;
    return BENCH_OK;
}

int main(void)
{
    volatile struct bench_ipc *ipc = BENCH_IPC;
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
    release_pins();
    /* Do not claim CTRLSEL until arm. Flash/header routing is still external.
     * Clear request state on every boot so a stale ARM can never replay. */
    ipc->ready = 0;
    ipc->magic = BENCH_MAGIC;
    ipc->abi = BENCH_ABI;
    ipc->request_seq = 0;
    ipc->completed_seq = 0;
    ipc->command = BENCH_CMD_DISARM;
    ipc->key = 0;
    ipc->armed = 0;
    ipc->result = BENCH_OK;
    ipc->tx_count = 0;
    ipc->sample_count = 0;
    ipc->core_hz = 0;
    ipc->vio_dir = 0;
    bench_release(&ipc->ready, BENCH_MAGIC);
    uint32_t done = 0;
    for (;;) {
        uint32_t seq = bench_acquire(&ipc->request_seq);
        if (seq == done) {
            continue;
        }
        ipc->result = execute(ipc);
        ipc->vio_in = nrf_vpr_csr_vio_in_get();
        ipc->vio_dir = nrf_vpr_csr_vio_dir_get();
        done = seq;
        bench_release(&ipc->completed_seq, seq);
    }
}
