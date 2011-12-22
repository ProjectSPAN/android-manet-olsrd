// Copyright (c) 2006-2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_parser.h"

#include "base/compiler_specific.h"
#include "base/trace_event.h"
#include "net/base/io_buffer.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"

namespace net {

HttpStreamParser::HttpStreamParser(ClientSocketHandle* connection,
                                   GrowableIOBuffer* read_buffer,
                                   LoadLog* load_log)
    : io_state_(STATE_NONE),
      request_(NULL),
      request_headers_(NULL),
      request_body_(NULL),
      read_buf_(read_buffer),
      read_buf_unused_offset_(0),
      response_header_start_offset_(-1),
      response_body_length_(-1),
      response_body_read_(0),
      chunked_decoder_(NULL),
      user_read_buf_(NULL),
      user_read_buf_len_(0),
      user_callback_(NULL),
      connection_(connection),
      load_log_(load_log),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          io_callback_(this, &HttpStreamParser::OnIOComplete)) {
  DCHECK_EQ(0, read_buffer->offset());
}

int HttpStreamParser::SendRequest(const HttpRequestInfo* request,
                                  const std::string& headers,
                                  UploadDataStream* request_body,
                                  HttpResponseInfo* response,
                                  CompletionCallback* callback) {
  DCHECK_EQ(STATE_NONE, io_state_);
  DCHECK(!user_callback_);
  DCHECK(callback);
  DCHECK(response);

  request_ = request;
  response_ = response;
  scoped_refptr<StringIOBuffer> headers_io_buf = new StringIOBuffer(headers);
  request_headers_ = new DrainableIOBuffer(headers_io_buf,
                                           headers_io_buf->size());
  request_body_.reset(request_body);

  io_state_ = STATE_SENDING_HEADERS;
  int result = DoLoop(OK);
  if (result == ERR_IO_PENDING)
    user_callback_ = callback;

  return result > 0 ? OK : result;
}

int HttpStreamParser::ReadResponseHeaders(CompletionCallback* callback) {
  DCHECK(io_state_ == STATE_REQUEST_SENT || io_state_ == STATE_DONE);
  DCHECK(!user_callback_);
  DCHECK(callback);

  // This function can be called with io_state_ == STATE_DONE if the
  // connection is closed after seeing just a 1xx response code.
  if (io_state_ == STATE_DONE)
    return ERR_CONNECTION_CLOSED;

  int result = OK;
  io_state_ = STATE_READ_HEADERS;

  if (read_buf_->offset() > 0) {
    // Simulate the state where the data was just read from the socket.
    result = read_buf_->offset() - read_buf_unused_offset_;
    read_buf_->set_offset(read_buf_unused_offset_);
  }
  if (result > 0)
    io_state_ = STATE_READ_HEADERS_COMPLETE;

  result = DoLoop(result);
  if (result == ERR_IO_PENDING)
    user_callback_ = callback;

  return result > 0 ? OK : result;
}

int HttpStreamParser::ReadResponseBody(IOBuffer* buf, int buf_len,
                                      CompletionCallback* callback) {
  DCHECK(io_state_ == STATE_BODY_PENDING || io_state_ == STATE_DONE);
  DCHECK(!user_callback_);
  DCHECK(callback);
  DCHECK_LE(buf_len, kMaxBufSize);

  if (io_state_ == STATE_DONE)
    return OK;

  user_read_buf_ = buf;
  user_read_buf_len_ = buf_len;
  io_state_ = STATE_READ_BODY;

  int result = DoLoop(OK);
  if (result == ERR_IO_PENDING)
    user_callback_ = callback;

  return result;
}

void HttpStreamParser::OnIOComplete(int result) {
  result = DoLoop(result);

  // The client callback can do anything, including destroying this class,
  // so any pending callback must be issued after everything else is done.
  if (result != ERR_IO_PENDING && user_callback_) {
    CompletionCallback* c = user_callback_;
    user_callback_ = NULL;
    c->Run(result);
  }
}

