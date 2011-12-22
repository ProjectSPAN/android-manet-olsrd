// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/ssl_client_socket.h"

#include "net/base/address_list.h"
#include "net/base/host_resolver.h"
#include "net/base/io_buffer.h"
#include "net/base/load_log.h"
#include "net/base/load_log_unittest.h"
#include "net/base/net_errors.h"
#include "net/base/ssl_config_service.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/ssl_test_util.h"
#include "net/socket/tcp_client_socket.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

//-----------------------------------------------------------------------------

const net::SSLConfig kDefaultSSLConfig;

class SSLClientSocketTest : public PlatformTest {
 public:
  SSLClientSocketTest()
    : resolver_(net::CreateSystemHostResolver(NULL)),
        socket_factory_(net::ClientSocketFactory::GetDefaultFactory()) {
  }

  void StartOKServer() {
    bool success = server_.Start(net::TestServerLauncher::ProtoHTTP,
        server_.kHostName, server_.kOKHTTPSPort,
        FilePath(), server_.GetOKCertPath(), std::wstring());
    ASSERT_TRUE(success);
  }

  void StartMismatchedServer() {
    bool success = server_.Start(net::TestServerLauncher::ProtoHTTP,
        server_.kMismatchedHostName, server_.kOKHTTPSPort,
        FilePath(), server_.GetOKCertPath(), std::wstring());
    ASSERT_TRUE(success);
  }

  void StartExpiredServer() {
    bool success = server_.Start(net::TestServerLauncher::ProtoHTTP,
        server_.kHostName, server_.kBadHTTPSPort,
        FilePath(), server_.GetExpiredCertPath(), std::wstring());
    ASSERT_TRUE(success);
  }

 protected:
  scoped_refptr<net::HostResolver> resolver_;
  net::ClientSocketFactory* socket_factory_;
  net::TestServerLauncher server_;
};

//-----------------------------------------------------------------------------

