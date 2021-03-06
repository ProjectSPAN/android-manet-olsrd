// Copyright 2007, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <string.h>
#include <vector>

#include "googleurl/src/url_util.h"

#include "base/logging.h"
#include "googleurl/src/url_file.h"

namespace url_util {

namespace {

// ASCII-specific tolower.  The standard library's tolower is locale sensitive,
// so we don't want to use it here.
template <class Char> inline Char ToLowerASCII(Char c) {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

// Backend for LowerCaseEqualsASCII.
template<typename Iter>
inline bool DoLowerCaseEqualsASCII(Iter a_begin, Iter a_end, const char* b) {
  for (Iter it = a_begin; it != a_end; ++it, ++b) {
    if (!*b || ToLowerASCII(*it) != *b)
      return false;
  }
  return *b == 0;
}

const char kFileScheme[] = "file";  // Used in a number of places.
const char kMailtoScheme[] = "mailto";

const int kNumStandardURLSchemes = 5;
const char* kStandardURLSchemes[kNumStandardURLSchemes] = {
  "http",
  "https",
  kFileScheme,  // Yes, file urls can have a hostname!
  "ftp",
  "gopher",
};

// List of the currently installed standard schemes. This list is lazily
// initialized by InitStandardSchemes and is leaked on shutdown to prevent
// any destructors from being called that will slow us down or cause problems.
std::vector<const char*>* standard_schemes = NULL;

// Ensures that the standard_schemes list is initialized, does nothing if it
// already has values.
void InitStandardSchemes() {
  if (standard_schemes)
    return;
  standard_schemes = new std::vector<const char*>;
  for (int i = 0; i < kNumStandardURLSchemes; i++)
    standard_schemes->push_back(kStandardURLSchemes[i]);
}

// Given a string and a range inside the string, compares it to the given
// lower-case |compare_to| buffer.
template<typename CHAR>
inline bool CompareSchemeComponent(const CHAR* spec,
                                   const url_parse::Component& component,
                                   const char* compare_to) {
  if (!component.is_nonempty())
    return compare_to[0] == 0;  // When component is empty, match empty scheme.
  return LowerCaseEqualsASCII(&spec[component.begin],
                              &spec[component.end()],
                              compare_to);
}

// Returns true if the given scheme identified by |scheme| within |spec| is one
// of the registered "standard" schemes. Note that this does not check for
// "://", use IsStandard for that.
template<typename CHAR>
bool IsStandardScheme(const CHAR* spec, const url_parse::Component& scheme) {
  if (!scheme.is_nonempty())
    return false;  // Empty or invalid schemes are non-standard.

  InitStandardSchemes();
  for (size_t i = 0; i < standard_schemes->size(); i++) {
    if (LowerCaseEqualsASCII(&spec[scheme.begin], &spec[scheme.end()],
                             standard_schemes->at(i)))
      return true;
  }
  return false;
}

// Returns true if the stuff following the scheme in the given spec indicates
// a "standard" URL. The presence of "://" after the scheme indicates that
// there is a hostname, etc. which we call a standard URL.
template<typename CHAR>
bool HasStandardSchemeSeparator(const CHAR* spec, int spec_len,
                                const url_parse::Component& scheme) {
  int after_scheme = scheme.end();
  if (spec_len < after_scheme + 3)
    return false;
  return spec[after_scheme] == ':' &&
         spec[after_scheme + 1] == '/' &&
         spec[after_scheme + 2] == '/';
}

template<typename CHAR>
bool DoIsStandard(const CHAR* spec, int spec_len,
                  const url_parse::Component& scheme) {
  return HasStandardSchemeSeparator(spec, spec_len, scheme) ||
         IsStandardScheme(spec, scheme);
}

template<typename CHAR>
bool DoFindAndCompareScheme(const CHAR* str,
                            int str_len,
                            const char* compare,
                            url_parse::Component* found_scheme) {
  url_parse::Component our_scheme;
  if (!url_parse::ExtractScheme(str, str_len, &our_scheme)) {
    // No scheme.
    if (found_scheme)
      *found_scheme = url_parse::Component();
    return false;
  }
  if (found_scheme)
    *found_scheme = our_scheme;
  return CompareSchemeComponent(str, our_scheme, compare);
}

template<typename CHAR>
bool DoCanonicalize(const CHAR* in_spec, int in_spec_len,
                    url_canon::CharsetConverter* charset_converter,
                    url_canon::CanonOutput* output,
                    url_parse::Parsed* output_parsed) {
  // Remove any whitespace from the middle of the relative URL, possibly
  // copying to the new buffer.
  url_canon::RawCanonOutputT<CHAR> whitespace_buffer;
  int spec_len;
  const CHAR* spec = RemoveURLWhitespace(in_spec, in_spec_len,
                                         &whitespace_buffer, &spec_len);

  url_parse::Parsed parsed_input;
#ifdef WIN32
  // For Windows, we allow things that look like absolute Windows paths to be
  // fixed up magically to file URLs. This is done for IE compatability. For
  // example, this will change "c:/foo" into a file URL rather than treating
  // it as a URL with the protocol "c". It also works for UNC ("\\foo\bar.txt").
  // There is similar logic in url_canon_relative.cc for
  //
  // For Max & Unix, we don't do this (the equivalent would be "/foo/bar" which
  // has no meaning as an absolute path name. This is because browsers on Mac
  // & Unix don't generally do this, so there is no compatibility reason for
  // doing so.
  if (url_parse::DoesBeginUNCPath(spec, 0, spec_len, false) ||
      url_parse::DoesBeginWindowsDriveSpec(spec, 0, spec_len)) {
    url_parse::ParseFileURL(spec, spec_len, &parsed_input);
    return url_canon::CanonicalizeFileURL(spec, spec_len, parsed_input,
                                           charset_converter,
                                           output, output_parsed);
  }
#endif

  url_parse::Component scheme;
  if(!url_parse::ExtractScheme(spec, spec_len, &scheme))
    return false;

  // This is the parsed version of the input URL, we have to canonicalize it
  // before storing it in our object.
  bool success;
  if (CompareSchemeComponent(spec, scheme, kFileScheme)) {
    // File URLs are special.
    url_parse::ParseFileURL(spec, spec_len, &parsed_input);
    success = url_canon::CanonicalizeFileURL(spec, spec_len, parsed_input,
                                             charset_converter,
                                             output, output_parsed);

  } else if (IsStandard(spec, spec_len, scheme)) {
    // All "normal" URLs.
    url_parse::ParseStandardURL(spec, spec_len, &parsed_input);
    success = url_canon::CanonicalizeStandardURL(spec, spec_len, parsed_input,
                                                 charset_converter,
                                                 output, output_parsed);

  } else if (CompareSchemeComponent(spec, scheme, kMailtoScheme)) {
    // Mailto are treated like a standard url with only a scheme, path, query
    url_parse::ParseMailtoURL(spec, spec_len, &parsed_input);
    success = url_canon::CanonicalizeMailtoURL(spec, spec_len, parsed_input,
                                               output, output_parsed);

  } else {
    // "Weird" URLs like data: and javascript:
    url_parse::ParsePathURL(spec, spec_len, &parsed_input);
    success = url_canon::CanonicalizePathURL(spec, spec_len, parsed_input,
                                             output, output_parsed);
  }
  return success;
}

template<typename CHAR>
bool DoResolveRelative(const char* base_spec,
                       int base_spec_len,
                       const url_parse::Parsed& base_parsed,
                       const CHAR* in_relative,
                       int in_relative_length,
                       url_canon::CharsetConverter* charset_converter,
                       url_canon::CanonOutput* output,
                       url_parse::Parsed* output_parsed) {
  // Remove any whitespace from the middle of the relative URL, possibly
  // copying to the new buffer.
  url_canon::RawCanonOutputT<CHAR> whitespace_buffer;
  int relative_length;
  const CHAR* relative = RemoveURLWhitespace(in_relative, in_relative_length,
                                             &whitespace_buffer,
                                             &relative_length);

  // See if our base URL should be treated as "standard".
  bool standard_base_scheme =
      base_parsed.scheme.is_nonempty() &&
      IsStandard(base_spec, base_spec_len, base_parsed.scheme);

  bool is_relative;
  url_parse::Component relative_component;
  if (!url_canon::IsRelativeURL(base_spec, base_parsed,
                                relative, relative_length,
                                standard_base_scheme,
                                &is_relative,
                                &relative_component)) {
    // Error resolving.
    return false;
  }

  if (is_relative) {
    // Relative, resolve and canonicalize.
    bool file_base_scheme = base_parsed.scheme.is_nonempty() &&
        CompareSchemeComponent(base_spec, base_parsed.scheme, kFileScheme);
    return url_canon::ResolveRelativeURL(base_spec, base_parsed,
                                         file_base_scheme, relative,
                                         relative_component, charset_converter,
                                         output, output_parsed);
  }

  // Not relative, canonicalize the input.
  return DoCanonicalize(relative, relative_length, charset_converter,
                        output, output_parsed);
}

template<typename CHAR>
bool DoReplaceComponents(const char* spec,
                         int spec_len,
                         const url_parse::Parsed& parsed,
                         const url_canon::Replacements<CHAR>& replacements,
                         url_canon::CharsetConverter* charset_converter,
                         url_canon::CanonOutput* output,
                         url_parse::Parsed* out_parsed) {
  // Note that we dispatch to the parser according the the scheme type of
  // the OUTPUT URL. Normally, this is the same as our scheme, but if the
  // scheme is being overridden, we need to test that.

  if (// Either the scheme is not replaced and the old one is a file,
      (!replacements.IsSchemeOverridden() &&
       CompareSchemeComponent(spec, parsed.scheme, kFileScheme)) ||
      // ...or it is being replaced and the new one is a file.
      (replacements.IsSchemeOverridden() &&
       CompareSchemeComponent(replacements.sources().scheme,
                              replacements.components().scheme,
                              kFileScheme))) {
    return url_canon::ReplaceFileURL(spec, parsed, replacements,
                                     charset_converter, output, out_parsed);
  }

  if (// Either the scheme is not replaced and the old one is standard,
      (!replacements.IsSchemeOverridden() &&
       IsStandard(spec, spec_len, parsed.scheme)) ||
      // ...or it is being replaced and the new one is standard.
      (replacements.IsSchemeOverridden() &&
       IsStandardScheme(replacements.sources().scheme,
                        replacements.components().scheme))) {
    // Standard URL with all parts.
    return url_canon::ReplaceStandardURL(spec, parsed, replacements,
                                         charset_converter, output, out_parsed);
  }

  if (// Either the scheme is not replaced and the old one is mailto,
      (!replacements.IsSchemeOverridden() &&
       CompareSchemeComponent(spec, parsed.scheme, kMailtoScheme)) ||
      // ...or it is being replaced and the new one is a mailto.
      (replacements.IsSchemeOverridden() &&
       CompareSchemeComponent(replacements.sources().scheme,
                              replacements.components().scheme,
                              kMailtoScheme))) {
     return url_canon::ReplaceMailtoURL(spec, parsed, replacements,
                                        output, out_parsed);
  }

  return url_canon::ReplacePathURL(spec, parsed, replacements,
                                   output, out_parsed);
}

}  // namespace

void AddStandardScheme(const char* new_scheme) {
  size_t scheme_len = strlen(new_scheme);
  if (scheme_len == 0)
    return;

  // Dulicate the scheme into a new buffer and add it to the list of standard
  // schemes. This pointer will be leaked on shutdown.
  char* dup_scheme = new char[scheme_len + 1];
  memcpy(dup_scheme, new_scheme, scheme_len + 1);

  InitStandardSchemes();
  standard_schemes->push_back(dup_scheme);
}

bool IsStandard(const char* spec, int spec_len,
                const url_parse::Component& scheme) {
  return DoIsStandard(spec, spec_len, scheme);
}

bool IsStandard(const char16* spec, int spec_len,
                const url_parse::Component& scheme) {
  return DoIsStandard(spec, spec_len, scheme);
}

bool FindAndCompareScheme(const char* str,
                          int str_len,
                          const char* compare,
                          url_parse::Component* found_scheme) {
  return DoFindAndCompareScheme(str, str_len, compare, found_scheme);
}

bool FindAndCompareScheme(const char16* str,
                          int str_len,
                          const char* compare,
                          url_parse::Component* found_scheme) {
  return DoFindAndCompareScheme(str, str_len, compare, found_scheme);
}

bool Canonicalize(const char* spec,
                  int spec_len,
                  url_canon::CharsetConverter* charset_converter,
                  url_canon::CanonOutput* output,
                  url_parse::Parsed* output_parsed) {
  return DoCanonicalize(spec, spec_len, charset_converter,
                        output, output_parsed);
}

bool Canonicalize(const char16* spec,
                  int spec_len,
                  url_canon::CharsetConverter* charset_converter,
                  url_canon::CanonOutput* output,
                  url_parse::Parsed* output_parsed) {
  return DoCanonicalize(spec, spec_len, charset_converter,
                        output, output_parsed);
}

bool ResolveRelative(const char* base_spec,
                     int base_spec_len,
                     const url_parse::Parsed& base_parsed,
                     const char* relative,
                     int relative_length,
                     url_canon::CharsetConverter* charset_converter,
                     url_canon::CanonOutput* output,
                     url_parse::Parsed* output_parsed) {
  return DoResolveRelative(base_spec, base_spec_len, base_parsed,
                           relative, relative_length,
                           charset_converter, output, output_parsed);
}

bool ResolveRelative(const char* base_spec,
                     int base_spec_len,
                     const url_parse::Parsed& base_parsed,
                     const char16* relative,
                     int relative_length,
                     url_canon::CharsetConverter* charset_converter,
                     url_canon::CanonOutput* output,
                     url_parse::Parsed* output_parsed) {
  return DoResolveRelative(base_spec, base_spec_len, base_parsed,
                           relative, relative_length,
                           charset_converter, output, output_parsed);
}

bool ReplaceComponents(const char* spec,
                       int spec_len,
                       const url_parse::Parsed& parsed,
                       const url_canon::Replacements<char>& replacements,
                       url_canon::CharsetConverter* charset_converter,
                       url_canon::CanonOutput* output,
                       url_parse::Parsed* out_parsed) {
  return DoReplaceComponents(spec, spec_len, parsed, replacements,
                             charset_converter, output, out_parsed);
}

bool ReplaceComponents(const char* spec,
                       int spec_len,
                       const url_parse::Parsed& parsed,
                       const url_canon::Replacements<char16>& replacements,
                       url_canon::CharsetConverter* charset_converter,
                       url_canon::CanonOutput* output,
                       url_parse::Parsed* out_parsed) {
  return DoReplaceComponents(spec, spec_len, parsed, replacements,
                             charset_converter, output, out_parsed);
}

// Front-ends for LowerCaseEqualsASCII.
bool LowerCaseEqualsASCII(const char* a_begin,
                          const char* a_end,
                          const char* b) {
  return DoLowerCaseEqualsASCII(a_begin, a_end, b);
}

bool LowerCaseEqualsASCII(const char* a_begin,
                          const char* a_end,
                          const char* b_begin,
                          const char* b_end) {
  while (a_begin != a_end && b_begin != b_end &&
         ToLowerASCII(*a_begin) == *b_begin) {
    a_begin++;
    b_begin++;
  }
  return a_begin == a_end && b_begin == b_end;
}

bool LowerCaseEqualsASCII(const char16* a_begin,
                          const char16* a_end,
                          const char* b) {
  return DoLowerCaseEqualsASCII(a_begin, a_end, b);
}

}  // namespace url_util
