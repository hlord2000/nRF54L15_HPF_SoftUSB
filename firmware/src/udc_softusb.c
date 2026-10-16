/*
 * Zephyr UDC driver for the SoftUSB FLPR low-speed engine.
 *
 * The FLPR answers all bus transactions by itself; this driver only moves
 * payload between Zephyr's UDC request buffers and the shared su_ipc block:
 * it stages IN packets (8 bytes + CRC16) one at a time, drains received OUT
 * packets and forwards SETUP/reset events to the USB device stack. A single
 * driver thread does all of this, woken by the FLPR's VEVIF event or by a
 * stack call, with a 1 ms poll as a safety net.
 *
 * The UDC API has no low-speed enum, so the device reports full speed to the
 * stack; descriptors must respect LS limits (EP0 MPS 8, interrupt EPs with
 * MPS <= 8 and bInterval >= 10 ms).
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/logging/log.h>
#include "udc_common.h"
#include "softusb_ipc.h"

LOG_MODULE_REGISTER(udc_softusb, CONFIG_UDC_DRIVER_LOG_LEVEL);

#define DT_DRV_COMPAT nordic_softusb_udc

struct su_in_state {
	uint32_t off;      /* bytes of the head buffer already sent */
	uint32_t chunk;    /* bytes in the staged packet */
	bool staged;       /* a packet of the head buffer is with the FLPR */
	bool zlp_sent;
};

struct udc_softusb_config {
	size_t num_of_eps;
	struct udc_ep_config *ep_cfg_in;
	struct udc_ep_config *ep_cfg_out;
	k_thread_stack_t *thread_stk;
	size_t thread_stk_sz;
	struct mbox_dt_spec mbox_rx;
};

struct udc_softusb_data {
	struct k_thread thread_data;
	struct k_sem sem;
	struct su_in_state in[SU_NUM_EP];
	uint32_t reset_seen;
	uint32_t setup_seen;
};

static volatile struct su_ipc *const ipc = SU_IPC;

static uint16_t crc16(const uint8_t *p, size_t n)
{
	uint32_t c = 0xFFFFu;

	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++) {
			c = (c & 1u) ? ((c >> 1) ^ 0xA001u) : (c >> 1);
		}
	}
	return (uint16_t)~c;
}

static void ctrl_changed(void)
{
	su_store(&ipc->ctrl_seq, ipc->ctrl_seq + 1u);
}

static void wake(const struct device *dev)
{
	struct udc_softusb_data *priv = udc_get_private(dev);

	k_sem_give(&priv->sem);
}

static void stage_in(const struct device *dev, uint32_t ep_idx)
{
	struct udc_softusb_data *priv = udc_get_private(dev);
	struct su_in_state *st = &priv->in[ep_idx];
	volatile struct su_in_ep *e = &ipc->in[ep_idx];
	struct udc_ep_config *cfg = udc_get_ep_cfg(dev, USB_EP_DIR_IN | ep_idx);
	struct net_buf *buf;

	/* Completion of the packet that was staged. */
	if (st->staged && su_load(&e->done) == e->req) {
		st->staged = false;
		st->off += st->chunk;
		buf = udc_buf_peek(cfg);
		if (buf != NULL && st->off >= buf->len) {
			bool need_zlp = udc_ep_buf_has_zlp(buf) && st->chunk == SU_MPS &&
					!st->zlp_sent && buf->len != 0;

			if (!need_zlp) {
				buf = udc_buf_get(cfg);
				st->off = 0;
				st->zlp_sent = false;
				udc_submit_ep_event(dev, buf, 0);
			}
		}
	}

	if (st->staged || e->req != su_load(&e->done) || cfg->stat.halted) {
		return;
	}
	buf = udc_buf_peek(cfg);
	if (buf == NULL) {
		return;
	}

	uint32_t n = MIN(SU_MPS, buf->len - MIN(st->off, buf->len));
	uint8_t pkt[SU_MPS + 2];

	if (st->off >= buf->len && buf->len != 0) {
		st->zlp_sent = true; /* trailing ZLP */
		n = 0;
	}
	memcpy(pkt, buf->data + st->off, n);
	uint16_t crc = crc16(pkt, n);

	pkt[n] = crc & 0xFF;
	pkt[n + 1] = crc >> 8;
	for (uint32_t i = 0; i < n + 2; i++) {
		e->data[i] = pkt[i];
	}
	e->len = n;
	e->gen = priv->setup_seen;
	st->chunk = n;
	st->staged = true;
	su_store(&e->req, e->req + 1u);
}

