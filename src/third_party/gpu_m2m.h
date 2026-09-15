/*
 * Copyright (c) 2026 wutno (aaron@installgentoo.net)
 * Copyright (c) 2026 Claude (Anthropic)
 * SPDX-License-Identifier: MIT (full text in gpu_m2m.cpp)
 */
#ifndef GPU_M2M_H
#define GPU_M2M_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define GPUM_PACKED __attribute__((packed))
#else
#define GPUM_PACKED
#endif

#pragma pack(push, 1)
/**
 * @brief NV2A M2MF hardware completion notifier record.
 *
 * Located at offset 0x10 within the notifier DMA context.
 */
typedef struct gpum_notifier {
  uint32_t ptimer_low;  /**< NV2A PTIMER timestamp low word */
  uint32_t ptimer_high; /**< NV2A PTIMER timestamp high word */
  uint32_t error;       /**< Hardware error word (0 = no error) */
  uint32_t status;      /**< Completion status (0 = success, 0xFFFFFFFF = armed/pending) */
} GPUM_PACKED gpum_notifier_t;
#pragma pack(pop)

/**
 * @brief Initializes the M2M DMA engine and preallocates notifier memory.
 *
 * Sets up the required DMA contexts for class 0x39 (NV_MEMORY_TO_MEMORY_FORMAT)
 * and allocates contiguous uncached notifier pages. Initialization is optional
 * as gpum_start() and gpum_copy() lazily initialize on first use if needed.
 * However, early initialization at startup avoids physical memory fragmentation.
 * Subsequent calls are safe idempotent no-ops.
 *
 * Performs a test transfer to verify class 0x39 support (e.g. on older xemu
 * releases that do not implement NV_MEMORY_TO_MEMORY_FORMAT). If the test fails
 * or times out, a warning is logged and all copy operations transparently fall back
 * to standard CPU memcpy.
 *
 * @return 0 on success, or a negative error code if allocation fails.
 */
int gpum_init(void);

/**
 * @brief Queues an asynchronous memory-to-memory copy on the NV2A M2MF engine (class 0x39).
 *
 * Performs a byte-granular DMA copy of arbitrary alignment without modifying 2D/3D
 * pipeline state. The source and destination buffers do not need to be physically
 * contiguous; this function walks both buffers page-by-page (using MmGetPhysicalAddress)
 * and coalesces contiguous physical runs into pushbuffer kicks.
 *
 * Only one M2M copy operation may be in-flight at a time. The source and destination
 * memory regions must not overlap.
 *
 * @note **Buffer Memory Requirements**:
 * - Both source and destination buffers must be locked into physical RAM and backed by valid
 *   physical pages. Memory allocated via MmAllocateContiguousMemory() or MmAllocateContiguousMemoryEx()
 *   (including framebuffer and texture memory in 0x80000000..0xBFFFFFFF) satisfies this naturally as it
 *   resides in nonpaged direct-mapped physical address space.
 * - If using user-space or heap memory (e.g. allocated via malloc(), operator new, or VirtualAlloc()),
 *   the caller MUST ensure the pages are locked into physical memory prior to calling M2M routines
 *   (for example, via `MmLockUnlockBufferPages((PVOID)buf, size, FALSE)` before initiating the transfer,
 *   and `MmLockUnlockBufferPages((PVOID)buf, size, TRUE)` once the transfer completes).
 *   On debug kernels, calling MmGetPhysicalAddress on unlocked or demand-zero paged memory will
 *   trigger a kernel assertion / breakpoint (SIGTRAP).
 *
 * @note The caller is responsible for ensuring source memory visibility prior to calling
 * gpum_start() (e.g. via sfence for Write-Combining memory or wbinvd for Write-Back cached
 * memory) and must not modify or access either buffer until the copy completes.
 *
 * @param dst Destination memory pointer (virtual address). Must be physically backed and locked.
 * @param src Source memory pointer (virtual address). Must be physically backed and locked.
 * @param n Number of bytes to copy.
 */
void gpum_start(void *dst, const void *src, size_t n);

