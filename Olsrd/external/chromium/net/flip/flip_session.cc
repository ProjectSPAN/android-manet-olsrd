// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/flip/flip_session.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/rand_util.h"
#include "base/stats_counters.h"
#include "base/stl_util-inl.h"
#include "base/string_util.h"
#include "net/base/connection_type_histograms.h"
#include "net/base/load_flags.h"
#include "net/base/load_log.h"
#include "net/base/net_util.h"
#include "net/flip/flip_frame_builder.h"
#include "net/flip/flip_protocol.h"
#include "net/flip/flip_stream.h"
#include "net/http/http_network_session.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/socket/client_socket.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/ssl_client_socket.h"
#include "net/tools/dump_cache/url_to_filename_encoder.h"

namespace {

// Diagnostics function to dump the headers of a request.
// TODO(mbelshe): Remove this function.
void DumpFlipHeaders(const flip::FlipHeaderBlock& headers) {
  // Because this function gets called on every request,
  // take extra care to optimize it away if logging is turned off.
  if (logging::LOG_INFO < logging::GetMinLogLevel())
    return;

  flip::FlipHeaderBlock::const_iterator it = headers.begin();
  while (it != headers.end()) {
    std::string val = (*it).second;
    std::string::size_type pos = 0;
    while ((pos = val.find('\0', pos)) != val.npos)
      val[pos] = '\n';
    LOG(INFO) << (*it).first << "==" << val;
    ++it;
  }
}

}  // namespace

namespace net {

namespace {

#ifdef WIN32
// We use an artificially small buffer size on windows because the async IO
// system will artifiially delay IO completions when we use large buffers.
const int kReadBufferSize = 2 * 1024;
#else
const int kReadBufferSize = 8 * 1024;
#endif

// Convert a FlipHeaderBlock into an HttpResponseInfo.
// |headers| input parameter with the FlipHeaderBlock.
// |info| output parameter for the HttpResponseInfo.
// Returns true if successfully converted.  False if there was a failure
// or if the FlipHeaderBlock was invalid.
bool FlipHeadersToHttpResponse(const flip::FlipHeaderBlock& headers,
                               HttpResponseInfo* response) {
  std::string version;
  std::string status;

  // The "status" and "version" headers are required.
  flip::FlipHeaderBlock::const_iterator it;
  it = headers.find("status");
  if (it == headers.end()) {
    LOG(ERROR) << "FlipHeaderBlock without status header.";
    return false;
  }
  status = it->second;

  // Grab the version.  If not provided by the server,
  it = headers.find("version");
  if (it == headers.end()) {
    LOG(ERROR) << "FlipHeaderBlock without version header.";
    return false;
  }
  version = it->second;

  std::string raw_headers(version);
  raw_headers.push_back(' ');
  raw_headers.append(status);
  raw_headers.push_back('\0');
  for (it = headers.begin(); it != headers.end(); ++it) {
    // For each value, if the server sends a NUL-separated
    // list of values, we separate that back out into
    // individual headers for each value in the list.
    // e.g.
    //    Set-Cookie "foo\0bar"
    // becomes
    //    Set-Cookie: foo\0
    //    Set-Cookie: bar\0
    std::string value = it->second;
    size_t start = 0;
    size_t end = 0;
    do {
      end = value.find('\0', start);
      std::string tval;
      if (end != value.npos)
        tval = value.substr(start, (end - start));
      else
        tval = value.substr(start);
      raw_headers.append(it->first);
      raw_headers.push_back(':');
      raw_headers.append(tval);
      raw_headers.push_back('\0');
      start = end + 1;
    } while (end != value.npos);
  }

  response->headers = new HttpResponseHeaders(raw_headers);
  response->was_fetched_via_spdy = true;
  return true;
}

// Create a FlipHeaderBlock for a Flip SYN_STREAM Frame from
// a HttpRequestInfo block.
void CreateFlipHeadersFromHttpRequest(
    const HttpRequestInfo& info, flip::FlipHeaderBlock* headers) {
  static const char kHttpProtocolVersion[] = "HTTP/1.1";

  HttpUtil::HeadersIterator it(info.extra_headers.begin(),
                               info.extra_headers.end(),
                               "\r\n");
  while (it.GetNext()) {
    std::string name = StringToLowerASCII(it.name());
    if (headers->find(name) == headers->end()) {
      (*headers)[name] = it.values();
    } else {
      std::string new_value = (*headers)[name];
      new_value += "\0";
      new_value += it.values();
      (*headers)[name] = new_value;
    }
  }

  // TODO(mbelshe): Add Proxy headers here. (See http_network_transaction.cc)
  // TODO(mbelshe): Add authentication headers here.

  (*headers)["method"] = info.method;
  (*headers)["url"] = info.url.spec();
  (*headers)["version"] = kHttpProtocolVersion;
  if (info.user_agent.length())
    (*headers)["user-agent"] = info.user_agent;
  if (!info.referrer.is_empty())
    (*headers)["referer"] = info.referrer.spec();

  // Honor load flags that impact proxy caches.
  if (info.load_flags & LOAD_BYPASS_CACHE) {
    (*headers)["pragma"] = "no-cache";
    (*headers)["cache-control"] = "no-cache";
  } else if (info.load_flags & LOAD_VALIDATE_CACHE) {
    (*headers)["cache-control"] = "max-age=0";
  }
}

void AdjustSocketBufferSizes(ClientSocket* socket) {
  // Adjust socket buffer sizes.
  // FLIP uses one socket, and we want a really big buffer.
  // This greatly helps on links with packet loss - we can even
  // outperform Vista's dynamic window sizing algorithm.
  // TODO(mbelshe): more study.
  const int kSocketBufferSize = 512 * 1024;
  socket->SetReceiveBufferSize(kSocketBufferSize);
  socket->SetSendBufferSize(kSocketBufferSize);
}

}  // namespace

// static
bool FlipSession::use_ssl_ = true;

FlipSession::FlipSession(const std::string& host, HttpNetworkSession* session)
    : ALLOW_THIS_IN_INITIALIZER_LIST(
          connect_callback_(this, &FlipSession::OnTCPConnect)),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          ssl_connect_callback_(this, &FlipSession::OnSSLConnect)),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          read_callback_(this, &FlipSession::OnReadComplete)),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          write_callback_(this, &FlipSession::OnWriteComplete)),
      domain_(host),
      session_(session),
      connection_(new ClientSocketHandle),
      read_buffer_(new IOBuffer(kReadBufferSize)),
      read_pending_(false),
      stream_hi_water_mark_(1),  // Always start at 1 for the first stream id.
      write_pending_(false),
      delayed_write_pending_(false),
      is_secure_(false),
      error_(OK),
      state_(IDLE),
      streams_initiated_count_(0),
      streams_pushed_count_(0),
      streams_pushed_and_claimed_count_(0),
      streams_abandoned_count_(0) {
  // TODO(mbelshe): consider randomization of the stream_hi_water_mark.

  flip_framer_.set_visitor(this);

  session_->ssl_config_service()->GetSSLConfig(&ssl_config_);

  // TODO(agl): This is a temporary hack for testing reasons. In the medium
  // term we'll want to use NPN for all HTTPS connections and use the protocol
  // suggested.
  //
  // In the event that the server supports Next Protocol Negotiation, but
  // doesn't support either of these protocols, we'll request the first
  // protocol in the list. Because of that, HTTP is listed first because it's
  // what we'll actually fallback to in the case that the server doesn't
  // support SPDY.
  ssl_config_.next_protos = "\007http1.1\004spdy";
}

