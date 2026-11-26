#ifndef PUSHBUFFER_H
#define PUSHBUFFER_H

#include <xboxkrnl/xboxdef.h>

#include <cstdint>

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
  //! Initializes the pushbuffer singleton.
  static void Initialize();

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
  //! already deferred.
  //!
  //! While submission is deferred, the pbkit pushbuffer must not be used
  //! directly (e.g., pb_begin()/pb_end(), pb_reset(), pb_draw_text_screen())
  //! without first calling Submit(), as pbkit does not know about the pending
  //! commands and will overwrite them.
  //!
  //! Note that debug builds of pbkit print a warning for any block larger than
  //! 128 DWORDs.
  static void BeginDeferredSubmit(uint32_t submit_interval_dwords = kDefaultSubmitIntervalDwords);

  //! Submits any pending commands and stops deferring submission.
  static void EndDeferredSubmit();

  //! Returns true if submission is currently deferred.
  [[nodiscard]] static bool IsSubmitDeferred() { return singleton_ && singleton_->submit_deferred_; }

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

  //! Ensures that at least num_dwords values may be added to the pushbuffer.
  void Reserve(uint32_t num_dwords);

  //! Starts a new underlying pbkit block.
  void StartPbkitBlock();

  //! Starts a new underlying pbkit block if one is not already open.
  void EnsurePbkitBlock();

  //! Ends the current underlying pbkit block, submitting it to the GPU.
  void SubmitBlock();

  //! Waits for the GPU to consume the pushbuffer and resets it to head.
  void ResetPushbuffer();

  uint32_t *head_ = nullptr;

  //! The number of dwords in the current underlying pbkit block.
  uint32_t current_block_elements_ = 0;

  //! The total number of dwords submitted since the last flush/reset.
  uint32_t total_block_elements_ = 0;

  //! Whether submission is deferred.
  bool submit_deferred_ = false;

  //! Whether a logical Begin() is outstanding (only tracked while submission is deferred).
  bool in_block_ = false;

  //! Number of DWORDs that may be pushed into one underlying pbkit block before it is submitted.
  uint32_t submit_interval_elements_ = 0;

  static Pushbuffer *singleton_;
};

}  // namespace PBKitPlusPlus

#endif  // PUSHBUFFER_H
