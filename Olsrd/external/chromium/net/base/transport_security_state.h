// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_TRANSPORT_SECURITY_STATE_H_
#define NET_BASE_TRANSPORT_SECURITY_STATE_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/lock.h"
#include "base/ref_counted.h"
#include "base/time.h"

class GURL;

namespace net {

// TransportSecurityState
//
// Tracks which hosts have enabled *-Transport-Security. This object manages
// the in-memory store. A separate object must register itself with this object
// in order to persist the state to disk.
class TransportSecurityState :
    public base::RefCountedThreadSafe<TransportSecurityState> {
 public:
  TransportSecurityState();

  // A DomainState is the information that we persist about a given domain.
  struct DomainState {
    enum Mode {
      // Strict mode implies:
      //   * We generate internal redirects from HTTP -> HTTPS.
      //   * Certificate issues are fatal.
      MODE_STRICT = 0,
      // Opportunistic mode implies:
      //   * We'll request HTTP URLs over HTTPS
      //   * Certificate issues are ignored.
      MODE_OPPORTUNISTIC = 1,
      // SPDY_ONLY (aka X-Bodge-Transport-Security) is a hopefully temporary
      // measure. It implies:
      //   * We'll request HTTP URLs over HTTPS iff we have SPDY support.
      //   * Certificate issues are fatal.
      MODE_SPDY_ONLY = 2,
    };
    Mode mode;

    DomainState()
        : mode(MODE_STRICT),
          include_subdomains(false) { }

    base::Time expiry;  // the absolute time (UTC) when this record expires
    bool include_subdomains;  // subdomains included?
  };

  // Enable TransportSecurity for |host|.
  void EnableHost(const std::string& host, const DomainState& state);

  // Returns true if |host| has TransportSecurity enabled. If that case,
  // *result is filled out.
  bool IsEnabledForHost(DomainState* result, const std::string& host);

  // Returns |true| if |value| parses as a valid *-Transport-Security
  // header value.  The values of max-age and and includeSubDomains are
  // returned in |max_age| and |include_subdomains|, respectively.  The out
  // parameters are not modified if the function returns |false|.
  static bool ParseHeader(const std::string& value,
                          int* max_age,
                          bool* include_subdomains);

  class Delegate {
   public:
    // This function may not block and may be called with internal locks held.
    // Thus it must not reenter the TransportSecurityState object.
    virtual void StateIsDirty(TransportSecurityState* state) = 0;
  };

  void SetDelegate(Delegate*);

  bool Serialise(std::string* output);
  bool Deserialise(const std::string& state);

 private:
  friend class base::RefCountedThreadSafe<TransportSecurityState>;

  ~TransportSecurityState() {}

  // If we have a callback configured, call it to let our serialiser know that
  // our state is dirty.
  void DirtyNotify();

  // The set of hosts that have enabled TransportSecurity. The keys here
  // are SHA256(DNSForm(domain)) where DNSForm converts from dotted form
  // ('www.google.com') to the form used in DNS: "\x03www\x06google\x03com"
  std::map<std::string, DomainState> enabled_hosts_;

  // Protect access to our data members with this lock.
  Lock lock_;

  // Our delegate who gets notified when we are dirtied, or NULL.
  Delegate* delegate_;

  static std::string CanonicaliseHost(const std::string& host);

  DISALLOW_COPY_AND_ASSIGN(TransportSecurityState);
};

}  // namespace net

#endif  // NET_BASE_TRANSPORT_SECURITY_STATE_H_
