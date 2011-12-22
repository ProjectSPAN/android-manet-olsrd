// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_STREAM_SOCKET_STREAM_H_
#define NET_SOCKET_STREAM_SOCKET_STREAM_H_

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "base/linked_ptr.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/task.h"
#include "net/base/address_list.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/http/http_auth.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_auth_handler.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/tcp_client_socket.h"
#include "net/url_request/request_tracker.h"
#include "net/url_request/url_request_context.h"

namespace net {

class AuthChallengeInfo;
class ClientSocketFactory;
class HostResolver;
class LoadLog;
class SSLConfigService;
class SingleRequestHostResolver;
class SocketStreamMetrics;
class SocketStreamThrottle;

// SocketStream is used to implement Web Sockets.
// It provides plain full-duplex stream with proxy and SSL support.
// For proxy authentication, only basic mechanisum is supported.  It will try
// authentication identity for proxy URL first.  If server requires proxy
// authentication, it will try authentication identity for realm that server
// requests.
class SocketStream : public base::RefCountedThreadSafe<SocketStream> {
 public:
  // Derive from this class and add your own data members to associate extra
  // information with a SocketStream.  Use GetUserData(key) and
  // SetUserData(key, data).
  class UserData {
   public:
    UserData() {}
    virtual ~UserData() {}
  };

  class Delegate {
   public:
    virtual ~Delegate() {}

    // Called when socket stream has been connected.  The socket stream accepts
    // at most |max_pending_send_allowed| so that a client of the socket stream
    // should keep track of how much it has pending and shouldn't go over
    // |max_pending_send_allowed| bytes.
    virtual void OnConnected(SocketStream* socket,
                             int max_pending_send_allowed) = 0;

    // Called when |amount_sent| bytes of data are sent.
    virtual void OnSentData(SocketStream* socket,
                            int amount_sent) = 0;

    // Called when |len| bytes of |data| are received.
    virtual void OnReceivedData(SocketStream* socket,
                                const char* data, int len) = 0;

    // Called when the socket stream has been closed.
    virtual void OnClose(SocketStream* socket) = 0;

    // Called when proxy authentication required.
    // The delegate should call RestartWithAuth() if credential for |auth_info|
    // is found in password database, or call Close() to close the connection.
    virtual void OnAuthRequired(SocketStream* socket,
                                AuthChallengeInfo* auth_info) {
      // By default, no credential is available and close the connection.
      socket->Close();
    }

    // Called when an error occured.
    // This is only for error reporting to the delegate.
    // |error| is net::Error.
    virtual void OnError(const SocketStream* socket, int error) {}
  };

  SocketStream(const GURL& url, Delegate* delegate);

  // The user data allows the clients to associate data with this job.
  // Multiple user data values can be stored under different keys.
  // This job will TAKE OWNERSHIP of the given data pointer, and will
  // delete the object if it is changed or the job is destroyed.
  UserData* GetUserData(const void* key) const;
  void SetUserData(const void* key, UserData* data);

  const GURL& url() const { return url_; }
  const AddressList& address_list() const { return addresses_; }
  Delegate* delegate() const { return delegate_; }
  int max_pending_send_allowed() const { return max_pending_send_allowed_; }

  URLRequestContext* context() const { return context_.get(); }
  void set_context(URLRequestContext* context);

  LoadLog* load_log() const { return load_log_; }

  // Opens the connection on the IO thread.
  // Once the connection is established, calls delegate's OnConnected.
  void Connect();

  // Requests to send |len| bytes of |data| on the connection.
  // Returns true if |data| is buffered in the job.
  // Returns false if size of buffered data would exceeds
  // |max_pending_send_allowed_| and |data| is not sent at all.
  bool SendData(const char* data, int len);

  // Requests to close the connection.
  // Once the connection is closed, calls delegate's OnClose.
  void Close();

  // Restarts with authentication info.
  // Should be used for response of OnAuthRequired.
  void RestartWithAuth(
      const std::wstring& username,
      const std::wstring& password);

  // Detach delegate.  Call before delegate is deleted.
  // Once delegate is detached, close the socket stream and never call delegate
  // back.
  void DetachDelegate();

  // Sets an alternative HostResolver. For testing purposes only.
  void SetHostResolver(HostResolver* host_resolver);

  // Sets an alternative ClientSocketFactory.  Doesn't take ownership of
  // |factory|.  For testing purposes only.
  void SetClientSocketFactory(ClientSocketFactory* factory);

 private:
  class RequestHeaders : public IOBuffer {
   public:
    RequestHeaders() : IOBuffer() {}

    void SetDataOffset(size_t offset) {
      data_ = const_cast<char*>(headers_.data()) + offset;
    }

    std::string headers_;

    private:
     ~RequestHeaders() { data_ = NULL; }
  };

  class ResponseHeaders : public IOBuffer {
   public:
    ResponseHeaders() : IOBuffer() {}

    void SetDataOffset(size_t offset) { data_ = headers_.get() + offset; }
    char* headers() const { return headers_.get(); }
    void Reset() { headers_.reset(); }
    void Realloc(size_t new_size);

   private:
     ~ResponseHeaders() { data_ = NULL; }

    scoped_ptr_malloc<char> headers_;
  };