FlipSession::~FlipSession() {
  // Cleanup all the streams.
  CloseAllStreams(net::ERR_ABORTED);

  if (connection_->is_initialized()) {
    // With Flip we can't recycle sockets.
    connection_->socket()->Disconnect();
  }

  // TODO(willchan): Don't hardcode port 80 here.
  DCHECK(!session_->flip_session_pool()->HasSession(
      HostResolver::RequestInfo(domain_, 80)));

  // Record per-session histograms here.
  UMA_HISTOGRAM_CUSTOM_COUNTS("Net.SpdyStreamsPerSession",
      streams_initiated_count_,
      0, 300, 50);
  UMA_HISTOGRAM_CUSTOM_COUNTS("Net.SpdyStreamsPushedPerSession",
      streams_pushed_count_,
      0, 300, 50);
  UMA_HISTOGRAM_CUSTOM_COUNTS("Net.SpdyStreamsPushedAndClaimedPerSession",
      streams_pushed_and_claimed_count_,
      0, 300, 50);
  UMA_HISTOGRAM_CUSTOM_COUNTS("Net.SpdyStreamsAbandonedPerSession",
      streams_abandoned_count_,
      0, 300, 50);
}

void FlipSession::InitializeWithSocket(ClientSocketHandle* connection) {
  static StatsCounter flip_sessions("flip.sessions");
  flip_sessions.Increment();

  AdjustSocketBufferSizes(connection->socket());

  state_ = CONNECTED;
  connection_.reset(connection);

  // This is a newly initialized session that no client should have a handle to
  // yet, so there's no need to start writing data as in OnTCPConnect(), but we
  // should start reading data.
  ReadSocket();
}

