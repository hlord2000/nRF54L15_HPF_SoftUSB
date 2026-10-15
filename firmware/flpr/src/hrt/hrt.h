#ifndef BENCH_HRT_H
#define BENCH_HRT_H
#include <stdint.h>
void bench_hrt_tx(const volatile uint32_t *cells, uint32_t count);
void bench_hrt_tx_sample(const volatile uint32_t *cells, uint32_t count,
                         volatile uint16_t *samples);
#endif