/**
 * @brief Checks if the currently active M2M copy operation has finished.
 *
 * Polls the hardware completion notifier status in uncached memory.
 *
 * @note Each call performs an uncached DRAM read that contends with the DMA copy.
 * In tight polling loops, this can degrade copy throughput by up to ~20%.
 * Callers should space out polls by at least 2,000 CPU cycles (as gpum_wait() does).
 *
 * @return 1 if the copy completed successfully.
 *         0 if the copy is still in progress.
 *         Negative value containing the GPU error word if an error occurred.
 */
int gpum_done(void);

/**
 * @brief Waits for the active M2M copy operation to finish using exponential backoff polling.
 *
 * Repeatedly checks gpum_done() while inserting CPU pause cycles and backoff intervals
 * to avoid starving the GPU memory bus during the copy. Times out if the copy does
 * not complete within ~1 second.
 *
 * @return 0 on successful completion.
 *         GPUM_ETIMEOUT if the operation timed out.
 *         Negative GPU error word if hardware reported an error.
 */
int gpum_wait(void);
#define GPUM_ETIMEOUT (-0x7FFF0000)

/**
 * @brief Synchronously copies memory using the GPU M2M engine with Write-Back (WB) cache coherency.
 *
 * Flushes and invalidates the entire CPU cache hierarchy (via wbinvd) before initiating
 * the GPU DMA transfer, ensuring dirty CPU cache lines are written back to DRAM and that
 * CPU caches will refetch freshly DMA'd data from DRAM upon subsequent access.
 *
 * ### When to use gpum_copy vs gpum_copy_wc:
 * - Use gpum_copy whenever the source or destination buffers reside in standard
 *   Write-Back (WB) cached memory (e.g. normal stack, heap, or malloc/vector allocations).
 * - Use gpum_copy_wc only when both the source and destination buffers are known to
 *   reside in Write-Combining (WC) or un-cached memory (e.g. video RAM, AGP aperture, or
 *   buffers allocated with PAGE_WRITECOMBINE / PAGE_NOCACHE).
 *
 * ### Synchronization Floor and Threshold:
 * - NV2A M2M DMA incurs a fixed synchronization/setup overhead floor of approximately
 *   3,600 CPU cycles (~5 microseconds at 733 MHz), attributable to pushbuffer packet
 *   emission, ring buffer advance, GPU context switches, and CPU spin-waiting on the DMA notifier.
 *   Additional per-page overhead is incurred walking page tables for scattered heap allocations.
 * - Because of this fixed latency floor, small copies (< 4 KB) are faster when performed
 *   with standard CPU memcpy() (which runs at multi-GB/s in L1/L2 cache).
 * - GPU M2M copy becomes generally beneficial for transfers starting around 4 KB to 16 KB,
 *   particularly when reading from un-cached GPU/AGP memory where CPU reads crawl at 10-30 MB/s.
 * - For bulk transfers (> 64 KB, up to multi-megabyte framebuffers and textures), M2M copy
 *   greatly outperforms CPU copy, achieving sustained transfer rates of 860 to 1,600 MB/s.
 *
 * @param dst Destination memory pointer (virtual address).
 * @param src Source memory pointer (virtual address).
 * @param n Number of bytes to copy.
 * @return Pointer to dst on success, or NULL if an error or timeout occurred.
 */
void *gpum_copy(void *dst, const void *src, size_t n);

