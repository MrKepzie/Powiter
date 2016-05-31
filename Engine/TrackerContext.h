/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2015 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#ifndef TRACKERCONTEXT_H
#define TRACKERCONTEXT_H

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include <set>
#include <list>

#include "Global/GlobalDefines.h"

#if !defined(Q_MOC_RUN) && !defined(SBK_RUN)
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#endif

#include <QtCore/QMutex>
#include <QtCore/QThread>

#include "Global/KeySymbols.h"
#include "Engine/EngineFwd.h"
#include "Engine/RectI.h"
#include "Engine/RectD.h"
#include "Engine/GenericSchedulerThread.h"
#include "Engine/Transform.h"
#include "Engine/ViewIdx.h"

namespace mv {
class AutoTrack;
}


NATRON_NAMESPACE_ENTER;

class TrackerParamsProvider
{
    mutable QMutex _trackParamsMutex;
    bool _centerTrack;
    bool _updateViewer;

public:

    TrackerParamsProvider()
        : _trackParamsMutex()
          , _centerTrack(false)
          , _updateViewer(false)
    {
    }

    virtual ~TrackerParamsProvider()
    {
    }

    void setCenterOnTrack(bool centerTrack)
    {
        QMutexLocker k(&_trackParamsMutex);

        _centerTrack = centerTrack;
    }

    void setUpdateViewer(bool updateViewer)
    {
        QMutexLocker k(&_trackParamsMutex);

        _updateViewer =  updateViewer;
    }

    bool getCenterOnTrack() const
    {
        QMutexLocker k(&_trackParamsMutex);

        return _centerTrack;
    }

    bool getUpdateViewer() const
    {
        QMutexLocker k(&_trackParamsMutex);

        return _updateViewer;
    }
};

