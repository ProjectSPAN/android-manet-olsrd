// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/sparse_control.h"

#include "base/format_macros.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/string_util.h"
#include "base/time.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/disk_cache/backend_impl.h"
#include "net/disk_cache/entry_impl.h"
#include "net/disk_cache/file.h"

using base::Time;

namespace {

// Stream of the sparse data index.
const int kSparseIndex = 2;

// Stream of the sparse data.
const int kSparseData = 1;

// We can have up to 64k children.
const int kMaxMapSize = 8 * 1024;

// The maximum number of bytes that a child can store.
const int kMaxEntrySize = 0x100000;

// The size of each data block (tracked by the child allocation bitmap).
const int kBlockSize = 1024;

// Returns the name of of a child entry given the base_name and signature of the
// parent and the child_id.
// If the entry is called entry_name, child entries will be named something
// like Range_entry_name:XXX:YYY where XXX is the entry signature and YYY is the
// number of the particular child.
std::string GenerateChildName(const std::string& base_name, int64 signature,
                              int64 child_id) {
  return StringPrintf("Range_%s:%" PRIx64 ":%" PRIx64, base_name.c_str(),
                      signature, child_id);
}

// This class deletes the children of a sparse entry.
class ChildrenDeleter
    : public base::RefCounted<ChildrenDeleter>,
      public disk_cache::FileIOCallback {
 public:
  ChildrenDeleter(disk_cache::BackendImpl* backend, const std::string& name)
      : backend_(backend), name_(name) {}

  virtual void OnFileIOComplete(int bytes_copied);

  // Two ways of deleting the children: if we have the children map, use Start()
  // directly, otherwise pass the data address to ReadData().
  void Start(char* buffer, int len);
  void ReadData(disk_cache::Addr address, int len);

 private:
  friend class base::RefCounted<ChildrenDeleter>;
  ~ChildrenDeleter() {}

  void DeleteChildren();

  disk_cache::BackendImpl* backend_;
  std::string name_;
  disk_cache::Bitmap children_map_;
  int64 signature_;
  scoped_array<char> buffer_;
  DISALLOW_EVIL_CONSTRUCTORS(ChildrenDeleter);
};

// This is the callback of the file operation.
void ChildrenDeleter::OnFileIOComplete(int bytes_copied) {
  char* buffer = buffer_.release();
  Start(buffer, bytes_copied);
}

void ChildrenDeleter::Start(char* buffer, int len) {
  buffer_.reset(buffer);
  if (len < static_cast<int>(sizeof(disk_cache::SparseData)))
    return Release();

  // Just copy the information from |buffer|, delete |buffer| and start deleting
  // the child entries.
  disk_cache::SparseData* data =
      reinterpret_cast<disk_cache::SparseData*>(buffer);
  signature_ = data->header.signature;

  int num_bits = (len - sizeof(disk_cache::SparseHeader)) * 8;
  children_map_.Resize(num_bits, false);
  children_map_.SetMap(data->bitmap, num_bits / 32);
  buffer_.reset();

  DeleteChildren();
}

void ChildrenDeleter::ReadData(disk_cache::Addr address, int len) {
  DCHECK(address.is_block_file());
  disk_cache::File* file(backend_->File(address));
  if (!file)
    return Release();

  size_t file_offset = address.start_block() * address.BlockSize() +
                       disk_cache::kBlockHeaderSize;

  buffer_.reset(new char[len]);
  bool completed;
  if (!file->Read(buffer_.get(), len, file_offset, this, &completed))
    return Release();

  if (completed)
    OnFileIOComplete(len);

  // And wait until OnFileIOComplete gets called.
}

void ChildrenDeleter::DeleteChildren() {
  int child_id = 0;
  if (!children_map_.FindNextSetBit(&child_id)) {
    // We are done. Just delete this object.
    return Release();
  }
  std::string child_name = GenerateChildName(name_, signature_, child_id);
  backend_->DoomEntry(child_name);
  children_map_.Set(child_id, false);

  // Post a task to delete the next child.
  MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &ChildrenDeleter::DeleteChildren));
}

}  // namespace.

