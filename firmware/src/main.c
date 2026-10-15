#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <hal/nrf_oscillators.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "bench_ipc.h"
#include "softusb.h"

static K_MUTEX_DEFINE(bench_mutex);
static K_SEM_DEFINE(clock_sem, 0, 1);
static struct onoff_client clock_client;
static atomic_t clock_ready;
static uint8_t states[BENCH_CAPACITY];
static uint8_t received[BENCH_CAPACITY];
static uint8_t packet[128];

static void clock_notify(struct onoff_manager *mgr, struct onoff_client *cli,
                         uint32_t state, int res)
{
    ARG_UNUSED(mgr); ARG_UNUSED(cli); ARG_UNUSED(state); ARG_UNUSED(res);
    k_sem_give(&clock_sem);
}

static bool is_ready(void)
{
    volatile struct bench_ipc *ipc = BENCH_IPC;
    return bench_acquire(&ipc->ready) == BENCH_MAGIC &&
        ipc->magic == BENCH_MAGIC && ipc->abi == BENCH_ABI;
}

/* Caller holds mutex, including descriptor preparation and response copying. */
static int request(uint32_t command, uint32_t count, uint32_t flags)
{
    volatile struct bench_ipc *ipc = BENCH_IPC;
    if (!is_ready()) { return -ENODEV; }
    uint32_t old_seq = bench_acquire(&ipc->request_seq);
    if (bench_acquire(&ipc->completed_seq) != old_seq) { return -EBUSY; }
    ipc->command = command;
    ipc->key = command == BENCH_CMD_DISARM ? 0 : BENCH_ARM_KEY;
    ipc->count = count;
    ipc->flags = flags;
    uint32_t seq = old_seq + 1;
    bench_release(&ipc->request_seq, seq);
    int64_t deadline = k_uptime_get() + 1000;
    while (bench_acquire(&ipc->completed_seq) != seq) {
        if (k_uptime_get() >= deadline) {
            /* Do not modify buffers/request or claim hardware is disarmed.
             * No cancellation while FLPR owns the slot. Reboot for recovery. */
            return -ETIMEDOUT;
        }
        k_sleep(K_MSEC(1));
    }
    return ipc->result == BENCH_OK ? 0 : -(100 + (int)ipc->result);
}

static bool parse_number(const char *text, unsigned long max, unsigned long *value)
{
    char *end;
    errno = 0;
    unsigned long v = strtoul(text, &end, 0);
    if (errno || !*text || *text == '-' || *end || v > max) { return false; }
    *value = v;
    return true;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    volatile struct bench_ipc *ipc = BENCH_IPC;
    k_mutex_lock(&bench_mutex, K_FOREVER);
    shell_print(sh, "bench only; ready=%u abi=%u armed=%u result=%u req=%u done=%u",
        is_ready(), ipc->abi, ipc->armed, ipc->result,
        bench_acquire(&ipc->request_seq), bench_acquire(&ipc->completed_seq));
    shell_print(sh, "tx=%u samples=%u last_VIO.IN=0x%04x last_VIO.DIR=0x%04x core_hz=%u HFXO=%u PLL.CURRENTFREQ=%u",
        ipc->tx_count, ipc->sample_count, ipc->vio_in, ipc->vio_dir,
        ipc->core_hz, (unsigned)atomic_get(&clock_ready), (unsigned)nrf_oscillators_pll_freq_get(NRF_OSCILLATORS));
    shell_print(sh, "P2.00 DP=VIO4 P2.01 DM=VIO0 P2.02 sync=VIO1 P2.03/04 RX=VIO3/2");
    k_mutex_unlock(&bench_mutex);
    return 0;
}

