// Copyright (c) 2006-2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SSL_CLIENT_SOCKET_NSS_H_
#define NET_SOCKET_SSL_CLIENT_SOCKET_NSS_H_

#include <certt.h>
#include <keyt.h>
#include <nspr.h>
#include <nss.h>

#include <string>
#include <vector>

#include "base/scoped_ptr.h"
#include "net/base/cert_verify_result.h"
#include "net/base/completion_callback.h"
#include "net/base/nss_memio.h"
#include "net/base/ssl_config_service.h"
#include "net/socket/ssl_client_socket.h"

namespace net {

class CertVerifier;
class LoadLog;
class X509Certificate;

// An SSL client socket implemented with Mozilla NSS.
class SSLClientSocketNSS : public SSLClientSocket {
 public:
  // Takes ownership of the transport_socket, which may already be connected.
  // The given hostname will be compared with the name(s) in the server's
  // certificate during the SSL handshake.  ssl_config specifies the SSL
  // settings.
  SSLClientSocketNSS(ClientSocket* transport_socket,
                     const std::string& hostname,
                     const SSLConfig& ssl_config);
  ~SSLClientSocketNSS();

  // SSLClientSocket methods:
  virtual void GetSSLInfo(SSLInfo* ssl_info);
  virtual void GetSSLCertRequestInfo(SSLCertRequestInfo* cert_request_info);
  virtual NextProtoStatus GetNextProto(std::string* proto);

  // ClientSocket methods:
  virtual int Connect(CompletionCallback* callback, LoadLog* load_log);
  virtual void Disconnect();
  virtual bool IsConnected() const;
  virtual bool IsConnectedAndIdle() const;
  virtual int GetPeerName(struct sockaddr* name, socklen_t* namelen);

  // Socket methods:
  virtual int Read(IOBuffer* buf, int buf_len, CompletionCallback* callback);
  virtual int Write(IOBuffer* buf, int buf_len, CompletionCallback* callback);
  virtual bool SetReceiveBufferSize(int32 size);
  virtual bool SetSendBufferSize(int32 size);

 private:
  // Initializes NSS SSL options.  Returns a net error code.
  int InitializeSSLOptions();

  void InvalidateSessionIfBadCertificate();
  X509Certificate* UpdateServerCert();
  void DoReadCallback(int result);
  void DoWriteCallback(int result);
  void DoConnectCallback(int result);
  void OnHandshakeIOComplete(int result);
  void OnSendComplete(int result);
  void OnRecvComplete(int result);

  int DoHandshakeLoop(int last_io_result);
  int DoReadLoop(int result);
  int DoWriteLoop(int result);

  int DoHandshake();
  int DoVerifyCert(int result);
  int DoVerifyCertComplete(int result);
  int DoPayloadRead();
  int DoPayloadWrite();
  int Init();

  bool DoTransportIO();
  int BufferSend(void);
  int BufferRecv(void);
  void BufferSendComplete(int result);
  void BufferRecvComplete(int result);

  // NSS calls this when checking certificates. We pass 'this' as the first
  // argument.
  static SECStatus OwnAuthCertHandler(void* arg, PRFileDesc* socket,
                                      PRBool checksig, PRBool is_server);
  // NSS calls this when client authentication is requested.
  static SECStatus ClientAuthHandler(void* arg,
                                     PRFileDesc* socket,
                                     CERTDistNames* ca_names,
                                     CERTCertificate** result_certificate,
                                     SECKEYPrivateKey** result_private_key);
  // NSS calls this when handshake is completed.  We pass 'this' as the second
  // argument.
  static void HandshakeCallback(PRFileDesc* socket, void* arg);

  CompletionCallbackImpl<SSLClientSocketNSS> buffer_send_callback_;
  CompletionCallbackImpl<SSLClientSocketNSS> buffer_recv_callback_;
  bool transport_send_busy_;
  bool transport_recv_busy_;
  scoped_refptr<IOBuffer> recv_buffer_;

  CompletionCallbackImpl<SSLClientSocketNSS> handshake_io_callback_;
  scoped_ptr<ClientSocket> transport_;
  std::string hostname_;
  SSLConfig ssl_config_;

  CompletionCallback* user_connect_callback_;
  CompletionCallback* user_read_callback_;
  CompletionCallback* user_write_callback_;

  // Used by Read function.
  scoped_refptr<IOBuffer> user_read_buf_;
  int user_read_buf_len_;

  // Used by Write function.
  scoped_refptr<IOBuffer> user_write_buf_;
  int user_write_buf_len_;

  // Set when handshake finishes.  The server certificate is first received
  // from NSS as an NSS certificate handle (server_cert_nss_), and then
  // converted into an X509Certificate object (server_cert_).
  scoped_refptr<X509Certificate> server_cert_;
  CERTCertificate* server_cert_nss_;
  CertVerifyResult server_cert_verify_result_;

  // Stores client authentication information between ClientAuthHandler and
  // GetSSLCertRequestInfo calls.
  std::vector<scoped_refptr<X509Certificate> > client_certs_;
  bool client_auth_cert_needed_;

  scoped_ptr<CertVerifier> verifier_;

  bool completed_handshake_;

  enum State {
    STATE_NONE,
    STATE_HANDSHAKE,
    STATE_VERIFY_CERT,
    STATE_VERIFY_CERT_COMPLETE,
  };
  State next_handshake_state_;

  // The NSS SSL state machine
  PRFileDesc* nss_fd_;

  // Buffers for the network end of the SSL state machine
  memio_Private* nss_bufs_;

  scoped_refptr<LoadLog> load_log_;

#if defined(OS_WIN)
  // A CryptoAPI in-memory certificate store that we import server
  // certificates into so that we can verify and display the certificates
  // using CryptoAPI.
  static HCERTSTORE cert_store_;
#endif
};

}  // namespace net

#endif  // NET_SOCKET_SSL_CLIENT_SOCKET_NSS_H_
