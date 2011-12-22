// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_NEW_FTP_JOB_H_
#define NET_URL_REQUEST_URL_REQUEST_NEW_FTP_JOB_H_

#include <string>

#include "net/base/auth.h"
#include "net/base/completion_callback.h"
#include "net/ftp/ftp_request_info.h"
#include "net/ftp/ftp_transaction.h"
#include "net/url_request/url_request_job.h"

class URLRequestContext;

namespace net {
struct list_state;
}

// A URLRequestJob subclass that is built on top of FtpTransaction. It
// provides an implementation for FTP.
class URLRequestNewFtpJob : public URLRequestJob {
 public:

  explicit URLRequestNewFtpJob(URLRequest* request);

  static URLRequestJob* Factory(URLRequest* request, const std::string& scheme);

  // URLRequestJob methods:
  virtual bool GetMimeType(std::string* mime_type) const;

 private:
  virtual ~URLRequestNewFtpJob();

  // URLRequestJob methods:
  virtual void Start();
  virtual void Kill();
  virtual net::LoadState GetLoadState() const;
  virtual bool NeedsAuth();
  virtual void GetAuthChallengeInfo(
      scoped_refptr<net::AuthChallengeInfo>* auth_info);
  virtual void SetAuth(const std::wstring& username,
                       const std::wstring& password);
  virtual void CancelAuth();

  // TODO(ibrar):  Yet to give another look at this function.
  virtual uint64 GetUploadProgress() const { return 0; }
  virtual bool ReadRawData(net::IOBuffer* buf, int buf_size, int *bytes_read);

  void DestroyTransaction();
  void StartTransaction();

  void OnStartCompleted(int result);
  void OnReadCompleted(int result);

  void RestartTransactionWithAuth();

  void LogFtpServerType(char server_type);

  net::FtpRequestInfo request_info_;
  scoped_ptr<net::FtpTransaction> transaction_;

  net::CompletionCallbackImpl<URLRequestNewFtpJob> start_callback_;
  net::CompletionCallbackImpl<URLRequestNewFtpJob> read_callback_;

  bool read_in_progress_;

  scoped_refptr<net::AuthData> server_auth_;

  // Keep a reference to the url request context to be sure it's not deleted
  // before us.
  scoped_refptr<URLRequestContext> context_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestNewFtpJob);
};

#endif  // NET_URL_REQUEST_URL_REQUEST_NEW_FTP_JOB_H_
