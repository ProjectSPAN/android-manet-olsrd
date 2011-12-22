// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MOCK_HOST_RESOLVER_H_
#define NET_BASE_MOCK_HOST_RESOLVER_H_

#include <list>

#include "base/waitable_event.h"
#include "net/base/host_resolver_impl.h"
#include "net/base/host_resolver_proc.h"

namespace net {

class RuleBasedHostResolverProc;

// In most cases, it is important that unit tests avoid making actual DNS
// queries since the resulting tests can be flaky, especially if the network is
// unreliable for some reason.  To simplify writing tests that avoid making
// actual DNS queries, pass a MockHostResolver as the HostResolver dependency.
// The socket addresses returned can be configured using the
// RuleBasedHostResolverProc:
//
//   host_resolver->rules()->AddRule("foo.com", "1.2.3.4");
//   host_resolver->rules()->AddRule("bar.com", "2.3.4.5");
//
// The above rules define a static mapping from hostnames to IP address
// literals.  The first parameter to AddRule specifies a host pattern to match
// against, and the second parameter indicates what value should be used to
// replace the given hostname.  So, the following is also supported:
//
//   host_mapper->AddRule("*.com", "127.0.0.1");
//
// Replacement doesn't have to be string representing an IP address. It can
// re-map one hostname to another as well.

// Base class shared by MockHostResolver and MockCachingHostResolver.
class MockHostResolverBase : public HostResolver {
 public:
  // HostResolver methods:
  virtual int Resolve(const RequestInfo& info,
                      AddressList* addresses,
                      CompletionCallback* callback,
                      RequestHandle* out_req,
                      LoadLog* load_log);
  virtual void CancelRequest(RequestHandle req);
  virtual void AddObserver(Observer* observer);
  virtual void RemoveObserver(Observer* observer);
  // TODO(eroman): temp hack for http://crbug.com/18373
  virtual void Shutdown();

  RuleBasedHostResolverProc* rules() { return rules_; }

  // Controls whether resolutions complete synchronously or asynchronously.
  void set_synchronous_mode(bool is_synchronous) {
    synchronous_mode_ = is_synchronous;
  }

  // Resets the mock.
  void Reset(HostResolverProc* interceptor);

 protected:
  MockHostResolverBase(bool use_caching);
  virtual ~MockHostResolverBase() {}

  scoped_refptr<HostResolverImpl> impl_;
  scoped_refptr<RuleBasedHostResolverProc> rules_;
  bool synchronous_mode_;
  bool use_caching_;
};

class MockHostResolver : public MockHostResolverBase {
 public:
  MockHostResolver() : MockHostResolverBase(false /*use_caching*/) {}

 private:
  virtual ~MockHostResolver() {}
};

// Same as MockHostResolver, except internally it uses a host-cache.
//
// Note that tests are advised to use MockHostResolver instead, since it is
// more predictable. (MockHostResolver also can be put into synchronous
// operation mode in case that is what you needed from the caching version).
class MockCachingHostResolver : public MockHostResolverBase {
 public:
  MockCachingHostResolver() : MockHostResolverBase(true /*use_caching*/) {}

 private:
  ~MockCachingHostResolver() {}
};

// RuleBasedHostResolverProc applies a set of rules to map a host string to
// a replacement host string. It then uses the system host resolver to return
// a socket address. Generally the replacement should be an IPv4 literal so
// there is no network dependency.
class RuleBasedHostResolverProc : public HostResolverProc {
 public:
  explicit RuleBasedHostResolverProc(HostResolverProc* previous);

  // Any hostname matching the given pattern will be replaced with the given
  // replacement value.  Usually, replacement should be an IP address literal.
  void AddRule(const std::string& host_pattern,
               const std::string& replacement);

  // Same as AddRule(), but further restricts to |address_family|.
  void AddRuleForAddressFamily(const std::string& host_pattern,
                               AddressFamily address_family,
                               const std::string& replacement);

  // Same as AddRule(), but the replacement is expected to be an IPV6 literal.
  // You should use this in place of AddRule(), since the system's host resolver
  // may not support IPv6 literals on all systems. Whereas this variant
  // constructs the socket address directly so it will always work.
  void AddIPv6Rule(const std::string& host_pattern,
                   const std::string& ipv6_literal);

  void AddRuleWithLatency(const std::string& host_pattern,
                          const std::string& replacement,
                          int latency_ms);

  // Make sure that |host| will not be re-mapped or even processed by underlying
  // host resolver procedures. It can also be a pattern.
  void AllowDirectLookup(const std::string& host);

  // Simulate a lookup failure for |host| (it also can be a pattern).
  void AddSimulatedFailure(const std::string& host);

  // HostResolverProc methods:
  virtual int Resolve(const std::string& host,
                      AddressFamily address_family,
                      AddressList* addrlist);

 private:
  ~RuleBasedHostResolverProc();

  struct Rule;
  typedef std::list<Rule> RuleList;

  RuleList rules_;
};

// Using WaitingHostResolverProc you can simulate very long lookups.
class WaitingHostResolverProc : public HostResolverProc {
 public:
  explicit WaitingHostResolverProc(HostResolverProc* previous)
      : HostResolverProc(previous), event_(false, false) {}

  void Signal() {
    event_.Signal();
  }

  // HostResolverProc methods:
  virtual int Resolve(const std::string& host,
                      AddressFamily address_family,
                      AddressList* addrlist) {
    event_.Wait();
    return ResolveUsingPrevious(host, address_family, addrlist);
  }

 private:
  ~WaitingHostResolverProc() {}

  base::WaitableEvent event_;
};

// This class sets the default HostResolverProc for a particular scope.  The
// chain of resolver procs starting at |proc| is placed in front of any existing
// default resolver proc(s).  This means that if multiple
// ScopedDefaultHostResolverProcs are declared, then resolving will start with
// the procs given to the last-allocated one, then fall back to the procs given
// to the previously-allocated one, and so forth.
//
// NOTE: Only use this as a catch-all safety net. Individual tests should use
// MockHostResolver.
class ScopedDefaultHostResolverProc {
 public:
  ScopedDefaultHostResolverProc() {}
  explicit ScopedDefaultHostResolverProc(HostResolverProc* proc);

  ~ScopedDefaultHostResolverProc();

  void Init(HostResolverProc* proc);

 private:
  scoped_refptr<HostResolverProc> current_proc_;
  scoped_refptr<HostResolverProc> previous_proc_;
};

}  // namespace net

#endif  // NET_BASE_MOCK_HOST_RESOLVER_H_