net::Error FlipSession::Connect(const std::string& group_name,
                                const HostResolver::RequestInfo& host,
                                RequestPriority priority,
                                LoadLog* load_log) {
  DCHECK(priority >= FLIP_PRIORITY_HIGHEST && priority <= FLIP_PRIORITY_LOWEST);

  // If the connect process is started, let the caller continue.
  if (state_ > IDLE)
    return net::OK;

  state_ = CONNECTING;

  static StatsCounter flip_sessions("flip.sessions");
  flip_sessions.Increment();

  int rv = connection_->Init(group_name, host, priority, &connect_callback_,
                            session_->tcp_socket_pool(), load_log);
  DCHECK(rv <= 0);

  // If the connect is pending, we still return ok.  The APIs enqueue
  // work until after the connect completes asynchronously later.
  if (rv == net::ERR_IO_PENDING)
    return net::OK;
  return static_cast<net::Error>(rv);
}

scoped_refptr<FlipStream> FlipSession::GetOrCreateStream(
    const HttpRequestInfo& request,
    const UploadDataStream* upload_data,
    LoadLog* log) {
  const GURL& url = request.url;
  const std::string& path = url.PathForRequest();

  scoped_refptr<FlipStream> stream;

  // Check if we have a push stream for this path.
  if (request.method == "GET") {
    stream = GetPushStream(path);
    if (stream) {
      DCHECK(streams_pushed_and_claimed_count_ < streams_pushed_count_);
      streams_pushed_and_claimed_count_++;
      return stream;
    }
  }

  // Check if we have a pending push stream for this url.
  PendingStreamMap::iterator it;
  it = pending_streams_.find(path);
  if (it != pending_streams_.end()) {
    DCHECK(!it->second);
    // Server will assign a stream id when the push stream arrives.  Use 0 for
    // now.
    LoadLog::AddEvent(log, LoadLog::TYPE_FLIP_STREAM_ADOPTED_PUSH_STREAM);
    FlipStream* stream = new FlipStream(this, 0, true, log);
    stream->set_path(path);
    it->second = stream;
    return it->second;
  }

  const flip::FlipStreamId stream_id = GetNewStreamId();

  // If we still don't have a stream, activate one now.
  stream = new FlipStream(this, stream_id, false, log);
  stream->set_priority(request.priority);
  stream->set_path(path);
  ActivateStream(stream);

  UMA_HISTOGRAM_CUSTOM_COUNTS("Net.SpdyPriorityCount",
      static_cast<int>(request.priority), 0, 10, 11);

  LOG(INFO) << "FlipStream: Creating stream " << stream_id << " for " << url;

  // TODO(mbelshe): Optimize memory allocations
  DCHECK(request.priority >= FLIP_PRIORITY_HIGHEST &&
         request.priority <= FLIP_PRIORITY_LOWEST);

  // Convert from HttpRequestHeaders to Flip Headers.
  flip::FlipHeaderBlock headers;
  CreateFlipHeadersFromHttpRequest(request, &headers);

  flip::FlipControlFlags flags = flip::CONTROL_FLAG_NONE;
  if (!request.upload_data || !upload_data->size())
    flags = flip::CONTROL_FLAG_FIN;

  // Create a SYN_STREAM packet and add to the output queue.
  scoped_ptr<flip::FlipSynStreamControlFrame> syn_frame(
      flip_framer_.CreateSynStream(stream_id, request.priority, flags, false,
                                   &headers));
  int length = flip::FlipFrame::size() + syn_frame->length();
  IOBuffer* buffer = new IOBuffer(length);
  memcpy(buffer->data(), syn_frame->data(), length);
  queue_.push(FlipIOBuffer(buffer, length, request.priority, stream));

  static StatsCounter flip_requests("flip.requests");
  flip_requests.Increment();

  LOG(INFO) << "FETCHING: " << request.url.spec();
  streams_initiated_count_++;

  LOG(INFO) << "FLIP SYN_STREAM HEADERS ----------------------------------";
  DumpFlipHeaders(headers);

  // Schedule to write to the socket after we've made it back
  // to the message loop so that we can aggregate multiple
  // requests.
  // TODO(mbelshe): Should we do the "first" request immediately?
  //                maybe we should only 'do later' for subsequent
  //                requests.
  WriteSocketLater();

  return stream;
}