namespace disk_cache {

SparseControl::~SparseControl() {
  if (child_)
    CloseChild();
  if (init_)
    WriteSparseData();
}

int SparseControl::Init() {
  DCHECK(!init_);

  // We should not have sparse data for the exposed entry.
  if (entry_->GetDataSize(kSparseData))
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  // Now see if there is something where we store our data.
  int rv = net::OK;
  int data_len = entry_->GetDataSize(kSparseIndex);
  if (!data_len) {
    rv = CreateSparseEntry();
  } else {
    rv = OpenSparseEntry(data_len);
  }

  if (rv == net::OK)
    init_ = true;
  return rv;
}

int SparseControl::StartIO(SparseOperation op, int64 offset, net::IOBuffer* buf,
                           int buf_len, net::CompletionCallback* callback) {
  DCHECK(init_);
  // We don't support simultaneous IO for sparse data.
  if (operation_ != kNoOperation)
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  if (offset < 0 || buf_len < 0)
    return net::ERR_INVALID_ARGUMENT;

  // We only support up to 64 GB.
  if (offset + buf_len >= 0x1000000000LL || offset + buf_len < 0)
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  DCHECK(!user_buf_);
  DCHECK(!user_callback_);

  if (!buf && (op == kReadOperation || op == kWriteOperation))
    return 0;

  // Copy the operation parameters.
  operation_ = op;
  offset_ = offset;
  user_buf_ = buf ? new net::DrainableIOBuffer(buf, buf_len) : NULL;
  buf_len_ = buf_len;
  user_callback_ = callback;

  result_ = 0;
  pending_ = false;
  finished_ = false;
  abort_ = false;

  DoChildrenIO();

  if (!pending_) {
    // Everything was done synchronously.
    operation_ = kNoOperation;
    user_buf_ = NULL;
    user_callback_ = NULL;
    return result_;
  }

  return net::ERR_IO_PENDING;
}

int SparseControl::GetAvailableRange(int64 offset, int len, int64* start) {
  DCHECK(init_);
  // We don't support simultaneous IO for sparse data.
  if (operation_ != kNoOperation)
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  DCHECK(start);

  range_found_ = false;
  int result = StartIO(kGetRangeOperation, offset, NULL, len, NULL);
  if (range_found_) {
    *start = offset_;
    return result;
  }

  // This is a failure. We want to return a valid start value in any case.
  *start = offset;
  return result < 0 ? result : 0;  // Don't mask error codes to the caller.
}

void SparseControl::CancelIO() {
  if (operation_ == kNoOperation)
    return;
  abort_ = true;
}

int SparseControl::ReadyToUse(net::CompletionCallback* completion_callback) {
  if (!abort_)
    return net::OK;

  // We'll grab another reference to keep this object alive because we just have
  // one extra reference due to the pending IO operation itself, but we'll
  // release that one before invoking user_callback_.
  entry_->AddRef();  // Balanced in DoAbortCallbacks.
  abort_callbacks_.push_back(completion_callback);
  return net::ERR_IO_PENDING;
}

// Static
void SparseControl::DeleteChildren(EntryImpl* entry) {
  DCHECK(entry->GetEntryFlags() & PARENT_ENTRY);
  int data_len = entry->GetDataSize(kSparseIndex);
  if (data_len < static_cast<int>(sizeof(SparseData)) ||
      entry->GetDataSize(kSparseData))
    return;

  int map_len = data_len - sizeof(SparseHeader);
  if (map_len > kMaxMapSize || map_len % 4)
    return;

  char* buffer;
  Addr address;
  entry->GetData(kSparseIndex, &buffer, &address);
  if (!buffer && !address.is_initialized())
    return;

  ChildrenDeleter* deleter = new ChildrenDeleter(entry->backend_,
                                                 entry->GetKey());
  // The object will self destruct when finished.
  deleter->AddRef();

  if (buffer) {
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
        deleter, &ChildrenDeleter::Start, buffer, data_len));
  } else {
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
        deleter, &ChildrenDeleter::ReadData, address, data_len));
  }
}

