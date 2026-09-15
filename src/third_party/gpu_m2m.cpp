/*
 * Copyright (c) 2026 wutno (aaron@installgentoo.net)
 * Copyright (c) 2026 Claude (Anthropic)
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * gpu_m2m.c -- synchronous GPU copy on M2MF (class 0x39) only, with no
 *              physical-contiguity requirement.
 *
 * M2MF itself neither knows nor cares about CPU virtual memory: our DMA
 * contexts are flat windows over physical address space, so OFFSET_IN/OUT
 * are physical addresses and the engine marches linearly.  Contiguity was
 * therefore OUR contract, not the hardware's -- and this module removes it
 * by walking both buffers a page at a time (MmGetPhysicalAddress) and
 * coalescing the longest run over which BOTH stay physically contiguous
 * into one kick:
 *
 *   - contiguous buffers (framebuffer, MmAllocateContiguousMemoryEx)
 *     coalesce to a single run: same kick pattern as before, the walk cost
 *     is ~2 PTE lookups per 4K page (~4% of the copy's GPU time, paid on
 *     the CPU before the kicks)
 *   - scattered heap degrades gracefully: worst case one kick per 4K page
 *     when both sides are scattered and misphased, with head/tail partial
 *     pages handled by the same run logic (byte granularity throughout)
 *
 * The silent-corruption footgun (a "contiguous" buffer that wasn't, copied
 * into whatever physical pages happened to follow) is gone: correctness no
 * longer depends on the caller checking anything.
 *
 * Completion is the validated notifier design from gpu_copy_sync.c:
 *   - notify objects built like pbkit's ctx 7 (SYSMEM branch via virtual
 *     0x8xxxxxxx base, limit 0x1F -- PGRAPH rejects less at bind time),
 *     each at the start of its own PAGE_NOCACHE page (the NV2A does not
 *     snoop: a WB-cached poll spins stale forever)
 *   - the M2MF completion record lives at offset 0x10 of the notify object
 *     (fixed layout; 0x00 is ordinary notifier #0, which M2MF never
 *     writes): {ptimer_lo, ptimer_hi, 0, status}, status written 0 LAST --
 *     arm non-zero, poll for change
 *   - M2MF notifiers cannot be turned off, so intermediate kicks notify
 *     into a scratch object and only the final kick binds the live one,
 *     via method 0x180 in-band (FIFO-ordered), pushed only on change
 *   - gpum_wait polls with exponential backoff: each poll is an uncached
 *     FSB->DRAM read that contends with the copy (a tight loop measured
 *     ~25% off a 1M copy)
 *
 * Caveats: ONE outstanding copy (gpum_start re-arms the single live
 * notifier); completion is FIFO-relative (earlier queued GPU work drains
 * first); ~3.6K-cycle sync floor plus the page walk, so wrong for small
 * hot copies; wbinvd in gpum_copy is system-wide; another module's M2MF
 * kick would notify through whichever object is bound -- do not interleave
 * with gpu_memcpy.c / gpu_copy_sync.c copies; errors do not recover
 * channel state.  Throughput is a function of the src/dst DRAM phase (see
 * gpum_pair_phase in the header): 863 MB/s at the worst phase, ~1550-1610
 * on the +8K..+13K plateau, 978 at +0 where power-of-two-spaced pairs
 * land; flat across sizes, identical under notifier and drain waits,
 * unaffected by line length; fragmented buffers average the curve
 * (~1150).  A silent CPU gains nothing over 2K-cycle-spaced polls.
 *
 * NOTE: The original cited channels (30..33) and external module references
 * are incorrect for PBKitPlusPlus. In PBKitPlusPlus, channels 1..17 are used by
 * pbkit, 18..24 are padding, and channels 25..28 (kM2MDmaInChannel through
 * kM2MDmaNotifyScratchChannel) are used for M2M, with kNextContextChannel = 29.
 * Similarly, SUBCH_2 is reused as kM2MSubchannel (configured by pbkit in pb_init).
 */

#include "gpu_m2m.h"

#include <string.h>

#include <pbkit/pbkit.h>
#include <pbkit/pbkit_dma.h>
#include <pbkit/pbkit_pushbuffer.h>
#include <pbkit/nv_objects.h>
#include <xboxkrnl/xboxkrnl.h>

#include "nv2astate.h"
#include "pushbuffer.h"