int FlipSession::WriteStreamData(flip::FlipStreamId stream_id,
                                 net::IOBuffer* data, int len) {
  LOG(INFO) << "Writing Stream Data for stream " << stream_id << " (" << len
            << " bytes)";
  const int kMss = 1430;  // This is somewhat arbitrary and not really fixed,
                          // but it will always work reasonably with ethernet.
  // Chop the world into 2-packet chunks.  This is somewhat arbitrary, but
  // is reasonably small and ensures that we elicit ACKs quickly from TCP
  // (because TCP tries to only ACK every other packet).
  const int kMaxFlipFrameChunkSize = (2 * kMss) - flip::FlipFrame::size();

  // Find our stream
  DCHECK(IsStreamActive(stream_id));
  scoped_refptr<FlipStream> stream = active_streams_[stream_id];
  CHECK(stream->stream_id() == stream_id);
  if (!stream)
    return ERR_INVALID_FLIP_STREAM;

  // TODO(mbelshe):  Setting of the FIN is assuming that the caller will pass
  //                 all data to write in a single chunk.  Is this always true?

  // Set the flags on the upload.
  flip::FlipDataFlags flags = flip::DATA_FLAG_FIN;
  if (len > kMaxFlipFrameChunkSize) {
    len = kMaxFlipFrameChunkSize;
    flags = flip::DATA_FLAG_NONE;
  }

  // TODO(mbelshe): reduce memory copies here.
  scoped_ptr<flip::FlipDataFrame> frame(
      flip_framer_.CreateDataFrame(stream_id, data->data(), len, flags));
  int length = flip::FlipFrame::size() + frame->length();
  IOBufferWithSize* buffer = new IOBufferWithSize(length);
  memcpy(buffer->data(), frame->data(), length);
  queue_.push(FlipIOBuffer(buffer, length, stream->priority(), stream));

  // Whenever we queue onto the socket we need to ensure that we will write to
  // it later.
  WriteSocketLater();

  return ERR_IO_PENDING;
}

bool FlipSession::CancelStream(flip::FlipStreamId stream_id) {
  LOG(INFO) << "Cancelling stream " << stream_id;
  if (!IsStreamActive(stream_id))
    return false;

  // TODO(mbelshe): We should send a FIN_STREAM control frame here
  //                so that the server can cancel a large send.

  // TODO(mbelshe): Write a method for tearing down a stream
  //                that cleans it out of the active list, the pending list,
  //                etc.
  scoped_refptr<FlipStream> stream = active_streams_[stream_id];
  DeactivateStream(stream_id);
  return true;
}

bool FlipSession::IsStreamActive(flip::FlipStreamId stream_id) const {
  return ContainsKey(active_streams_, stream_id);
}

LoadState FlipSession::GetLoadState() const {
  // NOTE: The application only queries the LoadState via the
  //       FlipNetworkTransaction, and details are only needed when
  //       we're in the process of connecting.

  // If we're connecting, defer to the connection to give us the actual
  // LoadState.
  if (state_ == CONNECTING)
    return connection_->GetLoadState();

  // Just report that we're idle since the session could be doing
  // many things concurrently.
  return LOAD_STATE_IDLE;
}

void FlipSession::OnTCPConnect(int result) {
  LOG(INFO) << "Flip socket connected (result=" << result << ")";

  // We shouldn't be coming through this path if we didn't just open a fresh
  // socket (or have an error trying to do so).
  DCHECK(!connection_->socket() || !connection_->is_reused());

  UpdateConnectionTypeHistograms(CONNECTION_SPDY, result >= 0);

  if (result != net::OK) {
    DCHECK_LT(result, 0);
    CloseSessionOnError(static_cast<net::Error>(result));
    return;
  }

  AdjustSocketBufferSizes(connection_->socket());

  if (use_ssl_) {
    // Add a SSL socket on top of our existing transport socket.
    ClientSocket* socket = connection_->release_socket();
    // TODO(mbelshe): Fix the hostname.  This is BROKEN without having
    //                a real hostname.
    socket = session_->socket_factory()->CreateSSLClientSocket(
        socket, "" /* request_->url.HostNoBrackets() */ , ssl_config_);
    connection_->set_socket(socket);
    is_secure_ = true;
    // TODO(willchan): Plumb LoadLog into FLIP code.
    int status = connection_->socket()->Connect(&ssl_connect_callback_, NULL);
    if (status != ERR_IO_PENDING)
      OnSSLConnect(status);
  } else {
    DCHECK_EQ(state_, CONNECTING);
    state_ = CONNECTED;

    // Make sure we get any pending data sent.
    WriteSocketLater();
    // Start reading
    ReadSocket();
  }
}

