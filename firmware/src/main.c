/*
 * SoftUSB demo: a low-speed vendor-defined HID device on the stock Zephyr
 * USB device stack (device_next), running over the FLPR bit-banged PHY.
 *
 * The vendor usage page means the host binds hidraw only: no keystrokes or
 * pointer motion. Each 8-byte OUT report is echoed back as the next IN
 * report; otherwise an IN report with a counter is sent every 500 ms.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include "softusb_ipc.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

USBD_DEVICE_DEFINE(su_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x2fe3, 0x0100);
USBD_DESC_LANG_DEFINE(su_lang);
USBD_DESC_MANUFACTURER_DEFINE(su_mfr, "HPF SoftUSB");
USBD_DESC_PRODUCT_DEFINE(su_product, "nRF54L15 FLPR LS HID");
USBD_DESC_CONFIG_DEFINE(su_cfg_desc, "LS configuration");
USBD_CONFIGURATION_DEFINE(su_config, 0, 50, &su_cfg_desc);

static const uint8_t report_desc[] = {
	0x06, 0x00, 0xFF, /* Usage Page (Vendor 0xFF00) */
	0x09, 0x01,       /* Usage (1) */
	0xA1, 0x01,       /* Collection (Application) */
	0x15, 0x00,       /*   Logical Minimum (0) */
	0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
	0x75, 0x08,       /*   Report Size (8) */
	0x95, 0x08,       /*   Report Count (8) */
	0x09, 0x01,       /*   Usage (1) */
	0x81, 0x02,       /*   Input (Data, Var, Abs) */
	0x09, 0x01,       /*   Usage (1) */
	0x91, 0x02,       /*   Output (Data, Var, Abs) */
	0xC0,             /* End Collection */
};

UDC_STATIC_BUF_DEFINE(in_report, 8);
static uint8_t echo[8];
static volatile bool echo_pending;
static volatile bool hid_ready;
static K_SEM_DEFINE(report_done, 1, 1);
static K_SEM_DEFINE(echo_sem, 0, 1);

static void iface_ready(const struct device *dev, const bool ready)
{
	LOG_INF("HID interface %s", ready ? "ready" : "not ready");
	hid_ready = ready;
}

static int get_report(const struct device *dev, const uint8_t type, const uint8_t id,
		      const uint16_t len, uint8_t *const buf)
{
	return 0;
}

static int set_report(const struct device *dev, const uint8_t type, const uint8_t id,
		      const uint16_t len, const uint8_t *const buf)
{
	return 0;
}

static void output_report(const struct device *dev, const uint16_t len,
			  const uint8_t *const buf)
{
	memcpy(echo, buf, MIN(len, sizeof(echo)));
	echo_pending = true;
	k_sem_give(&echo_sem);
	LOG_INF("OUT report %02x %02x %02x %02x %02x %02x %02x %02x", echo[0], echo[1],
		echo[2], echo[3], echo[4], echo[5], echo[6], echo[7]);
}

static void input_report_done(const struct device *dev, const uint8_t *const report)
{
	k_sem_give(&report_done);
}

static const struct hid_device_ops hid_ops = {
	.iface_ready = iface_ready,
	.get_report = get_report,
	.set_report = set_report,
	.output_report = output_report,
	.input_report_done = input_report_done,
};

static void msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));
}

static int usb_setup(void)
{
	int err;

	err = usbd_add_descriptor(&su_usbd, &su_lang);
	err = err ?: usbd_add_descriptor(&su_usbd, &su_mfr);
	err = err ?: usbd_add_descriptor(&su_usbd, &su_product);
	err = err ?: usbd_add_configuration(&su_usbd, USBD_SPEED_FS, &su_config);
	err = err ?: usbd_register_all_classes(&su_usbd, USBD_SPEED_FS, 1, NULL);
	err = err ?: usbd_device_set_code_triple(&su_usbd, USBD_SPEED_FS, 0, 0, 0);
	err = err ?: usbd_device_set_bcd_usb(&su_usbd, USBD_SPEED_FS, 0x0110);
	err = err ?: usbd_msg_register_cb(&su_usbd, msg_cb);
	err = err ?: usbd_init(&su_usbd);
	err = err ?: usbd_enable(&su_usbd);
	return err;
}

int main(void)
{
	const struct device *hid_dev = DEVICE_DT_GET(DT_NODELABEL(hid_dev_0));
	volatile struct su_ipc *ipc = SU_IPC;
	uint32_t counter = 0;
	int err;

	/* The FLPR's 128 MHz must be crystal-accurate: the RC-based clock was
	 * measured ~0.8% fast, enough to break 96-bit LS data packets.
	 */
	static struct onoff_client hfxo_cli;
	int res;

	sys_notify_init_spinwait(&hfxo_cli.notify);
	err = onoff_request(z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF), &hfxo_cli);
	while (err >= 0 && sys_notify_fetch_result(&hfxo_cli.notify, &res) == -EAGAIN) {
		k_msleep(1);
	}
	LOG_INF("HFXO request %d", err);

	LOG_INF("SoftUSB LS device, waiting for FLPR");
	while (su_load(&ipc->flpr_ready) != SU_MAGIC) {
		k_msleep(1);
	}
	LOG_INF("FLPR ready, core %u Hz", ipc->st_core_hz);

	err = hid_device_register(hid_dev, report_desc, sizeof(report_desc), &hid_ops);
	if (err) {
		LOG_ERR("HID register failed %d", err);
		return 0;
	}
	err = usb_setup();
	if (err) {
		LOG_ERR("USB setup failed %d", err);
		return 0;
	}

	uint32_t last_stats = k_uptime_get_32();

	while (true) {
		(void)k_sem_take(&echo_sem, K_MSEC(500));
		if (hid_ready && k_sem_take(&report_done, K_MSEC(100)) == 0) {
			if (echo_pending) {
				memcpy(in_report, echo, sizeof(echo));
				echo_pending = false;
			} else {
				sys_put_le32(counter++, in_report);
				memcpy(&in_report[4], "SUSB", 4);
			}
			err = hid_device_submit_report(hid_dev, sizeof(echo), in_report);
			if (err) {
				k_sem_give(&report_done);
			}
		}
		if (k_uptime_get_32() - last_stats > 5000) {
			last_stats = k_uptime_get_32();
			LOG_INF("addr %u rx %u tok %u setup %u tx %u ack %u noack %u nak %u "
				"crc %u stuff %u sync %u to %u rst %u",
				ipc->address, ipc->st_rx_pkts, ipc->st_tokens_mine,
				ipc->st_setups, ipc->st_tx_pkts, ipc->st_in_acked,
				ipc->st_in_noack, ipc->st_naks, ipc->st_rx_crc, ipc->st_rx_stuff,
				ipc->st_rx_bad_sync, ipc->st_data_timeout, ipc->st_resets);
			LOG_HEXDUMP_INF((const uint8_t *)ipc->dbg_raw, 16, "last bad data");
			uint32_t m0 = ipc->dbg_mcycle;
			uint32_t u0 = k_cycle_get_32();

			k_msleep(100);
			uint32_t m1 = ipc->dbg_mcycle;
			uint32_t u1 = k_cycle_get_32();

			LOG_INF("bad len %u core %u cal300 %u mcycle/100ms %u (app cyc %u @%u)",
				ipc->dbg_len, ipc->st_core_hz, ipc->cal_mcycle_per_300, m1 - m0,
				u1 - u0, sys_clock_hw_cycles_per_sec());
		}
	}
	return 0;
}