class TrackerContextPrivate;
class TrackerContext
    : public QObject, public boost::enable_shared_from_this<TrackerContext>, public TrackerParamsProvider
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    enum TrackSelectionReason
    {
        eTrackSelectionSettingsPanel,
        eTrackSelectionViewer,
        eTrackSelectionInternal,
    };


    TrackerContext(const boost::shared_ptr<Node> &node);

    virtual ~TrackerContext();

    void load(const TrackerContextSerialization& serialization);

    void save(TrackerContextSerialization* serialization) const;


    boost::shared_ptr<Node> getNode() const;
    boost::shared_ptr<KnobChoice> getCorrelationScoreTypeKnob() const;
    boost::shared_ptr<KnobBool> getEnabledKnob() const;
    boost::shared_ptr<KnobPage> getTrackingPageKnbo() const;

    bool isTrackerPMEnabled() const;

    TrackMarkerPtr createMarker();

    int getMarkerIndex(const TrackMarkerPtr& marker) const;

    TrackMarkerPtr getPrevMarker(const TrackMarkerPtr& marker, bool loop) const;
    TrackMarkerPtr getNextMarker(const TrackMarkerPtr& marker, bool loop) const;

    void appendMarker(const TrackMarkerPtr& marker);

    void insertMarker(const TrackMarkerPtr& marker, int index);

    void removeMarker(const TrackMarkerPtr& marker);

    TrackMarkerPtr getMarkerByName(const std::string & name) const;

    std::string generateUniqueTrackName(const std::string& baseName);

    int getTimeLineFirstFrame() const;
    int getTimeLineLastFrame() const;

    /**
     * @brief Tracks the selected markers over the range defined by [start,end[ (end pointing to the frame
     * after the last one, a la STL).
     **/
    void trackSelectedMarkers(int start, int end, int frameStep, OverlaySupport* viewer);
    void trackMarkers(const std::list<TrackMarkerPtr >& marks,
                      int start,
                      int end,
                      int frameStep,
                      OverlaySupport* viewer);


    void abortTracking();

    void abortTracking_blocking();

    bool isCurrentlyTracking() const;

    void quitTrackerThread_non_blocking();

    bool hasTrackerThreadQuit() const;

    void quitTrackerThread_blocking(bool allowRestart);

    void beginEditSelection(TrackSelectionReason reason);

    void endEditSelection(TrackSelectionReason reason);

    void addTracksToSelection(const std::list<TrackMarkerPtr >& marks, TrackSelectionReason reason);
    void addTrackToSelection(const TrackMarkerPtr& mark, TrackSelectionReason reason);

    void removeTracksFromSelection(const std::list<TrackMarkerPtr >& marks, TrackSelectionReason reason);
    void removeTrackFromSelection(const TrackMarkerPtr& mark, TrackSelectionReason reason);

    void clearSelection(TrackSelectionReason reason);

    void selectAll(TrackSelectionReason reason);

    void getAllMarkers(std::vector<TrackMarkerPtr >* markers) const;

    void getSelectedMarkers(std::list<TrackMarkerPtr >* markers) const;

    void getAllEnabledMarkers(std::vector<TrackMarkerPtr >* markers) const;

    bool isMarkerSelected(const TrackMarkerPtr& marker) const;

    static void getMotionModelsAndHelps(bool addPerspective,
                                        std::vector<std::string>* models,
                                        std::vector<std::string>* tooltips);

    int getTransformReferenceFrame() const;

    void goToPreviousKeyFrame(int time);
    void goToNextKeyFrame(int time);

    void resetTransformCenter();

    NodePtr getCurrentlySelectedTransformNode() const;

    void drawInternalNodesOverlay(double time,
                                  const RenderScale& scale,
                                  ViewIdx view,
                                  OverlaySupport* viewer);

    bool onOverlayPenDownInternalNodes(double time,
                                       const RenderScale & renderScale,
                                       ViewIdx view, const QPointF & viewportPos, const QPointF & pos, double pressure, double timestamp, PenType pen, OverlaySupport* viewer) WARN_UNUSED_RETURN;

    bool onOverlayPenMotionInternalNodes(double time,
                                         const RenderScale & renderScale,
                                         ViewIdx view, const QPointF & viewportPos, const QPointF & pos, double pressure, double timestamp, OverlaySupport* viewer) WARN_UNUSED_RETURN;

    bool onOverlayPenUpInternalNodes(double time,
                                     const RenderScale & renderScale,
                                     ViewIdx view, const QPointF & viewportPos, const QPointF & pos, double pressure, double timestamp, OverlaySupport* viewer) WARN_UNUSED_RETURN;

    bool onOverlayKeyDownInternalNodes(double time,
                                       const RenderScale & renderScale,
                                       ViewIdx view, Key key, KeyboardModifiers modifiers, OverlaySupport* viewer) WARN_UNUSED_RETURN;

    bool onOverlayKeyUpInternalNodes(double time,
                                     const RenderScale & renderScale,
                                     ViewIdx view, Key key, KeyboardModifiers modifiers, OverlaySupport* viewer) WARN_UNUSED_RETURN;

    bool onOverlayKeyRepeatInternalNodes(double time,
                                         const RenderScale & renderScale,
                                         ViewIdx view, Key key, KeyboardModifiers modifiers, OverlaySupport* viewer) WARN_UNUSED_RETURN;

    bool onOverlayFocusGainedInternalNodes(double time,
                                           const RenderScale & renderScale,
                                           ViewIdx view, OverlaySupport* viewer) WARN_UNUSED_RETURN;

    bool onOverlayFocusLostInternalNodes(double time,
                                         const RenderScale & renderScale,
                                         ViewIdx view, OverlaySupport* viewer) WARN_UNUSED_RETURN;


    void solveTransformParams();

    void solveTransformParamsIfAutomatic();


    void exportTrackDataFromExportOptions();


    void onKnobsLoaded();

    void inputChanged(int inputNb);

    bool knobChanged(KnobI* k,
                     ValueChangedReasonEnum reason,
                     ViewSpec view,
                     double time,
                     bool originatedFromMainThread);


    void declarePythonFields();

    void removeItemAsPythonField(const TrackMarkerPtr& item);

    void declareItemAsPythonField(const TrackMarkerPtr& item);

    /*boost::shared_ptr<KnobDouble> getSearchWindowBottomLeftKnob() const;
       boost::shared_ptr<KnobDouble> getSearchWindowTopRightKnob() const;
       boost::shared_ptr<KnobDouble> getPatternTopLeftKnob() const;
       boost::shared_ptr<KnobDouble> getPatternTopRightKnob() const;
       boost::shared_ptr<KnobDouble> getPatternBtmRightKnob() const;
       boost::shared_ptr<KnobDouble> getPatternBtmLeftKnob() const;
       boost::shared_ptr<KnobDouble> getWeightKnob() const;
       boost::shared_ptr<KnobDouble> getCenterKnob() const;
       boost::shared_ptr<KnobDouble> getOffsetKnob() const;
       boost::shared_ptr<KnobDouble> getCorrelationKnob() const;
       boost::shared_ptr<KnobChoice> getMotionModelKnob() const;*/

    void s_keyframeSetOnTrack(const TrackMarkerPtr& marker,
                              int key) { Q_EMIT keyframeSetOnTrack(marker, key); }

    void s_keyframeRemovedOnTrack(const TrackMarkerPtr& marker,
                                  int key) { Q_EMIT keyframeRemovedOnTrack(marker, key); }

    void s_allKeyframesRemovedOnTrack(const TrackMarkerPtr& marker) { Q_EMIT allKeyframesRemovedOnTrack(marker); }

    void s_keyframeSetOnTrackCenter(const TrackMarkerPtr& marker,
                                    int key) { Q_EMIT keyframeSetOnTrackCenter(marker, key); }

    void s_keyframeRemovedOnTrackCenter(const TrackMarkerPtr& marker,
                                        int key) { Q_EMIT keyframeRemovedOnTrackCenter(marker, key); }

    void s_multipleKeyframesRemovedOnTrackCenter(const TrackMarkerPtr& marker,
                                                 const std::list<double>& keys) { Q_EMIT multipleKeyframesRemovedOnTrackCenter(marker, keys); }

    void s_allKeyframesRemovedOnTrackCenter(const TrackMarkerPtr& marker) { Q_EMIT allKeyframesRemovedOnTrackCenter(marker); }

    void s_multipleKeyframesSetOnTrackCenter(const TrackMarkerPtr& marker,
                                             const std::list<double>& keys) { Q_EMIT multipleKeyframesSetOnTrackCenter(marker, keys); }

    void s_trackAboutToClone(const TrackMarkerPtr& marker) { Q_EMIT trackAboutToClone(marker); }

    void s_trackCloned(const TrackMarkerPtr& marker) { Q_EMIT trackCloned(marker); }

    void s_centerKnobValueChanged(const TrackMarkerPtr& marker,
                                  int dimension,
                                  int reason) { Q_EMIT centerKnobValueChanged(marker, dimension, reason); }

    void s_offsetKnobValueChanged(const TrackMarkerPtr& marker,
                                  int dimension,
                                  int reason) { Q_EMIT offsetKnobValueChanged(marker, dimension, reason); }

    void s_errorKnobValueChanged(const TrackMarkerPtr& marker,
                                 int dimension,
                                 int reason) { Q_EMIT errorKnobValueChanged(marker, dimension, reason); }

    void s_weightKnobValueChanged(const TrackMarkerPtr& marker,
                                  int dimension,
                                  int reason) { Q_EMIT weightKnobValueChanged(marker, dimension, reason); }

    void s_motionModelKnobValueChanged(const TrackMarkerPtr& marker,
                                       int dimension,
                                       int reason) { Q_EMIT motionModelKnobValueChanged(marker, dimension, reason); }

    /* void s_patternTopLeftKnobValueChanged(const TrackMarkerPtr& marker,int dimension,int reason) { Q_EMIT patternTopLeftKnobValueChanged(marker,dimension,reason); }
       void s_patternTopRightKnobValueChanged(const TrackMarkerPtr& marker,int dimension,int reason) { Q_EMIT patternTopRightKnobValueChanged(marker,dimension, reason); }
       void s_patternBtmRightKnobValueChanged(const TrackMarkerPtr& marker,int dimension,int reason) { Q_EMIT patternBtmRightKnobValueChanged(marker,dimension, reason); }
       void s_patternBtmLeftKnobValueChanged(const TrackMarkerPtr& marker,int dimension,int reason) { Q_EMIT patternBtmLeftKnobValueChanged(marker,dimension, reason); }*/

    void s_searchBtmLeftKnobValueChanged(const TrackMarkerPtr& marker,
                                         int dimension,
                                         int reason) { Q_EMIT searchBtmLeftKnobValueChanged(marker, dimension, reason); }

    void s_searchTopRightKnobValueChanged(const TrackMarkerPtr& marker,
                                          int dimension,
                                          int reason) { Q_EMIT searchTopRightKnobValueChanged(marker, dimension, reason); }

    void s_onNodeInputChanged(int inputNb) { Q_EMIT onNodeInputChanged(inputNb); }