void FlipSession::OnSSLConnect(int result) {
  // TODO(mbelshe): We need to replicate the functionality of
  //   HttpNetworkTransaction::DoSSLConnectComplete here, where it calls
  //   HandleCertificateError() and such.
  if (IsCertificateError(result))
    result = OK;   // TODO(mbelshe): pretend we're happy anyway.

  if (result == OK) {
    DCHECK_EQ(state_, CONNECTING);
    state_ = CONNECTED;

    // After we've connected, send any data to the server, and then issue
    // our read.
    WriteSocketLater();
    ReadSocket();
  } else {
    DCHECK_LT(result, 0);  // It should be an error, not a byte count.
    CloseSessionOnError(static_cast<net::Error>(result));
  }
}

void FlipSession::OnReadComplete(int bytes_read) {
  // Parse a frame.  For now this code requires that the frame fit into our
  // buffer (32KB).
  // TODO(mbelshe): support arbitrarily large frames!

  LOG(INFO) << "Flip socket read: " << bytes_read << " bytes";

  read_pending_ = false;

  if (bytes_read <= 0) {
    // Session is tearing down.
    net::Error error = static_cast<net::Error>(bytes_read);
    if (error == OK)
      error = ERR_CONNECTION_CLOSED;
    CloseSessionOnError(error);
    return;
  }

  // The FlipFramer will use callbacks onto |this| as it parses frames.
  // When errors occur, those callbacks can lead to teardown of all references
  // to |this|, so maintain a reference to self during this call for safe
  // cleanup.
  scoped_refptr<FlipSession> self(this);

  char *data = read_buffer_->data();
  while (bytes_read &&
         flip_framer_.error_code() == flip::FlipFramer::FLIP_NO_ERROR) {
    uint32 bytes_processed = flip_framer_.ProcessInput(data, bytes_read);
    bytes_read -= bytes_processed;
    data += bytes_processed;
    if (flip_framer_.state() == flip::FlipFramer::FLIP_DONE)
      flip_framer_.Reset();
  }

  if (state_ != CLOSED)
    ReadSocket();
}

void FlipSession::OnWriteComplete(int result) {
  DCHECK(write_pending_);
  DCHECK(in_flight_write_.size());
  DCHECK(result != 0);  // This shouldn't happen for write.

  write_pending_ = false;

  LOG(INFO) << "Flip write complete (result=" << result << ") for stream: "
            << in_flight_write_.stream()->stream_id();

  if (result >= 0) {
    // It should not be possible to have written more bytes than our
    // in_flight_write_.
    DCHECK_LE(result, in_flight_write_.buffer()->BytesRemaining());

    in_flight_write_.buffer()->DidConsume(result);

    // We only notify the stream when we've fully written the pending frame.
    if (!in_flight_write_.buffer()->BytesRemaining()) {
      scoped_refptr<FlipStream> stream = in_flight_write_.stream();
      DCHECK(stream.get());

      // Report the number of bytes written to the caller, but exclude the
      // frame size overhead.  NOTE:  if this frame was compressed the reported
      // bytes written is the compressed size, not the original size.
      if (result > 0) {
        result = in_flight_write_.buffer()->size();
        DCHECK_GT(result, static_cast<int>(flip::FlipFrame::size()));
        result -= static_cast<int>(flip::FlipFrame::size());
      }

      // It is possible that the stream was cancelled while we were writing
      // to the socket.
      if (!stream->cancelled())
        stream->OnWriteComplete(result);

      // Cleanup the write which just completed.
      in_flight_write_.release();
    }

    // Write more data.  We're already in a continuation, so we can
    // go ahead and write it immediately (without going back to the
    // message loop).
    WriteSocketLater();
  } else {
    in_flight_write_.release();

    // The stream is now errored.  Close it down.
    CloseSessionOnError(static_cast<net::Error>(result));
  }
}

void FlipSession::ReadSocket() {
  if (read_pending_)
    return;

  if (state_ == CLOSED) {
    NOTREACHED();
    return;
  }

  CHECK(connection_.get());
  CHECK(connection_->socket());
  int bytes_read = connection_->socket()->Read(read_buffer_.get(),
                                               kReadBufferSize,
                                               &read_callback_);
  switch (bytes_read) {
    case 0:
      // Socket is closed!
      // TODO(mbelshe): Need to abort any active streams here.
      DCHECK(!active_streams_.size());
      return;
    case net::ERR_IO_PENDING:
      // Waiting for data.  Nothing to do now.
      read_pending_ = true;
      return;
    default:
      // Data was read, process it.
      // Schedule the work through the message loop to avoid recursive
      // callbacks.
      read_pending_ = true;
      MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &FlipSession::OnReadComplete, bytes_read));
      break;
  }
}

void FlipSession::WriteSocketLater() {
  if (delayed_write_pending_)
    return;

  delayed_write_pending_ = true;
  MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &FlipSession::WriteSocket));
}

