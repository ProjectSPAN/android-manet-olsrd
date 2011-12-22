/*
 * Copyright 2006, The Android Open Source Project
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

#ifndef WEBVIEWCORE_H
#define WEBVIEWCORE_H

#include "android_npapi.h"
#include "FileChooser.h"
#include "CacheBuilder.h"
#include "CachedHistory.h"
#include "PictureSet.h"
#include "PlatformGraphicsContext.h"
#include "SkColor.h"
#include "SkTDArray.h"
#include "SkRegion.h"
#include "Timer.h"
#include "WebCoreRefObject.h"
#include "WebCoreJni.h"
#include <jni.h>
#include <ui/KeycodeLabels.h>
#include <ui/PixelFormat.h>

namespace WebCore {
    class AtomicString;
    class Color;
    class FrameView;
    class HTMLAnchorElement;
    class HTMLSelectElement;
    class RenderPart;
    class RenderText;
    class Node;
    class PlatformKeyboardEvent;
    class RenderTextControl;
    class ScrollView;
    class TimerBase;
    class PageGroup;
}

#if USE(ACCELERATED_COMPOSITING)
namespace WebCore {
    class GraphicsLayerAndroid;
    class LayerAndroid;
}
#endif

struct PluginWidgetAndroid;
class SkPicture;
class SkIRect;

namespace android {

    class CachedFrame;
    class CachedNode;
    class CachedRoot;
    class ListBoxReply;

    class WebCoreReply : public WebCoreRefObject {
    public:
        virtual ~WebCoreReply() {}

        virtual void replyInt(int value) {
            SkDEBUGF(("WebCoreReply::replyInt(%d) not handled\n", value));
        }

        virtual void replyIntArray(const int* array, int count) {
            SkDEBUGF(("WebCoreReply::replyIntArray() not handled\n"));
        }
            // add more replyFoo signatures as needed
    };

    // one instance of WebViewCore per page for calling into Java's WebViewCore
    class WebViewCore : public WebCoreRefObject {
    public:
        /**
         * Initialize the native WebViewCore with a JNI environment, a Java
         * WebViewCore object and the main frame.
         */
        WebViewCore(JNIEnv* env, jobject javaView, WebCore::Frame* mainframe);
        ~WebViewCore();

        // helper function
        static WebViewCore* getWebViewCore(const WebCore::FrameView* view);
        static WebViewCore* getWebViewCore(const WebCore::ScrollView* view);

        // Followings are called from native WebCore to Java

        /**
         * Scroll to an absolute position.
         * @param x The x coordinate.
         * @param y The y coordinate.
         * @param animate If it is true, animate to the new scroll position
         *
         * This method calls Java to trigger a gradual scroll event.
         */
        void scrollTo(int x, int y, bool animate = false);

        /**
         * Scroll to the point x,y relative to the current position.
         * @param x The relative x position.
         * @param y The relative y position.
         * @param animate If it is true, animate to the new scroll position
         */
        void scrollBy(int x, int y, bool animate);

        /**
         * Record the invalid rectangle
         */
        void contentInvalidate(const WebCore::IntRect &rect);

        /**
         * Satisfy any outstanding invalidates, so that the current state
         * of the DOM is drawn.
         */
        void contentDraw();

#if USE(ACCELERATED_COMPOSITING)
        GraphicsLayerAndroid* graphicsRootLayer() const;
        void immediateRepaint();
        void setUIRootLayer(const LayerAndroid* layer);
