// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/dump_cache/cache_dumper.h"

#include "net/base/io_buffer.h"
#include "net/disk_cache/entry_impl.h"
#include "net/http/http_cache.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/tools/dump_cache/url_to_filename_encoder.h"

bool CacheDumper::CreateEntry(const std::string& key,
                              disk_cache::Entry** entry) {
  return cache_->CreateEntry(key, entry);
}

bool CacheDumper::WriteEntry(disk_cache::Entry* entry, int index, int offset,
                             net::IOBuffer* buf, int buf_len) {
  int written = entry->WriteData(index, offset, buf, buf_len, NULL, false);
  return written == buf_len;
}

void CacheDumper::CloseEntry(disk_cache::Entry* entry, base::Time last_used,
                             base::Time last_modified) {
  if (entry) {
    static_cast<disk_cache::EntryImpl*>(entry)->SetTimes(last_used,
                                                         last_modified);
    entry->Close();
  }
}

// A version of CreateDirectory which supports lengthy filenames.
// Returns true on success, false on failure.
bool SafeCreateDirectory(const std::wstring& path) {
#ifdef WIN32_LARGE_FILENAME_SUPPORT
  // Due to large paths on windows, it can't simply do a
  // CreateDirectory("a/b/c").  Instead, create each subdirectory manually.
  bool rv = false;
  std::wstring::size_type pos(0);
  std::wstring backslash(L"\\");

  // If the path starts with the long file header, skip over that
  const std::wstring kLargeFilenamePrefix(L"\\\\?\\");
  std::wstring header(kLargeFilenamePrefix);
  if (path.find(header) == 0)
    pos = 4;

  // Create the subdirectories individually
  while((pos = path.find(backslash, pos)) != std::wstring::npos) {
    std::wstring subdir = path.substr(0, pos);
    CreateDirectoryW(subdir.c_str(), NULL);
    // we keep going even if directory creation failed.
    pos++;
  }
  // Now create the full path
  return CreateDirectoryW(path.c_str(), NULL) == TRUE;
#else
  return file_util::CreateDirectory(path);
#endif
}

bool DiskDumper::CreateEntry(const std::string& key,
                             disk_cache::Entry** entry) {
  FilePath path(path_);
  entry_path_ = net::UrlToFilenameEncoder::Encode(key, path);

#ifdef WIN32_LARGE_FILENAME_SUPPORT
  // In order for long filenames to work, we'll need to prepend
  // the windows magic token.
  const std::wstring kLongFilenamePrefix(L"\\\\?\\");
  // There is no way to prepend to a filename.  We simply *have*
  // to convert to a wstring to do this.
  std::wstring name = kLongFilenamePrefix;
  name.append(entry_path_.ToWStringHack());
  entry_path_ = FilePath(name);
#endif

  entry_url_ = key;

  FilePath directory = entry_path_.DirName();
  SafeCreateDirectory(directory.value());

  std::wstring file = entry_path_.value();
#ifdef WIN32_LARGE_FILENAME_SUPPORT
  entry_ = CreateFileW(file.c_str(), GENERIC_WRITE|GENERIC_READ, 0, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  return entry_ != INVALID_HANDLE_VALUE;
#else
  entry_ = file_util::OpenFile(entry_path_, "w+");
  return entry_ != NULL;
#endif
}

// Utility Function to create a normalized header string from a
// HttpResponseInfo.  The output will be formatted exactly
// like so:
//     HTTP/<version> <status_code> <status_text>\n
//     [<header-name>: <header-values>\n]*
// meaning, each line is \n-terminated, and there is no extra whitespace
// beyond the single space separators shown (of course, values can contain
// whitespace within them).  If a given header-name appears more than once
// in the set of headers, they are combined into a single line like so:
//     <header-name>: <header-value1>, <header-value2>, ...<header-valueN>\n
//
// DANGER: For some headers (e.g., "Set-Cookie"), the normalized form can be
// a lossy format.  This is due to the fact that some servers generate
// Set-Cookie headers that contain unquoted commas (usually as part of the
// value of an "expires" attribute).  So, use this function with caution.  Do
// not expect to be able to re-parse Set-Cookie headers from this output.
//
// NOTE: Do not make any assumptions about the encoding of this output
// string.  It may be non-ASCII, and the encoding used by the server is not
// necessarily known to us.  Do not assume that this output is UTF-8!
void GetNormalizedHeaders(const net::HttpResponseInfo& info,
                          std::string* output) {
  // Start with the status line
  output->assign(info.headers->GetStatusLine());
  output->append("\r\n");

  // Enumerate the headers
  void* iter = 0;
  std::string name, value;
  while (info.headers->EnumerateHeaderLines(&iter, &name, &value)) {
    output->append(name);
    output->append(": ");
    output->append(value);
    output->append("\r\n");
  }

  // Mark the end of headers
  output->append("\r\n");
}

bool DiskDumper::WriteEntry(disk_cache::Entry* entry, int index, int offset,
                            net::IOBuffer* buf, int buf_len) {
  if (!entry_)
    return false;

  std::string headers;
  const char *data;
  int len;
  if (index == 0) {  // Stream 0 is the headers.
    net::HttpResponseInfo response_info;
    bool truncated;
    if (!net::HttpCache::ParseResponseInfo(buf->data(), buf_len,
                                           &response_info, &truncated))
      return false;

    // Skip this entry if it was truncated (results in an empty file).
    if (truncated)
      return true;

    // Remove the size headers.
    response_info.headers->RemoveHeader("transfer-encoding");
    response_info.headers->RemoveHeader("content-length");
    response_info.headers->RemoveHeader("x-original-url");

    // Convert the headers into a string ending with LF.
    GetNormalizedHeaders(response_info, &headers);

    // Append a header for the original URL.
    std::string url = entry_url_;
    // strip off the "XXGET" which may be in the key.
    std::string::size_type pos(0);
    if ((pos = url.find("http")) != 0) {
      if (pos != std::string::npos)
        url = url.substr(pos);
    }
    std::string x_original_url = "X-Original-Url: " + url + "\r\n";
    // we know that the last two bytes are CRLF.
    headers.replace(headers.length() - 2, 0, x_original_url);

    data = headers.c_str();
    len = headers.size();
  } else if (index == 1) {  // Stream 1 is the data.
    data = buf->data();
    len = buf_len;
  }
#ifdef WIN32_LARGE_FILENAME_SUPPORT
  DWORD bytes;
  DWORD rv = WriteFile(entry_, data, len, &bytes, 0);
  return rv == TRUE && bytes == len;
#else
  int bytes = fwrite(data, 1, len, entry_);
  return bytes == len;
#endif
}

void DiskDumper::CloseEntry(disk_cache::Entry* entry, base::Time last_used,
                          base::Time last_modified) {
#ifdef WIN32_LARGE_FILENAME_SUPPORT
  CloseHandle(entry_);
#else
  file_util::CloseFile(entry_);
#endif
}

