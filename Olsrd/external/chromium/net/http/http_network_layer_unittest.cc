// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/mock_host_resolver.h"
#include "net/base/ssl_config_service_defaults.h"
#include "net/http/http_network_layer.h"
#include "net/http/http_transaction_unittest.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/socket_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

class HttpNetworkLayerTest : public PlatformTest {
};

TEST_F(HttpNetworkLayerTest, CreateAndDestroy) {
  net::HttpNetworkLayer factory(
      NULL, NULL, new net::MockHostResolver, net::ProxyService::CreateNull(),
      new net::SSLConfigServiceDefaults);

  scoped_ptr<net::HttpTransaction> trans;
  int rv = factory.CreateTransaction(&trans);
  EXPECT_EQ(net::OK, rv);
  EXPECT_TRUE(trans.get() != NULL);
}

TEST_F(HttpNetworkLayerTest, Suspend) {
  net::HttpNetworkLayer factory(
      NULL, NULL, new net::MockHostResolver, net::ProxyService::CreateNull(),
      new net::SSLConfigServiceDefaults);

  scoped_ptr<net::HttpTransaction> trans;
  int rv = factory.CreateTransaction(&trans);
  EXPECT_EQ(net::OK, rv);

  trans.reset();

  factory.Suspend(true);

  rv = factory.CreateTransaction(&trans);
  EXPECT_EQ(net::ERR_NETWORK_IO_SUSPENDED, rv);

  ASSERT_TRUE(trans == NULL);

  factory.Suspend(false);

  rv = factory.CreateTransaction(&trans);
  EXPECT_EQ(net::OK, rv);
}

TEST_F(HttpNetworkLayerTest, GET) {
  net::MockClientSocketFactory mock_socket_factory;
  net::MockRead data_reads[] = {
    net::MockRead("HTTP/1.0 200 OK\r\n\r\n"),
    net::MockRead("hello world"),
    net::MockRead(false, net::OK),
  };
  net::MockWrite data_writes[] = {
    net::MockWrite("GET / HTTP/1.1\r\n"
                   "Host: www.google.com\r\n"
                   "Connection: keep-alive\r\n"
                   "User-Agent: Foo/1.0\r\n\r\n"),
  };
  net::StaticSocketDataProvider data(data_reads, data_writes);
  mock_socket_factory.AddSocketDataProvider(&data);

  net::HttpNetworkLayer factory(&mock_socket_factory, NULL,
                                new net::MockHostResolver,
                                net::ProxyService::CreateNull(),
                                new net::SSLConfigServiceDefaults);

  TestCompletionCallback callback;

  scoped_ptr<net::HttpTransaction> trans;
  int rv = factory.CreateTransaction(&trans);
  EXPECT_EQ(net::OK, rv);

  net::HttpRequestInfo request_info;
  request_info.url = GURL("http://www.google.com/");
  request_info.method = "GET";
  request_info.user_agent = "Foo/1.0";
  request_info.load_flags = net::LOAD_NORMAL;

  rv = trans->Start(&request_info, &callback, NULL);
  if (rv == net::ERR_IO_PENDING)
    rv = callback.WaitForResult();
  ASSERT_EQ(net::OK, rv);

  std::string contents;
  rv = ReadTransaction(trans.get(), &contents);
  EXPECT_EQ(net::OK, rv);
  EXPECT_EQ("hello world", contents);
}
