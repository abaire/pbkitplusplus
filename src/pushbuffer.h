#ifndef PUSHBUFFER_H
#define PUSHBUFFER_H

#include <pbkit/pbkit.h>
#include <xboxkrnl/xboxdef.h>

#include <cstdint>
#include <cstring>

#include "pbkpp_assert.h"

namespace PBKitPlusPlus {

//! Manages pb_kit pushbuffers to prevent overflows and optimize resets.
//!
//! In particular, this class manages the underlying pbkit begin/end blocks,
//! transparently splitting large logical blocks into smaller submissions and
//! resetting the pushbuffer before it overflows.
//!
//! Each underlying pbkit block is submitted to the GPU when it ends, which
//! flushes the write-combine cache and writes DMA_PUT. By default blocks are
//! kept small, so every logical Begin()/End() pair (and every 96 DWORDs within
//! one) costs a submission. Use BeginDeferredSubmit()/EndDeferredSubmit() (or
//! DeferredSubmitScope) to accumulate many logical blocks into a single, larger
//! submission.
//!
//! Commands are written to the pushbuffer directly rather than through pbkit's
//! pb_push*() functions, which debug builds of pbkit (nxdk built with
//! DEBUG=y) rely on to track writes. Those builds are not supported.
class Pushbuffer {
 public:
  //! Default number of DWORDs pushed between submissions while submission is deferred.
  static constexpr uint32_t kDefaultSubmitIntervalDwords = 8 * 1024;

  //! Defers submission for the lifetime of the scope. See BeginDeferredSubmit().
  class DeferredSubmitScope {
   public:
    explicit DeferredSubmitScope(uint32_t submit_interval_dwords = kDefaultSubmitIntervalDwords) {
      BeginDeferredSubmit(submit_interval_dwords);
    }
    ~DeferredSubmitScope() { EndDeferredSubmit(); }

    DeferredSubmitScope(const DeferredSubmitScope &) = delete;
    DeferredSubmitScope &operator=(const DeferredSubmitScope &) = delete;
    DeferredSubmitScope(DeferredSubmitScope &&) = delete;
    DeferredSubmitScope &operator=(DeferredSubmitScope &&) = delete;
  };

 public:
  //! Initializes the pushbuffer singleton. Only the first call has any effect.
  //!
  //! `pushbuffer_bytes` must be the size pbkit allocated for its pushbuffer, i.e. the value passed to pb_size() before
  //! pb_init(), or PBKIT_PUSHBUFFER_SIZE if pb_size() was not called. pbkit does not expose its size, so it cannot be
  //! checked. To use a larger pushbuffer, call this before anything else that uses Pushbuffer (including the NV2AState
  //! constructor, which calls it with the default).
  static void Initialize(uint32_t pushbuffer_bytes = PBKIT_PUSHBUFFER_SIZE);

  //! Returns the number of DWORDs that may be pushed between pushbuffer resets. Pushing more than this between calls
  //! to Flush() causes an automatic reset, which waits for the GPU to finish all submitted work.
  [[nodiscard]] static uint32_t CapacityDwords() { return instance_.capacity_elements_; }

  //! Starts a logical pushbuffer block.
  static void Begin();

  //! Commits any outstanding pushbuffer messages.
  //!
  //! While submission is deferred this does not submit anything unless `flush` is true.
  static void End(bool flush = false);

  //! Flushes and ressets the underlying pushbuffer.
  //!
  //! May be called while submission is deferred, in which case any pending
  //! commands are submitted before the flush and submission remains deferred
  //! afterwards.
  static void Flush();

  //! Submits any commands pending due to deferred submission so that the GPU will process them.
  //!
  //! Does nothing if submission is not deferred. Must not be called inside a Begin()/End() block. After this call
  //! (and until the next Begin() or push), pbkit's pushbuffer may be used directly (e.g., pb_finished()) even while
  //! submission is deferred.
  static void Submit();