  enum State {
    STATE_NONE,
    STATE_RESOLVE_PROXY,
    STATE_RESOLVE_PROXY_COMPLETE,
    STATE_RESOLVE_HOST,
    STATE_RESOLVE_HOST_COMPLETE,
    STATE_TCP_CONNECT,
    STATE_TCP_CONNECT_COMPLETE,
    STATE_WRITE_TUNNEL_HEADERS,
    STATE_WRITE_TUNNEL_HEADERS_COMPLETE,
    STATE_READ_TUNNEL_HEADERS,
    STATE_READ_TUNNEL_HEADERS_COMPLETE,
    STATE_SOCKS_CONNECT,
    STATE_SOCKS_CONNECT_COMPLETE,
    STATE_SSL_CONNECT,
    STATE_SSL_CONNECT_COMPLETE,
    STATE_READ_WRITE,
    STATE_AUTH_REQUIRED,
    STATE_CLOSE,
  };

  enum ProxyMode {
    kDirectConnection,  // If using a direct connection
    kTunnelProxy,  // If using a tunnel (CONNECT method as HTTPS)
    kSOCKSProxy,  // If using a SOCKS proxy
  };

  typedef std::deque< scoped_refptr<IOBufferWithSize> > PendingDataQueue;
  friend class RequestTracker<SocketStream>;
  friend class base::RefCountedThreadSafe<SocketStream>;
  ~SocketStream();

  friend class WebSocketThrottleTest;

  // Copies the given addrinfo list in |addresses_|.
  // Used for WebSocketThrottleTest.
  void CopyAddrInfo(struct addrinfo* head);

  // Finishes the job.
  // Calls OnError and OnClose of delegate, and no more
  // notifications will be sent to delegate.
  void Finish(int result);

  int DidEstablishConnection();
  int DidReceiveData(int result);
  int DidSendData(int result);

  void OnIOCompleted(int result);
  void OnReadCompleted(int result);
  void OnWriteCompleted(int result);

  void DoLoop(int result);

  int DoResolveProxy();
  int DoResolveProxyComplete(int result);
  int DoResolveHost();
  int DoResolveHostComplete(int result);
  int DoTcpConnect();
  int DoTcpConnectComplete(int result);
  int DoWriteTunnelHeaders();
  int DoWriteTunnelHeadersComplete(int result);
  int DoReadTunnelHeaders();
  int DoReadTunnelHeadersComplete(int result);
  int DoSOCKSConnect();
  int DoSOCKSConnectComplete(int result);
  int DoSSLConnect();
  int DoSSLConnectComplete(int result);
  int DoReadWrite(int result);

  GURL ProxyAuthOrigin() const;
  int HandleAuthChallenge(const HttpResponseHeaders* headers);
  void DoAuthRequired();
  void DoRestartWithAuth();

  int HandleCertificateError(int result);

  bool is_secure() const;
  SSLConfigService* ssl_config_service() const;
  ProxyService* proxy_service() const;

  void GetInfoForTracker(
      RequestTracker<SocketStream>::RecentRequestInfo* info) const;

  scoped_refptr<LoadLog> load_log_;

  GURL url_;
  Delegate* delegate_;
  int max_pending_send_allowed_;
  scoped_refptr<URLRequestContext> context_;

  typedef std::map<const void*, linked_ptr<UserData> > UserDataMap;
  UserDataMap user_data_;

  State next_state_;
  scoped_refptr<HostResolver> host_resolver_;
  ClientSocketFactory* factory_;

  ProxyMode proxy_mode_;

  GURL proxy_url_;
  ProxyService::PacRequest* pac_request_;
  ProxyInfo proxy_info_;

  HttpAuthCache auth_cache_;
  scoped_refptr<HttpAuthHandler> auth_handler_;
  HttpAuth::Identity auth_identity_;
  scoped_refptr<AuthChallengeInfo> auth_info_;

  scoped_refptr<RequestHeaders> tunnel_request_headers_;
  size_t tunnel_request_headers_bytes_sent_;
  scoped_refptr<ResponseHeaders> tunnel_response_headers_;
  int tunnel_response_headers_capacity_;
  int tunnel_response_headers_len_;

  // Use the same number as HttpNetworkTransaction::kMaxHeaderBufSize.
  enum { kMaxTunnelResponseHeadersSize = 32768 };  // 32 kilobytes.

  scoped_ptr<SingleRequestHostResolver> resolver_;
  AddressList addresses_;
  scoped_ptr<ClientSocket> socket_;

  SSLConfig ssl_config_;

  CompletionCallbackImpl<SocketStream> io_callback_;
  CompletionCallbackImpl<SocketStream> read_callback_;
  CompletionCallbackImpl<SocketStream> write_callback_;

  scoped_refptr<IOBuffer> read_buf_;
  int read_buf_size_;

  // Total amount of buffer (|write_buf_size_| - |write_buf_offset_| +
  // sum of size of |pending_write_bufs_|) should not exceed
  // |max_pending_send_allowed_|.
  // |write_buf_| holds requested data and |current_write_buf_| is used
  // for Write operation, that is, |current_write_buf_| is
  // |write_buf_| + |write_buf_offset_|.
  scoped_refptr<IOBuffer> write_buf_;
  scoped_refptr<DrainableIOBuffer> current_write_buf_;
  int write_buf_offset_;
  int write_buf_size_;
  PendingDataQueue pending_write_bufs_;

  SocketStreamThrottle* throttle_;

  scoped_ptr<SocketStreamMetrics> metrics_;

  RequestTracker<SocketStream>::Node request_tracker_node_;

  DISALLOW_COPY_AND_ASSIGN(SocketStream);
};

}  // namespace net

#endif  // NET_SOCKET_STREAM_SOCKET_STREAM_H_