public Q_SLOTS:

    void onMarkerEnabledChanged(int reason);

    void onSchedulerTrackingStarted(int frameStep);
    void onSchedulerTrackingFinished();

    void onSchedulerTrackingProgress(double progress);

Q_SIGNALS:

    void keyframeSetOnTrack(TrackMarkerPtr marker, int);
    void keyframeRemovedOnTrack(TrackMarkerPtr marker, int);
    void allKeyframesRemovedOnTrack(TrackMarkerPtr marker);

    void keyframeSetOnTrackCenter(TrackMarkerPtr marker, int);
    void keyframeRemovedOnTrackCenter(TrackMarkerPtr marker, int);
    void multipleKeyframesRemovedOnTrackCenter(TrackMarkerPtr marker, std::list<double>);
    void allKeyframesRemovedOnTrackCenter(TrackMarkerPtr marker);
    void multipleKeyframesSetOnTrackCenter(TrackMarkerPtr marker, std::list<double>);

    void trackAboutToClone(TrackMarkerPtr marker);
    void trackCloned(TrackMarkerPtr marker);

    //reason is of type TrackSelectionReason
    void selectionChanged(int reason);
    void selectionAboutToChange(int reason);

    void trackInserted(TrackMarkerPtr marker, int index);
    void trackRemoved(TrackMarkerPtr marker);

    void enabledChanged(TrackMarkerPtr marker, int reason);

    void centerKnobValueChanged(TrackMarkerPtr marker, int, int);
    void offsetKnobValueChanged(TrackMarkerPtr marker, int, int);
    void errorKnobValueChanged(TrackMarkerPtr marker, int, int);
    void weightKnobValueChanged(TrackMarkerPtr marker, int, int);
    void motionModelKnobValueChanged(TrackMarkerPtr marker, int, int);

    /*void patternTopLeftKnobValueChanged(TrackMarkerPtr marker,int,int);
       void patternTopRightKnobValueChanged(TrackMarkerPtr marker,int,int);
       void patternBtmRightKnobValueChanged(TrackMarkerPtr marker,int,int);
       void patternBtmLeftKnobValueChanged(TrackMarkerPtr marker,int,int);*/

    void searchBtmLeftKnobValueChanged(TrackMarkerPtr marker, int, int);
    void searchTopRightKnobValueChanged(TrackMarkerPtr marker, int, int);

    void trackingStarted(int);
    void trackingFinished();

    void onNodeInputChanged(int inputNb);