int HttpStreamParser::DoLoop(int result) {
  bool can_do_more = true;
  do {
    switch (io_state_) {
      case STATE_SENDING_HEADERS:
        TRACE_EVENT_BEGIN("http.write_headers", request_, request_->url.spec());
        if (result < 0)
          can_do_more = false;
        else
          result = DoSendHeaders(result);
        TRACE_EVENT_END("http.write_headers", request_, request_->url.spec());
        break;
      case STATE_SENDING_BODY:
        TRACE_EVENT_BEGIN("http.write_body", request_, request_->url.spec());
        if (result < 0)
          can_do_more = false;
        else
          result = DoSendBody(result);
        TRACE_EVENT_END("http.write_body", request_, request_->url.spec());
        break;
      case STATE_REQUEST_SENT:
        DCHECK(result != ERR_IO_PENDING);
        can_do_more = false;
        break;
      case STATE_READ_HEADERS:
        TRACE_EVENT_BEGIN("http.read_headers", request_, request_->url.spec());
        LoadLog::BeginEvent(load_log_,
                            LoadLog::TYPE_HTTP_STREAM_PARSER_READ_HEADERS);
        result = DoReadHeaders();
        break;
      case STATE_READ_HEADERS_COMPLETE:
        result = DoReadHeadersComplete(result);
        LoadLog::EndEvent(load_log_,
                          LoadLog::TYPE_HTTP_STREAM_PARSER_READ_HEADERS);
        TRACE_EVENT_END("http.read_headers", request_, request_->url.spec());
        break;
      case STATE_BODY_PENDING:
        DCHECK(result != ERR_IO_PENDING);
        can_do_more = false;
        break;
      case STATE_READ_BODY:
        TRACE_EVENT_BEGIN("http.read_body", request_, request_->url.spec());
        result = DoReadBody();
        // DoReadBodyComplete handles error conditions.
        break;
      case STATE_READ_BODY_COMPLETE:
        result = DoReadBodyComplete(result);
        TRACE_EVENT_END("http.read_body", request_, request_->url.spec());
        break;
      case STATE_DONE:
        DCHECK(result != ERR_IO_PENDING);
        can_do_more = false;
        break;
      default:
        NOTREACHED();
        can_do_more = false;
        break;
    }
  } while (result != ERR_IO_PENDING && can_do_more);

  return result;
}

int HttpStreamParser::DoSendHeaders(int result) {
  request_headers_->DidConsume(result);

  if (request_headers_->BytesRemaining() > 0) {
    // Record our best estimate of the 'request time' as the time when we send
    // out the first bytes of the request headers.
    if (request_headers_->BytesRemaining() == request_headers_->size()) {
      response_->request_time = base::Time::Now();
    }
    result = connection_->socket()->Write(request_headers_,
                                          request_headers_->BytesRemaining(),
                                          &io_callback_);
  } else if (request_body_ != NULL && request_body_->size()) {
    io_state_ = STATE_SENDING_BODY;
    result = OK;
  } else {
    io_state_ = STATE_REQUEST_SENT;
  }
  return result;
}

int HttpStreamParser::DoSendBody(int result) {
  if (result > 0)
    request_body_->DidConsume(result);

  if (request_body_->position() < request_body_->size()) {
    int buf_len = static_cast<int>(request_body_->buf_len());
    result = connection_->socket()->Write(request_body_->buf(), buf_len,
                                          &io_callback_);
  } else {
    io_state_ = STATE_REQUEST_SENT;
  }
  return result;
}

int HttpStreamParser::DoReadHeaders() {
  io_state_ = STATE_READ_HEADERS_COMPLETE;

  // Grow the read buffer if necessary.
  if (read_buf_->RemainingCapacity() == 0)
    read_buf_->SetCapacity(read_buf_->capacity() + kHeaderBufInitialSize);

  // http://crbug.com/16371: We're seeing |user_buf_->data()| return NULL.
  // See if the user is passing in an IOBuffer with a NULL |data_|.
  CHECK(read_buf_->data());

  return connection_->socket()->Read(read_buf_,
                                     read_buf_->RemainingCapacity(),
                                     &io_callback_);
}