static int cmd_arm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    if (strcmp(argv[1], "CONFIRM_BENCH_WIRING_3V3")) {
        shell_error(sh, "Revision/routing + measured3.3V + bench-only wiring must be checked first");
        return -EINVAL;
    }
    if (!atomic_get(&clock_ready) || nrf_oscillators_pll_freq_get(NRF_OSCILLATORS) != NRF_OSCILLATORS_PLL_FREQ_128M) {
        shell_error(sh, "Stable HFXO and actual128MHz PLL required");
        return -EIO;
    }
    k_mutex_lock(&bench_mutex, K_FOREVER);
    int rc = request(BENCH_CMD_ARM, 0, 0);
    k_mutex_unlock(&bench_mutex);
    shell_print(sh, "arm rc=%d (inputs remain high-Z until a TX command)", rc);
    return rc;
}

static int cmd_disarm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    k_mutex_lock(&bench_mutex, K_FOREVER);
    int rc = request(BENCH_CMD_DISARM, 0, 0);
    k_mutex_unlock(&bench_mutex);
    shell_print(sh, "disarm rc=%d", rc);
    return rc;
}

static int submit_states(const struct shell *sh, bool low_speed, bool sample, size_t count)
{
    volatile struct bench_ipc *ipc = BENCH_IPC;
    if (!count || count > BENCH_CAPACITY) { return -EMSGSIZE; }
    if (!is_ready() || !ipc->armed) { return -EACCES; }
    if (bench_acquire(&ipc->request_seq) != bench_acquire(&ipc->completed_seq)) { return -EBUSY; }
    if (!atomic_get(&clock_ready) || nrf_oscillators_pll_freq_get(NRF_OSCILLATORS) != NRF_OSCILLATORS_PLL_FREQ_128M) {
        return -EIO;
    }
    /* Rational 128MHz periods: LS85,85,86, FS10,11,11 cycles.
     * Mean frequency is not a USB jitter/compliance claim. */
    for (size_t i = 0; i < count; ++i) {
        uint32_t ticks = low_speed ? (i % 3 == 2 ? 86 : 85) : (i % 3 == 0 ? 10 : 11);
        uint32_t out = BENCH_SYNC;
        if (states[i] & SOFTUSB_DP) { out |= BENCH_DP; }
        if (states[i] & SOFTUSB_DM) { out |= BENCH_DM; }
        if (states[i] > SOFTUSB_DM) { return -EINVAL; }
        ipc->cells[i] = ((ticks - 1) << 16) | out;
    }
    uint32_t flags = (low_speed ? 0 : BENCH_FS_EXPERIMENTAL) | (sample ? BENCH_SAMPLE : 0);
    int rc = request(BENCH_CMD_TX, count, flags);
    if (!rc) {
        shell_print(sh, "%s cells=%u rc=0; completed with outputs released",
            low_speed ? "LS target" : "FS NONCOMPLIANT EXPERIMENT", (unsigned)count);
    } else {
        shell_error(sh, "TX rc=%d; no successful completion claimed", rc);
    }
    if (!rc && sample) {
        unsigned mismatches = 0;
        for (size_t i = 0; i < count; ++i) {
            uint16_t raw = ipc->samples[i];
            received[i] = ((raw & BENCH_RX_DP) ? SOFTUSB_DP : 0) |
                          ((raw & BENCH_RX_DM) ? SOFTUSB_DM : 0);
            mismatches += received[i] != states[i];
        }
        size_t bytes;
        softusb_status decoded = softusb_decode(true, received, count, packet, sizeof(packet), &bytes);
        shell_print(sh, "external-input samples=%u mismatch=%u codec=%s bytes=%u (no wiring inferred)",
            ipc->sample_count, mismatches, softusb_status_string(decoded), (unsigned)bytes);
    }
    return rc;
}

