#ifndef SOFTUSB_BENCH_IPC_H
#define SOFTUSB_BENCH_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/devicetree.h>

#define BENCH_MAGIC 0x53554231u
#define BENCH_ABI 1u
#define BENCH_CAPACITY 1024u
#define BENCH_ARM_KEY 0x42454e43u
#define BENCH_DP (1u << 4) /* P2.00 -> VIO4 */
#define BENCH_DM (1u << 0) /* P2.01 -> VIO0 */
#define BENCH_SYNC (1u << 1) /* P2.02 -> VIO1 */
#define BENCH_RX_DP (1u << 3) /* P2.03 -> VIO3 */
#define BENCH_RX_DM (1u << 2) /* P2.04 -> VIO2 */
#define BENCH_DRIVE (BENCH_DP | BENCH_DM | BENCH_SYNC)
#define BENCH_SAMPLE 1u
#define BENCH_FS_EXPERIMENTAL 2u

enum bench_command { BENCH_CMD_DISARM=1, BENCH_CMD_ARM, BENCH_CMD_TX };
enum bench_result { BENCH_OK, BENCH_BAD_ABI, BENCH_BAD_COMMAND,
    BENCH_NOT_ARMED, BENCH_BAD_LENGTH, BENCH_BAD_FLAGS, BENCH_BAD_CELL };

/* Both cores: 32-bit little endian, naturally aligned. No pointers/size_t in ABI.
 * CPUAPP is the only request producer; FLPR the only completion producer.
 * Producer fills request/cells then release-publishes request_seq.
 * Consumer acquire-loads request_seq, finishes all reads/writes then release-
 * publishes completed_seq. Request/cells must stay immutable until completion.
 * No data cache. Only one in-flight request. Polling, no mailbox ISR in HRT.
 */
struct bench_ipc {
    uint32_t magic, abi, ready, request_seq, completed_seq;
    uint32_t command, key, count, flags;
    uint32_t result, armed, tx_count, sample_count;
    uint32_t vio_in, vio_dir, core_hz;
    uint32_t reserved[16];
    /* Cell = (following cell duration minus 1) << 16 | complete VIO OUT.
     * Only DP/DM/SYNC may be set. SE1 is rejected. */
    uint32_t cells[BENCH_CAPACITY];
    uint16_t samples[BENCH_CAPACITY];
};
_Static_assert(offsetof(struct bench_ipc, cells) == 128, "IPC header ABI");
_Static_assert(sizeof(struct bench_ipc) <= DT_REG_SIZE(DT_NODELABEL(softusb_ipc)), "IPC size");
#define BENCH_IPC ((volatile struct bench_ipc *)DT_REG_ADDR(DT_NODELABEL(softusb_ipc)))
static inline uint32_t bench_acquire(volatile uint32_t *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static inline void bench_release(volatile uint32_t *p, uint32_t value)
{
    __atomic_store_n(p, value, __ATOMIC_RELEASE);
}
#endif
