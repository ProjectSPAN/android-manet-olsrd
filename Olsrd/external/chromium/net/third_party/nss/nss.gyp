# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'conditions': [
    [ 'OS == "linux"', {
      'conditions': [
        ['sysroot!=""', {
          'variables': {
            'pkg-config': './pkg-config-wrapper "<(sysroot)"',
          },
        }, {
          'variables': {
            'pkg-config': 'pkg-config'
          },
        }],
      ],
    }],
  ],

  'targets': [
    {
      'target_name': 'ssl',
      'product_name': 'ssl',
      'type': '<(library)',
      'sources': [
        'ssl/authcert.c',
        'ssl/cmpcert.c',
        'ssl/derive.c',
        'ssl/nsskea.c',
        'ssl/os2_err.c',
        'ssl/os2_err.h',
        'ssl/preenc.h',
        'ssl/prelib.c',
        'ssl/ssl.h',
        'ssl/ssl3con.c',
        'ssl/ssl3ecc.c',
        'ssl/ssl3ext.c',
        'ssl/ssl3gthr.c',
        'ssl/ssl3prot.h',
        'ssl/sslauth.c',
        'ssl/sslcon.c',
        'ssl/ssldef.c',
        'ssl/sslenum.c',
        'ssl/sslerr.c',
        'ssl/sslerr.h',
        'ssl/sslgathr.c',
        'ssl/sslimpl.h',
        'ssl/sslinfo.c',
        'ssl/sslmutex.c',
        'ssl/sslmutex.h',
        'ssl/sslnonce.c',
        'ssl/sslproto.h',
        'ssl/sslreveal.c',
        'ssl/sslsecur.c',
        'ssl/sslsnce.c',
        'ssl/sslsock.c',
        'ssl/sslt.h',
        'ssl/ssltrace.c',
        'ssl/sslver.c',
        'ssl/unix_err.c',
        'ssl/unix_err.h',
        'ssl/win32err.c',
        'ssl/win32err.h',
        'ssl/bodge/loader.c',
        'ssl/bodge/loader.h',
        'ssl/bodge/secure_memcmp.c',
      ],
      'defines': [
        'NSS_ENABLE_ECC',
        'NSS_ENABLE_ZLIB',
        'USE_UTIL_DIRECTLY',
      ],
      'defines!': [
        # Regrettably, NSS can't be compiled with NO_NSPR_10_SUPPORT yet.
        'NO_NSPR_10_SUPPORT',
      ],
      'conditions': [
        [ 'OS == "linux"', {
          'sources!': [
            'ssl/os2_err.c',
            'ssl/os2_err.h',
            'ssl/win32err.c',
            'ssl/win32err.h',
          ],
          'defines': [
            # These macros are needed only for compiling the files in
            # ssl/bodge.
            'SHLIB_PREFIX="lib"',
            'SHLIB_SUFFIX="so"',
            'SHLIB_VERSION="3"',
            'SOFTOKEN_SHLIB_VERSION="3"',
          ],
          'include_dirs': [
            'ssl/bodge',
          ],
          'cflags': [
            '<!@(<(pkg-config) --cflags nss)',
          ],
          'ldflags': [
            '<!@(<(pkg-config) --libs-only-L --libs-only-other nss)',
          ],
          'libraries': [
            '<!@(<(pkg-config) --libs-only-l nss | sed -e "s/-lssl3//")',
          ],
        }],
        [ 'OS == "win"', {
          'sources/': [
            ['exclude', 'ssl/bodge/'],
          ],
          'sources!': [
            'ssl/os2_err.c',
            'ssl/os2_err.h',
            'ssl/unix_err.c',
            'ssl/unix_err.h',
          ],
          'dependencies': [
            '../../../third_party/zlib/zlib.gyp:zlib',
            '../../../third_party/nss/nss.gyp:nss',
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              'ssl',
            ],
          },
        }],
      ],
    },
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