/**
 * @brief Synchronously copies memory using the GPU M2M engine with Write-Combining (WC) store fence.
 *
 * Issues an sfence instruction before initiating the GPU DMA transfer, flushing CPU
 * write-combining store buffers without invalidating the CPU L1/L2 caches.
 *
 * ### When to use gpum_copy_wc vs gpum_copy:
 * - Use gpum_copy_wc when BOTH source and destination buffers reside in Write-Combining (WC)
 *   or un-cached memory (such as GPU memory allocated with PAGE_WRITECOMBINE or PAGE_NOCACHE).
 *   Because sfence executes in single-digit CPU cycles compared to the expensive wbinvd
 *   (which can take thousands of cycles flushing dirty L1/L2 lines), gpum_copy_wc provides
 *   substantially lower latency when WB cache flushing is unnecessary.
 * - DO NOT use gpum_copy_wc if either buffer is Write-Back cached, as dirty cache lines
 *   will not be flushed to DRAM, causing silent data corruption or reading stale memory.
 *   Use gpum_copy instead for any WB-cached buffers.
 *
 * ### Synchronization Floor and Threshold:
 * - Although sfence avoids the wbinvd penalty, the underlying GPU DMA sync floor
 *   remains ~3,600 CPU cycles (~5 microseconds) due to pushbuffer dispatch and notifier polling.
 * - M2M WC copy becomes beneficial for transfers of 4 KB or larger, and is ideal for
 *   bulk VRAM-to-VRAM or WC-to-WC copies (> 64 KB) operating at 860 to 1,600 MB/s.
 *
 * @param dst Destination memory pointer (virtual address).
 * @param src Source memory pointer (virtual address).
 * @param n Number of bytes to copy.
 * @return Pointer to dst on success, or NULL if an error or timeout occurred.
 */
void *gpum_copy_wc(void *dst, const void *src, size_t n);

/**
 * @brief Queues an asynchronous 2D pitched memory-to-memory copy on the NV2A M2MF engine (class 0x39).
 *
 * Performs a 2D rectangular stride copy of @p lines rows, each of length @p line_len bytes.
 * After copying each row, the source pointer advances by @p pitch_in bytes and the destination
 * pointer advances by @p pitch_out bytes. Inter-line destination padding bytes
 * (@p pitch_out - @p line_len) remain untouched.
 *
 * Transfers with @p lines exceeding the NV2A hardware limit of 2047 lines per kick are
 * automatically chunked across multiple kicks, with the live completion notifier bound to the final kick.
 *
 * @note **Physical Contiguity Contract**:
 * - Unlike 1D linear copies (gpum_start/gpum_copy), which page-walk and coalesce scattered physical pages,
 *   2D pitched copies stride directly in physical address space across lines.
 * - **Both source and destination buffers MUST be physically contiguous across their entire active span**:
 *   - The source buffer must be physically contiguous for at least `(lines - 1) * pitch_in + line_len` bytes.
 *   - The destination buffer must be physically contiguous for at least `(lines - 1) * pitch_out + line_len` bytes.
 * - Buffers allocated via MmAllocateContiguousMemory() or MmAllocateContiguousMemoryEx() (such as
 *   framebuffers, textures, and linear surfaces) satisfy this contract.
 * - Scattered heap allocations (e.g. standard malloc() or new) that cross page boundaries are NOT supported
 *   for pitched copies because physical page adjacency cannot be guaranteed.
 *
 * @note Only one M2M operation (1D or pitched) may be in-flight at a time. The source and destination
 * regions must not overlap.
 *
 * @note The caller is responsible for ensuring source memory visibility prior to calling gpum_start_pitched()
 * (e.g. via sfence for Write-Combining memory or wbinvd for Write-Back cached memory) and must not modify or
 * access either buffer until the copy completes (poll gpum_done() or await gpum_wait()).
 *
 * @param dst Destination memory pointer (virtual address). Must be physically contiguous.
 * @param src Source memory pointer (virtual address). Must be physically contiguous.
 * @param line_len Number of bytes to copy per row.
 * @param lines Number of rows to copy.
 * @param pitch_in Source stride in bytes from the start of one row to the start of the next.
 * @param pitch_out Destination stride in bytes from the start of one row to the start of the next.
 */
void gpum_start_pitched(void *dst, const void *src, uint32_t line_len,
                        uint32_t lines, uint32_t pitch_in, uint32_t pitch_out);