// We are going to start using this entry to store sparse data, so we have to
// initialize our control info.
int SparseControl::CreateSparseEntry() {
  if (CHILD_ENTRY & entry_->GetEntryFlags())
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  memset(&sparse_header_, 0, sizeof(sparse_header_));
  sparse_header_.signature = Time::Now().ToInternalValue();
  sparse_header_.magic = kIndexMagic;
  sparse_header_.parent_key_len = entry_->GetKey().size();
  children_map_.Resize(kNumSparseBits, true);

  // Save the header. The bitmap is saved in the destructor.
  scoped_refptr<net::IOBuffer> buf =
      new net::WrappedIOBuffer(reinterpret_cast<char*>(&sparse_header_));

  int rv = entry_->WriteData(kSparseIndex, 0, buf, sizeof(sparse_header_), NULL,
                             false);
  if (rv != sizeof(sparse_header_)) {
    DLOG(ERROR) << "Unable to save sparse_header_";
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;
  }

  entry_->SetEntryFlags(PARENT_ENTRY);
  return net::OK;
}

// We are opening an entry from disk. Make sure that our control data is there.
int SparseControl::OpenSparseEntry(int data_len) {
  if (data_len < static_cast<int>(sizeof(SparseData)))
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  if (entry_->GetDataSize(kSparseData))
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  if (!(PARENT_ENTRY & entry_->GetEntryFlags()))
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  // Dont't go over board with the bitmap. 8 KB gives us offsets up to 64 GB.
  int map_len = data_len - sizeof(sparse_header_);
  if (map_len > kMaxMapSize || map_len % 4)
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  scoped_refptr<net::IOBuffer> buf =
      new net::WrappedIOBuffer(reinterpret_cast<char*>(&sparse_header_));

  // Read header.
  int rv = entry_->ReadData(kSparseIndex, 0, buf, sizeof(sparse_header_), NULL);
  if (rv != static_cast<int>(sizeof(sparse_header_)))
    return net::ERR_CACHE_READ_FAILURE;

  // The real validation should be performed by the caller. This is just to
  // double check.
  if (sparse_header_.magic != kIndexMagic ||
      sparse_header_.parent_key_len !=
          static_cast<int>(entry_->GetKey().size()))
    return net::ERR_CACHE_OPERATION_NOT_SUPPORTED;

  // Read the actual bitmap.
  buf = new net::IOBuffer(map_len);
  rv = entry_->ReadData(kSparseIndex, sizeof(sparse_header_), buf, map_len,
                        NULL);
  if (rv != map_len)
    return net::ERR_CACHE_READ_FAILURE;

  // Grow the bitmap to the current size and copy the bits.
  children_map_.Resize(map_len * 8, false);
  children_map_.SetMap(reinterpret_cast<uint32*>(buf->data()), map_len);
  return net::OK;
}

bool SparseControl::OpenChild() {
  DCHECK_GE(result_, 0);

  std::string key = GenerateChildKey();
  if (child_) {
    // Keep using the same child or open another one?.
    if (key == child_->GetKey())
      return true;
    CloseChild();
  }

  // Se if we are tracking this child.
  bool child_present = ChildPresent();
  if (!child_present || !entry_->backend_->OpenEntry(key, &child_))
    return ContinueWithoutChild(key);

  EntryImpl* child = static_cast<EntryImpl*>(child_);
  if (!(CHILD_ENTRY & child->GetEntryFlags()) ||
      child->GetDataSize(kSparseIndex) <
          static_cast<int>(sizeof(child_data_)))
    return KillChildAndContinue(key, false);

  scoped_refptr<net::WrappedIOBuffer> buf =
      new net::WrappedIOBuffer(reinterpret_cast<char*>(&child_data_));

  // Read signature.
  int rv = child_->ReadData(kSparseIndex, 0, buf, sizeof(child_data_), NULL);
  if (rv != sizeof(child_data_))
    return KillChildAndContinue(key, true);  // This is a fatal failure.

  if (child_data_.header.signature != sparse_header_.signature ||
      child_data_.header.magic != kIndexMagic)
    return KillChildAndContinue(key, false);

  if (child_data_.header.last_block_len < 0 ||
      child_data_.header.last_block_len > kBlockSize) {
    // Make sure this values are always within range.
    child_data_.header.last_block_len = 0;
    child_data_.header.last_block = -1;
  }

  return true;
}