static void drain_out(const struct device *dev, uint32_t ep_idx)
{
	struct udc_softusb_data *priv = udc_get_private(dev);
	volatile struct su_out_ep *o = &ipc->out[ep_idx];
	struct udc_ep_config *cfg = udc_get_ep_cfg(dev, USB_EP_DIR_OUT | ep_idx);
	uint32_t full = su_load(&o->full);

	if (full == o->ack) {
		return;
	}
	if (ep_idx == 0 && o->gen != priv->setup_seen) {
		su_store(&o->ack, full); /* belongs to an older control transfer */
		return;
	}
	struct net_buf *buf = udc_buf_peek(cfg);

	if (buf == NULL || udc_get_buf_info(buf)->setup) {
		return; /* stack has not queued the data/status buffer yet: FLPR NAKs */
	}
	uint32_t len = o->len;
	uint32_t room = net_buf_tailroom(buf);
	uint32_t n = MIN(len, room);

	for (uint32_t i = 0; i < n; i++) {
		net_buf_add_u8(buf, o->data[i]);
	}
	su_store(&o->ack, full);
	if (len < udc_mps_ep_size(cfg) || net_buf_tailroom(buf) == 0) {
		buf = udc_buf_get(cfg);
		udc_submit_ep_event(dev, buf, 0);
	}
}

static void handle_events(const struct device *dev)
{
	const struct udc_softusb_config *config = dev->config;
	struct udc_softusb_data *priv = udc_get_private(dev);

	uint32_t rs = su_load(&ipc->reset_seq);

	if (rs != priv->reset_seen) {
		priv->reset_seen = rs;
		for (size_t i = 0; i < config->num_of_eps; i++) {
			priv->in[i].staged = false;
			priv->in[i].off = 0;
			priv->in[i].zlp_sent = false;
		}
		LOG_INF("bus reset");
		udc_submit_event(dev, UDC_EVT_RESET, 0);
	}

	uint32_t ss = su_load(&ipc->setup_seq);

	if (ss != priv->setup_seen) {
		uint8_t setup[8];
		uint32_t again;

		do {
			ss = su_load(&ipc->setup_seq);
			for (int i = 0; i < 8; i++) {
				setup[i] = ipc->setup[i];
			}
			again = su_load(&ipc->setup_seq);
		} while (again != ss);

		priv->setup_seen = ss;
		priv->in[0].staged = false;
		priv->in[0].off = 0;
		priv->in[0].zlp_sent = false;
		LOG_DBG("setup %02x %02x %02x %02x %02x %02x %02x %02x", setup[0], setup[1],
			setup[2], setup[3], setup[4], setup[5], setup[6], setup[7]);
		udc_setup_received(dev, setup);
	}

	for (size_t i = 0; i < config->num_of_eps; i++) {
		drain_out(dev, i);
		stage_in(dev, i);
	}
}

static void softusb_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct udc_softusb_data *priv = udc_get_private(dev);

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		(void)k_sem_take(&priv->sem, K_MSEC(1));
		if (!udc_is_enabled(dev)) {
			continue;
		}
		udc_lock_internal(dev, K_FOREVER);
		handle_events(dev);
		udc_unlock_internal(dev);
	}
}

static void mbox_cb(const struct device *mdev, mbox_channel_id_t ch, void *user_data,
		    struct mbox_msg *msg)
{
	ARG_UNUSED(mdev);
	ARG_UNUSED(ch);
	ARG_UNUSED(msg);
	wake(user_data);
}

static int udc_softusb_ep_enqueue(const struct device *dev, struct udc_ep_config *const cfg,
				  struct net_buf *buf)
{
	udc_buf_put(cfg, buf);
	wake(dev);
	return 0;
}

static int udc_softusb_ep_dequeue(const struct device *dev, struct udc_ep_config *const cfg)
{
	struct udc_softusb_data *priv = udc_get_private(dev);
	uint32_t idx = USB_EP_GET_IDX(cfg->addr);

	udc_ep_cancel_queued(dev, cfg);
	if (USB_EP_DIR_IS_IN(cfg->addr)) {
		/* A packet already staged may still go out; it is not tracked. */
		priv->in[idx].staged = false;
		priv->in[idx].off = 0;
		priv->in[idx].zlp_sent = false;
	}
	return 0;
}

static int udc_softusb_ep_enable(const struct device *dev, struct udc_ep_config *const cfg)
{
	uint32_t idx = USB_EP_GET_IDX(cfg->addr);

	if (idx == 0) {
		return 0;
	}
	if (USB_EP_DIR_IS_IN(cfg->addr)) {
		ipc->in_toggle_reset[idx]++;
		ipc->ep_in_enabled |= BIT(idx);
	} else {
		ipc->out_toggle_reset[idx]++;
		ipc->ep_out_enabled |= BIT(idx);
	}
	ctrl_changed();
	LOG_DBG("enable ep 0x%02x mps %u", cfg->addr, udc_mps_ep_size(cfg));
	return 0;
}