int HttpStreamParser::DoReadHeadersComplete(int result) {
  if (result == 0)
    result = ERR_CONNECTION_CLOSED;

  if (result < 0 && result != ERR_CONNECTION_CLOSED) {
    io_state_ = STATE_DONE;
    return result;
  }
  if (result == ERR_CONNECTION_CLOSED && read_buf_->offset() == 0 &&
      connection_->ShouldResendFailedRequest(result)) {
    io_state_ = STATE_DONE;
    return result;
  }

  // Record our best estimate of the 'response time' as the time when we read
  // the first bytes of the response headers.
  if (read_buf_->offset() == 0 && result != ERR_CONNECTION_CLOSED)
    response_->response_time = base::Time::Now();

  if (result == ERR_CONNECTION_CLOSED) {
    // The connection closed before we detected the end of the headers.
    // parse things as well as we can and let the caller decide what to do.
    if (read_buf_->offset() == 0) {
      // The connection was closed before any data was sent. Likely an error
      // rather than empty HTTP/0.9 response.
      io_state_ = STATE_DONE;
      return ERR_EMPTY_RESPONSE;
    } else {
      int end_offset;
      if (response_header_start_offset_ >= 0) {
        io_state_ = STATE_READ_BODY_COMPLETE;
        end_offset = read_buf_->offset();
      } else {
        io_state_ = STATE_BODY_PENDING;
        end_offset = 0;
      }
      DoParseResponseHeaders(end_offset);
      return result;
    }
  }

  read_buf_->set_offset(read_buf_->offset() + result);
  DCHECK_LE(read_buf_->offset(), read_buf_->capacity());
  DCHECK(result >= 0);

  int end_of_header_offset = ParseResponseHeaders();
  if (end_of_header_offset == -1) {
    io_state_ = STATE_READ_HEADERS;
    // Prevent growing the headers buffer indefinitely.
    if (read_buf_->offset() - read_buf_unused_offset_ >= kMaxHeaderBufSize) {
      io_state_ = STATE_DONE;
      return ERR_RESPONSE_HEADERS_TOO_BIG;
    }
  } else {
    // Note where the headers stop.
    read_buf_unused_offset_ = end_of_header_offset;

    if (response_->headers->response_code() / 100 == 1) {
      // After processing a 1xx response, the caller will ask for the next
      // header, so reset state to support that.  We don't just skip these
      // completely because 1xx codes aren't acceptable when establishing a
      // tunnel.
      io_state_ = STATE_REQUEST_SENT;
      response_header_start_offset_ = -1;
    } else {
      io_state_ = STATE_BODY_PENDING;
      CalculateResponseBodySize();
      // If the body is 0, the caller may not call ReadResponseBody, which
      // is where any extra data is copied to read_buf_, so we move the
      // data here and transition to DONE.
      if (response_body_length_ == 0) {
        io_state_ = STATE_DONE;
        int extra_bytes = read_buf_->offset() - read_buf_unused_offset_;
        if (extra_bytes) {
          CHECK(extra_bytes > 0);
          memmove(read_buf_->StartOfBuffer(),
                  read_buf_->StartOfBuffer() + read_buf_unused_offset_,
                  extra_bytes);
        }
        read_buf_->SetCapacity(extra_bytes);
        read_buf_unused_offset_ = 0;
        return OK;
      }
    }
  }
  return result;
}

int HttpStreamParser::DoReadBody() {
  io_state_ = STATE_READ_BODY_COMPLETE;

  // There may be some data left over from reading the response headers.
  if (read_buf_->offset()) {
    int available = read_buf_->offset() - read_buf_unused_offset_;
    if (available) {
      CHECK(available > 0);
      int bytes_from_buffer = std::min(available, user_read_buf_len_);
      memcpy(user_read_buf_->data(),
             read_buf_->StartOfBuffer() + read_buf_unused_offset_,
             bytes_from_buffer);
      read_buf_unused_offset_ += bytes_from_buffer;
      if (bytes_from_buffer == available) {
        read_buf_->SetCapacity(0);
        read_buf_unused_offset_ = 0;
      }
      return bytes_from_buffer;
    } else {
      read_buf_->SetCapacity(0);
      read_buf_unused_offset_ = 0;
    }
  }

  // Check to see if we're done reading.
  if (IsResponseBodyComplete())
    return 0;

  DCHECK_EQ(0, read_buf_->offset());
  return connection_->socket()->Read(user_read_buf_, user_read_buf_len_,
                                     &io_callback_);
}

int HttpStreamParser::DoReadBodyComplete(int result) {
  if (result == 0)
    result = ERR_CONNECTION_CLOSED;

  // Filter incoming data if appropriate.  FilterBuf may return an error.
  if (result > 0 && chunked_decoder_.get()) {
    result = chunked_decoder_->FilterBuf(user_read_buf_->data(), result);
    if (result == 0 && !chunked_decoder_->reached_eof()) {
      // Don't signal completion of the Read call yet or else it'll look like
      // we received end-of-file.  Wait for more data.
      io_state_ = STATE_READ_BODY;
      return OK;
    }
  }

  if (result > 0)
    response_body_read_ += result;

  if (result < 0 || IsResponseBodyComplete()) {
    io_state_ = STATE_DONE;

    // Save the overflow data, which can be in two places.  There may be
    // some left over in |user_read_buf_|, plus there may be more
    // in |read_buf_|.  But the part left over in |user_read_buf_| must have
    // come from the |read_buf_|, so there's room to put it back at the
    // start first.
    int additional_save_amount = read_buf_->offset() - read_buf_unused_offset_;
    int save_amount = 0;
    if (chunked_decoder_.get()) {
      save_amount = chunked_decoder_->bytes_after_eof();
    } else if (response_body_length_ >= 0) {
      int64 extra_data_read = response_body_read_ - response_body_length_;
      if (extra_data_read > 0) {
        save_amount = static_cast<int>(extra_data_read);
        if (result > 0)
          result -= save_amount;
      }
    }

    CHECK(save_amount + additional_save_amount <= kMaxBufSize);
    if (read_buf_->capacity() < save_amount + additional_save_amount) {
      read_buf_->SetCapacity(save_amount + additional_save_amount);
    }

    if (save_amount) {
      memcpy(read_buf_->StartOfBuffer(), user_read_buf_->data() + result,
             save_amount);
    }
    read_buf_->set_offset(save_amount);
    if (additional_save_amount) {
      memmove(read_buf_->data(),
              read_buf_->StartOfBuffer() + read_buf_unused_offset_,
              additional_save_amount);
      read_buf_->set_offset(save_amount + additional_save_amount);
    }
    read_buf_unused_offset_ = 0;
  } else {
    io_state_ = STATE_BODY_PENDING;
    user_read_buf_ = NULL;
    user_read_buf_len_ = 0;
  }

  return result;
}