private:

    void setFromPointsToInputRod();

    void endSelection(TrackSelectionReason reason);

    boost::scoped_ptr<TrackerContextPrivate> _imp;
};

struct TrackArgsPrivate;
class TrackArgs
    : public GenericThreadStartArgs
{
public:

    TrackArgs();

    TrackArgs(int start,
              int end,
              int step,
              const boost::shared_ptr<TimeLine>& timeline,
              ViewerInstance* viewer,
              const boost::shared_ptr<mv::AutoTrack>& autoTrack,
              const boost::shared_ptr<TrackerFrameAccessor>& fa,
              const std::vector<boost::shared_ptr<TrackMarkerAndOptions> >& tracks,
              double formatWidth,
              double formatHeight);

    TrackArgs(const TrackArgs& other);
    void operator=(const TrackArgs& other);

    virtual ~TrackArgs();

    double getFormatHeight() const;
    double getFormatWidth() const;

    QMutex* getAutoTrackMutex() const;
    int getStart() const;

    int getEnd() const;

    int getStep() const;

    boost::shared_ptr<TimeLine> getTimeLine() const;
    ViewerInstance* getViewer() const;

    int getNumTracks() const;
    const std::vector<boost::shared_ptr<TrackMarkerAndOptions> >& getTracks() const;
    boost::shared_ptr<mv::AutoTrack> getLibMVAutoTrack() const;

    void getEnabledChannels(bool* r, bool* g, bool* b) const;

    void getRedrawAreasNeeded(int time, std::list<RectD>* canonicalRects) const;

private:

    boost::scoped_ptr<TrackArgsPrivate> _imp;
};

struct TrackSchedulerPrivate;
class TrackScheduler
    : public GenericSchedulerThread
{
GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
GCC_DIAG_SUGGEST_OVERRIDE_ON

public:


    TrackScheduler(TrackerParamsProvider* paramsProvider,
                   const NodeWPtr& node);

    virtual ~TrackScheduler();

    /**
     * @brief Track the selectedInstances, calling the instance change action on each button (either the previous or
     * next button) in a separate thread.
     * @param start the first frame to track, if forward is true then start < end otherwise start > end
     * @param end the next frame after the last frame to track (a la STL iterators), if forward is true then end > start
     **/
    void track(const boost::shared_ptr<TrackArgs>& args)
    {
        startTask(args);
    }

    void emit_trackingStarted(int step)
    {
        Q_EMIT trackingStarted(step);
    }

    void emit_trackingFinished( )
    {
        Q_EMIT trackingFinished();
    }

private Q_SLOTS:

    void doRenderCurrentFrameForViewer(ViewerInstance* viewer);


Q_SIGNALS:

    void trackingStarted(int step);

    void trackingFinished();

    void trackingProgress(double progress);

    void renderCurrentFrameForViewer(ViewerInstance* viewer);

private:

    virtual TaskQueueBehaviorEnum tasksQueueBehaviour() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return eTaskQueueBehaviorSkipToMostRecent;
    }

    virtual ThreadStateEnum threadLoopOnce(const ThreadStartArgsPtr& inArgs) OVERRIDE FINAL WARN_UNUSED_RETURN;
    boost::scoped_ptr<TrackSchedulerPrivate> _imp;
};

NATRON_NAMESPACE_EXIT;

#endif // TRACKERCONTEXT_H
