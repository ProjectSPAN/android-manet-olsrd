// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_HOST_CACHE_H_
#define NET_BASE_HOST_CACHE_H_

#include <map>
#include <string>

#include "base/ref_counted.h"
#include "base/time.h"
#include "net/base/address_family.h"
#include "net/base/address_list.h"
#include "testing/gtest/include/gtest/gtest_prod.h"

namespace net {

// Cache used by HostResolver to map hostnames to their resolved result.
class HostCache {
 public:
  // Stores the latest address list that was looked up for a hostname.
  struct Entry : public base::RefCounted<Entry> {
    Entry(int error, const AddressList& addrlist, base::TimeTicks expiration);

    // The resolve results for this entry.
    int error;
    AddressList addrlist;

    // The time when this entry expires.
    base::TimeTicks expiration;

   private:
    friend class base::RefCounted<Entry>;

    ~Entry();
  };

  struct Key {
    Key(const std::string& hostname, AddressFamily address_family)
        : hostname(hostname), address_family(address_family) {}

    bool operator==(const Key& other) const {
      return other.hostname == hostname &&
             other.address_family == address_family;
    }

    bool operator<(const Key& other) const {
      if (address_family == other.address_family)
        return hostname < other.hostname;
      return address_family < other.address_family;
    }

    std::string hostname;
    AddressFamily address_family;
  };

  typedef std::map<Key, scoped_refptr<Entry> > EntryMap;

  // Constructs a HostCache that caches successful host resolves for
  // |success_entry_ttl| time, and failed host resolves for
  // |failure_entry_ttl|. The cache will store up to |max_entries|.
  HostCache(size_t max_entries,
            base::TimeDelta success_entry_ttl,
            base::TimeDelta failure_entry_ttl);

  ~HostCache();

  // Returns a pointer to the entry for |key|, which is valid at time
  // |now|. If there is no such entry, returns NULL.
  const Entry* Lookup(const Key& key, base::TimeTicks now) const;

  // Overwrites or creates an entry for |key|. Returns the pointer to the
  // entry, or NULL on failure (fails if caching is disabled).
  // (|error|, |addrlist|) is the value to set, and |now| is the current
  // timestamp.
  Entry* Set(const Key& key,
             int error,
             const AddressList addrlist,
             base::TimeTicks now);

  // Empties the cache.
  void clear() {
    entries_.clear();
  }

  // Returns true if this HostCache can contain no entries.
  bool caching_is_disabled() const {
    return max_entries_ == 0;
  }

  // Returns the number of entries in the cache.
  size_t size() const {
    return entries_.size();
  }

  size_t max_entries() const {
    return max_entries_;
  }

  base::TimeDelta success_entry_ttl() const {
    return success_entry_ttl_;
  }

  base::TimeDelta failure_entry_ttl() const {
    return failure_entry_ttl_;
  }

  // Note that this map may contain expired entries.
  const EntryMap& entries() const {
    return entries_;
  }

 private:
  FRIEND_TEST(HostCacheTest, Compact);
  FRIEND_TEST(HostCacheTest, NoCache);

  // Returns true if this cache entry's result is valid at time |now|.
  static bool CanUseEntry(const Entry* entry, const base::TimeTicks now);

  // Prunes entries from the cache to bring it below max entry bound. Entries
  // matching |pinned_entry| will NOT be pruned.
  void Compact(base::TimeTicks now, const Entry* pinned_entry);

  // Bound on total size of the cache.
  size_t max_entries_;

  // Time to live for cache entries.
  base::TimeDelta success_entry_ttl_;
  base::TimeDelta failure_entry_ttl_;

  // Map from hostname (presumably in lowercase canonicalized format) to
  // a resolved result entry.
  EntryMap entries_;

  DISALLOW_COPY_AND_ASSIGN(HostCache);
};

}  // namespace net

#endif  // NET_BASE_HOST_CACHE_H_