TEST_F(SSLClientSocketTest, Connect) {
  StartOKServer();

  net::AddressList addr;
  TestCompletionCallback callback;

  net::HostResolver::RequestInfo info(server_.kHostName, server_.kOKHTTPSPort);
  int rv = resolver_->Resolve(info, &addr, NULL, NULL, NULL);
  EXPECT_EQ(net::OK, rv);

  net::ClientSocket *transport = new net::TCPClientSocket(addr);
  rv = transport->Connect(&callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  scoped_ptr<net::SSLClientSocket> sock(
      socket_factory_->CreateSSLClientSocket(transport,
          server_.kHostName, kDefaultSSLConfig));

  EXPECT_FALSE(sock->IsConnected());

  scoped_refptr<net::LoadLog> log(new net::LoadLog(net::LoadLog::kUnbounded));
  rv = sock->Connect(&callback, log);
  EXPECT_TRUE(net::LogContainsBeginEvent(
      *log, 0, net::LoadLog::TYPE_SSL_CONNECT));
  if (rv != net::OK) {
    ASSERT_EQ(net::ERR_IO_PENDING, rv);
    EXPECT_FALSE(sock->IsConnected());
    EXPECT_FALSE(net::LogContainsEndEvent(
        *log, -1, net::LoadLog::TYPE_SSL_CONNECT));

    rv = callback.WaitForResult();
    EXPECT_EQ(net::OK, rv);
  }

  EXPECT_TRUE(sock->IsConnected());
  EXPECT_TRUE(net::LogContainsEndEvent(
      *log, -1, net::LoadLog::TYPE_SSL_CONNECT));

  sock->Disconnect();
  EXPECT_FALSE(sock->IsConnected());
}

TEST_F(SSLClientSocketTest, ConnectExpired) {
  StartExpiredServer();

  net::AddressList addr;
  TestCompletionCallback callback;

  net::HostResolver::RequestInfo info(server_.kHostName, server_.kBadHTTPSPort);
  int rv = resolver_->Resolve(info, &addr, NULL, NULL, NULL);
  EXPECT_EQ(net::OK, rv);

  net::ClientSocket *transport = new net::TCPClientSocket(addr);
  rv = transport->Connect(&callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  scoped_ptr<net::SSLClientSocket> sock(
      socket_factory_->CreateSSLClientSocket(transport,
          server_.kHostName, kDefaultSSLConfig));

  EXPECT_FALSE(sock->IsConnected());

  scoped_refptr<net::LoadLog> log(new net::LoadLog(net::LoadLog::kUnbounded));
  rv = sock->Connect(&callback, log);
  EXPECT_TRUE(net::LogContainsBeginEvent(
      *log, 0, net::LoadLog::TYPE_SSL_CONNECT));
  if (rv != net::OK) {
    ASSERT_EQ(net::ERR_IO_PENDING, rv);
    EXPECT_FALSE(sock->IsConnected());
    EXPECT_FALSE(net::LogContainsEndEvent(
        *log, -1, net::LoadLog::TYPE_SSL_CONNECT));

    rv = callback.WaitForResult();
    EXPECT_EQ(net::ERR_CERT_DATE_INVALID, rv);
  }

  // We cannot test sock->IsConnected(), as the NSS implementation disconnects
  // the socket when it encounters an error, whereas other implementations
  // leave it connected.

  EXPECT_TRUE(net::LogContainsEndEvent(
      *log, -1, net::LoadLog::TYPE_SSL_CONNECT));
}

TEST_F(SSLClientSocketTest, ConnectMismatched) {
  StartMismatchedServer();

  net::AddressList addr;
  TestCompletionCallback callback;

  net::HostResolver::RequestInfo info(server_.kMismatchedHostName,
                                      server_.kOKHTTPSPort);
  int rv = resolver_->Resolve(info, &addr, NULL, NULL, NULL);
  EXPECT_EQ(net::OK, rv);

  net::ClientSocket *transport = new net::TCPClientSocket(addr);
  rv = transport->Connect(&callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  scoped_ptr<net::SSLClientSocket> sock(
      socket_factory_->CreateSSLClientSocket(transport,
          server_.kMismatchedHostName, kDefaultSSLConfig));

  EXPECT_FALSE(sock->IsConnected());

  scoped_refptr<net::LoadLog> log(new net::LoadLog(net::LoadLog::kUnbounded));
  rv = sock->Connect(&callback, log);
  EXPECT_TRUE(net::LogContainsBeginEvent(
      *log, 0, net::LoadLog::TYPE_SSL_CONNECT));
  if (rv != net::ERR_CERT_COMMON_NAME_INVALID) {
    ASSERT_EQ(net::ERR_IO_PENDING, rv);
    EXPECT_FALSE(sock->IsConnected());
    EXPECT_FALSE(net::LogContainsEndEvent(
        *log, -1, net::LoadLog::TYPE_SSL_CONNECT));

    rv = callback.WaitForResult();
    EXPECT_EQ(net::ERR_CERT_COMMON_NAME_INVALID, rv);
  }

  // We cannot test sock->IsConnected(), as the NSS implementation disconnects
  // the socket when it encounters an error, whereas other implementations
  // leave it connected.

  EXPECT_TRUE(net::LogContainsEndEvent(
      *log, -1, net::LoadLog::TYPE_SSL_CONNECT));
}

// TODO(wtc): Add unit tests for IsConnectedAndIdle:
//   - Server closes an SSL connection (with a close_notify alert message).
//   - Server closes the underlying TCP connection directly.
//   - Server sends data unexpectedly.

TEST_F(SSLClientSocketTest, Read) {
  StartOKServer();

  net::AddressList addr;
  TestCompletionCallback callback;

  net::HostResolver::RequestInfo info(server_.kHostName, server_.kOKHTTPSPort);
  int rv = resolver_->Resolve(info, &addr, &callback, NULL, NULL);
  EXPECT_EQ(net::ERR_IO_PENDING, rv);

  rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  net::ClientSocket *transport = new net::TCPClientSocket(addr);
  rv = transport->Connect(&callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  scoped_ptr<net::SSLClientSocket> sock(
      socket_factory_->CreateSSLClientSocket(transport,
                                             server_.kHostName,
                                             kDefaultSSLConfig));

  rv = sock->Connect(&callback, NULL);
  if (rv != net::OK) {
    ASSERT_EQ(net::ERR_IO_PENDING, rv);

    rv = callback.WaitForResult();
    EXPECT_EQ(net::OK, rv);
  }
  EXPECT_TRUE(sock->IsConnected());

  const char request_text[] = "GET / HTTP/1.0\r\n\r\n";
  scoped_refptr<net::IOBuffer> request_buffer =
      new net::IOBuffer(arraysize(request_text) - 1);
  memcpy(request_buffer->data(), request_text, arraysize(request_text) - 1);

  rv = sock->Write(request_buffer, arraysize(request_text) - 1, &callback);
  EXPECT_TRUE(rv >= 0 || rv == net::ERR_IO_PENDING);

  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(static_cast<int>(arraysize(request_text) - 1), rv);

  scoped_refptr<net::IOBuffer> buf = new net::IOBuffer(4096);
  for (;;) {
    rv = sock->Read(buf, 4096, &callback);
    EXPECT_TRUE(rv >= 0 || rv == net::ERR_IO_PENDING);

    if (rv == net::ERR_IO_PENDING)
      rv = callback.WaitForResult();

    EXPECT_GE(rv, 0);
    if (rv <= 0)
      break;
  }
}

// Test the full duplex mode, with Read and Write pending at the same time.
// This test also serves as a regression test for http://crbug.com/29815.
TEST_F(SSLClientSocketTest, Read_FullDuplex) {
  StartOKServer();

  net::AddressList addr;
  TestCompletionCallback callback;  // Used for everything except Write.
  TestCompletionCallback callback2;  // Used for Write only.

  net::HostResolver::RequestInfo info(server_.kHostName, server_.kOKHTTPSPort);
  int rv = resolver_->Resolve(info, &addr, &callback, NULL, NULL);
  EXPECT_EQ(net::ERR_IO_PENDING, rv);

  rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  net::ClientSocket *transport = new net::TCPClientSocket(addr);
  rv = transport->Connect(&callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  scoped_ptr<net::SSLClientSocket> sock(
      socket_factory_->CreateSSLClientSocket(transport,
                                             server_.kHostName,
                                             kDefaultSSLConfig));

  rv = sock->Connect(&callback, NULL);
  if (rv != net::OK) {
    ASSERT_EQ(net::ERR_IO_PENDING, rv);

    rv = callback.WaitForResult();
    EXPECT_EQ(net::OK, rv);
  }
  EXPECT_TRUE(sock->IsConnected());

  // Issue a "hanging" Read first.
  scoped_refptr<net::IOBuffer> buf = new net::IOBuffer(4096);
  rv = sock->Read(buf, 4096, &callback);
  // We haven't written the request, so there should be no response yet.
  ASSERT_EQ(net::ERR_IO_PENDING, rv);

  // Write the request.
  // The request is padded with a User-Agent header to a size that causes the
  // memio circular buffer (4k bytes) in SSLClientSocketNSS to wrap around.
  // This tests the fix for http://crbug.com/29815.
  std::string request_text = "GET / HTTP/1.1\r\nUser-Agent: long browser name ";
  for (int i = 0; i < 3800; ++i)
    request_text.push_back('*');
  request_text.append("\r\n\r\n");
  scoped_refptr<net::IOBuffer> request_buffer =
      new net::StringIOBuffer(request_text);

  rv = sock->Write(request_buffer, request_text.size(), &callback2);
  EXPECT_TRUE(rv >= 0 || rv == net::ERR_IO_PENDING);

  if (rv == net::ERR_IO_PENDING)
    rv = callback2.WaitForResult();
  EXPECT_EQ(static_cast<int>(request_text.size()), rv);

  // Now get the Read result.
  rv = callback.WaitForResult();
  EXPECT_GT(rv, 0);
}

TEST_F(SSLClientSocketTest, Read_SmallChunks) {
  StartOKServer();

  net::AddressList addr;
  TestCompletionCallback callback;

  net::HostResolver::RequestInfo info(server_.kHostName, server_.kOKHTTPSPort);
  int rv = resolver_->Resolve(info, &addr, NULL, NULL, NULL);
  EXPECT_EQ(net::OK, rv);

  net::ClientSocket *transport = new net::TCPClientSocket(addr);
  rv = transport->Connect(&callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  scoped_ptr<net::SSLClientSocket> sock(
      socket_factory_->CreateSSLClientSocket(transport,
          server_.kHostName, kDefaultSSLConfig));

  rv = sock->Connect(&callback, NULL);
  if (rv != net::OK) {
    ASSERT_EQ(net::ERR_IO_PENDING, rv);

    rv = callback.WaitForResult();
    EXPECT_EQ(net::OK, rv);
  }

  const char request_text[] = "GET / HTTP/1.0\r\n\r\n";
  scoped_refptr<net::IOBuffer> request_buffer =
      new net::IOBuffer(arraysize(request_text) - 1);
  memcpy(request_buffer->data(), request_text, arraysize(request_text) - 1);

  rv = sock->Write(request_buffer, arraysize(request_text) - 1, &callback);
  EXPECT_TRUE(rv >= 0 || rv == net::ERR_IO_PENDING);

  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(static_cast<int>(arraysize(request_text) - 1), rv);

  scoped_refptr<net::IOBuffer> buf = new net::IOBuffer(1);
  for (;;) {
    rv = sock->Read(buf, 1, &callback);
    EXPECT_TRUE(rv >= 0 || rv == net::ERR_IO_PENDING);

    if (rv == net::ERR_IO_PENDING)
      rv = callback.WaitForResult();

    EXPECT_GE(rv, 0);
    if (rv <= 0)
      break;
  }
}

TEST_F(SSLClientSocketTest, Read_Interrupted) {
  StartOKServer();

  net::AddressList addr;
  TestCompletionCallback callback;

  net::HostResolver::RequestInfo info(server_.kHostName, server_.kOKHTTPSPort);
  int rv = resolver_->Resolve(info, &addr, NULL, NULL, NULL);
  EXPECT_EQ(net::OK, rv);

  net::ClientSocket *transport = new net::TCPClientSocket(addr);
  rv = transport->Connect(&callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(net::OK, rv);

  scoped_ptr<net::SSLClientSocket> sock(
      socket_factory_->CreateSSLClientSocket(transport,
          server_.kHostName, kDefaultSSLConfig));

  rv = sock->Connect(&callback, NULL);
  if (rv != net::OK) {
    ASSERT_EQ(net::ERR_IO_PENDING, rv);

    rv = callback.WaitForResult();
    EXPECT_EQ(net::OK, rv);
  }

  const char request_text[] = "GET / HTTP/1.0\r\n\r\n";
  scoped_refptr<net::IOBuffer> request_buffer =
      new net::IOBuffer(arraysize(request_text) - 1);
  memcpy(request_buffer->data(), request_text, arraysize(request_text) - 1);

  rv = sock->Write(request_buffer, arraysize(request_text) - 1, &callback);
  EXPECT_TRUE(rv >= 0 || rv == net::ERR_IO_PENDING);

  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(static_cast<int>(arraysize(request_text) - 1), rv);

  // Do a partial read and then exit.  This test should not crash!
  scoped_refptr<net::IOBuffer> buf = new net::IOBuffer(512);
  rv = sock->Read(buf, 512, &callback);
  EXPECT_TRUE(rv > 0 || rv == net::ERR_IO_PENDING);

  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();

  EXPECT_GT(rv, 0);
}
