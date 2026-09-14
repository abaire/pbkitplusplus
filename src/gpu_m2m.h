/*
 * Copyright (c) 2026 wutno (aaron@installgentoo.net)
 * Copyright (c) 2026 Claude (Anthropic)
 * SPDX-License-Identifier: MIT (full text in gpu_m2m.c)
 */
#ifndef GPU_M2M_H
#define GPU_M2M_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after pb_init().  0 on success, negative on alloc failure. */
int gpum_init(void);

/* Queue a copy on M2MF (class 0x39): byte-granular, any alignment, no 2D
 * state touched, and NO CONTIGUITY REQUIREMENT -- both buffers are walked
 * page by page and physically contiguous runs are coalesced into kicks, so
 * plain heap memory is safe.  One outstanding copy at a time.  Caller makes
 * src globally visible first (sfence for WC, wbinvd for WB) and leaves both
 * buffers alone until completion.  Regions must not overlap. */
void gpum_start(void *dst, const void *src, size_t n);

/* 1 = done ok, 0 = still running, negative = GPU error word.
 * Each call is an uncached DRAM read that competes with the copy: a tight
 * loop on it measured 20% off the copy's throughput.  Space polls >= 2K
 * cycles apart (gpum_wait's backoff does). */
int gpum_done(void);

/* Poll with backoff until done/error/timeout.
 * 0 = ok, negative = GPU error or GPUM_ETIMEOUT. */
int gpum_wait(void);
#define GPUM_ETIMEOUT (-0x7FFF0000)

/* Synchronous wrappers: wbinvd (WB-safe) / sfence-only (WC buffers).
 * NULL on error or timeout, dst otherwise. */
void *gpum_copy(void *dst, const void *src, size_t n);
void *gpum_copy_wc(void *dst, const void *src, size_t n);

/* Informational now (copies no longer require it): 1 if [p, p+n) is
 * physically contiguous.  A yes means the copy will be a single run with
 * zero page-walk fragmentation. */
int gpum_is_contiguous(const void *p, size_t n);

/* DRAM phase.  M2MF throughput on this box is set by (phys(dst) - phys(src))
 * mod 16K -- hardware-measured on 1M copies, MB/s by phase (1K steps):
 *     +0  978 | +1K 863 | +2K 901 | +3K 1048 | +4K 1213 | +5K 1456
 *     +6K 1486 | +7K 1456 | +8K 1519 | +9K 1545 | +10K 1529 | +11K 1475
 *     +12K 1548 | +13K 1549 | +14K 1387 | +15K 1130 | +16K = +0
 * The engine alternates the src and dst streams; when their bank/row phase
 * coincides every switch is a precharge/activate.  Pairs allocated at
 * power-of-two distances land on +0.  Pad one buffer so the phase falls in
 * GPUM_PHASE_GOOD_LO..HI and the same copy runs ~1.6x faster. */
#define GPUM_PHASE_PERIOD  16384u
#define GPUM_PHASE_GOOD_LO  8192u
#define GPUM_PHASE_GOOD_HI 13312u
uint32_t gpum_pair_phase(const void *dst, const void *src);   /* 0..16383 */

/* Tuning: bytes per M2MF line (default 4096, cap 32767 -- signed 16-bit
 * pitch).  Measured irrelevant to throughput (the engine's internal
 * buffering sets its src<->dst cadence); kept as a knob. */
uint32_t gpum_set_line(uint32_t bytes);

/* Diagnostics: last notifier record {ptimer_lo, ptimer_hi, error, status}. */
void gpum_notifier_dump(uint32_t out[4]);

#ifdef __cplusplus
}
#endif
#endif
