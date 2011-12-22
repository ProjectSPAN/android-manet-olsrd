// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/flip/flip_io_buffer.h"

#include "net/base/test_completion_callback.h"
#include "net/flip/flip_session.h"
#include "net/flip/flip_stream.h"
#include "net/socket/socket_test_util.h"
#include "testing/platform_test.h"

namespace net {

class FlipSessionTest : public PlatformTest {
 public:
};

// Test the FlipIOBuffer class.
TEST_F(FlipSessionTest, FlipIOBuffer) {
  std::priority_queue<FlipIOBuffer> queue_;
  const size_t kQueueSize = 100;

  // Insert 100 items; pri 100 to 1.
  for (size_t index = 0; index < kQueueSize; ++index) {
    FlipIOBuffer buffer(new IOBuffer(), 0, kQueueSize - index, NULL);
    queue_.push(buffer);
  }

  // Insert several priority 0 items last.
  const size_t kNumDuplicates = 12;
  IOBufferWithSize* buffers[kNumDuplicates];
  for (size_t index = 0; index < kNumDuplicates; ++index) {
    buffers[index] = new IOBufferWithSize(index+1);
    queue_.push(FlipIOBuffer(buffers[index], buffers[index]->size(), 0, NULL));
  }

  EXPECT_EQ(kQueueSize + kNumDuplicates, queue_.size());

  // Verify the P0 items come out in FIFO order.
  for (size_t index = 0; index < kNumDuplicates; ++index) {
    FlipIOBuffer buffer = queue_.top();
    EXPECT_EQ(0, buffer.priority());
    EXPECT_EQ(index + 1, buffer.size());
    queue_.pop();
  }

  int priority = 1;
  while (queue_.size()) {
    FlipIOBuffer buffer = queue_.top();
    EXPECT_EQ(priority++, buffer.priority());
    queue_.pop();
  }
}

}  // namespace net
