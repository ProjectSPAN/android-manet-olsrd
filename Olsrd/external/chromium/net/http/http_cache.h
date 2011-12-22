// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file declares a HttpTransactionFactory implementation that can be
// layered on top of another HttpTransactionFactory to add HTTP caching.  The
// caching logic follows RFC 2616 (any exceptions are called out in the code).
//
// The HttpCache takes a disk_cache::Backend as a parameter, and uses that for
// the cache storage.
//
// See HttpTransactionFactory and HttpTransaction for more details.

#ifndef NET_HTTP_HTTP_CACHE_H_
#define NET_HTTP_HTTP_CACHE_H_

#include <list>
#include <set>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/hash_tables.h"
#include "base/scoped_ptr.h"
#include "base/task.h"
#include "base/weak_ptr.h"
#include "net/base/cache_type.h"
#include "net/base/completion_callback.h"
#include "net/http/http_transaction_factory.h"

namespace disk_cache {
class Backend;
class Entry;
}

namespace net {

class HostResolver;
class HttpNetworkSession;
class HttpRequestInfo;
class HttpResponseInfo;
class NetworkChangeNotifier;
class ProxyService;
class SSLConfigService;

class HttpCache : public HttpTransactionFactory,
                  public base::SupportsWeakPtr<HttpCache> {
 public:
  ~HttpCache();

  // The cache mode of operation.
  enum Mode {
    // Normal mode just behaves like a standard web cache.
    NORMAL = 0,
    // Record mode caches everything for purposes of offline playback.
    RECORD,
    // Playback mode replays from a cache without considering any
    // standard invalidations.
    PLAYBACK,
    // Disables reads and writes from the cache.
    // Equivalent to setting LOAD_DISABLE_CACHE on every request.
    DISABLE
  };

  // Initialize the cache from the directory where its data is stored. The
  // disk cache is initialized lazily (by CreateTransaction) in this case. If
  // |cache_size| is zero, a default value will be calculated automatically.
  HttpCache(NetworkChangeNotifier* network_change_notifier,
            HostResolver* host_resolver,
            ProxyService* proxy_service,
            SSLConfigService* ssl_config_service,
            const FilePath& cache_dir,
            int cache_size);

  // Initialize the cache from the directory where its data is stored. The
  // disk cache is initialized lazily (by CreateTransaction) in  this case. If
  // |cache_size| is zero, a default value will be calculated automatically.
  // Provide an existing HttpNetworkSession, the cache can construct a
  // network layer with a shared HttpNetworkSession in order for multiple
  // network layers to share information (e.g. authenication data).
  HttpCache(HttpNetworkSession* session, const FilePath& cache_dir,
            int cache_size);

  // Initialize using an in-memory cache. The cache is initialized lazily
  // (by CreateTransaction) in this case. If |cache_size| is zero, a default
  // value will be calculated automatically.
  HttpCache(NetworkChangeNotifier* network_change_notifier,
            HostResolver* host_resolver,
            ProxyService* proxy_service,
            SSLConfigService* ssl_config_service,
            int cache_size);

  // Initialize the cache from its component parts, which is useful for
  // testing.  The lifetime of the network_layer and disk_cache are managed by
  // the HttpCache and will be destroyed using |delete| when the HttpCache is
  // destroyed.
  HttpCache(HttpTransactionFactory* network_layer,
            disk_cache::Backend* disk_cache);

  HttpTransactionFactory* network_layer() { return network_layer_.get(); }

  // Returns the cache backend for this HttpCache instance. If the backend
  // is not initialized yet, this method will initialize it. If the return
  // value is NULL then the backend cannot be initialized.
  disk_cache::Backend* GetBackend();

  // HttpTransactionFactory implementation:
  virtual int CreateTransaction(scoped_ptr<HttpTransaction>* trans);
  virtual HttpCache* GetCache();
  virtual HttpNetworkSession* GetSession();
  virtual void Suspend(bool suspend);

  // Helper function for reading response info from the disk cache.  If the
  // cache doesn't have the whole resource *|request_truncated| is set to true.
  // Avoid this function for performance critical paths as it uses blocking IO.
  static bool ReadResponseInfo(disk_cache::Entry* disk_entry,
                               HttpResponseInfo* response_info,
                               bool* response_truncated);

  // Helper function for writing response info into the disk cache.  If the
  // cache doesn't have the whole resource |request_truncated| should be true.
  // Avoid this function for performance critical paths as it uses blocking IO.
  static bool WriteResponseInfo(disk_cache::Entry* disk_entry,
                                const HttpResponseInfo* response_info,
                                bool skip_transient_headers,
                                bool response_truncated);

  // Given a header data blob, convert it to a response info object.
  static bool ParseResponseInfo(const char* data, int len,
                                HttpResponseInfo* response_info,
                                bool* response_truncated);

  // Get/Set the cache's mode.
  void set_mode(Mode value) { mode_ = value; }
  Mode mode() { return mode_; }

  void set_type(CacheType type) { type_ = type; }
  CacheType type() { return type_; }

  // Close currently active sockets so that fresh page loads will not use any
  // recycled connections.  For sockets currently in use, they may not close
  // immediately, but they will not be reusable. This is for debugging.
  void CloseCurrentConnections();

  void set_enable_range_support(bool value) {
    enable_range_support_ = value;
  }

 private:

  // Types --------------------------------------------------------------------

  class BackendCallback;
  class Transaction;
  class WorkItem;
  friend class Transaction;
  struct NewEntry;  // Info for an entry under construction.

  typedef std::list<Transaction*> TransactionList;
  typedef std::list<WorkItem*> WorkItemList;

  struct ActiveEntry {
    disk_cache::Entry* disk_entry;
    Transaction*       writer;
    TransactionList    readers;
    TransactionList    pending_queue;
    bool               will_process_pending_queue;
    bool               doomed;

    explicit ActiveEntry(disk_cache::Entry*);
    ~ActiveEntry();
  };

  typedef base::hash_map<std::string, ActiveEntry*> ActiveEntriesMap;
  typedef base::hash_map<std::string, NewEntry*> NewEntriesMap;
  typedef std::set<ActiveEntry*> ActiveEntriesSet;


  // Methods ------------------------------------------------------------------

  // Generates the cache key for this request.
  std::string GenerateCacheKey(const HttpRequestInfo*);

  // Dooms the entry selected by |key|. |callback| is used for completion
  // notification if this function returns ERR_IO_PENDING. The entry can be
  // currently in use or not.
  int DoomEntry(const std::string& key, CompletionCallback* callback);

  // Dooms the entry selected by |key|. |callback| is used for completion
  // notification if this function returns ERR_IO_PENDING. The entry should not
  // be currently in use.
  int AsyncDoomEntry(const std::string& key, CompletionCallback* callback);

  // Closes a previously doomed entry.
  void FinalizeDoomedEntry(ActiveEntry* entry);

  // Returns an entry that is currently in use and not doomed, or NULL.
  ActiveEntry* FindActiveEntry(const std::string& key);

  // Creates a new ActiveEntry and starts tracking it. |disk_entry| is the disk
  // cache entry that corresponds to the desired |key|.
  // TODO(rvargas): remove the |key| argument.
  ActiveEntry* ActivateEntry(const std::string& key,
                             disk_cache::Entry* disk_entry);

  // Deletes an ActiveEntry.
  void DeactivateEntry(ActiveEntry* entry);

  // Deletes an ActiveEntry using an exhaustive search.
  void SlowDeactivateEntry(ActiveEntry* entry);

  // Returns the NewEntry for the desired |key|. If an entry is not under
  // construction already, a new NewEntry structure is created.
  NewEntry* GetNewEntry(const std::string& key);

  // Deletes a NewEntry.
  void DeleteNewEntry(NewEntry* entry);

  // Opens the disk cache entry associated with |key|, returning an ActiveEntry
  // in |*entry|. |callback| is used for completion notification if this
  // function returns ERR_IO_PENDING.
  int OpenEntry(const std::string& key, ActiveEntry** entry,
                CompletionCallback* callback);

  // Creates the disk cache entry associated with |key|, returning an
  // ActiveEntry in |*entry|. |callback| is used for completion notification if
  // this function returns ERR_IO_PENDING.
  int CreateEntry(const std::string& key, ActiveEntry** entry,
                  CompletionCallback* callback);

  // Destroys an ActiveEntry (active or doomed).
  void DestroyEntry(ActiveEntry* entry);

  // Adds a transaction to an ActiveEntry.
  int AddTransactionToEntry(ActiveEntry* entry, Transaction* trans);

  // Called when the transaction has finished working with this entry. |cancel|
  // is true if the operation was cancelled by the caller instead of running
  // to completion.
  void DoneWithEntry(ActiveEntry* entry, Transaction* trans, bool cancel);

  // Called when the transaction has finished writting to this entry. |success|
  // is false if the cache entry should be deleted.
  void DoneWritingToEntry(ActiveEntry* entry, bool success);

  // Called when the transaction has finished reading from this entry.
  void DoneReadingFromEntry(ActiveEntry* entry, Transaction* trans);

  // Convers the active writter transaction to a reader so that other
  // transactions can start reading from this entry.
  void ConvertWriterToReader(ActiveEntry* entry);

  // Removes the transaction |trans|, waiting for |callback|, from the pending
  // list of an entry (NewEntry, active or doomed entry).
  void RemovePendingTransaction(Transaction* trans, CompletionCallback* cb);

  // Removes the transaction |trans|, from the pending list of |entry|.
  bool RemovePendingTransactionFromEntry(ActiveEntry* entry,
                                         Transaction* trans);

  // Removes the callback |cb|, from the pending list of |entry|.
  bool RemovePendingCallbackFromNewEntry(NewEntry* entry,
                                         CompletionCallback* cb);

  // Resumes processing the pending list of |entry|.
  void ProcessPendingQueue(ActiveEntry* entry);

  // Events (called via PostTask) ---------------------------------------------

  void OnProcessPendingQueue(ActiveEntry* entry);

  // Callbacks ----------------------------------------------------------------

  // Processes BackendCallback notifications.
  void OnIOComplete(int result, NewEntry* entry);


  // Variables ----------------------------------------------------------------

  // Used when lazily constructing the disk_cache_.
  FilePath disk_cache_dir_;

  Mode mode_;
  CacheType type_;

  scoped_ptr<HttpTransactionFactory> network_layer_;
  scoped_ptr<disk_cache::Backend> disk_cache_;

  // The set of active entries indexed by cache key.
  ActiveEntriesMap active_entries_;

  // The set of doomed entries.
  ActiveEntriesSet doomed_entries_;

  // The set of entries "under construction".
  NewEntriesMap new_entries_;

  ScopedRunnableMethodFactory<HttpCache> task_factory_;

  bool enable_range_support_;
  int cache_size_;

  typedef base::hash_map<std::string, int> PlaybackCacheMap;
  scoped_ptr<PlaybackCacheMap> playback_cache_map_;

  DISALLOW_COPY_AND_ASSIGN(HttpCache);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_CACHE_H_