int HttpStreamParser::ParseResponseHeaders() {
  int end_offset = -1;

  // Look for the start of the status line, if it hasn't been found yet.
  if (response_header_start_offset_ < 0) {
    response_header_start_offset_ = HttpUtil::LocateStartOfStatusLine(
        read_buf_->StartOfBuffer() + read_buf_unused_offset_,
        read_buf_->offset() - read_buf_unused_offset_);
  }

  if (response_header_start_offset_ >= 0) {
    end_offset = HttpUtil::LocateEndOfHeaders(
        read_buf_->StartOfBuffer() + read_buf_unused_offset_,
        read_buf_->offset() - read_buf_unused_offset_,
        response_header_start_offset_);
  } else if (read_buf_->offset() - read_buf_unused_offset_ >= 8) {
    // Enough data to decide that this is an HTTP/0.9 response.
    // 8 bytes = (4 bytes of junk) + "http".length()
    end_offset = 0;
  }

  if (end_offset == -1)
    return -1;

  DoParseResponseHeaders(end_offset);
  return end_offset + read_buf_unused_offset_;
}

void HttpStreamParser::DoParseResponseHeaders(int end_offset) {
  scoped_refptr<HttpResponseHeaders> headers;
  if (response_header_start_offset_ >= 0) {
    headers = new HttpResponseHeaders(HttpUtil::AssembleRawHeaders(
        read_buf_->StartOfBuffer() + read_buf_unused_offset_, end_offset));
  } else {
    // Enough data was read -- there is no status line.
    headers = new HttpResponseHeaders(std::string("HTTP/0.9 200 OK"));
  }

  response_->headers = headers;
  response_->vary_data.Init(*request_, *response_->headers);
}

void HttpStreamParser::CalculateResponseBodySize() {
  // Figure how to determine EOF:

  // For certain responses, we know the content length is always 0. From
  // RFC 2616 Section 4.3 Message Body:
  //
  // For response messages, whether or not a message-body is included with
  // a message is dependent on both the request method and the response
  // status code (section 6.1.1). All responses to the HEAD request method
  // MUST NOT include a message-body, even though the presence of entity-
  // header fields might lead one to believe they do. All 1xx
  // (informational), 204 (no content), and 304 (not modified) responses
  // MUST NOT include a message-body. All other responses do include a
  // message-body, although it MAY be of zero length.
  switch (response_->headers->response_code()) {
    // Note that 1xx was already handled earlier.
    case 204:  // No Content
    case 205:  // Reset Content
    case 304:  // Not Modified
      response_body_length_ = 0;
      break;
  }
  if (request_->method == "HEAD")
    response_body_length_ = 0;

  if (response_body_length_ == -1) {
    // Ignore spurious chunked responses from HTTP/1.0 servers and
    // proxies. Otherwise "Transfer-Encoding: chunked" trumps
    // "Content-Length: N"
    if (response_->headers->GetHttpVersion() >= HttpVersion(1, 1) &&
        response_->headers->HasHeaderValue("Transfer-Encoding", "chunked")) {
      chunked_decoder_.reset(new HttpChunkedDecoder());
    } else {
      response_body_length_ = response_->headers->GetContentLength();
      // If response_body_length_ is still -1, then we have to wait
      // for the server to close the connection.
    }
  }
}

uint64 HttpStreamParser::GetUploadProgress() const {
  if (!request_body_.get())
    return 0;

  return request_body_->position();
}

HttpResponseInfo* HttpStreamParser::GetResponseInfo() {
  return response_;
}

bool HttpStreamParser::IsResponseBodyComplete() const {
  if (chunked_decoder_.get())
    return chunked_decoder_->reached_eof();
  if (response_body_length_ != -1)
    return response_body_read_ >= response_body_length_;

  return false;  // Must read to EOF.
}

bool HttpStreamParser::CanFindEndOfResponse() const {
  return chunked_decoder_.get() || response_body_length_ >= 0;
}

bool HttpStreamParser::IsMoreDataBuffered() const {
  return read_buf_->offset() > read_buf_unused_offset_;
}

}  // namespace net