static int udc_softusb_ep_disable(const struct device *dev, struct udc_ep_config *const cfg)
{
	uint32_t idx = USB_EP_GET_IDX(cfg->addr);

	if (idx == 0) {
		return 0;
	}
	if (USB_EP_DIR_IS_IN(cfg->addr)) {
		ipc->ep_in_enabled &= ~BIT(idx);
	} else {
		ipc->ep_out_enabled &= ~BIT(idx);
	}
	ctrl_changed();
	return 0;
}

static int udc_softusb_ep_set_halt(const struct device *dev, struct udc_ep_config *const cfg)
{
	struct udc_softusb_data *priv = udc_get_private(dev);
	uint32_t idx = USB_EP_GET_IDX(cfg->addr);

	if (idx == 0) {
		/* Protocol stall: ends with the next SETUP (FLPR compares gens). */
		ipc->ep0_stall_gen = priv->setup_seen + 1u;
		return 0;
	}
	if (USB_EP_DIR_IS_IN(cfg->addr)) {
		ipc->ep_in_halt |= BIT(idx);
	} else {
		ipc->ep_out_halt |= BIT(idx);
	}
	cfg->stat.halted = true;
	return 0;
}

static int udc_softusb_ep_clear_halt(const struct device *dev, struct udc_ep_config *const cfg)
{
	uint32_t idx = USB_EP_GET_IDX(cfg->addr);

	if (idx == 0) {
		return 0;
	}
	if (USB_EP_DIR_IS_IN(cfg->addr)) {
		ipc->ep_in_halt &= ~BIT(idx);
		ipc->in_toggle_reset[idx]++;
	} else {
		ipc->ep_out_halt &= ~BIT(idx);
		ipc->out_toggle_reset[idx]++;
	}
	ctrl_changed();
	cfg->stat.halted = false;
	wake(dev);
	return 0;
}

static int udc_softusb_set_address(const struct device *dev, const uint8_t addr)
{
	/* The FLPR already switched after the SET_ADDRESS status stage; this
	 * keeps both sides consistent (and handles address 0 on reset).
	 */
	ipc->addr_req = addr;
	ipc->addr_req_seq++;
	ctrl_changed();
	LOG_INF("address %u", addr);
	return 0;
}

static int udc_softusb_host_wakeup(const struct device *dev)
{
	return -ENOTSUP;
}

static enum udc_bus_speed udc_softusb_device_speed(const struct device *dev)
{
	return UDC_BUS_SPEED_FS; /* the UDC API has no low-speed value */
}

static int udc_softusb_enable(const struct device *dev)
{
	ipc->connect = 1u;
	ctrl_changed();
	LOG_INF("connect (D- pull-up on)");
	return 0;
}

static int udc_softusb_disable(const struct device *dev)
{
	ipc->connect = 0u;
	ctrl_changed();
	return 0;
}

static int udc_softusb_init(const struct device *dev)
{
	if (udc_ep_enable_internal(dev, USB_CONTROL_EP_OUT, USB_EP_TYPE_CONTROL, SU_MPS, 0) ||
	    udc_ep_enable_internal(dev, USB_CONTROL_EP_IN, USB_EP_TYPE_CONTROL, SU_MPS, 0)) {
		LOG_ERR("Failed to enable control endpoint");
		return -EIO;
	}
	return 0;
}

static int udc_softusb_shutdown(const struct device *dev)
{
	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_OUT) ||
	    udc_ep_disable_internal(dev, USB_CONTROL_EP_IN)) {
		return -EIO;
	}
	return 0;
}