void SparseControl::CloseChild() {
  scoped_refptr<net::WrappedIOBuffer> buf =
      new net::WrappedIOBuffer(reinterpret_cast<char*>(&child_data_));

  // Save the allocation bitmap before closing the child entry.
  int rv = child_->WriteData(kSparseIndex, 0, buf, sizeof(child_data_),
                             NULL, false);
  if (rv != sizeof(child_data_))
    DLOG(ERROR) << "Failed to save child data";
  child_->Close();
  child_ = NULL;
}

std::string SparseControl::GenerateChildKey() {
  return GenerateChildName(entry_->GetKey(), sparse_header_.signature,
                           offset_ >> 20);
}

// We are deleting the child because something went wrong.
bool SparseControl::KillChildAndContinue(const std::string& key, bool fatal) {
  SetChildBit(false);
  child_->Doom();
  child_->Close();
  child_ = NULL;
  if (fatal) {
    result_ = net::ERR_CACHE_READ_FAILURE;
    return false;
  }
  return ContinueWithoutChild(key);
}

// We were not able to open this child; see what we can do.
bool SparseControl::ContinueWithoutChild(const std::string& key) {
  if (kReadOperation == operation_)
    return false;
  if (kGetRangeOperation == operation_)
    return true;

  if (!entry_->backend_->CreateEntry(key, &child_)) {
    child_ = NULL;
    result_ = net::ERR_CACHE_READ_FAILURE;
    return false;
  }
  // Write signature.
  InitChildData();
  return true;
}

bool SparseControl::ChildPresent() {
  int child_bit = static_cast<int>(offset_ >> 20);
  if (children_map_.Size() <= child_bit)
    return false;

  return children_map_.Get(child_bit);
}

void SparseControl::SetChildBit(bool value) {
  int child_bit = static_cast<int>(offset_ >> 20);

  // We may have to increase the bitmap of child entries.
  if (children_map_.Size() <= child_bit)
    children_map_.Resize(Bitmap::RequiredArraySize(child_bit + 1) * 32, true);

  children_map_.Set(child_bit, value);
}

void SparseControl::WriteSparseData() {
  scoped_refptr<net::IOBuffer> buf = new net::WrappedIOBuffer(
      reinterpret_cast<const char*>(children_map_.GetMap()));

  int len = children_map_.ArraySize() * 4;
  int rv = entry_->WriteData(kSparseIndex, sizeof(sparse_header_), buf, len,
                             NULL, false);
  if (rv != len) {
    DLOG(ERROR) << "Unable to save sparse map";
  }
}