  //! Defers submission. Until EndDeferredSubmit() is called, Begin()/End()
  //! pairs append to a single underlying pbkit block that is only submitted to
  //! the GPU once `submit_interval_dwords` DWORDs have been pushed, when the
  //! pushbuffer must be reset to avoid overflow, on Flush(), or on
  //! EndDeferredSubmit().
  //!
  //! Must not be called inside a Begin()/End() block or while submission is
  //! already deferred. `submit_interval_dwords` may be at most CapacityDwords(),
  //! in which case nothing is submitted until EndDeferredSubmit() unless the
  //! pushbuffer has to be reset.
  //!
  //! While submission is deferred, the pbkit pushbuffer must not be used
  //! directly (e.g., pb_begin()/pb_end(), pb_reset(), pb_draw_text_screen())
  //! without first calling Submit(), as pbkit does not know about the pending
  //! commands and will overwrite them.
  //!
  //! Debug builds of pbkit (nxdk built with DEBUG=y) assert on any block
  //! larger than 128 DWORDs, so deferred submission cannot be used with them.
  static void BeginDeferredSubmit(uint32_t submit_interval_dwords = kDefaultSubmitIntervalDwords);

  //! Submits any pending commands and stops deferring submission.
  static void EndDeferredSubmit();

  //! Cumulative counts of pushbuffer resets. Every reset waits for the GPU to finish all submitted work, so callers
  //! timing a span of pushes can compare two snapshots to separate that wait from the time spent pushing.
  struct ResetStats {
    //! Number of resets performed, whether automatic (to avoid overflow) or requested via Flush().
    uint32_t resets;
    //! Total time spent in resets, in KeQueryPerformanceCounter ticks.
    uint64_t wait_ticks;
  };

  //! Returns the cumulative reset statistics.
  [[nodiscard]] static ResetStats GetResetStats() { return instance_.reset_stats_; }

  //! Returns true if submission is currently deferred.
  [[nodiscard]] static bool IsSubmitDeferred() { return instance_.initialized_ && instance_.submit_deferred_; }

  //! Pushes the given command and param to the given subchannel.
  static void PushTo(uint32_t subchannel, uint32_t command, uint32_t param1);

  //! Pushes the given command and params to the given subchannel.
  static void PushTo(uint32_t subchannel, uint32_t command, uint32_t param1, uint32_t param2);

  //! Pushes the given command and params to the given subchannel.
  static void PushTo(uint32_t subchannel, uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3);

  //! Pushes the given command and params to the given subchannel.
  static void PushTo(uint32_t subchannel, uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3,
                     uint32_t param4);

  //! Pushes the given command and floating point params to the given subchannel.
  static void PushTo(uint32_t subchannel, uint32_t command, float param1, float param2, float param3, float param4);

  //! Pushes the given command and floating point param to the subchannel assigned for 3D operations.
  static void PushF(uint32_t command, float param1);

  //! Pushes the given command and floating point params to the subchannel assigned for 3D operations.
  static void PushF(uint32_t command, float param1, float param2);

  //! Pushes the given command and floating point params to the subchannel assigned for 3D operations.
  static void PushF(uint32_t command, float param1, float param2, float param3);

  //! Pushes the given command and floating point params to the subchannel assigned for 3D operations.
  static void PushF(uint32_t command, float param1, float param2, float param3, float param4);

  //! Pushes the given command and param to the subchannel assigned for 3D operations.
  static void Push(uint32_t command, uint32_t param1);

  //! Pushes the given command and params to the subchannel assigned for 3D operations.
  static void Push(uint32_t command, uint32_t param1, uint32_t param2);

  //! Pushes the given command and params to the subchannel assigned for 3D operations.
  static void Push(uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3);

  //! Pushes the given command and params to the subchannel assigned for 3D operations.
  static void Push(uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4);

  //! Pushes the given command and 2-element vector of floating point params to the subchannel assigned for 3D
  //! operations.
  static void Push2F(uint32_t command, const float *vector2);

  //! Pushes the given command and 3-element vector of floating point params to the subchannel assigned for 3D
  //! operations.
  static void Push3F(uint32_t command, const float *vector3);

  //! Pushes the given command and 4-element vector of floating point params to the subchannel assigned for 3D
  //! operations.
  static void Push4F(uint32_t command, const float *vector4);

  //! Pushes the given command and 2-element vector of uint32_t params to the subchannel assigned for 3D operations
  static void Push2(uint32_t command, const DWORD *vector2);

  //! Pushes the given command and 3-element vector of uint32_t params to the subchannel assigned for 3D operations
  static void Push3(uint32_t command, const DWORD *vector3);

  //! Pushes the given command and 4-element vector of uint32_t params to the subchannel assigned for 3D operations
  static void Push4(uint32_t command, const DWORD *vector4);

  //! Pushes an arbitrary number of values to the subchannel assigned for 3D operations.
  static void PushN(uint32_t command, uint32_t num_values, const DWORD *values);