void FlipSession::WriteSocket() {
  // This function should only be called via WriteSocketLater.
  DCHECK(delayed_write_pending_);
  delayed_write_pending_ = false;

  // If the socket isn't connected yet, just wait; we'll get called
  // again when the socket connection completes.  If the socket is
  // closed, just return.
  if (state_ < CONNECTED || state_ == CLOSED)
    return;

  if (write_pending_)   // Another write is in progress still.
    return;

  // Loop sending frames until we've sent everything or until the write
  // returns error (or ERR_IO_PENDING).
  while (in_flight_write_.buffer() || queue_.size()) {
    if (!in_flight_write_.buffer()) {
      // Grab the next FlipFrame to send.
      FlipIOBuffer next_buffer = queue_.top();
      queue_.pop();

      // We've deferred compression until just before we write it to the socket,
      // which is now.  At this time, we don't compress our data frames.
      flip::FlipFrame uncompressed_frame(next_buffer.buffer()->data(), false);
      size_t size;
      if (uncompressed_frame.is_control_frame()) {
        scoped_ptr<flip::FlipFrame> compressed_frame(
            flip_framer_.CompressFrame(&uncompressed_frame));
        size = compressed_frame->length() + flip::FlipFrame::size();

        DCHECK(size > 0);

        // TODO(mbelshe): We have too much copying of data here.
        IOBufferWithSize* buffer = new IOBufferWithSize(size);
        memcpy(buffer->data(), compressed_frame->data(), size);

        // Attempt to send the frame.
        in_flight_write_ = FlipIOBuffer(buffer, size, 0, next_buffer.stream());
      } else {
        size = uncompressed_frame.length() + flip::FlipFrame::size();
        in_flight_write_ = next_buffer;
      }
    } else {
      DCHECK(in_flight_write_.buffer()->BytesRemaining());
    }

    write_pending_ = true;
    int rv = connection_->socket()->Write(in_flight_write_.buffer(),
        in_flight_write_.buffer()->BytesRemaining(), &write_callback_);
    if (rv == net::ERR_IO_PENDING)
      break;

    // We sent the frame successfully.
    OnWriteComplete(rv);

    // TODO(mbelshe):  Test this error case.  Maybe we should mark the socket
    //                 as in an error state.
    if (rv < 0)
      break;
  }
}

void FlipSession::CloseAllStreams(net::Error code) {
  LOG(INFO) << "Closing all FLIP Streams";

  static StatsCounter abandoned_streams("flip.abandoned_streams");
  static StatsCounter abandoned_push_streams("flip.abandoned_push_streams");

  if (active_streams_.size()) {
    abandoned_streams.Add(active_streams_.size());

    // Create a copy of the list, since aborting streams can invalidate
    // our list.
    FlipStream** list = new FlipStream*[active_streams_.size()];
    ActiveStreamMap::const_iterator it;
    int index = 0;
    for (it = active_streams_.begin(); it != active_streams_.end(); ++it)
      list[index++] = it->second;

    // Issue the aborts.
    for (--index; index >= 0; index--) {
      LOG(ERROR) << "ABANDONED (stream_id=" << list[index]->stream_id()
                 << "): " << list[index]->path();
      list[index]->OnClose(code);
    }

    // Clear out anything pending.
    active_streams_.clear();

    delete[] list;
  }

  if (pushed_streams_.size()) {
    streams_abandoned_count_ += pushed_streams_.size();
    abandoned_push_streams.Add(pushed_streams_.size());
    pushed_streams_.clear();
  }
}

int FlipSession::GetNewStreamId() {
  int id = stream_hi_water_mark_;
  stream_hi_water_mark_ += 2;
  if (stream_hi_water_mark_ > 0x7fff)
    stream_hi_water_mark_ = 1;
  return id;
}

void FlipSession::CloseSessionOnError(net::Error err) {
  DCHECK_LT(err, OK);
  LOG(INFO) << "Flip::CloseSessionOnError(" << err << ")";

  // Don't close twice.  This can occur because we can have both
  // a read and a write outstanding, and each can complete with
  // an error.
  if (state_ != CLOSED) {
    state_ = CLOSED;
    error_ = err;
    CloseAllStreams(err);
    session_->flip_session_pool()->Remove(this);
  }
}

void FlipSession::ActivateStream(FlipStream* stream) {
  const flip::FlipStreamId id = stream->stream_id();
  DCHECK(!IsStreamActive(id));

  active_streams_[id] = stream;
}

