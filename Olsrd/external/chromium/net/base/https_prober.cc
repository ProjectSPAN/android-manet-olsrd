// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/https_prober.h"

#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"

namespace net {

bool HTTPSProber::HaveProbed(const std::string& host) const {
  return probed_.find(host) != probed_.end();
}

bool HTTPSProber::InFlight(const std::string& host) const {
  return inflight_probes_.find(host) != inflight_probes_.end();
}

bool HTTPSProber::ProbeHost(const std::string& host, URLRequestContext* ctx,
                            HTTPSProberDelegate* delegate) {
  if (HaveProbed(host) || InFlight(host)) {
    return false;
  }

  inflight_probes_[host] = delegate;

  GURL url("https://" + host);
  DCHECK_EQ(url.host(), host);

  URLRequest* req = new URLRequest(url, this);
  req->set_context(ctx);
  req->Start();
  return true;
}

void HTTPSProber::Success(URLRequest* request) {
  DoCallback(request, true);
}

void HTTPSProber::Failure(URLRequest* request) {
  DoCallback(request, false);
}

void HTTPSProber::DoCallback(URLRequest* request, bool result) {
  std::map<std::string, HTTPSProberDelegate*>::iterator i =
    inflight_probes_.find(request->original_url().host());
  DCHECK(i != inflight_probes_.end());

  HTTPSProberDelegate* delegate = i->second;
  inflight_probes_.erase(i);
  probed_.insert(request->original_url().host());
  delete request;
  delegate->ProbeComplete(result);
}

void HTTPSProber::OnAuthRequired(URLRequest* request,
                                 net::AuthChallengeInfo* auth_info) {
  Success(request);
}

void HTTPSProber::OnSSLCertificateError(URLRequest* request,
                                        int cert_error,
                                        net::X509Certificate* cert) {
  request->ContinueDespiteLastError();
}

void HTTPSProber::OnResponseStarted(URLRequest* request) {
  if (request->status().status() == URLRequestStatus::SUCCESS) {
    Success(request);
  } else {
    Failure(request);
  }
}

void HTTPSProber::OnReadCompleted(URLRequest* request, int bytes_read) {
  NOTREACHED();
}

}  // namespace net