using namespace PBKitPlusPlus;

#define M2MF_LINE     4096u              /* default; see gpum_set_line() */
#define M2MF_MAXLINES 2047u
#define NOTIF_ARMED   0xFFFFFFFFu
#define PG            4096u

#ifndef GPUM_TIMEOUT_CYC
#define GPUM_TIMEOUT_CYC 366666666ull        /* ~0.5 s at 733 MHz */
#endif
#ifndef GPUM_BACKOFF_CAP
#define GPUM_BACKOFF_CAP 1024u
#endif

static uint32_t g_line = M2MF_LINE;      /* bytes per M2MF line */
static struct s_CtxDma g_in, g_out, g_ntfy, g_ntfy_scr;
static volatile gpum_notifier_t *g_notif;           /* M2MF slot: page + 0x10 */
static int g_bound_live = -1;

static inline uint32_t phys(const void *p)
{
    uintptr_t va = (uintptr_t)p;
    if (va >= 0x80000000 && va < 0xC0000000) {
        return (uint32_t)(va & 0x1FFFFFFF);
    }
    return (uint32_t)MmGetPhysicalAddress((PVOID)p);
}

static inline uint64_t tsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpu_pause(void)  { __asm__ volatile("pause"); }
static inline void cpu_wbinvd(void) { __asm__ volatile("wbinvd" : : : "memory"); }
static inline void cpu_sfence(void) { __asm__ volatile("sfence" : : : "memory"); }

int gpum_init(void)
{
    if (g_notif) {
      return 0;
    }

    unsigned char *pages = (unsigned char *)MmAllocateContiguousMemoryEx(
        2 * PG, 0, MAXRAM, 0, PAGE_READWRITE | PAGE_NOCACHE);
    if (!pages) return -1;
    memset(pages, 0, 32);
    memset(pages + PG, 0, 32);
    g_notif = (volatile gpum_notifier_t *)(pages + 0x10);

    pb_create_dma_ctx(kM2MDmaInChannel,  DMA_CLASS_3D, 0, MAXRAM, &g_in);
    pb_create_dma_ctx(kM2MDmaOutChannel, DMA_CLASS_3D, 0, MAXRAM, &g_out);
    pb_create_dma_ctx(kM2MDmaNotifyChannel,     DMA_CLASS_3D, (DWORD)(uintptr_t)pages,
                      0x1F, &g_ntfy);
    pb_create_dma_ctx(kM2MDmaNotifyScratchChannel, DMA_CLASS_3D,
                      (DWORD)(uintptr_t)(pages + PG), 0x1F, &g_ntfy_scr);
    pb_bind_channel(&g_in);
    pb_bind_channel(&g_out);
    pb_bind_channel(&g_ntfy);
    pb_bind_channel(&g_ntfy_scr);

    Pushbuffer::Begin();
    Pushbuffer::PushTo(kM2MSubchannel, NV_MEMORY_TO_MEMORY_FORMAT_OBJECT_IN,
                       kM2MDmaInChannel, kM2MDmaOutChannel);
    Pushbuffer::End();
    g_bound_live = -1;
    return 0;
}

static void m2mf_kick(uint32_t dst_pa, uint32_t src_pa, uint32_t line_len,
                      uint32_t lines, int live)
{
    if (live != g_bound_live) {
        Pushbuffer::PushTo(kM2MSubchannel, NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY,
                           live ? kM2MDmaNotifyChannel : kM2MDmaNotifyScratchChannel);
        g_bound_live = live;
    }
    Pushbuffer::PushTo(kM2MSubchannel, NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN,
                       src_pa, dst_pa);
    Pushbuffer::PushTo(kM2MSubchannel, NV_MEMORY_TO_MEMORY_FORMAT_PITCH_IN,
                       line_len, line_len, line_len, lines);
    Pushbuffer::PushTo(kM2MSubchannel, NV_MEMORY_TO_MEMORY_FORMAT_FORMAT, 0x0101, 0);
}

/* one physically contiguous stretch, chunked to the engine's limits;
 * final_live tags the very last kick of the whole copy */