/* wave ls|fs [cells] emits alternating K/J plus physical SE0,SE0,J. */
static int cmd_wave(const struct shell *sh, size_t argc, char **argv)
{
    bool ls = !strcmp(argv[1], "ls");
    if (!ls && strcmp(argv[1], "fs")) { return -EINVAL; }
    unsigned long count = 96;
    if (argc == 3 && !parse_number(argv[2], BENCH_CAPACITY - 3, &count)) { return -EINVAL; }
    if (count < 8) { return -EINVAL; }
    k_mutex_lock(&bench_mutex, K_FOREVER);
    for (size_t i = 0; i < count; ++i) {
        states[i] = ((i & 1) == 0) == ls ? SOFTUSB_DP : SOFTUSB_DM;
    }
    states[count] = SOFTUSB_SE0;
    states[count+1] = SOFTUSB_SE0;
    states[count+2] = ls ? SOFTUSB_DM : SOFTUSB_DP;
    int rc = submit_states(sh, ls, false, count + 3);
    k_mutex_unlock(&bench_mutex);
    return rc;
}

/* tx ls|fs full_pid [payload bytes]; loop full_pid [payload bytes] is LS
 * independent-pad sample capture during TX. All numbers accept0x prefix. */
static int packet_command(const struct shell *sh, size_t argc, char **argv, bool sample)
{
    bool ls = sample || !strcmp(argv[1], "ls");
    if (!sample && !ls && strcmp(argv[1], "fs")) { return -EINVAL; }
    size_t first = sample ? 1 : 2;
    unsigned long pid;
    if (!parse_number(argv[first], 255, &pid)) { return -EINVAL; }
    uint8_t payload[64];
    size_t len = argc - first - 1;
    if (len > sizeof(payload)) { return -EMSGSIZE; }
    for (size_t i = 0; i < len; ++i) {
        unsigned long v;
        if (!parse_number(argv[first + 1 + i], 255, &v)) { return -EINVAL; }
        payload[i] = v;
    }
    k_mutex_lock(&bench_mutex, K_FOREVER);
    size_t count;
    softusb_status status = softusb_encode_packet(ls, pid, payload, len,
        states, sizeof(states), &count);
    int rc;
    if (status != SOFTUSB_OK) {
        shell_error(sh, "codec: %s", softusb_status_string(status));
        rc = -EINVAL;
    } else {
        rc = submit_states(sh, ls, sample, count);
    }
    k_mutex_unlock(&bench_mutex);
    return rc;
}
static int cmd_tx(const struct shell *sh, size_t argc, char **argv)
{
    return packet_command(sh, argc, argv, false);
}
static int cmd_loop(const struct shell *sh, size_t argc, char **argv)
{
    return packet_command(sh, argc, argv, true);
}
SHELL_STATIC_SUBCMD_SET_CREATE(bench_sub,
    SHELL_CMD_ARG(status, NULL, "IPC + pin + clock status", cmd_status, 1, 0),
    SHELL_CMD_ARG(arm, NULL, "CONFIRM_BENCH_WIRING_3V3 (inputs only)", cmd_arm, 2, 0),
    SHELL_CMD_ARG(disarm, NULL, "Release all bench outputs", cmd_disarm, 1, 0),
    SHELL_CMD_ARG(wave, NULL, "ls|fs [alternating cells8..1021]", cmd_wave, 2, 1),
    SHELL_CMD_ARG(tx, NULL, "ls|fs full_pid [payload bytes]", cmd_tx, 3, 64),
    SHELL_CMD_ARG(loop, NULL, "full_pid [payload bytes]; LS external-pad capture", cmd_loop, 2, 8),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(softusb, &bench_sub, "SoftUSB isolated bench fixture", NULL);

int main(void)
{
    struct onoff_manager *hfxo = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    sys_notify_init_callback(&clock_client.notify, clock_notify);
    if (hfxo != NULL && onoff_request(hfxo, &clock_client) >= 0 &&
        k_sem_take(&clock_sem, K_SECONDS(2)) == 0) {
        int result;
        atomic_set(&clock_ready, sys_notify_fetch_result(&clock_client.notify, &result) == 0 && result == 0);
    }
    /* Keep the crystal request for the lifetime of this fixture. No auto-TX. */
    printk("SoftUSB bench: disarmed, no auto-TX; HFXO=%u. Use softusb status.\n", (unsigned)atomic_get(&clock_ready));
    return 0;
}
