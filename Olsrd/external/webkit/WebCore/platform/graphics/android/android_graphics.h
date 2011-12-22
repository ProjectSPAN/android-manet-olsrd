/*
 * Copyright 2007, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef android_graphics_DEFINED
#define android_graphics_DEFINED

#include "DrawExtra.h"
#include "IntRect.h"
#include "SkTypes.h"
#include "wtf/Vector.h"

namespace WebCore {
    class GraphicsContext;
}

SkCanvas* android_gc2canvas(GraphicsContext* gc);

namespace android {

class CachedFrame;
class CachedNode;
class CachedRoot;
class WebViewCore;

// Data and methods for cursor rings

// used to inflate node cache entry
#define CURSOR_RING_HIT_TEST_RADIUS 5

// used to inval rectangle enclosing pressed state of ring
#define CURSOR_RING_OUTER_DIAMETER SkFixedToScalar(SkIntToFixed(13)>>2) // 13/4 == 3.25

class CursorRing : public DrawExtra {
public:
    enum Flavor {
        NORMAL_FLAVOR,
        FAKE_FLAVOR,
        NORMAL_ANIMATING,
        FAKE_ANIMATING,
        ANIMATING_COUNT = 2
    };

    CursorRing(WebViewCore* core) : m_viewImpl(core) {}
    virtual ~CursorRing() {}
    virtual void draw(SkCanvas* , LayerAndroid* );
    bool setup();
private:
    friend class WebView;
    WebViewCore* m_viewImpl; // copy for convenience
    WTF::Vector<IntRect> m_rings;
    IntRect m_bounds;
    const CachedRoot* m_root;
    const CachedFrame* m_frame;
    const CachedNode* m_node;
    Flavor m_flavor;
    bool m_followedLink;
    bool m_isButton;
};

}

#endif
