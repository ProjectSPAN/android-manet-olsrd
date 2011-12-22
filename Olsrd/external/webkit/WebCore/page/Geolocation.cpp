/*
 * Copyright (C) 2008, 2009 Apple Inc. All Rights Reserved.
 * Copyright (C) 2009 Torch Mobile, Inc.
 * Copyright 2010, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "Geolocation.h"

#include "Chrome.h"
// ANDROID
#include "DOMWindow.h"
// END ANDROID
#include "Document.h"
// ANDROID
#include "EventNames.h"
// END ANDROID
#include "Frame.h"
#include "Page.h"
#if PLATFORM(ANDROID)
#include "PlatformBridge.h"
#endif
#include <wtf/CurrentTime.h>

#if ENABLE(CLIENT_BASED_GEOLOCATION)
#include "Coordinates.h"
#include "GeolocationController.h"
#include "GeolocationError.h"
#include "GeolocationPosition.h"
#include "PositionError.h"
#endif

namespace WebCore {

static const char permissionDeniedErrorMessage[] = "User denied Geolocation";
static const char failedToStartServiceErrorMessage[] = "Failed to start Geolocation service";

#if ENABLE(CLIENT_BASED_GEOLOCATION)

static PassRefPtr<Geoposition> createGeoposition(GeolocationPosition* position)
{
    if (!position)
        return 0;
    
    RefPtr<Coordinates> coordinates = Coordinates::create(position->latitude(), position->longitude(), position->canProvideAltitude(), position->altitude(), 
                                                          position->accuracy(), position->canProvideAltitudeAccuracy(), position->altitudeAccuracy(),
                                                          position->canProvideHeading(), position->heading(), position->canProvideSpeed(), position->speed());
    return Geoposition::create(coordinates.release(), position->timestamp());
}

static PassRefPtr<PositionError> createPositionError(GeolocationError* error)
{
    PositionError::ErrorCode code = PositionError::POSITION_UNAVAILABLE;
    switch (error->code()) {
    case GeolocationError::PermissionDenied:
        code = PositionError::PERMISSION_DENIED;
        break;
    case GeolocationError::PositionUnavailable:
        code = PositionError::POSITION_UNAVAILABLE;
        break;
    }

    return PositionError::create(code, error->message());
}
#endif

Geolocation::GeoNotifier::GeoNotifier(Geolocation* geolocation, PassRefPtr<PositionCallback> successCallback, PassRefPtr<PositionErrorCallback> errorCallback, PassRefPtr<PositionOptions> options)
    : m_geolocation(geolocation)
    , m_successCallback(successCallback)
    , m_errorCallback(errorCallback)
    , m_options(options)
    , m_timer(this, &Geolocation::GeoNotifier::timerFired)
    , m_useCachedPosition(false)
{
    ASSERT(m_geolocation);
    ASSERT(m_successCallback);
    // If no options were supplied from JS, we should have created a default set
    // of options in JSGeolocationCustom.cpp.
    ASSERT(m_options);
}

void Geolocation::GeoNotifier::setFatalError(PassRefPtr<PositionError> error)
{
    // This method is called at most once on a given GeoNotifier object.
    ASSERT(!m_fatalError);
    m_fatalError = error;
    m_timer.startOneShot(0);
}

void Geolocation::GeoNotifier::setUseCachedPosition()
{
    m_useCachedPosition = true;
    m_timer.startOneShot(0);
}

bool Geolocation::GeoNotifier::hasZeroTimeout() const
{
    return m_options->hasTimeout() && m_options->timeout() == 0;
}

void Geolocation::GeoNotifier::runSuccessCallback(Geoposition* position)
{
    m_successCallback->handleEvent(position);
}

void Geolocation::GeoNotifier::startTimerIfNeeded()
{
    if (m_options->hasTimeout())
        m_timer.startOneShot(m_options->timeout() / 1000.0);
}

void Geolocation::GeoNotifier::timerFired(Timer<GeoNotifier>*)
{
    m_timer.stop();

    // Protect this GeoNotifier object, since it
    // could be deleted by a call to clearWatch in a callback.
    RefPtr<GeoNotifier> protect(this);

    if (m_fatalError) {
        if (m_errorCallback)
            m_errorCallback->handleEvent(m_fatalError.get());
        // This will cause this notifier to be deleted.
        m_geolocation->fatalErrorOccurred(this);
        return;
    }

    if (m_useCachedPosition) {
        // Clear the cached position flag in case this is a watch request, which
        // will continue to run.
        m_useCachedPosition = false;
        m_geolocation->requestUsesCachedPosition(this);
        return;
    }

    if (m_errorCallback) {
        RefPtr<PositionError> error = PositionError::create(PositionError::TIMEOUT, "Timeout expired");
        m_errorCallback->handleEvent(error.get());
    }
    m_geolocation->requestTimedOut(this);
}

void Geolocation::Watchers::set(int id, PassRefPtr<GeoNotifier> prpNotifier)
{
    RefPtr<GeoNotifier> notifier = prpNotifier;

    m_idToNotifierMap.set(id, notifier.get());
    m_notifierToIdMap.set(notifier.release(), id);
}

void Geolocation::Watchers::remove(int id)
{
    IdToNotifierMap::iterator iter = m_idToNotifierMap.find(id);
    if (iter == m_idToNotifierMap.end())
        return;
    m_notifierToIdMap.remove(iter->second);
    m_idToNotifierMap.remove(iter);
}

void Geolocation::Watchers::remove(GeoNotifier* notifier)
{
    NotifierToIdMap::iterator iter = m_notifierToIdMap.find(notifier);
    if (iter == m_notifierToIdMap.end())
        return;
    m_idToNotifierMap.remove(iter->second);
    m_notifierToIdMap.remove(iter);
}

bool Geolocation::Watchers::contains(GeoNotifier* notifier) const
{
    return m_notifierToIdMap.contains(notifier);
}

void Geolocation::Watchers::clear()
{
    m_idToNotifierMap.clear();
    m_notifierToIdMap.clear();
}

bool Geolocation::Watchers::isEmpty() const
{
    return m_idToNotifierMap.isEmpty();
}

void Geolocation::Watchers::getNotifiersVector(Vector<RefPtr<GeoNotifier> >& copy) const
{
    copyValuesToVector(m_idToNotifierMap, copy);
}

Geolocation::Geolocation(Frame* frame)
// ANDROID
    : EventListener(GeolocationEventListenerType)
    , m_frame(frame)
// END ANDROID
#if !ENABLE(CLIENT_BASED_GEOLOCATION)
    , m_service(GeolocationService::create(this))
#endif
    , m_allowGeolocation(Unknown)
    , m_shouldClearCache(false)
    , m_positionCache(new GeolocationPositionCache)
{
    if (!m_frame)
        return;
    ASSERT(m_frame->document());
    m_frame->document()->setUsingGeolocation(true);

// ANDROID
    if (m_frame->domWindow())
        m_frame->domWindow()->addEventListener(eventNames().unloadEvent, this, false);
// END ANDROID
}

Geolocation::~Geolocation()
{
// ANDROID
    if (m_frame && m_frame->domWindow())
        m_frame->domWindow()->removeEventListener(eventNames().unloadEvent, this, false);
// END ANDROID
}

void Geolocation::disconnectFrame()
{
    stopUpdating();
    if (m_frame) {
        if (m_frame->document())
            m_frame->document()->setUsingGeolocation(false);
        if (m_frame->page() && m_allowGeolocation == InProgress)
            m_frame->page()->chrome()->cancelGeolocationPermissionRequestForFrame(m_frame);
    }
    m_frame = 0;
}

Geoposition* Geolocation::lastPosition()
{
#if ENABLE(CLIENT_BASED_GEOLOCATION)
    if (!m_frame)
        return 0;

    Page* page = m_frame->page();
    if (!page)
        return 0;

    m_lastPosition = createGeoposition(page->geolocationController()->lastPosition());
#else
    m_lastPosition = m_service->lastPosition();
#endif

    return m_lastPosition.get();
}

void Geolocation::getCurrentPosition(PassRefPtr<PositionCallback> successCallback, PassRefPtr<PositionErrorCallback> errorCallback, PassRefPtr<PositionOptions> options)
{
    if (!m_frame)
        return;

    RefPtr<GeoNotifier> notifier = startRequest(successCallback, errorCallback, options);
    ASSERT(notifier);

    m_oneShots.add(notifier);
}

int Geolocation::watchPosition(PassRefPtr<PositionCallback> successCallback, PassRefPtr<PositionErrorCallback> errorCallback, PassRefPtr<PositionOptions> options)
{
    if (!m_frame)
        return 0;

    RefPtr<GeoNotifier> notifier = startRequest(successCallback, errorCallback, options);
    ASSERT(notifier);

    static int nextAvailableWatchId = 1;
    // In case of overflow, make sure the ID remains positive, but reuse the ID values.
    if (nextAvailableWatchId < 1)
        nextAvailableWatchId = 1;
    m_watchers.set(nextAvailableWatchId, notifier.release());
    return nextAvailableWatchId++;
}

PassRefPtr<Geolocation::GeoNotifier> Geolocation::startRequest(PassRefPtr<PositionCallback> successCallback, PassRefPtr<PositionErrorCallback> errorCallback, PassRefPtr<PositionOptions> options)
{
    RefPtr<GeoNotifier> notifier = GeoNotifier::create(this, successCallback, errorCallback, options);

    // Check whether permissions have already been denied. Note that if this is the case,
    // the permission state can not change again in the lifetime of this page.
    if (isDenied())
        notifier->setFatalError(PositionError::create(PositionError::PERMISSION_DENIED, permissionDeniedErrorMessage));
    else if (haveSuitableCachedPosition(notifier->m_options.get()))
        notifier->setUseCachedPosition();
    else if (notifier->hasZeroTimeout() || startUpdating(notifier.get())) {
#if ENABLE(CLIENT_BASED_GEOLOCATION)
        // Only start timer if we're not waiting for user permission.
        if (!m_startRequestPermissionNotifier)
#endif            
            notifier->startTimerIfNeeded();
    } else
        notifier->setFatalError(PositionError::create(PositionError::POSITION_UNAVAILABLE, failedToStartServiceErrorMessage));

    return notifier.release();
}

void Geolocation::fatalErrorOccurred(Geolocation::GeoNotifier* notifier)
{
    // This request has failed fatally. Remove it from our lists.
    m_oneShots.remove(notifier);
    m_watchers.remove(notifier);

    if (!hasListeners())
        stopUpdating();
}

void Geolocation::requestUsesCachedPosition(GeoNotifier* notifier)
{
    // This is called asynchronously, so the permissions could have been denied
    // since we last checked in startRequest.
    if (isDenied()) {
        notifier->setFatalError(PositionError::create(PositionError::PERMISSION_DENIED, permissionDeniedErrorMessage));
        return;
    }

    m_requestsAwaitingCachedPosition.add(notifier);

    // If permissions are allowed, make the callback
    if (isAllowed()) {
        makeCachedPositionCallbacks();
        return;
    }

    // Request permissions, which may be synchronous or asynchronous.
    requestPermission();
}

void Geolocation::makeCachedPositionCallbacks()
{
    // All modifications to m_requestsAwaitingCachedPosition are done
    // asynchronously, so we don't need to worry about it being modified from
    // the callbacks.
    GeoNotifierSet::const_iterator end = m_requestsAwaitingCachedPosition.end();
    for (GeoNotifierSet::const_iterator iter = m_requestsAwaitingCachedPosition.begin(); iter != end; ++iter) {
        GeoNotifier* notifier = iter->get();
        notifier->runSuccessCallback(m_positionCache->cachedPosition());

        // If this is a one-shot request, stop it. Otherwise, if the watch still
        // exists, start the service to get updates.
        if (m_oneShots.contains(notifier))
            m_oneShots.remove(notifier);
        else if (m_watchers.contains(notifier)) {
            if (notifier->hasZeroTimeout() || startUpdating(notifier))
                notifier->startTimerIfNeeded();
            else
                notifier->setFatalError(PositionError::create(PositionError::POSITION_UNAVAILABLE, failedToStartServiceErrorMessage));
        }
    }

    m_requestsAwaitingCachedPosition.clear();

    if (!hasListeners())
        stopUpdating();
}

void Geolocation::requestTimedOut(GeoNotifier* notifier)
{
    // If this is a one-shot request, stop it.
    m_oneShots.remove(notifier);

    if (!hasListeners())
        stopUpdating();
}

bool Geolocation::haveSuitableCachedPosition(PositionOptions* options)
{
    if (!m_positionCache->cachedPosition())
        return false;
    if (!options->hasMaximumAge())
        return true;
    if (!options->maximumAge())
        return false;
    DOMTimeStamp currentTimeMillis = currentTime() * 1000.0;
    return m_positionCache->cachedPosition()->timestamp() > currentTimeMillis - options->maximumAge();
}

void Geolocation::clearWatch(int watchId)
{
    m_watchers.remove(watchId);
    
    if (!hasListeners())
        stopUpdating();
}

void Geolocation::suspend()
{
#if !ENABLE(CLIENT_BASED_GEOLOCATION)
    if (hasListeners())
        m_service->suspend();
#endif
}

void Geolocation::resume()
{
#if !ENABLE(CLIENT_BASED_GEOLOCATION)
    if (hasListeners())
        m_service->resume();
#endif
}

void Geolocation::setIsAllowed(bool allowed)
{
    // This may be due to either a new position from the service, or a cached
    // position.
    m_allowGeolocation = allowed ? Yes : No;
    
#if ENABLE(CLIENT_BASED_GEOLOCATION)
    if (m_startRequestPermissionNotifier) {
        if (isAllowed()) {
            // Permission request was made during the startUpdating process
            m_startRequestPermissionNotifier->startTimerIfNeeded();
            m_startRequestPermissionNotifier = 0;
            if (!m_frame)
                return;
            Page* page = m_frame->page();
            if (!page)
                return;
            page->geolocationController()->addObserver(this);
        } else {
            m_startRequestPermissionNotifier->setFatalError(PositionError::create(PositionError::PERMISSION_DENIED, permissionDeniedErrorMessage));
            m_oneShots.add(m_startRequestPermissionNotifier);
            m_startRequestPermissionNotifier = 0;
        }
        return;
    }
#endif

    if (!isAllowed()) {
        RefPtr<PositionError> error = PositionError::create(PositionError::PERMISSION_DENIED, permissionDeniedErrorMessage);
        error->setIsFatal(true);
        handleError(error.get());
        m_requestsAwaitingCachedPosition.clear();
        return;
    }

    // If the service has a last position, use it to call back for all requests.
    // If any of the requests are waiting for permission for a cached position,
    // the position from the service will be at least as fresh.
    if (lastPosition())
        makeSuccessCallbacks();
    else
        makeCachedPositionCallbacks();
}

void Geolocation::sendError(Vector<RefPtr<GeoNotifier> >& notifiers, PositionError* error)
{
     Vector<RefPtr<GeoNotifier> >::const_iterator end = notifiers.end();
     for (Vector<RefPtr<GeoNotifier> >::const_iterator it = notifiers.begin(); it != end; ++it) {
         RefPtr<GeoNotifier> notifier = *it;
         
         if (notifier->m_errorCallback)
             notifier->m_errorCallback->handleEvent(error);
     }
}

void Geolocation::sendPosition(Vector<RefPtr<GeoNotifier> >& notifiers, Geoposition* position)
{
    Vector<RefPtr<GeoNotifier> >::const_iterator end = notifiers.end();
    for (Vector<RefPtr<GeoNotifier> >::const_iterator it = notifiers.begin(); it != end; ++it) {
        RefPtr<GeoNotifier> notifier = *it;
        ASSERT(notifier->m_successCallback);
        
        notifier->m_successCallback->handleEvent(position);
    }
}

void Geolocation::stopTimer(Vector<RefPtr<GeoNotifier> >& notifiers)
{
    Vector<RefPtr<GeoNotifier> >::const_iterator end = notifiers.end();
    for (Vector<RefPtr<GeoNotifier> >::const_iterator it = notifiers.begin(); it != end; ++it) {
        RefPtr<GeoNotifier> notifier = *it;
        notifier->m_timer.stop();
    }
}

void Geolocation::stopTimersForOneShots()
{
    Vector<RefPtr<GeoNotifier> > copy;
    copyToVector(m_oneShots, copy);
    
    stopTimer(copy);
}

void Geolocation::stopTimersForWatchers()
{
    Vector<RefPtr<GeoNotifier> > copy;
    m_watchers.getNotifiersVector(copy);
    
    stopTimer(copy);
}

void Geolocation::stopTimers()
{
    stopTimersForOneShots();
    stopTimersForWatchers();
}

void Geolocation::handleError(PositionError* error)
{
    ASSERT(error);
    
    Vector<RefPtr<GeoNotifier> > oneShotsCopy;
    copyToVector(m_oneShots, oneShotsCopy);

    Vector<RefPtr<GeoNotifier> > watchersCopy;
    m_watchers.getNotifiersVector(watchersCopy);

    // Clear the lists before we make the callbacks, to avoid clearing notifiers
    // added by calls to Geolocation methods from the callbacks, and to prevent
    // further callbacks to these notifiers.
    m_oneShots.clear();
    if (error->isFatal())
        m_watchers.clear();

    sendError(oneShotsCopy, error);
    sendError(watchersCopy, error);

    if (!hasListeners())
        stopUpdating();
}

void Geolocation::requestPermission()
{
    if (m_allowGeolocation > Unknown)
        return;

    if (!m_frame)
        return;
    
    Page* page = m_frame->page();
    if (!page)
        return;
    
    m_allowGeolocation = InProgress;

    // Ask the chrome: it maintains the geolocation challenge policy itself.
    page->chrome()->requestGeolocationPermissionForFrame(m_frame, this);
}

void Geolocation::positionChanged(PassRefPtr<Geoposition> newPosition)
{
    m_currentPosition = newPosition;

    m_positionCache->setCachedPosition(m_currentPosition.get());

    // Stop all currently running timers.
    stopTimers();
    
    if (!isAllowed()) {
        // requestPermission() will ask the chrome for permission. This may be
        // implemented synchronously or asynchronously. In both cases,
        // makeSuccessCallbacks() will be called if permission is granted, so
        // there's nothing more to do here.
        requestPermission();
        return;
    }

    makeSuccessCallbacks();
}

void Geolocation::makeSuccessCallbacks()
{
    ASSERT(m_currentPosition);
    ASSERT(isAllowed());
    
    Vector<RefPtr<GeoNotifier> > oneShotsCopy;
    copyToVector(m_oneShots, oneShotsCopy);
    
    Vector<RefPtr<GeoNotifier> > watchersCopy;
    m_watchers.getNotifiersVector(watchersCopy);
    
    // Clear the lists before we make the callbacks, to avoid clearing notifiers
    // added by calls to Geolocation methods from the callbacks, and to prevent
    // further callbacks to these notifiers.
    m_oneShots.clear();

    sendPosition(oneShotsCopy, m_currentPosition.get());
    sendPosition(watchersCopy, m_currentPosition.get());

    if (!hasListeners())
        stopUpdating();
}

#if ENABLE(CLIENT_BASED_GEOLOCATION)

void Geolocation::setPosition(GeolocationPosition* position)
{
    positionChanged(createGeoposition(position));
}

void Geolocation::setError(GeolocationError* error)
{
    RefPtr<PositionError> positionError = createPositionError(error);
    handleError(positionError.get());
}

#else

void Geolocation::geolocationServicePositionChanged(GeolocationService* service)
{
    ASSERT_UNUSED(service, service == m_service);
    ASSERT(m_service->lastPosition());

    positionChanged(m_service->lastPosition());
}

void Geolocation::geolocationServiceErrorOccurred(GeolocationService* service)
{
    ASSERT(service->lastError());

    // Note that we do not stop timers here. For one-shots, the request is
    // cleared in handleError. For watchers, the spec requires that the timer is
    // not cleared.
    handleError(service->lastError());
}

#endif

bool Geolocation::startUpdating(GeoNotifier* notifier)
{
#if ENABLE(CLIENT_BASED_GEOLOCATION)
    // FIXME: Pass options to client.

    if (!isAllowed()) {
        m_startRequestPermissionNotifier = notifier;
        requestPermission();
        return true;
    }
    
    if (!m_frame)
        return false;

    Page* page = m_frame->page();
    if (!page)
        return false;

    page->geolocationController()->addObserver(this);
    return true;
#else
#if PLATFORM(ANDROID)
    // TODO: Upstream to webkit.org. See https://bugs.webkit.org/show_bug.cgi?id=34082
    // Note that the correct fix is to use a 'paused' flag in WebCore, rather
    // than calling into PlatformBridge.
    if (!m_frame)
        return false;
    FrameView* view = m_frame->view();
    if (!view)
        return false;
    return m_service->startUpdating(notifier->m_options.get(), PlatformBridge::isWebViewPaused(view));
#else
    return m_service->startUpdating(notifier->m_options.get());
#endif
#endif
}

void Geolocation::stopUpdating()
{
#if ENABLE(CLIENT_BASED_GEOLOCATION)
    if (!m_frame)
        return;

    Page* page = m_frame->page();
    if (!page)
        return;

    page->geolocationController()->removeObserver(this);
#else
    m_service->stopUpdating();
#endif

}

// ANDROID
bool Geolocation::operator==(const EventListener& listener)
{
    if (listener.type() != GeolocationEventListenerType)
        return false;
    const Geolocation* geolocation = static_cast<const Geolocation*>(&listener);
    return m_frame == geolocation->m_frame;
}

void Geolocation::handleEvent(ScriptExecutionContext*, Event* event)
{
    ASSERT_UNUSED(event, event->type() == eventNames().unloadEvent);
    // Cancel any ongoing requests on page unload. This is required to release
    // references to JS callbacks in the page, to allow the frame to be cleaned up
    // by WebKit.
    m_oneShots.clear();
    m_watchers.clear();
}
// END ANDROID

} // namespace WebCore