static void emit_run(uint32_t d, uint32_t s, size_t n, int final_live)
{
    while (n > g_line * (size_t)M2MF_MAXLINES) {
        m2mf_kick(d, s, g_line, M2MF_MAXLINES, 0);
        d += g_line * M2MF_MAXLINES;
        s += g_line * M2MF_MAXLINES;
        n -= g_line * (size_t)M2MF_MAXLINES;
    }
    if (n >= g_line) {
        uint32_t lines = (uint32_t)(n / g_line);
        int last = (n % g_line) == 0;
        m2mf_kick(d, s, g_line, lines, last && final_live);
        d += lines * g_line;
        s += lines * g_line;
        n -= (size_t)lines * g_line;
        if (last) return;
    }
    m2mf_kick(d, s, (uint32_t)n, 1, final_live);
}

/* Tuning hook: bytes per M2MF line (default 4096).  The engine alternates
 * between the src and dst streams, so the line size sets the switch cadence
 * against DRAM bank/row phase.  Pitch is pushed equal to the line, and the
 * NV04-family pitch field is signed 16-bit, so 32767 is the hard cap.
 * Returns the value in effect. */
uint32_t gpum_pair_phase(const void *dst, const void *src)
{
    return (phys(dst) - phys(src)) & (GPUM_PHASE_PERIOD - 1);
}

uint32_t gpum_set_line(uint32_t bytes)
{
    if (bytes >= 4 && bytes <= 32767u)
        g_line = bytes;
    return g_line;
}

/* bytes from v over which the buffer stays physically contiguous, capped */
static size_t run_len(const unsigned char *v, size_t max)
{
    size_t run = PG - ((uintptr_t)v & (PG - 1));
    uint32_t expect = (phys(v) & ~(uint32_t)(PG - 1)) + PG;

    if (run >= max) return max;
    while (run < max) {
        if (phys(v + run) != expect) break;
        expect += PG;
        run += PG;
    }
    return run < max ? run : max;
}

void gpum_start(void *dst, const void *src, size_t n)
{
    if (!g_notif) {
        if (gpum_init() < 0) return;
    }

    if (!gpum_done()) {
        gpum_wait();
    }

    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    g_notif->error = 0;
    g_notif->status = NOTIF_ARMED;

    if (n == 0) {
        g_notif->status = 0;
        return;
    }

    Pushbuffer::Begin();

    while (n) {
        size_t rd = run_len(d, n);
        size_t rs = run_len(s, rd);          /* cap src probe at dst's run */
        size_t chunk = rs;                   /* = min(rs, rd, n)           */
        emit_run(phys(d), phys(s), chunk, chunk == n);
        d += chunk;
        s += chunk;
        n -= chunk;
    }

    Pushbuffer::End();
}

int gpum_done(void)
{
    if (!g_notif) {
      return 1;
    }
    uint32_t st = g_notif->status;
    if (st == NOTIF_ARMED) {
      return 0;
    }
    if (g_notif->error != 0) {
      return -(int)g_notif->error;
    }
    return 1;
}

static int gpum_wait_poll_loop(void)
{
    uint64_t t0 = tsc();
    unsigned backoff = 16, i;
    for (;;) {
        int r = gpum_done();
        if (r > 0) {
          return 0;
        }
        if (r < 0) {
          return r;
        }
        if (tsc() - t0 > GPUM_TIMEOUT_CYC) {
          return GPUM_ETIMEOUT;
        }
        for (i = 0; i < backoff; i++) {
          cpu_pause();
        }
        if (backoff < GPUM_BACKOFF_CAP) {
          backoff <<= 1;
        }
    }
  return 0;
}

int gpum_wait(void)
{
    return gpum_wait_poll_loop();
}

void *gpum_copy(void *dst, const void *src, size_t n)
{
    cpu_wbinvd();
    gpum_start(dst, src, n);
    return gpum_wait() == 0 ? dst : NULL;
}

void *gpum_copy_wc(void *dst, const void *src, size_t n)
{
    cpu_sfence();
    gpum_start(dst, src, n);
    return gpum_wait() == 0 ? dst : NULL;
}

int gpum_is_contiguous(const void *p, size_t n)
{
    return run_len((const unsigned char *)p, n ? n : 1) >= (n ? n : 1);
}

void gpum_notifier_dump(gpum_notifier_t *out)
{
    if (!out) {
        return;
    }
    if (!g_notif) {
        memset(out, 0, sizeof(*out));
        return;
    }
    out->ptimer_low = g_notif->ptimer_low;
    out->ptimer_high = g_notif->ptimer_high;
    out->error = g_notif->error;
    out->status = g_notif->status;
}