#endif

        /** Invalidate the view/screen, NOT the content/DOM, but expressed in
         *  content/DOM coordinates (i.e. they need to eventually be scaled,
         *  by webview into view.java coordinates
         */
        void viewInvalidate(const WebCore::IntRect& rect);

        /**
         * Invalidate part of the content that may be offscreen at the moment
         */
        void offInvalidate(const WebCore::IntRect &rect);

        /**
         * Called by webcore when the progress indicator is done
         * used to rebuild and display any changes in focus
         */
        void notifyProgressFinished();

        /**
         * Notify the view that WebCore did its first layout.
         */
        void didFirstLayout();

        /**
         * Notify the view to update the viewport.
         */
        void updateViewport();

        /**
         * Notify the view to restore the screen width, which in turn restores
         * the scale.
         */
        void restoreScale(int);

        /**
         * Notify the view to restore the scale used to calculate the screen
         * width for wrapping the text
         */
        void restoreScreenWidthScale(int);

        /**
         * Tell the java side to update the focused textfield
         * @param pointer   Pointer to the node for the input field.
         * @param   changeToPassword  If true, we are changing the textfield to
         *          a password field, and ignore the String
         * @param text  If changeToPassword is false, this is the new text that
         *              should go into the textfield.
         */
        void updateTextfield(WebCore::Node* pointer,
                bool changeToPassword, const WebCore::String& text);

        /**
         * Tell the java side to update the current selection in the focused
         * textfield to the WebTextView.  This function finds the currently
         * focused textinput, and passes its selection to java.
         * If there is no focus, or it is not a text input, this does nothing.
         */
        void updateTextSelection();

        void clearTextEntry();
        // JavaScript support
        void jsAlert(const WebCore::String& url, const WebCore::String& text);
        bool jsConfirm(const WebCore::String& url, const WebCore::String& text);
        bool jsPrompt(const WebCore::String& url, const WebCore::String& message,
                const WebCore::String& defaultValue, WebCore::String& result);
        bool jsUnload(const WebCore::String& url, const WebCore::String& message);
        bool jsInterrupt();

        /**
         * Tell the Java side that the origin has exceeded its database quota.
         * @param url The URL of the page that caused the quota overflow
         * @param databaseIdentifier the id of the database that caused the
         *     quota overflow.
         * @param currentQuota The current quota for the origin
         * @param estimatedSize The estimated size of the database
         */
        void exceededDatabaseQuota(const WebCore::String& url,
                                   const WebCore::String& databaseIdentifier,
                                   const unsigned long long currentQuota,
                                   const unsigned long long estimatedSize);

        /**
         * Tell the Java side that the appcache has exceeded its max size.
         * @param spaceNeeded is the amount of disk space that would be needed
         * in order for the last appcache operation to succeed.
         */
        void reachedMaxAppCacheSize(const unsigned long long spaceNeeded);

        /**
         * Set up the PageGroup's idea of which links have been visited,
         * with the browser history.
         * @param group the object to deliver the links to.
         */
        void populateVisitedLinks(WebCore::PageGroup*);

        /**
         * Instruct the browser to show a Geolocation permission prompt for the
         * specified origin.
         * @param origin The origin of the frame requesting Geolocation
         *     permissions.
         */
        void geolocationPermissionsShowPrompt(const WebCore::String& origin);
        /**
         * Instruct the browser to hide the Geolocation permission prompt.
         */
        void geolocationPermissionsHidePrompt();

        void addMessageToConsole(const String& message, unsigned int lineNumber, const String& sourceID, int msgLevel);

        /**
         * Tell the Java side of the scrollbar mode
         */
        void setScrollbarModes(ScrollbarMode horizontalMode, ScrollbarMode verticalMode);

        //
        // Followings support calls from Java to native WebCore
        //


        WebCore::String retrieveHref(WebCore::Frame* frame, WebCore::Node* node);
        WebCore::String retrieveAnchorText(WebCore::Frame* frame, WebCore::Node* node);
        WebCore::String requestLabel(WebCore::Frame* , WebCore::Node* );

        // Create a single picture to represent the drawn DOM (used by navcache)
        void recordPicture(SkPicture* picture);

        // Create a set of pictures to represent the drawn DOM, driven by
        // the invalidated region and the time required to draw (used to draw)
        void recordPictureSet(PictureSet* master);
        void moveFocus(WebCore::Frame* frame, WebCore::Node* node);
        void moveMouse(WebCore::Frame* frame, int x, int y);
        void moveMouseIfLatest(int moveGeneration,
            WebCore::Frame* frame, int x, int y);

        // set the scroll amount that webview.java is currently showing
        void setScrollOffset(int moveGeneration, int dx, int dy);

        void setGlobalBounds(int x, int y, int h, int v);

        void setSizeScreenWidthAndScale(int width, int height, int screenWidth,
            float scale, int realScreenWidth, int screenHeight, int anchorX,
            int anchorY, bool ignoreHeight);

        /**
         * Handle key events from Java.
         * @return Whether keyCode was handled by this class.
         */
        bool key(const WebCore::PlatformKeyboardEvent& event);

        /**
         * Handle (trackball) click event from Java
         */
        void click(WebCore::Frame* frame, WebCore::Node* node);

        /**
         * Handle touch event
         */
        bool handleTouchEvent(int action, int x, int y, int metaState);

        /**
         * Handle motionUp event from the UI thread (called touchUp in the
         * WebCore thread).
         */
        void touchUp(int touchGeneration, WebCore::Frame* frame,
                WebCore::Node* node, int x, int y);

        /**
         * Sets the index of the label from a popup
         */
        void popupReply(int index);
        void popupReply(const int* array, int count);

        /**
         *  Delete text from start to end in the focused textfield.
         *  If start == end, set the selection, but perform no deletion.
         *  If there is no focus, silently fail.
         *  If start and end are out of order, swap them.
         */
        void deleteSelection(int start, int end, int textGeneration);

        /**
         *  Set the selection of the currently focused textfield to (start, end).
         *  If start and end are out of order, swap them.
         */
        void setSelection(int start, int end);
        /**
         *  In the currently focused textfield, replace the characters from oldStart to oldEnd
         *  (if oldStart == oldEnd, this will be an insert at that position) with replace,
         *  and set the selection to (start, end).
         */
        void replaceTextfieldText(int oldStart,
            int oldEnd, const WebCore::String& replace, int start, int end,
            int textGeneration);
        void passToJs(int generation,
            const WebCore::String& , const WebCore::PlatformKeyboardEvent& );
        /**
         * Scroll the focused textfield to (x, y) in document space
         */
        void scrollFocusedTextInput(float x, int y);
        /**
         * Set the FocusController's active and focused states, so that
         * the caret will draw (true) or not.
         */
        void setFocusControllerActive(WebCore::Frame*, bool active);

        void saveDocumentState(WebCore::Frame* frame);

        void addVisitedLink(const UChar*, int);

        // TODO: I don't like this hack but I need to access the java object in
        // order to send it as a parameter to java
        AutoJObject getJavaObject();

        // Return the parent WebView Java object associated with this
        // WebViewCore.
        jobject getWebViewJavaObject();

        void setBackgroundColor(SkColor c);
        void updateFrameCache();
        void updateCacheOnNodeChange();
        void dumpDomTree(bool);
        void dumpRenderTree(bool);
        void dumpNavTree();

        /*  We maintain a list of active plugins. The list is edited by the
            pluginview itself. The list is used to service invals to the plugin
            pageflipping bitmap.
         */
        void addPlugin(PluginWidgetAndroid*);
        void removePlugin(PluginWidgetAndroid*);
        // returns true if the pluginwidgit is in our active list
        bool isPlugin(PluginWidgetAndroid*) const;
        void invalPlugin(PluginWidgetAndroid*);
        void drawPlugins();

        // send the current screen size/zoom to all of the plugins in our list
        void sendPluginVisibleScreen();

	// send onLoad event to plugins who are descendents of the given frame
        void notifyPluginsOnFrameLoad(const Frame*);

        // send this event to all of the plugins in our list
        void sendPluginEvent(const ANPEvent&);

        // lookup the plugin widget struct given an NPP
        PluginWidgetAndroid* getPluginWidget(NPP npp);

        // return the cursorNode if it is a plugin
        Node* cursorNodeIsPlugin();

        // Notify the Java side whether it needs to pass down the touch events
        void needTouchEvents(bool);

        void requestKeyboardWithSelection(const WebCore::Node*, int selStart, int selEnd);
        // Notify the Java side that webkit is requesting a keyboard
        void requestKeyboard(bool showKeyboard);

        // Generates a class loader that contains classes from the plugin's apk
        jclass getPluginClass(const WebCore::String& libName, const char* className);

        // Creates a full screen surface for a plugin
        void showFullScreenPlugin(jobject webkitPlugin, NPP npp);

        // Instructs the UI thread to discard the plugin's full-screen surface
        void hideFullScreenPlugin();

        // Adds the plugin's view (aka surface) to the view hierarchy
        jobject addSurface(jobject view, int x, int y, int width, int height);

        // Updates a Surface coordinates and dimensions for a plugin
        void updateSurface(jobject childView, int x, int y, int width, int height);

        // Destroys a SurfaceView for a plugin
        void destroySurface(jobject childView);

        // Returns the context (android.content.Context) of the WebView
        jobject getContext();

        bool validNodeAndBounds(Frame* , Node* , const IntRect& );

        // Make the rect (left, top, width, height) visible. If it can be fully
        // fit, center it on the screen. Otherwise make sure the point specified
        // by (left + xPercentInDoc * width, top + yPercentInDoc * height)
        // pinned at the screen position (xPercentInView, yPercentInView).
        void showRect(int left, int top, int width, int height, int contentWidth,
            int contentHeight, float xPercentInDoc, float xPercentInView,
            float yPercentInDoc, float yPercentInView);

        // Scale the rect (x, y, width, height) to make it just fit and centered
        // in the current view.
        void centerFitRect(int x, int y, int width, int height);

        // other public functions
    public:
        // Open a file chooser for selecting a file to upload
        void openFileChooser(PassRefPtr<WebCore::FileChooser> );

        // reset the picture set to empty
        void clearContent();

        // flatten the picture set to a picture
        void copyContentToPicture(SkPicture* );

        // draw the picture set with the specified background color
        bool drawContent(SkCanvas* , SkColor );
        bool focusBoundsChanged();
        bool pictureReady();

        // record the inval area, and the picture size
        bool recordContent(SkRegion* , SkIPoint* );
        int screenWidth() const { return m_screenWidth; }
        int screenHeight() const { return m_screenHeight; }
        float scale() const { return m_scale; }
        float screenWidthScale() const { return m_screenWidthScale; }
        WebCore::Frame* mainFrame() const { return m_mainFrame; }
        void updateCursorBounds(const CachedRoot* root,
                const CachedFrame* cachedFrame, const CachedNode* cachedNode);
        void updateFrameCacheIfLoading();

        // utility to split slow parts of the picture set
        void splitContent();

        // these members are shared with webview.cpp
        static Mutex gFrameCacheMutex;
        CachedRoot* m_frameCacheKit; // nav data being built by webcore
        SkPicture* m_navPictureKit;
        int m_moveGeneration; // copy of state in WebViewNative triggered by move
        int m_touchGeneration; // copy of state in WebViewNative triggered by touch
        int m_lastGeneration; // last action using up to date cache
        bool m_updatedFrameCache;
        bool m_findIsUp;
        bool m_hasCursorBounds;
        WebCore::IntRect m_cursorBounds;
        WebCore::IntRect m_cursorHitBounds;
        void* m_cursorFrame;
        IntPoint m_cursorLocation;
        void* m_cursorNode;
        static Mutex gCursorBoundsMutex;
        // These two fields go together: we use the mutex to protect access to
        // m_buttons, so that we, and webview.cpp can look/modify the m_buttons
        // field safely from our respective threads
        static Mutex gButtonMutex;
        WTF::Vector<Container> m_buttons;
        bool isPaused() const { return m_isPaused; }
        void setIsPaused(bool isPaused) { m_isPaused = isPaused; }
        // end of shared members

        // internal functions
    private:
        CacheBuilder& cacheBuilder();
        WebCore::Node* currentFocus();
        // Compare the new set of buttons to the old one.  All of the new
        // buttons either replace our old ones or should be added to our list.
        // Then check the old buttons to see if any are no longer needed.
        void updateButtonList(WTF::Vector<Container>* buttons);
        void reset(bool fromConstructor);

        void listBoxRequest(WebCoreReply* reply, const uint16_t** labels,
                size_t count, const int enabled[], size_t enabledCount,
                bool multiple, const int selected[], size_t selectedCountOrSelection);

        friend class ListBoxReply;
        struct JavaGlue;
        struct JavaGlue*       m_javaGlue;
        WebCore::Frame*        m_mainFrame;
        WebCoreReply*          m_popupReply;
        WebCore::Node* m_lastFocused;
        WebCore::IntRect m_lastFocusedBounds;
        int m_lastFocusedSelStart;
        int m_lastFocusedSelEnd;
        static Mutex m_contentMutex; // protects ui/core thread pictureset access
        PictureSet m_content; // the set of pictures to draw (accessed by UI too)
        SkRegion m_addInval; // the accumulated inval region (not yet drawn)
        SkRegion m_rebuildInval; // the accumulated region for rebuilt pictures
        // Used in passToJS to avoid updating the UI text field until after the
        // key event has been processed.
        bool m_blockTextfieldUpdates;
        bool m_focusBoundsChanged;
        bool m_skipContentDraw;
        // Passed in with key events to know when they were generated.  Store it
        // with the cache so that we can ignore stale text changes.
        int m_textGeneration;
        CachedRoot* m_temp;
        SkPicture* m_tempPict;
        int m_maxXScroll;
        int m_maxYScroll;
        int m_scrollOffsetX; // webview.java's current scroll in X
        int m_scrollOffsetY; // webview.java's current scroll in Y
        WebCore::IntPoint m_mousePos;
        bool m_frameCacheOutOfDate;
        bool m_progressDone;
        int m_lastPassed;
        int m_lastVelocity;
        CachedHistory m_history;
        int m_screenWidth; // width of the visible rect in document coordinates
        int m_screenHeight;// height of the visible rect in document coordinates
        float m_scale;
        float m_screenWidthScale;
        unsigned m_domtree_version;
        bool m_check_domtree_version;
        PageGroup* m_groupForVisitedLinks;
        bool m_isPaused;

        SkTDArray<PluginWidgetAndroid*> m_plugins;
        WebCore::Timer<WebViewCore> m_pluginInvalTimer;
        void pluginInvalTimerFired(WebCore::Timer<WebViewCore>*) {
            this->drawPlugins();
        }

        void doMaxScroll(CacheBuilder::Direction dir);
        SkPicture* rebuildPicture(const SkIRect& inval);
        void rebuildPictureSet(PictureSet* );
        void sendNotifyProgressFinished();
        bool handleMouseClick(WebCore::Frame* framePtr, WebCore::Node* nodePtr);
        WebCore::HTMLAnchorElement* retrieveAnchorElement(WebCore::Frame* frame, WebCore::Node* node);

#if ENABLE(TOUCH_EVENTS)
        bool m_forwardingTouchEvents;
        IntPoint m_lastTouchPoint;
#endif

#if DEBUG_NAV_UI
        uint32_t m_now;
#endif

    private:
        // called from constructor, to add this to a global list
        static void addInstance(WebViewCore*);
        // called from destructor, to remove this from a global list
        static void removeInstance(WebViewCore*);
    public:
        // call only from webkit thread (like add/remove), return true if inst
        // is still alive
        static bool isInstance(WebViewCore*);

        // if there exists at least on WebViewCore instance then we return the
        // application context, otherwise NULL is returned.
        static jobject getApplicationContext();

        // Check whether a media mimeType is supported in Android media framework.
        static bool supportsMimeType(const WebCore::String& mimeType);
    };

}   // namespace android

#endif // WEBVIEWCORE_H
