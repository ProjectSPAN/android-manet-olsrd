// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "net/base/mime_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
  class MimeUtilTest : public testing::Test {
  };
}

TEST(MimeUtilTest, ExtensionTest) {
  const struct {
    const FilePath::CharType* extension;
    const char* mime_type;
    bool valid;
  } tests[] = {
    { FILE_PATH_LITERAL("png"), "image/png", true },
    { FILE_PATH_LITERAL("css"), "text/css", true },
    { FILE_PATH_LITERAL("pjp"), "image/jpeg", true },
    { FILE_PATH_LITERAL("pjpeg"), "image/jpeg", true },
    { FILE_PATH_LITERAL("not an extension / for sure"), "", false },
  };

  std::string mime_type;
  bool rv;

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(tests); ++i) {
    rv = net::GetMimeTypeFromExtension(tests[i].extension, &mime_type);
    EXPECT_EQ(tests[i].valid, rv);
    if (rv)
      EXPECT_EQ(tests[i].mime_type, mime_type);
  }
}

TEST(MimeUtilTest, FileTest) {
  const struct {
    const FilePath::CharType* file_path;
    const char* mime_type;
    bool valid;
  } tests[] = {
    { FILE_PATH_LITERAL("c:\\foo\\bar.css"), "text/css", true },
    { FILE_PATH_LITERAL("c:\\blah"), "", false },
    { FILE_PATH_LITERAL("/usr/local/bin/mplayer"), "", false },
    { FILE_PATH_LITERAL("/home/foo/bar.css"), "text/css", true },
    { FILE_PATH_LITERAL("/blah."), "", false },
    { FILE_PATH_LITERAL("c:\\blah."), "", false },
  };

  std::string mime_type;
  bool rv;

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(tests); ++i) {
    rv = net::GetMimeTypeFromFile(FilePath(tests[i].file_path),
                                  &mime_type);
    EXPECT_EQ(tests[i].valid, rv);
    if (rv)
      EXPECT_EQ(tests[i].mime_type, mime_type);
  }
}

TEST(MimeUtilTest, LookupTypes) {
  EXPECT_EQ(true, net::IsSupportedImageMimeType("image/jpeg"));
  EXPECT_EQ(false, net::IsSupportedImageMimeType("image/lolcat"));
  EXPECT_EQ(true, net::IsSupportedNonImageMimeType("text/html"));
  EXPECT_EQ(false, net::IsSupportedNonImageMimeType("text/virus"));

  EXPECT_EQ(true, net::IsSupportedMimeType("image/jpeg"));
  EXPECT_EQ(false, net::IsSupportedMimeType("image/lolcat"));
  EXPECT_EQ(true, net::IsSupportedMimeType("text/html"));
  EXPECT_EQ(false, net::IsSupportedMimeType("text/virus"));
}

TEST(MimeUtilTest, MatchesMimeType) {
  EXPECT_EQ(true, net::MatchesMimeType("*", "video/x-mpeg"));
  EXPECT_EQ(true, net::MatchesMimeType("video/*", "video/x-mpeg"));
  EXPECT_EQ(true, net::MatchesMimeType("video/x-mpeg", "video/x-mpeg"));
  EXPECT_EQ(true, net::MatchesMimeType("application/*+xml",
                                             "application/html+xml"));
  EXPECT_EQ(true, net::MatchesMimeType("application/*+xml",
                                             "application/+xml"));
  EXPECT_EQ(true, net::MatchesMimeType("aaa*aaa",
                                             "aaaaaa"));
  EXPECT_EQ(false, net::MatchesMimeType("video/", "video/x-mpeg"));
  EXPECT_EQ(false, net::MatchesMimeType("", "video/x-mpeg"));
  EXPECT_EQ(false, net::MatchesMimeType("", ""));
  EXPECT_EQ(false, net::MatchesMimeType("video/x-mpeg", ""));
  EXPECT_EQ(false, net::MatchesMimeType("application/*+xml",
                                              "application/xml"));
  EXPECT_EQ(false, net::MatchesMimeType("application/*+xml",
                                              "application/html+xmlz"));
  EXPECT_EQ(false, net::MatchesMimeType("application/*+xml",
                                              "applcation/html+xml"));
  EXPECT_EQ(false, net::MatchesMimeType("aaa*aaa",
                                              "aaaaa"));
}

// Note: codecs should only be a list of 2 or fewer; hence the restriction of
// results' length to 2.
TEST(MimeUtilTest, ParseCodecString) {
  const struct {
    const char* original;
    size_t expected_size;
    const char* results[2];
  } tests[] = {
    { "\"bogus\"",                  1, { "bogus" }            },
    { "0",                          1, { "0" }                },
    { "avc1.42E01E, mp4a.40.2",     2, { "avc1",   "mp4a" }   },
    { "\"mp4v.20.240, mp4a.40.2\"", 2, { "mp4v",   "mp4a" }   },
    { "mp4v.20.8, samr",            2, { "mp4v",   "samr" }   },
    { "\"theora, vorbis\"",         2, { "theora", "vorbis" } },
    { "",                           1, { "" }                 },
    { "\"\"",                       1, { "" }                 },
    { ",",                          2, { "", "" }             },
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(tests); ++i) {
    std::vector<std::string> codecs_out;
    net::ParseCodecString(tests[i].original, &codecs_out);
    EXPECT_EQ(tests[i].expected_size, codecs_out.size());
    for (size_t j = 0; j < tests[i].expected_size; ++j) {
      EXPECT_EQ(tests[i].results[j], codecs_out[j]);
    }
  }
}