bool SparseControl::VerifyRange() {
  DCHECK_GE(result_, 0);

  child_offset_ = static_cast<int>(offset_) & (kMaxEntrySize - 1);
  child_len_ = std::min(buf_len_, kMaxEntrySize - child_offset_);

  // We can write to (or get info from) anywhere in this child.
  if (operation_ != kReadOperation)
    return true;

  // Check that there are no holes in this range.
  int last_bit = (child_offset_ + child_len_ + 1023) >> 10;
  int start = child_offset_ >> 10;
  if (child_map_.FindNextBit(&start, last_bit, false)) {
    // Something is not here.
    DCHECK_GE(child_data_.header.last_block_len, 0);
    DCHECK_LT(child_data_.header.last_block_len, kMaxEntrySize);
    int partial_block_len = PartialBlockLength(start);
    if (start == child_offset_ >> 10) {
      // It looks like we don't have anything.
      if (partial_block_len <= (child_offset_ & (kBlockSize - 1)))
        return false;
    }

    // We have the first part.
    child_len_ = (start << 10) - child_offset_;
    if (partial_block_len) {
      // We may have a few extra bytes.
      child_len_ = std::min(child_len_ + partial_block_len, buf_len_);
    }
    // There is no need to read more after this one.
    buf_len_ = child_len_;
  }
  return true;
}

void SparseControl::UpdateRange(int result) {
  if (result <= 0 || operation_ != kWriteOperation)
    return;

  DCHECK_GE(child_data_.header.last_block_len, 0);
  DCHECK_LT(child_data_.header.last_block_len, kMaxEntrySize);

  // Write the bitmap.
  int first_bit = child_offset_ >> 10;
  int block_offset = child_offset_ & (kBlockSize - 1);
  if (block_offset && (child_data_.header.last_block != first_bit ||
                       child_data_.header.last_block_len < block_offset)) {
    // The first block is not completely filled; ignore it.
    first_bit++;
  }

  int last_bit = (child_offset_ + result) >> 10;
  block_offset = (child_offset_ + result) & (kBlockSize - 1);

  // This condition will hit with the following criteria:
  // 1. The first byte doesn't follow the last write.
  // 2. The first byte is in the middle of a block.
  // 3. The first byte and the last byte are in the same block.
  if (first_bit > last_bit)
    return;

  if (block_offset && !child_map_.Get(last_bit)) {
    // The last block is not completely filled; save it for later.
    child_data_.header.last_block = last_bit;
    child_data_.header.last_block_len = block_offset;
  } else {
    child_data_.header.last_block = -1;
  }

  child_map_.SetRange(first_bit, last_bit, true);
}

int SparseControl::PartialBlockLength(int block_index) const {
  if (block_index == child_data_.header.last_block)
    return child_data_.header.last_block_len;

  // This may be the last stored index.
  int entry_len = child_->GetDataSize(kSparseData);
  if (block_index == entry_len >> 10)
    return entry_len & (kBlockSize - 1);

  // This is really empty.
  return 0;
}

void SparseControl::InitChildData() {
  // We know the real type of child_.
  EntryImpl* child = static_cast<EntryImpl*>(child_);
  child->SetEntryFlags(CHILD_ENTRY);

  memset(&child_data_, 0, sizeof(child_data_));
  child_data_.header = sparse_header_;

  scoped_refptr<net::WrappedIOBuffer> buf =
      new net::WrappedIOBuffer(reinterpret_cast<char*>(&child_data_));

  int rv = child_->WriteData(kSparseIndex, 0, buf, sizeof(child_data_),
                             NULL, false);
  if (rv != sizeof(child_data_))
    DLOG(ERROR) << "Failed to save child data";
  SetChildBit(true);
}

void SparseControl::DoChildrenIO() {
  while (DoChildIO()) continue;

  if (pending_ && finished_)
    DoUserCallback();
}

bool SparseControl::DoChildIO() {
  finished_ = true;
  if (!buf_len_ || result_ < 0)
    return false;

  if (!OpenChild())
    return false;

  if (!VerifyRange())
    return false;

  // We have more work to do. Let's not trigger a callback to the caller.
  finished_ = false;
  net::CompletionCallback* callback = user_callback_ ? &child_callback_ : NULL;

  int rv = 0;
  switch (operation_) {
    case kReadOperation:
      rv = child_->ReadData(kSparseData, child_offset_, user_buf_, child_len_,
                            callback);
      break;
    case kWriteOperation:
      rv = child_->WriteData(kSparseData, child_offset_, user_buf_, child_len_,
                             callback, false);
      break;
    case kGetRangeOperation:
      rv = DoGetAvailableRange();
      break;
    default:
      NOTREACHED();
  }

  if (rv == net::ERR_IO_PENDING) {
    if (!pending_) {
      pending_ = true;
      // The child will protect himself against closing the entry while IO is in
      // progress. However, this entry can still be closed, and that would not
      // be a good thing for us, so we increase the refcount until we're
      // finished doing sparse stuff.
      entry_->AddRef();  // Balanced in DoUserCallback.
    }
    return false;
  }
  if (!rv)
    return false;

  DoChildIOCompleted(rv);
  return true;
}

