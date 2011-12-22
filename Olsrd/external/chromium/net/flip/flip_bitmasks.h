// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FLIP_FLIP_BITMASKS_H_
#define NET_FLIP_FLIP_BITMASKS_H_

namespace flip {

// StreamId mask from the FlipHeader
const unsigned int kStreamIdMask = 0x7fffffff;

// Control flag mask from the FlipHeader
const unsigned int kControlFlagMask = 0x8000;

// Priority mask from the SYN_FRAME
const unsigned int kPriorityMask = 0xc0;

// Mask the lower 24 bits.
const unsigned int kLengthMask = 0xffffff;

}  // flip

#endif  // NET_FLIP_FLIP_BITMASKS_H_
