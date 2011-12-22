// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "flip_frame_builder.h"    // cross-google3 directory naming.
#include "flip_protocol.h"

namespace flip {

// We mark a read only FlipFrameBuilder with a special capacity_.
static const size_t kCapacityReadOnly = std::numeric_limits<size_t>::max();

FlipFrameBuilder::FlipFrameBuilder()
    : buffer_(NULL),
      capacity_(0),
      length_(0),
      variable_buffer_offset_(0) {
  Resize(kInitialPayload);
}

FlipFrameBuilder::FlipFrameBuilder(const char* data, int data_len)
    : buffer_(const_cast<char*>(data)),
      capacity_(kCapacityReadOnly),
      length_(data_len),
      variable_buffer_offset_(0) {
}

FlipFrameBuilder::~FlipFrameBuilder() {
  if (capacity_ != kCapacityReadOnly)
    delete[] buffer_;
}

bool FlipFrameBuilder::ReadUInt16(void** iter, uint16* result) const {
  DCHECK(iter);
  if (!*iter)
    *iter = const_cast<char*>(buffer_);

  if (!IteratorHasRoomFor(*iter, sizeof(*result)))
    return false;

  *result = ntohs(*(reinterpret_cast<uint16*>(*iter)));

  UpdateIter(iter, sizeof(*result));
  return true;
}

bool FlipFrameBuilder::ReadUInt32(void** iter, uint32* result) const {
  DCHECK(iter);
  if (!*iter)
    *iter = const_cast<char*>(buffer_);

  if (!IteratorHasRoomFor(*iter, sizeof(*result)))
    return false;

  *result = ntohl(*(reinterpret_cast<uint32*>(*iter)));

  UpdateIter(iter, sizeof(*result));
  return true;
}

bool FlipFrameBuilder::ReadString(void** iter, std::string* result) const {
  DCHECK(iter);

  uint16 len;
  if (!ReadUInt16(iter, &len))
    return false;

  if (!IteratorHasRoomFor(*iter, len))
    return false;

  char* chars = reinterpret_cast<char*>(*iter);
  result->assign(chars, len);

  UpdateIter(iter, len);
  return true;
}

bool FlipFrameBuilder::ReadBytes(void** iter, const char** data,
                                 uint16 length) const {
  DCHECK(iter);
  DCHECK(data);

  if (!IteratorHasRoomFor(*iter, length))
    return false;

  *data = reinterpret_cast<const char*>(*iter);

  UpdateIter(iter, length);
  return true;
}

bool FlipFrameBuilder::ReadData(void** iter, const char** data,
                                uint16* length) const {
  DCHECK(iter);
  DCHECK(data);
  DCHECK(length);

  if (!ReadUInt16(iter, length))
    return false;

  return ReadBytes(iter, data, *length);
}

char* FlipFrameBuilder::BeginWrite(size_t length) {
  size_t offset = length_;
  size_t needed_size = length_ + length;
  if (needed_size > capacity_ && !Resize(std::max(capacity_ * 2, needed_size)))
    return NULL;

#ifdef ARCH_CPU_64_BITS
  DCHECK_LE(length, std::numeric_limits<uint32>::max());
#endif

  return buffer_ + offset;
}

void FlipFrameBuilder::EndWrite(char* dest, int length) {
}

bool FlipFrameBuilder::WriteBytes(const void* data, uint16 data_len) {
  DCHECK(capacity_ != kCapacityReadOnly);

  char* dest = BeginWrite(data_len);
  if (!dest)
    return false;

  memcpy(dest, data, data_len);

  EndWrite(dest, data_len);
  length_ += data_len;
  return true;
}

bool FlipFrameBuilder::WriteString(const std::string& value) {
  if (value.size() > 0xffff)
    return false;

  if (!WriteUInt16(static_cast<int>(value.size())))
    return false;

  return WriteBytes(value.data(), static_cast<uint16>(value.size()));
}

char* FlipFrameBuilder::BeginWriteData(uint16 length) {
  DCHECK_EQ(variable_buffer_offset_, 0U) <<
    "There can only be one variable buffer in a FlipFrameBuilder";

  if (!WriteUInt16(length))
    return false;

  char *data_ptr = BeginWrite(length);
  if (!data_ptr)
    return NULL;

  variable_buffer_offset_ = data_ptr - buffer_ - sizeof(int);

  // EndWrite doesn't necessarily have to be called after the write operation,
  // so we call it here to pad out what the caller will eventually write.
  EndWrite(data_ptr, length);
  return data_ptr;
}

bool FlipFrameBuilder::Resize(size_t new_capacity) {
  if (new_capacity < capacity_)
    return true;

  char* p = new char[new_capacity];
  if (buffer_) {
    memcpy(p, buffer_, capacity_);
    delete[] buffer_;
  }
  if (!p && new_capacity > 0)
    return false;
  buffer_ = p;
  capacity_ = new_capacity;
  return true;
}

}  // namespace flip