  //! Pushes the given command and 4x4 floating point matrix to the subchannel assigned for 3D operations. The matrix is
  //! pushed in transposed order.
  static void PushTransposedMatrix(uint32_t command, const float *m);

  //! Pushes a 4 column x 3 row matrix (used to push the inverse model view matrix which intentionally omits the 4th row
  //! and is not transposed).
  static void Push4x3Matrix(uint32_t command, const float *m);

  //! Pushes a 4x4 matrix without transposing it.
  static void Push4x4Matrix(uint32_t command, const float *m);

 private:
  Pushbuffer() = default;

  //! Returns a pointer to `num_dwords` DWORDs of pushbuffer space and advances past them, submitting and resetting the
  //! pushbuffer as needed.
  static uint32_t *Allocate(uint32_t num_dwords);

  //! Handles an allocation that does not fit in the current underlying pbkit block. Leaves a block open with room for
  //! at least `num_dwords` DWORDs.
  void AllocateSlow(uint32_t num_dwords);

  //! Largest number of parameters a single method header can carry.
  static constexpr uint32_t kMaxMethodParams = 0x7FF;

  //! Encodes a method header for `count` parameters.
  static uint32_t Method(uint32_t subchannel, uint32_t command, uint32_t count) {
    PBKPP_ASSERT(count <= kMaxMethodParams && "Too many parameters for one method");
    PBKPP_ASSERT(subchannel <= 7 && "Invalid subchannel");
    return (count << 18) | (subchannel << 13) | command;
  }

  //! Returns the bits of the given float.
  static uint32_t Bits(float value) {
    uint32_t ret;
    std::memcpy(&ret, &value, sizeof(ret));
    return ret;
  }

  //! Starts a new underlying pbkit block.
  void StartPbkitBlock();

  //! Ends the current underlying pbkit block, submitting it to the GPU.
  void SubmitBlock();

  //! Waits for the GPU to consume the pushbuffer and resets it to head.
  void ResetPushbuffer();

  //! Write position within the current underlying pbkit block, or nullptr if no block is open.
  uint32_t *head_ = nullptr;

  //! Start of the current underlying pbkit block.
  uint32_t *block_start_ = nullptr;

  //! Number of DWORDs that may still be written to the current underlying pbkit block before it must be submitted
  //! (or the pushbuffer reset). 0 if no block is open.
  uint32_t remaining_ = 0;

  //! The total number of dwords in blocks submitted since the last flush/reset, excluding the current block.
  uint32_t total_block_elements_ = 0;

  //! Whether submission is deferred.
  bool submit_deferred_ = false;

  //! Whether a logical Begin() is outstanding (only tracked while submission is deferred).
  bool in_block_ = false;

  //! Number of DWORDs that may be pushed between resets.
  uint32_t capacity_elements_ = 0;

  //! Number of DWORDs that may be pushed into one underlying pbkit block before it is submitted.
  uint32_t submit_interval_elements_ = 0;

  ResetStats reset_stats_{};

  //! Whether Initialize() has been called.
  bool initialized_ = false;

