#include "pushbuffer.h"

#include <pbkit/pbkit.h>
#include <xboxkrnl/xboxkrnl.h>

#include <cstring>

#include "pbkpp_assert.h"

namespace PBKitPlusPlus {

// Number of DWORDs pushed into a pbkit block before it is submitted when submission is not deferred. This is not a
// hardware limit; it keeps blocks under the 128 DWORD size that debug builds of pbkit assert on.
static constexpr uint32_t kMaxElementsPerBlock = 96;

Pushbuffer Pushbuffer::instance_;

void Pushbuffer::Initialize(uint32_t pushbuffer_bytes) {
  if (!instance_.initialized_) {
    instance_.initialized_ = true;
    // pbkit does not wrap the pushbuffer, so everything pushed between resets must fit within it. The size is in
    // bytes, so this is ~80% of its DWORD capacity.
    instance_.capacity_elements_ = pushbuffer_bytes / 5;
    instance_.submit_interval_elements_ = kMaxElementsPerBlock;
  }
}

void Pushbuffer::Flush() {
  if (instance_.submit_deferred_) {
    PBKPP_ASSERT(!instance_.in_block_ && "Flush must not be called within a pushbuffer block");
    if (instance_.head_) {
      instance_.SubmitBlock();
    }
    instance_.ResetPushbuffer();
    return;
  }

  PBKPP_ASSERT(!instance_.head_ && "Flush must not be called within a pushbuffer block");
  instance_.ResetPushbuffer();
}

void Pushbuffer::Submit() {
  PBKPP_ASSERT(instance_.initialized_);
  if (instance_.submit_deferred_) {
    PBKPP_ASSERT(!instance_.in_block_ && "Submit must not be called within a pushbuffer block");
    if (instance_.head_) {
      instance_.SubmitBlock();
    }
    return;
  }

  PBKPP_ASSERT(!instance_.head_ && "Submit must not be called within a pushbuffer block");
}

void Pushbuffer::BeginDeferredSubmit(uint32_t submit_interval_dwords) {
  PBKPP_ASSERT(instance_.initialized_);
  PBKPP_ASSERT(!instance_.submit_deferred_ && "Nested deferred submission is not supported");
  PBKPP_ASSERT(!instance_.head_ && "BeginDeferredSubmit must not be called within a pushbuffer block");
  PBKPP_ASSERT(submit_interval_dwords > 0 && submit_interval_dwords <= instance_.capacity_elements_ &&
               "submit_interval_dwords must not exceed the pushbuffer capacity");

  instance_.submit_deferred_ = true;
  instance_.in_block_ = false;
  instance_.submit_interval_elements_ = submit_interval_dwords;
}

void Pushbuffer::EndDeferredSubmit() {
  PBKPP_ASSERT(instance_.initialized_ && instance_.submit_deferred_ &&
               "EndDeferredSubmit must not be called without BeginDeferredSubmit");
  PBKPP_ASSERT(!instance_.in_block_ && "EndDeferredSubmit must not be called within a pushbuffer block");

  if (instance_.head_) {
    instance_.SubmitBlock();
  }
  instance_.submit_deferred_ = false;
  instance_.submit_interval_elements_ = kMaxElementsPerBlock;
}

void Pushbuffer::StartPbkitBlock() {
  head_ = block_start_ = pb_begin();
  const uint32_t available =
      total_block_elements_ < capacity_elements_ ? capacity_elements_ - total_block_elements_ : 0;
  remaining_ = available < submit_interval_elements_ ? available : submit_interval_elements_;
}

void Pushbuffer::SubmitBlock() {
  total_block_elements_ += head_ - block_start_;
  pb_end(head_);
  head_ = block_start_ = nullptr;
  remaining_ = 0;
}

void Pushbuffer::ResetPushbuffer() {
  const auto start = KeQueryPerformanceCounter();

  while (pb_busy()) {
    /* Wait for completion... */
  }

  pb_reset();

  ++reset_stats_.resets;
  reset_stats_.wait_ticks += KeQueryPerformanceCounter() - start;

  total_block_elements_ = 0;
}

void Pushbuffer::AllocateSlow(uint32_t num_dwords) {
  PBKPP_ASSERT(num_dwords < capacity_elements_ && "Single push exceeds the pushbuffer capacity");

  // Allocate() has already checked that this is between Begin() and End(). With no block open, this is the first push
  // after a deferred Begin().
  if (head_) {
    SubmitBlock();
  }

  if (total_block_elements_ + num_dwords > capacity_elements_) {
    ResetPushbuffer();
  }

  StartPbkitBlock();

  // A single push larger than the submit interval is allowed to overrun it; the capacity was checked above.
  if (remaining_ < num_dwords) {
    remaining_ = num_dwords;
  }
}

void Pushbuffer::PushTransposedMatrix(uint32_t command, const float *m) {
  pb_push_transposed_matrix(Allocate(17), command, m);
}

void Pushbuffer::Push4x3Matrix(uint32_t command, const float *m) { pb_push_4x3_matrix(Allocate(13), command, m); }

void Pushbuffer::Push4x4Matrix(uint32_t command, const float *m) { pb_push_4x4_matrix(Allocate(17), command, m); }

}  // namespace PBKitPlusPlus