/**
 * @brief Synchronously copies 2D pitched memory using the GPU M2M engine with Write-Back (WB) cache coherency.
 *
 * Flushes and invalidates the CPU cache hierarchy (via wbinvd) before initiating the GPU DMA
 * transfer, ensuring dirty CPU cache lines are written back to DRAM and that subsequent CPU reads
 * refetch updated data from memory. Uses the hardware completion notifier to await transfer completion.
 *
 * ### When to use gpum_copy_pitched vs gpum_copy_pitched_wc:
 * - Use gpum_copy_pitched whenever the source or destination buffers reside in standard Write-Back (WB)
 *   cached memory (e.g. MmAllocateContiguousMemory without PAGE_WRITECOMBINE / PAGE_NOCACHE).
 * - Use gpum_copy_pitched_wc when both buffers reside in Write-Combining (WC) or un-cached memory
 *   to avoid the CPU cache invalidation penalty of wbinvd.
 *
 * @see gpum_start_pitched for the Physical Contiguity Contract.
 *
 * @param dst Destination memory pointer (virtual address). Must be physically contiguous.
 * @param src Source memory pointer (virtual address). Must be physically contiguous.
 * @param line_len Number of bytes to copy per row.
 * @param lines Number of rows to copy.
 * @param pitch_in Source stride in bytes from the start of one row to the start of the next.
 * @param pitch_out Destination stride in bytes from the start of one row to the start of the next.
 * @return Pointer to dst on success, or NULL if an error or timeout occurred.
 */
void *gpum_copy_pitched(void *dst, const void *src, uint32_t line_len,
                        uint32_t lines, uint32_t pitch_in, uint32_t pitch_out);

/**
 * @brief Synchronously copies 2D pitched memory using the GPU M2M engine with Write-Combining (WC) store fence.
 *
 * Issues an sfence instruction before initiating the GPU DMA transfer, flushing CPU write-combining
 * store buffers without invalidating the CPU L1/L2 caches. Awaits completion via the hardware completion notifier.
 *
 * ### When to use gpum_copy_pitched_wc vs gpum_copy_pitched:
 * - Use gpum_copy_pitched_wc when BOTH source and destination buffers reside in Write-Combining (WC)
 *   or un-cached memory (e.g. video RAM, textures, or buffers allocated with PAGE_WRITECOMBINE / PAGE_NOCACHE).
 * - DO NOT use gpum_copy_pitched_wc if either buffer is Write-Back cached, as dirty CPU cache lines
 *   will not be flushed to DRAM, causing stale reads or silent corruption. Use gpum_copy_pitched instead.
 *
 * @see gpum_start_pitched for the Physical Contiguity Contract.
 *
 * @param dst Destination memory pointer (virtual address). Must be physically contiguous.
 * @param src Source memory pointer (virtual address). Must be physically contiguous.
 * @param line_len Number of bytes to copy per row.
 * @param lines Number of rows to copy.
 * @param pitch_in Source stride in bytes from the start of one row to the start of the next.
 * @param pitch_out Destination stride in bytes from the start of one row to the start of the next.
 * @return Pointer to dst on success, or NULL if an error or timeout occurred.
 */
void *gpum_copy_pitched_wc(void *dst, const void *src, uint32_t line_len,
                           uint32_t lines, uint32_t pitch_in, uint32_t pitch_out);

/**
 * @brief Queues an asynchronous 2D strided memory-to-memory copy on the NV2A M2MF engine (class 0x39).
 *
 * Performs a 2D pitched transfer allowing per-element source and destination stride stepping to be specified.
 * The format word pushed to NV_MEMORY_TO_MEMORY_FORMAT_FORMAT is configured as:
 *   (stride_out << 8) | (stride_in & 0xFF)
 *
 * @param dst Destination memory pointer (virtual address). Must be physically contiguous.
 * @param src Source memory pointer (virtual address). Must be physically contiguous.
 * @param line_len Number of elements to copy per row.
 * @param lines Number of rows to copy.
 * @param pitch_in Source row stride in bytes.
 * @param pitch_out Destination row stride in bytes.
 * @param stride_in Source element stride in bytes (must be >= 1).
 * @param stride_out Destination element stride in bytes (must be >= 1).
 */
void gpum_start_strided(void *dst, const void *src, uint32_t line_len,
                        uint32_t lines, uint32_t pitch_in, uint32_t pitch_out,
                        uint32_t stride_in, uint32_t stride_out);