void FlipSession::DeactivateStream(flip::FlipStreamId id) {
  DCHECK(IsStreamActive(id));

  // Verify it is not on the pushed_streams_ list.
  ActiveStreamList::iterator it;
  for (it = pushed_streams_.begin(); it != pushed_streams_.end(); ++it) {
    scoped_refptr<FlipStream> curr = *it;
    if (id == curr->stream_id()) {
      pushed_streams_.erase(it);
      break;
    }
  }

  active_streams_.erase(id);
}

scoped_refptr<FlipStream> FlipSession::GetPushStream(const std::string& path) {
  static StatsCounter used_push_streams("flip.claimed_push_streams");

  LOG(INFO) << "Looking for push stream: " << path;

  scoped_refptr<FlipStream> stream;

  // We just walk a linear list here.
  ActiveStreamList::iterator it;
  for (it = pushed_streams_.begin(); it != pushed_streams_.end(); ++it) {
    stream = *it;
    if (path == stream->path()) {
      CHECK(stream->pushed());
      pushed_streams_.erase(it);
      used_push_streams.Increment();
      LOG(INFO) << "Push Stream Claim for: " << path;
      break;
    }
  }

  return stream;
}

void FlipSession::GetSSLInfo(SSLInfo* ssl_info) {
  if (is_secure_) {
    SSLClientSocket* ssl_socket =
        reinterpret_cast<SSLClientSocket*>(connection_->socket());
    ssl_socket->GetSSLInfo(ssl_info);
  }
}

void FlipSession::OnError(flip::FlipFramer* framer) {
  LOG(ERROR) << "FlipSession error: " << framer->error_code();
  CloseSessionOnError(net::ERR_FLIP_PROTOCOL_ERROR);
}

void FlipSession::OnStreamFrameData(flip::FlipStreamId stream_id,
                                    const char* data,
                                    size_t len) {
  LOG(INFO) << "Flip data for stream " << stream_id << ", " << len << " bytes";
  bool valid_stream = IsStreamActive(stream_id);
  if (!valid_stream) {
    // NOTE:  it may just be that the stream was cancelled.
    LOG(WARNING) << "Received data frame for invalid stream " << stream_id;
    return;
  }

  scoped_refptr<FlipStream> stream = active_streams_[stream_id];
  bool success = stream->OnDataReceived(data, len);
  // |len| == 0 implies a closed stream.
  if (!success || !len)
    DeactivateStream(stream_id);
}

void FlipSession::OnSyn(const flip::FlipSynStreamControlFrame* frame,
                        const flip::FlipHeaderBlock* headers) {
  flip::FlipStreamId stream_id = frame->stream_id();

  // Server-initiated streams should have even sequence numbers.
  if ((stream_id & 0x1) != 0) {
    LOG(ERROR) << "Received invalid OnSyn stream id " << stream_id;
    return;
  }

  if (IsStreamActive(stream_id)) {
    LOG(ERROR) << "Received OnSyn for active stream " << stream_id;
    return;
  }

  streams_pushed_count_++;

  LOG(INFO) << "FlipSession: Syn received for stream: " << stream_id;

  LOG(INFO) << "FLIP SYN RESPONSE HEADERS -----------------------";
  DumpFlipHeaders(*headers);

  // TODO(mbelshe): DCHECK that this is a GET method?

  const std::string& path = ContainsKey(*headers, "path") ?
      headers->find("path")->second : "";

  // Verify that the response had a URL for us.
  DCHECK(!path.empty());
  if (path.empty()) {
    LOG(WARNING) << "Pushed stream did not contain a path.";
    return;
  }

  scoped_refptr<FlipStream> stream;

  // Check if we already have a delegate awaiting this stream.
  PendingStreamMap::iterator it;
  it = pending_streams_.find(path);
  if (it != pending_streams_.end()) {
    stream = it->second;
    pending_streams_.erase(it);
    if (stream)
      pushed_streams_.push_back(stream);
  } else {
    pushed_streams_.push_back(stream);
  }

  if (stream) {
    CHECK(stream->pushed());
    CHECK(stream->stream_id() == 0);
    stream->set_stream_id(stream_id);
  } else {
    // TODO(mbelshe): can we figure out how to use a LoadLog here?
    stream = new FlipStream(this, stream_id, true, NULL);
  }

  // Activate a stream and parse the headers.
  ActivateStream(stream);

  stream->set_path(path);

  // TODO(mbelshe): For now we convert from our nice hash map back
  // to a string of headers; this is because the HttpResponseInfo
  // is a bit rigid for its http (non-flip) design.
  HttpResponseInfo response;
  if (FlipHeadersToHttpResponse(*headers, &response)) {
    GetSSLInfo(&response.ssl_info);
    stream->OnResponseReceived(response);
  } else {
    stream->OnClose(ERR_INVALID_RESPONSE);
    DeactivateStream(stream_id);
    return;
  }

  LOG(INFO) << "Got pushed stream for " << stream->path();

  static StatsCounter push_requests("flip.pushed_streams");
  push_requests.Increment();
}