int SparseControl::DoGetAvailableRange() {
  if (!child_)
    return child_len_;  // Move on to the next child.

  // Check that there are no holes in this range.
  int last_bit = (child_offset_ + child_len_ + 1023) >> 10;
  int start = child_offset_ >> 10;
  int partial_start_bytes = PartialBlockLength(start);
  int found = start;
  int bits_found = child_map_.FindBits(&found, last_bit, true);

  // We don't care if there is a partial block in the middle of the range.
  int block_offset = child_offset_ & (kBlockSize - 1);
  if (!bits_found && partial_start_bytes <= block_offset)
    return child_len_;

  // We are done. Just break the loop and reset result_ to our real result.
  range_found_ = true;

  // found now points to the first 1. Lets see if we have zeros before it.
  int empty_start = std::max((found << 10) - child_offset_, 0);

  int bytes_found = bits_found << 10;
  bytes_found += PartialBlockLength(found + bits_found);

  if (start == found)
    bytes_found -= block_offset;

  // If the user is searching past the end of this child, bits_found is the
  // right result; otherwise, we have some empty space at the start of this
  // query that we have to subtract from the range that we searched.
  result_ = std::min(bytes_found, child_len_ - empty_start);

  if (!bits_found) {
    result_ = std::min(partial_start_bytes - block_offset, child_len_);
    empty_start = 0;
  }

  // Only update offset_ when this query found zeros at the start.
  if (empty_start)
    offset_ += empty_start;

  // This will actually break the loop.
  buf_len_ = 0;
  return 0;
}

void SparseControl::DoChildIOCompleted(int result) {
  if (result < 0) {
    // We fail the whole operation if we encounter an error.
    result_ = result;
    return;
  }

  UpdateRange(result);

  result_ += result;
  offset_ += result;
  buf_len_ -= result;

  // We'll be reusing the user provided buffer for the next chunk.
  if (buf_len_ && user_buf_)
    user_buf_->DidConsume(result);
}

void SparseControl::OnChildIOCompleted(int result) {
  DCHECK_NE(net::ERR_IO_PENDING, result);
  DoChildIOCompleted(result);

  if (abort_) {
    // We'll return the current result of the operation, which may be less than
    // the bytes to read or write, but the user cancelled the operation.
    abort_ = false;
    DoUserCallback();
    return DoAbortCallbacks();
  }

  // We are running a callback from the message loop. It's time to restart what
  // we were doing before.
  DoChildrenIO();
}

void SparseControl::DoUserCallback() {
  DCHECK(user_callback_);
  net::CompletionCallback* c = user_callback_;
  user_callback_ = NULL;
  user_buf_ = NULL;
  pending_ = false;
  operation_ = kNoOperation;
  entry_->Release();  // Don't touch object after this line.
  c->Run(result_);
}

void SparseControl::DoAbortCallbacks() {
  for (size_t i = 0; i < abort_callbacks_.size(); i++) {
    // Releasing all references to entry_ may result in the destruction of this
    // object so we should not be touching it after the last Release().
    net::CompletionCallback* c = abort_callbacks_[i];
    if (i == abort_callbacks_.size() - 1)
      abort_callbacks_.clear();

    entry_->Release();  // Don't touch object after this line.
    c->Run(net::OK);
  }
}

}  // namespace disk_cache
