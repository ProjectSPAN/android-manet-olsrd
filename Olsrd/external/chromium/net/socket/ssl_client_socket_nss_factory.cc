// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_factory.h"

#include "net/socket/ssl_client_socket_nss.h"

// This file is only used on platforms where NSS is not the system SSL
// library.  When compiled, this file is the only object module that pulls
// in the dependency on NSPR and NSS.  This allows us to control which
// projects depend on NSPR and NSS on those platforms.

namespace net {

SSLClientSocket* SSLClientSocketNSSFactory(
    ClientSocket* transport_socket,
    const std::string& hostname,
    const SSLConfig& ssl_config) {
  return new SSLClientSocketNSS(transport_socket, hostname, ssl_config);
}

}  // namespace net
