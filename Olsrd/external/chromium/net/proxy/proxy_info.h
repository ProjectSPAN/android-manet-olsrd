// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_INFO_H_
#define NET_PROXY_PROXY_INFO_H_

#include <string>

#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_list.h"
#include "net/proxy/proxy_retry_info.h"
#include "net/proxy/proxy_server.h"

class GURL;

namespace net {

// This object holds proxy information returned by ResolveProxy.
class ProxyInfo {
 public:
  ProxyInfo();
  // Default copy-constructor and assignment operator are OK!

  // Use the same proxy server as the given |proxy_info|.
  void Use(const ProxyInfo& proxy_info);

  // Use a direct connection.
  void UseDirect();

  // Use a specific proxy server, of the form:
  //   proxy-uri = [<scheme> "://"] <hostname> [":" <port>]
  // This may optionally be a semi-colon delimited list of <proxy-uri>.
  // It is OK to have LWS between entries.
  void UseNamedProxy(const std::string& proxy_uri_list);

  // Set the proxy list to a single entry, |proxy_server|.
  void UseProxyServer(const ProxyServer& proxy_server);

  // Parse from the given PAC result.
  void UsePacString(const std::string& pac_string) {
    proxy_list_.SetFromPacString(pac_string);
  }

  // Returns true if this proxy info specifies a direct connection.
  bool is_direct() const {
    // We don't implicitly fallback to DIRECT unless it was added to the list.
    if (is_empty())
      return false;
    return proxy_list_.Get().is_direct();
  }

  // Returns true if this proxy info has no proxies left to try.
  bool is_empty() const {
    return proxy_list_.IsEmpty();
  }

  // Returns the first valid proxy server. is_empty() must be false to be able
  // to call this function.
  ProxyServer proxy_server() const { return proxy_list_.Get(); }

  // See description in ProxyList::ToPacString().
  std::string ToPacString() const;

  // Marks the current proxy as bad. Returns true if there is another proxy
  // available to try in proxy list_.
  bool Fallback(ProxyRetryInfoMap* proxy_retry_info) {
    return proxy_list_.Fallback(proxy_retry_info);
  }

  // De-prioritizes the proxies that we have cached as not working, by moving
  // them to the end of the proxy list.
  void DeprioritizeBadProxies(const ProxyRetryInfoMap& proxy_retry_info) {
    proxy_list_.DeprioritizeBadProxies(proxy_retry_info);
  }

  // Delete any entry which doesn't have one of the specified proxy schemes.
  void RemoveProxiesWithoutScheme(int scheme_bit_field) {
    proxy_list_.RemoveProxiesWithoutScheme(scheme_bit_field);
  }

 private:
  friend class ProxyService;

  // The ordered list of proxy servers (including DIRECT attempts) remaining to
  // try. If proxy_list_ is empty, then there is nothing left to fall back to.
  ProxyList proxy_list_;

  // This value identifies the proxy config used to initialize this object.
  ProxyConfig::ID config_id_;
};

}  // namespace net

#endif  // NET_PROXY_PROXY_INFO_H_
