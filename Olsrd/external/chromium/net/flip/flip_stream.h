// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FLIP_FLIP_STREAM_H_
#define NET_FLIP_FLIP_STREAM_H_

#include <string>
#include <list>

#include "base/basictypes.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/singleton.h"
#include "net/base/bandwidth_metrics.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/load_log.h"
#include "net/flip/flip_framer.h"
#include "net/flip/flip_protocol.h"

namespace net {

class FlipSession;
class HttpRequestInfo;
class HttpResponseInfo;
class UploadData;
class UploadDataStream;

// The FlipStream is used by the FlipSession to represent each stream known
// on the FlipSession.
// Streams can be created either by the client or by the server.  When they
// are initiated by the client, both the FlipSession and client object (such as
// a FlipNetworkTransaction) will maintain a reference to the stream.  When
// initiated by the server, only the FlipSession will maintain any reference,
// until such a time as a client object requests a stream for the path.
class FlipStream : public base::RefCounted<FlipStream> {
 public:
  // FlipStream constructor
  FlipStream(FlipSession* session, flip::FlipStreamId stream_id, bool pushed,
             LoadLog* log);

  // Ideally I'd use two abstract classes as interfaces for these two sections,
  // but since we're ref counted, I can't make both abstract classes inherit
  // from RefCounted or we'll have two separate ref counts for the same object.
  // TODO(willchan): Consider using linked_ptr here orcreating proxy wrappers
  // for FlipStream to provide the appropriate interface.

  // ===================================================
  // Interface for [Http|Flip]NetworkTransaction to use.

  // Sends the request.  If |upload_data| is non-NULL, sends that in the request
  // body.  |callback| is used when this completes asynchronously.  Note that
  // the actual SYN_STREAM packet will have already been sent by this point.
  // Also note that FlipStream takes ownership of |upload_data|.
  int SendRequest(UploadDataStream* upload_data,
                  HttpResponseInfo* response,
                  CompletionCallback* callback);

  // Reads the response headers.  Returns a net error code.
  int ReadResponseHeaders(CompletionCallback* callback);

  // Reads the response body.  Returns a net error code or the number of bytes
  // read.
  int ReadResponseBody(
      IOBuffer* buf, int buf_len, CompletionCallback* callback);

  // Cancels the stream.  Note that this does not immediately cause deletion of
  // the stream.  This function is used to cancel any callbacks from being
  // invoked.  TODO(willchan): It should also free up any memory associated with
  // the stream, such as IOBuffers.
  void Cancel();

  // Returns the number of bytes uploaded.
  uint64 GetUploadProgress() const;

  const HttpResponseInfo* GetResponseInfo() const;

  // Is this stream a pushed stream from the server.
  bool pushed() const { return pushed_; }

  // =================================
  // Interface for FlipSession to use.

  flip::FlipStreamId stream_id() const { return stream_id_; }
  void set_stream_id(flip::FlipStreamId stream_id) { stream_id_ = stream_id; }

  // For pushed streams, we track a path to identify them.
  const std::string& path() const { return path_; }
  void set_path(const std::string& path) { path_ = path; }

  int priority() const { return priority_; }
  void set_priority(int priority) { priority_ = priority; }

  // Called by the FlipSession when a response (e.g. a SYN_REPLY) has been
  // received for this stream.  |path| is the path of the URL for a server
  // initiated stream, otherwise is empty.
  void OnResponseReceived(const HttpResponseInfo& response);

  // Called by the FlipSession when response data has been received for this
  // stream.  This callback may be called multiple times as data arrives
  // from the network, and will never be called prior to OnResponseReceived.
  // |buffer| contains the data received.  The stream must copy any data
  //          from this buffer before returning from this callback.
  // |length| is the number of bytes received or an error.
  //         A zero-length count does not indicate end-of-stream.
  // Returns true on success and false on error.
  bool OnDataReceived(const char* buffer, int bytes);

  // Called by the FlipSession when a write has completed.  This callback
  // will be called multiple times for each write which completes.  Writes
  // include the SYN_STREAM write and also DATA frame writes.
  // |result| is the number of bytes written or a net error code.
  void OnWriteComplete(int status);

  // Called by the FlipSession when the request is finished.  This callback
  // will always be called at the end of the request and signals to the
  // stream that the stream has no more network events.  No further callbacks
  // to the stream will be made after this call.
  // |status| is an error code or OK.
  void OnClose(int status);

  bool cancelled() const { return cancelled_; }

 private:
  friend class base::RefCounted<FlipStream>;

  enum State {
    STATE_NONE,
    STATE_SEND_HEADERS,
    STATE_SEND_HEADERS_COMPLETE,
    STATE_SEND_BODY,
    STATE_SEND_BODY_COMPLETE,
    STATE_READ_HEADERS,
    STATE_READ_HEADERS_COMPLETE,
    STATE_READ_BODY,
    STATE_READ_BODY_COMPLETE,
    STATE_DONE
  };

  ~FlipStream();

  // Try to make progress sending/receiving the request/response.
  int DoLoop(int result);

  // Call the user callback.
  void DoCallback(int rv);

  // The implementations of each state of the state machine.
  int DoSendHeaders();
  int DoSendHeadersComplete(int result);
  int DoSendBody();
  int DoSendBodyComplete(int result);
  int DoReadHeaders();
  int DoReadHeadersComplete(int result);
  int DoReadBody();
  int DoReadBodyComplete(int result);

  // Update the histograms.  Can safely be called repeatedly, but should only
  // be called after the stream has completed.
  void UpdateHistograms();

  flip::FlipStreamId stream_id_;
  std::string path_;
  int priority_;
  const bool pushed_;
  // We buffer the response body as it arrives asynchronously from the stream.
  // TODO(mbelshe):  is this infinite buffering?
  std::list<scoped_refptr<IOBufferWithSize> > response_body_;
  bool download_finished_;
  ScopedBandwidthMetrics metrics_;

  scoped_refptr<FlipSession> session_;

  HttpResponseInfo* response_;
  scoped_ptr<UploadDataStream> request_body_stream_;

  bool response_complete_;  // TODO(mbelshe): fold this into the io_state.
  State io_state_;

  // Since we buffer the response, we also buffer the response status.
  // Not valid until response_complete_ is true.
  int response_status_;

  CompletionCallback* user_callback_;

  // User provided buffer for the ReadResponseBody() response.
  scoped_refptr<IOBuffer> user_buffer_;
  int user_buffer_len_;

  bool cancelled_;

  scoped_refptr<LoadLog> load_log_;

  base::TimeTicks send_time_;
  base::TimeTicks recv_first_byte_time_;
  base::TimeTicks recv_last_byte_time_;
  int send_bytes_;
  int recv_bytes_;
  bool histograms_recorded_;

  DISALLOW_COPY_AND_ASSIGN(FlipStream);
};

}  // namespace net

#endif  // NET_FLIP_FLIP_STREAM_H_