static int udc_softusb_preinit(const struct device *dev)
{
	const struct udc_softusb_config *config = dev->config;
	struct udc_softusb_data *priv = udc_get_private(dev);
	struct udc_data *data = dev->data;
	int err;

	k_mutex_init(&data->mutex);
	k_sem_init(&priv->sem, 0, 1);
	data->caps.rwup = false;
	data->caps.mps0 = UDC_MPS0_8;
	data->caps.addr_before_status = false;

	for (size_t i = 0; i < config->num_of_eps; i++) {
		struct udc_ep_config *o = &config->ep_cfg_out[i];
		struct udc_ep_config *in = &config->ep_cfg_in[i];

		o->caps.out = 1;
		in->caps.in = 1;
		if (i == 0) {
			o->caps.control = 1;
			in->caps.control = 1;
		} else {
			/* Low speed: interrupt endpoints only. */
			o->caps.interrupt = 1;
			in->caps.interrupt = 1;
		}
		o->caps.mps = SU_MPS;
		in->caps.mps = SU_MPS;
		o->addr = USB_EP_DIR_OUT | i;
		in->addr = USB_EP_DIR_IN | i;
		err = udc_register_ep(dev, o);
		if (!err) {
			err = udc_register_ep(dev, in);
		}
		if (err) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	/* Take ownership of the app-written half of the IPC block. */
	ipc->connect = 0;
	ipc->ep_in_enabled = 0;
	ipc->ep_out_enabled = 0;
	ipc->ep_in_halt = 0;
	ipc->ep_out_halt = 0;
	ipc->ep0_stall_gen = 0;
	for (size_t i = 0; i < SU_NUM_EP; i++) {
		ipc->in[i].req = ipc->in[i].done;
		ipc->out[i].ack = ipc->out[i].full;
	}
	priv->reset_seen = ipc->reset_seq;
	priv->setup_seen = ipc->setup_seq;
	su_store(&ipc->app_magic, SU_MAGIC);
	ctrl_changed();

	if (config->mbox_rx.dev != NULL && device_is_ready(config->mbox_rx.dev)) {
		err = mbox_register_callback_dt(&config->mbox_rx, mbox_cb, (void *)dev);
		if (!err) {
			err = mbox_set_enabled_dt(&config->mbox_rx, true);
		}
		if (err) {
			LOG_WRN("VEVIF event unavailable (%d), polling only", err);
		}
	}

	k_thread_create(&priv->thread_data, config->thread_stk, config->thread_stk_sz,
			softusb_thread, (void *)dev, NULL, NULL, K_PRIO_COOP(8), K_ESSENTIAL,
			K_NO_WAIT);
	k_thread_name_set(&priv->thread_data, "udc_softusb");
	return 0;
}

static void udc_softusb_lock(const struct device *dev)
{
	udc_lock_internal(dev, K_FOREVER);
}

static void udc_softusb_unlock(const struct device *dev)
{
	udc_unlock_internal(dev);
}

static const struct udc_api udc_softusb_api = {
	.lock = udc_softusb_lock,
	.unlock = udc_softusb_unlock,
	.device_speed = udc_softusb_device_speed,
	.init = udc_softusb_init,
	.enable = udc_softusb_enable,
	.disable = udc_softusb_disable,
	.shutdown = udc_softusb_shutdown,
	.set_address = udc_softusb_set_address,
	.host_wakeup = udc_softusb_host_wakeup,
	.ep_enable = udc_softusb_ep_enable,
	.ep_disable = udc_softusb_ep_disable,
	.ep_set_halt = udc_softusb_ep_set_halt,
	.ep_clear_halt = udc_softusb_ep_clear_halt,
	.ep_enqueue = udc_softusb_ep_enqueue,
	.ep_dequeue = udc_softusb_ep_dequeue,
};

#define UDC_SOFTUSB_DEVICE_DEFINE(n)                                                       \
	K_THREAD_STACK_DEFINE(udc_softusb_stack_##n, 2048);                                \
	static struct udc_ep_config ep_cfg_out_##n[SU_NUM_EP];                             \
	static struct udc_ep_config ep_cfg_in_##n[SU_NUM_EP];                              \
	static const struct udc_softusb_config udc_softusb_config_##n = {                  \
		.num_of_eps = SU_NUM_EP,                                                   \
		.ep_cfg_in = ep_cfg_in_##n,                                                \
		.ep_cfg_out = ep_cfg_out_##n,                                              \
		.thread_stk = udc_softusb_stack_##n,                                       \
		.thread_stk_sz = K_THREAD_STACK_SIZEOF(udc_softusb_stack_##n),             \
		.mbox_rx = MBOX_DT_SPEC_INST_GET(n, rx),                                   \
	};                                                                                 \
	static struct udc_softusb_data udc_priv_##n;                                       \
	static struct udc_data udc_data_##n = {                                            \
		.mutex = Z_MUTEX_INITIALIZER(udc_data_##n.mutex),                          \
		.priv = &udc_priv_##n,                                                     \
	};                                                                                 \
	DEVICE_DT_INST_DEFINE(n, udc_softusb_preinit, NULL, &udc_data_##n,                 \
			      &udc_softusb_config_##n, POST_KERNEL,                        \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &udc_softusb_api);

DT_INST_FOREACH_STATUS_OKAY(UDC_SOFTUSB_DEVICE_DEFINE)
