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

#ifndef ChromeClientAndroid_h
#define ChromeClientAndroid_h

#include "ChromeClient.h"

#include "GeolocationPermissions.h"
#include "Timer.h"
#include <wtf/Threading.h>

namespace WebCore {
    class Geolocation;
}

using namespace WebCore;

namespace android {
    class WebFrame;

    class ChromeClientAndroid : public ChromeClient {
    public:
        ChromeClientAndroid() : m_webFrame(0), m_geolocationPermissions(0)
#if USE(ACCELERATED_COMPOSITING)
                                , m_rootGraphicsLayer(0)
                                , m_askToDrawAgain(false)
                                , m_syncTimer(this, &ChromeClientAndroid::syncTimerFired)
#endif
                                , m_triedToReclaimDBQuota(false)
                                { }
        virtual void chromeDestroyed();
        
        virtual void setWindowRect(const FloatRect&);
        virtual FloatRect windowRect();
        
        virtual FloatRect pageRect();
        
        virtual float scaleFactor();
        
#ifdef ANDROID_USER_GESTURE
        virtual void focus(bool userGesture);
#else
        virtual void focus();
#endif
        virtual void unfocus();
        
        virtual bool canTakeFocus(FocusDirection);
        virtual void takeFocus(FocusDirection);

        virtual void focusedNodeChanged(Node*);

        // The Frame pointer provides the ChromeClient with context about which
        // Frame wants to create the new Page.  Also, the newly created window
        // should not be shown to the user until the ChromeClient of the newly
        // created Page has its show method called.
        virtual Page* createWindow(Frame*, const FrameLoadRequest&, const WindowFeatures&);
        virtual void show();
        
        virtual bool canRunModal();
        virtual void runModal();
        
        virtual void setToolbarsVisible(bool);
        virtual bool toolbarsVisible();
        
        virtual void setStatusbarVisible(bool);
        virtual bool statusbarVisible();
        
        virtual void setScrollbarsVisible(bool);
        virtual bool scrollbarsVisible();
        
        virtual void setMenubarVisible(bool);
        virtual bool menubarVisible();
        
        virtual void setResizable(bool);

        virtual void addMessageToConsole(MessageSource, MessageType, MessageLevel, const String& message, unsigned int lineNumber, const String& sourceID);
        
        virtual bool canRunBeforeUnloadConfirmPanel();
        virtual bool runBeforeUnloadConfirmPanel(const String& message, Frame* frame);
        
        virtual void closeWindowSoon();
        
        virtual void runJavaScriptAlert(Frame*, const String&);
        virtual bool runJavaScriptConfirm(Frame*, const String&);
        virtual bool runJavaScriptPrompt(Frame*, const String& message, const String& defaultValue, String& result);
        virtual void setStatusbarText(const String&);
        virtual bool shouldInterruptJavaScript();
        virtual bool tabsToLinks() const;

        virtual IntRect windowResizerRect() const;

        // Methods used by HostWindow.
        virtual void repaint(const IntRect&, bool contentChanged, bool immediate = false, bool repaintContentOnly = false);
        virtual void scroll(const IntSize& scrollDelta, const IntRect& rectToScroll, const IntRect& clipRect);
        virtual IntPoint screenToWindow(const IntPoint&) const;
        virtual IntRect windowToScreen(const IntRect&) const;
        virtual PlatformPageClient platformPageClient() const;
        virtual void contentsSizeChanged(Frame*, const IntSize&) const;
        virtual void scrollRectIntoView(const IntRect&, const ScrollView*) const;
        // End methods used by HostWindow.

        virtual void scrollbarsModeDidChange() const;
        virtual void mouseDidMoveOverElement(const HitTestResult&, unsigned int);

        virtual void setToolTip(const String&, TextDirection);
        
        virtual void print(Frame*);
#if ENABLE(DATABASE)
        virtual void exceededDatabaseQuota(Frame*, const String&);
#endif
#if ENABLE(OFFLINE_WEB_APPLICATIONS)
        virtual void reachedMaxAppCacheSize(int64_t spaceNeeded);
#endif

	virtual void populateVisitedLinks();

#if ENABLE(TOUCH_EVENTS)
        virtual void needTouchEvents(bool);
#endif

        // Methods used to request and provide Geolocation permissions.
        virtual void requestGeolocationPermissionForFrame(Frame*, Geolocation*);
        virtual void cancelGeolocationPermissionRequestForFrame(Frame*);
        // Android-specific
        void provideGeolocationPermissions(const String &origin, bool allow, bool remember);
        void storeGeolocationPermissions();
        void onMainFrameLoadStarted();

        virtual void runOpenPanel(Frame*, PassRefPtr<FileChooser>);
        virtual bool setCursor(PlatformCursorHandle);

        // Notification that the given form element has changed. This function
        // will be called frequently, so handling should be very fast.
        virtual void formStateDidChange(const Node*);

        virtual PassOwnPtr<HTMLParserQuirks> createHTMLParserQuirks() { return 0; }

        // Android-specific
        void setWebFrame(android::WebFrame* webframe);
        void wakeUpMainThreadWithNewQuota(long newQuota);

#if USE(ACCELERATED_COMPOSITING)
        virtual void attachRootGraphicsLayer(WebCore::Frame*, WebCore::GraphicsLayer* g);
        virtual void setNeedsOneShotDrawingSynchronization();
        virtual void scheduleCompositingLayerSync();
        void syncTimerFired(Timer<ChromeClientAndroid>*);
#endif

    private:
        android::WebFrame* m_webFrame;
        // The Geolocation permissions manager.
        OwnPtr<GeolocationPermissions> m_geolocationPermissions;
#if USE(ACCELERATED_COMPOSITING)
        WebCore::GraphicsLayer* m_rootGraphicsLayer;
        bool m_askToDrawAgain;
        Timer<ChromeClientAndroid> m_syncTimer;
#endif
        WTF::ThreadCondition m_quotaThreadCondition;
        WTF::Mutex m_quotaThreadLock;
        long m_newQuota;
        bool m_triedToReclaimDBQuota;
    };

}

#endif