/**
 * @brief Synchronously copies 2D strided memory using the GPU M2M engine with Write-Back (WB) cache coherency.
 *
 * @param dst Destination memory pointer (virtual address). Must be physically contiguous.
 * @param src Source memory pointer (virtual address). Must be physically contiguous.
 * @param line_len Number of elements to copy per row.
 * @param lines Number of rows to copy.
 * @param pitch_in Source row stride in bytes.
 * @param pitch_out Destination row stride in bytes.
 * @param stride_in Source element stride in bytes (must be >= 1).
 * @param stride_out Destination element stride in bytes (must be >= 1).
 * @return Pointer to dst on success, or NULL if an error or timeout occurred.
 */
void *gpum_copy_strided(void *dst, const void *src, uint32_t line_len,
                        uint32_t lines, uint32_t pitch_in, uint32_t pitch_out,
                        uint32_t stride_in, uint32_t stride_out);

/**
 * @brief Synchronously copies 2D strided memory using the GPU M2M engine with Write-Combining (WC) store fence.
 *
 * @param dst Destination memory pointer (virtual address). Must be physically contiguous.
 * @param src Source memory pointer (virtual address). Must be physically contiguous.
 * @param line_len Number of elements to copy per row.
 * @param lines Number of rows to copy.
 * @param pitch_in Source row stride in bytes.
 * @param pitch_out Destination row stride in bytes.
 * @param stride_in Source element stride in bytes (must be >= 1).
 * @param stride_out Destination element stride in bytes (must be >= 1).
 * @return Pointer to dst on success, or NULL if an error or timeout occurred.
 */
void *gpum_copy_strided_wc(void *dst, const void *src, uint32_t line_len,
                           uint32_t lines, uint32_t pitch_in, uint32_t pitch_out,
                           uint32_t stride_in, uint32_t stride_out);


/**
 * @brief Checks if a virtual memory range is physically contiguous in system RAM.
 *
 * Informational query: M2M copies do not require contiguous memory as page-walking
 * handles scattered pages automatically. However, physically contiguous buffers
 * coalesce into a single kick with zero page-walk fragmentation.
 *
 * @param p Starting virtual address.
 * @param n Length of the memory range in bytes.
 * @return 1 if the entire range [p, p+n) is physically contiguous; 0 otherwise.
 */
int gpum_is_contiguous(const void *p, size_t n);

/**
 * @brief Calculates the DRAM bank/row phase difference between destination and source addresses.
 *
 * NV2A M2MF throughput is sensitive to the DRAM bank phase `(phys(dst) - phys(src)) mod 16K`.
 * Hardware measurements show throughput variations from ~860 MB/s (when phase is near 0 or 16K,
 * causing frequent bank precharge/activate conflicts during alternating src/dst bursts)
 * up to ~1,550 MB/s (when phase falls within the optimal range GPUM_PHASE_GOOD_LO..HI).
 *
 * @param dst Destination memory pointer (virtual address).
 * @param src Source memory pointer (virtual address).
 * @return DRAM phase offset in bytes in the range [0, GPUM_PHASE_PERIOD - 1].
 */
uint32_t gpum_pair_phase(const void *dst, const void *src);

#define GPUM_PHASE_PERIOD  16384u
#define GPUM_PHASE_GOOD_LO  8192u
#define GPUM_PHASE_GOOD_HI 13312u

/**
 * @brief Sets the bytes-per-line pitch for M2MF transfer commands.
 *
 * Configures the M2MF pitch parameter (default: 4096 bytes; maximum: 32767 bytes).
 * Note: Hardware measurements show line size is largely irrelevant to throughput,
 * but this function is retained as an operational tuning knob.
 *
 * @param bytes Desired bytes per line.
 * @return The previously configured bytes-per-line value.
 */
uint32_t gpum_set_line(uint32_t bytes);

/**
 * @brief Copies the most recent hardware completion notifier record for diagnostics.
 *
 * @param out Pointer to a gpum_notifier_t structure to receive the notifier record.
 */
void gpum_notifier_dump(gpum_notifier_t *out);

#ifdef __cplusplus
}
#endif
#endif
