/*
 * Attach-detect experiment: no external resistors.
 * P2.00 (D+) input, no pull. P2.01 (D-) input with the internal pull-up,
 * which is the only thing that can signal an LS attach to the host.
 * Cycle: pull-up on 10 s (attached), off 2 s (detached), repeat.
 * Neither pin is ever driven as an output.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <hal/nrf_gpio.h>

#define DP_PIN NRF_GPIO_PIN_MAP(2, 0)
#define DM_PIN NRF_GPIO_PIN_MAP(2, 1)

static void cfg_input(uint32_t pin, nrf_gpio_pin_pull_t pull)
{
	nrf_gpio_cfg(pin, NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT, pull,
		     NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
}

int main(void)
{
	cfg_input(DP_PIN, NRF_GPIO_PIN_NOPULL);
	cfg_input(DM_PIN, NRF_GPIO_PIN_NOPULL);

	for (uint32_t cycle = 0;; cycle++) {
		cfg_input(DM_PIN, NRF_GPIO_PIN_PULLUP);
		k_msleep(50);
		printk("cycle %u attach: D+=%u D-=%u\n", cycle, nrf_gpio_pin_read(DP_PIN),
		       nrf_gpio_pin_read(DM_PIN));
		k_msleep(9950);
		cfg_input(DM_PIN, NRF_GPIO_PIN_NOPULL);
		k_msleep(50);
		printk("cycle %u detach: D+=%u D-=%u\n", cycle, nrf_gpio_pin_read(DP_PIN),
		       nrf_gpio_pin_read(DM_PIN));
		k_msleep(1950);
	}
	return 0;
}
