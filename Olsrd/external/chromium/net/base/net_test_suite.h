// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NET_TEST_SUITE_H_
#define NET_BASE_NET_TEST_SUITE_H_

#include "base/message_loop.h"
#include "base/ref_counted.h"
#include "base/test/test_suite.h"
#include "net/base/mock_host_resolver.h"

class NetTestSuite : public TestSuite {
 public:
  NetTestSuite(int argc, char** argv) : TestSuite(argc, argv) {
  }

  virtual void Initialize() {
    TestSuite::Initialize();
    InitializeTestThread();
  }

  // Called from within Initialize(), but separate so that derived classes
  // can initialize the NetTestSuite instance only and not
  // TestSuite::Initialize().  TestSuite::Initialize() performs some global
  // initialization that can only be done once.
  void InitializeTestThread() {
    host_resolver_proc_ = new net::RuleBasedHostResolverProc(NULL);
    scoped_host_resolver_proc_.Init(host_resolver_proc_.get());
    // In case any attempts are made to resolve host names, force them all to
    // be mapped to localhost.  This prevents DNS queries from being sent in
    // the process of running these unit tests.
    host_resolver_proc_->AddRule("*", "127.0.0.1");

    message_loop_.reset(new MessageLoopForIO());
  }

  virtual void Shutdown() {
    // We want to destroy this here before the TestSuite continues to tear down
    // the environment.
    message_loop_.reset();

    TestSuite::Shutdown();
  }

 private:
  scoped_ptr<MessageLoop> message_loop_;
  scoped_refptr<net::RuleBasedHostResolverProc> host_resolver_proc_;
  net::ScopedDefaultHostResolverProc scoped_host_resolver_proc_;
};

#endif  // NET_BASE_NET_TEST_SUITE_H_