  //! The only instance. It is a static object rather than a pointer so that inlined pushes address its fields
  //! directly, instead of reloading a pointer after every store to the pushbuffer.
  static Pushbuffer instance_;
};

inline void Pushbuffer::Begin() {
  PBKPP_ASSERT(instance_.initialized_);
  auto self = &instance_;

  if (self->submit_deferred_) {
    PBKPP_ASSERT(!self->in_block_ && "Nested pushbuffer blocks are not supported");
    // The pbkit block is opened lazily by the first push so that Submit() can hand the pushbuffer back to pbkit
    // between blocks.
    self->in_block_ = true;
    return;
  }

  PBKPP_ASSERT(!self->head_ && "Nested pushbuffer blocks are not supported");
  self->StartPbkitBlock();
}

inline void Pushbuffer::End(bool flush) {
  PBKPP_ASSERT(instance_.initialized_);
  auto self = &instance_;
  if (self->submit_deferred_) {
    PBKPP_ASSERT(self->in_block_ && "End must not be called without Begin");
    self->in_block_ = false;
  } else {
    PBKPP_ASSERT(self->head_ && "End must not be called without Begin");
    self->SubmitBlock();
  }

  if (flush) {
    Flush();
  }
}

inline uint32_t *Pushbuffer::Allocate(uint32_t num_dwords) {
  auto self = &instance_;
  PBKPP_ASSERT(self->initialized_ && (self->submit_deferred_ ? self->in_block_ : self->head_ != nullptr) &&
               "Push must be called between Begin and End");
  if (__builtin_expect(num_dwords > self->remaining_, 0)) {
    self->AllocateSlow(num_dwords);
  }
  self->remaining_ -= num_dwords;
  auto ret = self->head_;
  self->head_ = ret + num_dwords;
  return ret;
}

inline void Pushbuffer::PushTo(uint32_t subchannel, uint32_t command, uint32_t param1) {
  auto p = Allocate(2);
  p[0] = Method(subchannel, command, 1);
  p[1] = param1;
}

inline void Pushbuffer::PushTo(uint32_t subchannel, uint32_t command, uint32_t param1, uint32_t param2) {
  auto p = Allocate(3);
  p[0] = Method(subchannel, command, 2);
  p[1] = param1;
  p[2] = param2;
}

inline void Pushbuffer::PushTo(uint32_t subchannel, uint32_t command, uint32_t param1, uint32_t param2,
                               uint32_t param3) {
  auto p = Allocate(4);
  p[0] = Method(subchannel, command, 3);
  p[1] = param1;
  p[2] = param2;
  p[3] = param3;
}

inline void Pushbuffer::PushTo(uint32_t subchannel, uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3,
                               uint32_t param4) {
  auto p = Allocate(5);
  p[0] = Method(subchannel, command, 4);
  p[1] = param1;
  p[2] = param2;
  p[3] = param3;
  p[4] = param4;
}

inline void Pushbuffer::PushTo(uint32_t subchannel, uint32_t command, float param1, float param2, float param3,
                               float param4) {
  PushTo(subchannel, command, Bits(param1), Bits(param2), Bits(param3), Bits(param4));
}

inline void Pushbuffer::PushF(uint32_t command, float param1) { PushTo(SUBCH_3D, command, Bits(param1)); }

inline void Pushbuffer::PushF(uint32_t command, float param1, float param2) {
  PushTo(SUBCH_3D, command, Bits(param1), Bits(param2));
}

inline void Pushbuffer::PushF(uint32_t command, float param1, float param2, float param3) {
  PushTo(SUBCH_3D, command, Bits(param1), Bits(param2), Bits(param3));
}

inline void Pushbuffer::PushF(uint32_t command, float param1, float param2, float param3, float param4) {
  PushTo(SUBCH_3D, command, Bits(param1), Bits(param2), Bits(param3), Bits(param4));
}

inline void Pushbuffer::Push(uint32_t command, uint32_t param1) { PushTo(SUBCH_3D, command, param1); }

inline void Pushbuffer::Push(uint32_t command, uint32_t param1, uint32_t param2) {
  PushTo(SUBCH_3D, command, param1, param2);
}

inline void Pushbuffer::Push(uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3) {
  PushTo(SUBCH_3D, command, param1, param2, param3);
}

inline void Pushbuffer::Push(uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4) {
  PushTo(SUBCH_3D, command, param1, param2, param3, param4);
}

inline void Pushbuffer::Push2F(uint32_t command, const float *vector2) { PushF(command, vector2[0], vector2[1]); }

inline void Pushbuffer::Push3F(uint32_t command, const float *vector3) {
  PushF(command, vector3[0], vector3[1], vector3[2]);
}

inline void Pushbuffer::Push4F(uint32_t command, const float *vector4) {
  PushF(command, vector4[0], vector4[1], vector4[2], vector4[3]);
}

inline void Pushbuffer::Push2(uint32_t command, const DWORD *vector2) { Push(command, vector2[0], vector2[1]); }

inline void Pushbuffer::Push3(uint32_t command, const DWORD *vector3) {
  Push(command, vector3[0], vector3[1], vector3[2]);
}

inline void Pushbuffer::Push4(uint32_t command, const DWORD *vector4) {
  Push(command, vector4[0], vector4[1], vector4[2], vector4[3]);
}

inline void Pushbuffer::PushN(uint32_t command, uint32_t num_values, const DWORD *values) {
  PBKPP_ASSERT(values && num_values > 0 && num_values <= kMaxMethodParams && "Invalid PushN parameters");
  auto p = Allocate(num_values + 1);
  p[0] = Method(SUBCH_3D, command, num_values);
  std::memcpy(p + 1, values, num_values * sizeof(*values));
}

}  // namespace PBKitPlusPlus

#endif  // PUSHBUFFER_H
