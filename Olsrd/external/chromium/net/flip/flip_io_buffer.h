// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FLIP_FLIP_IO_BUFFER_H_
#define NET_FLIP_FLIP_IO_BUFFER_H_

#include "base/ref_counted.h"
#include "net/base/io_buffer.h"

namespace net {

class FlipStream;

// A class for managing FLIP IO buffers.  These buffers need to be prioritized
// so that the FlipSession sends them in the right order.  Further, they need
// to track the FlipStream which they are associated with so that incremental
// completion of the IO can notify the appropriate stream of completion.
class FlipIOBuffer {
 public:
  // Constructor
  // |buffer| is the actual data buffer.
  // |size| is the size of the data buffer.
  // |priority| is the priority of this buffer.  Lower numbers are higher
  //            priority.
  // |stream| is a pointer to the stream which is managing this buffer.
  FlipIOBuffer(IOBuffer* buffer, int size, int priority, FlipStream* stream);
  FlipIOBuffer();
  ~FlipIOBuffer();

  // Accessors.
  DrainableIOBuffer* buffer() const { return buffer_; }
  size_t size() const { return buffer_->size(); }
  void release();
  int priority() const { return priority_; }
  const scoped_refptr<FlipStream>& stream() const { return stream_; }

  // Comparison operator to support sorting.
  bool operator<(const FlipIOBuffer& other) const {
    if (priority_ != other.priority_)
      return priority_ > other.priority_;
    return position_ > other.position_;
  }

 private:
  scoped_refptr<DrainableIOBuffer> buffer_;
  int priority_;
  uint64 position_;
  scoped_refptr<FlipStream> stream_;
  static uint64 order_;  // Maintains a FIFO order for equal priorities.
};

}  // namespace net

#endif  // NET_FLIP_FLIP_IO_BUFFER_H_