void FlipSession::OnSynReply(const flip::FlipSynReplyControlFrame* frame,
                             const flip::FlipHeaderBlock* headers) {
  DCHECK(headers);
  flip::FlipStreamId stream_id = frame->stream_id();
  bool valid_stream = IsStreamActive(stream_id);
  if (!valid_stream) {
    // NOTE:  it may just be that the stream was cancelled.
    LOG(WARNING) << "Received SYN_REPLY for invalid stream " << stream_id;
    return;
  }

  LOG(INFO) << "FLIP SYN_REPLY RESPONSE HEADERS for stream: " << stream_id;
  DumpFlipHeaders(*headers);

  // We record content declared as being pushed so that we don't
  // request a duplicate stream which is already scheduled to be
  // sent to us.
  flip::FlipHeaderBlock::const_iterator it;
  it = headers->find("X-Associated-Content");
  if (it != headers->end()) {
    const std::string& content = it->second;
    std::string::size_type start = 0;
    std::string::size_type end = 0;
    do {
      end = content.find("||", start);
      if (end == std::string::npos)
        end = content.length();
      std::string url = content.substr(start, end - start);
      std::string::size_type pos = url.find("??");
      if (pos == std::string::npos)
        break;
      url = url.substr(pos + 2);
      GURL gurl(url);
      std::string path = gurl.PathForRequest();
      if (path.length())
        pending_streams_[path] = NULL;
      else
        LOG(INFO) << "Invalid X-Associated-Content path: " << url;
      start = end + 2;
    } while (start < content.length());
  }

  scoped_refptr<FlipStream> stream = active_streams_[stream_id];
  CHECK(stream->stream_id() == stream_id);
  CHECK(!stream->cancelled());
  HttpResponseInfo response;
  if (FlipHeadersToHttpResponse(*headers, &response)) {
    GetSSLInfo(&response.ssl_info);
    stream->OnResponseReceived(response);
  } else {
    stream->OnClose(ERR_INVALID_RESPONSE);
    DeactivateStream(stream_id);
  }
}

void FlipSession::OnControl(const flip::FlipControlFrame* frame) {
  flip::FlipHeaderBlock headers;
  uint32 type = frame->type();
  if (type == flip::SYN_STREAM || type == flip::SYN_REPLY) {
    if (!flip_framer_.ParseHeaderBlock(frame, &headers)) {
      LOG(WARNING) << "Could not parse Flip Control Frame Header";
      // TODO(mbelshe):  Error the session?
      return;
    }
  }

  switch (type) {
    case flip::SYN_STREAM:
      LOG(INFO) << "Flip SynStream for stream " << frame->stream_id();
      OnSyn(reinterpret_cast<const flip::FlipSynStreamControlFrame*>(frame),
            &headers);
      break;
    case flip::SYN_REPLY:
      LOG(INFO) << "Flip SynReply for stream " << frame->stream_id();
      OnSynReply(
          reinterpret_cast<const flip::FlipSynReplyControlFrame*>(frame),
          &headers);
      break;
    case flip::FIN_STREAM:
      LOG(INFO) << "Flip Fin for stream " << frame->stream_id();
      OnFin(reinterpret_cast<const flip::FlipFinStreamControlFrame*>(frame));
      break;
    default:
      DCHECK(false);  // Error!
  }
}

void FlipSession::OnFin(const flip::FlipFinStreamControlFrame* frame) {
  flip::FlipStreamId stream_id = frame->stream_id();
  bool valid_stream = IsStreamActive(stream_id);
  if (!valid_stream) {
    // NOTE:  it may just be that the stream was cancelled.
    LOG(WARNING) << "Received FIN for invalid stream" << stream_id;
    return;
  }
  scoped_refptr<FlipStream> stream = active_streams_[stream_id];
  CHECK(stream->stream_id() == stream_id);
  CHECK(!stream->cancelled());
  if (frame->status() == 0) {
    stream->OnDataReceived(NULL, 0);
  } else {
    LOG(ERROR) << "Flip stream closed: " << frame->status();
    // TODO(mbelshe): Map from Flip-protocol errors to something sensical.
    //                For now, it doesn't matter much - it is a protocol error.
    stream->OnClose(ERR_FAILED);
  }

  DeactivateStream(stream_id);
}

}  // namespace net
