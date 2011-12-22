// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/flip/flip_io_buffer.h"
#include "net/flip/flip_stream.h"

namespace net {

// static
uint64 FlipIOBuffer::order_ = 0;

FlipIOBuffer::FlipIOBuffer(
    IOBuffer* buffer, int size, int priority, FlipStream* stream)
  : buffer_(new DrainableIOBuffer(buffer, size)),
    priority_(priority),
    position_(++order_),
    stream_(stream) {}

FlipIOBuffer::FlipIOBuffer() : priority_(0), position_(0), stream_(NULL) {}

FlipIOBuffer::~FlipIOBuffer() {}

void FlipIOBuffer::release() {
  buffer_ = NULL;
  stream_ = NULL;
}

}  // namespace net
