//  Natron
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Created by Alexandre GAUTHIER-FOICHAT on 6/1/2012.
 * contact: immarespond at gmail dot com
 *
 */

#include "TimeLineGui.h"

#include <cmath>
#include <set>
#include <QtGui/QFont>
CLANG_DIAG_OFF(unused-private-field)
// /opt/local/include/QtGui/qmime.h:119:10: warning: private field 'type' is not used [-Wunused-private-field]
#include <QtGui/QMouseEvent>
CLANG_DIAG_ON(unused-private-field)
#include <QCoreApplication>
#include <QThread>
#include "Global/GlobalDefines.h"

#include "Engine/Cache.h"
#include "Engine/Node.h"
#include "Engine/Project.h"
#include "Engine/ViewerInstance.h"
#include "Engine/TimeLine.h"
#include "Engine/Settings.h"
#include "Engine/KnobTypes.h"
#include "Engine/Image.h"

#include "Gui/ViewerTab.h"
#include "Gui/TextRenderer.h"
#include "Gui/ticks.h"
#include "Gui/Gui.h"
#include "Gui/GuiMacros.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/GuiApplicationManager.h"
// warning: 'gluErrorString' is deprecated: first deprecated in OS X 10.9 [-Wdeprecated-declarations]
CLANG_DIAG_OFF(deprecated-declarations)
GCC_DIAG_OFF(deprecated-declarations)

using namespace Natron;

#define TICK_HEIGHT 7
#define CURSOR_WIDTH 15
#define CURSOR_HEIGHT 8

#define DEFAULT_TIMELINE_LEFT_BOUND 0
#define DEFAULT_TIMELINE_RIGHT_BOUND 100

namespace { // protect local classes in anonymous namespace
struct ZoomContext
{
    ZoomContext()
        : bottom(0.)
          , left(0.)
          , zoomFactor(1.)
    {
    }

    QPoint oldClick; /// the last click pressed, in widget coordinates [ (0,0) == top left corner ]
    double bottom; /// the bottom edge of orthographic projection
    double left; /// the left edge of the orthographic projection
    double zoomFactor; /// the zoom factor applied to the current image
};

struct CachedFrame
{
    SequenceTime time;
    StorageModeEnum mode;

    CachedFrame(SequenceTime t,
                StorageModeEnum m)
        : time(t)
          , mode(m)
    {
    }
};

struct CachedFrame_compare_time
{
    bool operator() (const CachedFrame & lhs,
                     const CachedFrame & rhs) const
    {
        return lhs.time < rhs.time;
    }
};

typedef std::set<CachedFrame,CachedFrame_compare_time> CachedFrames;
}

struct TimelineGuiPrivate
{
    ViewerInstance* _viewer;
    boost::shared_ptr<TimeLine> _timeline; //ptr to the internal timeline
    Gui* _gui; //< ptr to the gui
    bool _alphaCursor; // should cursor be drawn semi-transparant
    QPoint _lastMouseEventWidgetCoord;
    Natron::TimelineStateEnum _state; //state machine for mouse events
    ZoomContext _zoomCtx;
    Natron::TextRenderer _textRenderer;
    QFont _font;
    bool _firstPaint;
    CachedFrames cachedFrames;

    mutable QMutex boundariesMutex;
    SequenceTime leftBoundary, rightBoundary;

    mutable QMutex frameRangeEditedMutex;
    bool isFrameRangeEdited;
    
    TimelineGuiPrivate(ViewerInstance* viewer,
                       Gui* gui)
    :  _viewer(viewer)
    , _timeline()
    , _gui(gui)
    , _alphaCursor(false)
    , _lastMouseEventWidgetCoord()
    , _state(eTimelineStateIdle)
    , _zoomCtx()
    , _textRenderer()
    , _font(appFont,appFontSize)
    , _firstPaint(true)
    , cachedFrames()
    , boundariesMutex()
    , leftBoundary(0)
    , rightBoundary(0)
    , frameRangeEditedMutex()
    , isFrameRangeEdited(false)
    {
    }
};

TimeLineGui::TimeLineGui(ViewerInstance* viewer,
                         boost::shared_ptr<TimeLine> timeline,
                         Gui* gui,
                         QWidget* parent,
                         const QGLWidget *shareWidget)
    : QGLWidget(parent,shareWidget)
      , _imp( new TimelineGuiPrivate(viewer,gui) )
{
    setTimeline(timeline);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setMouseTracking(true);
}

TimeLineGui::~TimeLineGui()
{
}

void
TimeLineGui::setTimeline(const boost::shared_ptr<TimeLine>& timeline)
{
    if (_imp->_timeline) {
        //connect the internal timeline to the gui
        QObject::disconnect( _imp->_timeline.get(), SIGNAL( frameChanged(SequenceTime,int) ), this, SLOT( onFrameChanged(SequenceTime,int) ) );
        QObject::disconnect( _imp->_timeline.get(), SIGNAL( frameRangeChanged(SequenceTime,SequenceTime) ),
                         this, SLOT( onFrameRangeChanged(SequenceTime,SequenceTime) ) );
        
        
        //connect the gui to the internal timeline
        QObject::disconnect( this, SIGNAL( frameChanged(SequenceTime) ), _imp->_timeline.get(), SLOT( onFrameChanged(SequenceTime) ) );
        QObject::disconnect( _imp->_timeline.get(), SIGNAL( keyframeIndicatorsChanged() ), this, SLOT( onKeyframesIndicatorsChanged() ) );
    }
  
    //connect the internal timeline to the gui
    QObject::connect( timeline.get(), SIGNAL( frameChanged(SequenceTime,int) ), this, SLOT( onFrameChanged(SequenceTime,int) ) );
    
    
    //connect the gui to the internal timeline
    QObject::connect( this, SIGNAL( frameChanged(SequenceTime) ), timeline.get(), SLOT( onFrameChanged(SequenceTime) ) );
    
    QObject::connect( timeline.get(), SIGNAL( keyframeIndicatorsChanged() ), this, SLOT( onKeyframesIndicatorsChanged() ) );
   
    _imp->_timeline = timeline;


}

boost::shared_ptr<TimeLine>
TimeLineGui::getTimeline() const
{
    return _imp->_timeline;
}

QSize
TimeLineGui::sizeHint() const
{
    return QSize(1000,45);
}

void
TimeLineGui::initializeGL()
{
}

void
TimeLineGui::resizeGL(int width,
                      int height)
{
    if (height == 0) {
        height = 1;
    }
    glViewport (0, 0, width, height);
}

void
TimeLineGui::discardGuiPointer()
{
    _imp->_gui = 0;
}

void
TimeLineGui::paintGL()
{
    if (!_imp->_gui) {
        return;
    }
    glCheckError();
    
    SequenceTime leftBound,rightBound;
    {
        QMutexLocker k(&_imp->boundariesMutex);
        leftBound = _imp->leftBoundary;
        rightBound = _imp->rightBoundary;
    }
    SequenceTime cur = _imp->_timeline->currentFrame();

    if (_imp->_firstPaint) {
        _imp->_firstPaint = false;
        
        if ( (rightBound - leftBound) > 10000 ) {
            centerOn(cur - 100, cur + 100);
        } else if ( (rightBound - leftBound) < 50 ) {
            centerOn(cur - DEFAULT_TIMELINE_LEFT_BOUND, cur + DEFAULT_TIMELINE_RIGHT_BOUND);
        } else {
            centerOn(leftBound,rightBound);
        }
    }

    double w = (double)width();
    double h = (double)height();
    //assert(_zoomCtx._zoomFactor > 0);
    if (_imp->_zoomCtx.zoomFactor <= 0) {
        return;
    }
    //assert(_zoomCtx._zoomFactor <= 1024);
    double bottom = _imp->_zoomCtx.bottom;
    double left = _imp->_zoomCtx.left;
    double top = bottom +  h / (double)_imp->_zoomCtx.zoomFactor;
    double right = left +  (w / (double)_imp->_zoomCtx.zoomFactor);

    double clearR,clearG,clearB;
    boost::shared_ptr<Settings> settings = appPTR->getCurrentSettings();
    settings->getTimelineBGColor(&clearR, &clearG, &clearB);
    
    if ( (left == right) || (top == bottom) ) {
        glClearColor(clearR,clearG,clearB,1.);
        glClear(GL_COLOR_BUFFER_BIT);

        return;
    }

    {
        GLProtectAttrib a(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT | GL_LINE_BIT | GL_ENABLE_BIT | GL_HINT_BIT | GL_SCISSOR_BIT | GL_TRANSFORM_BIT);
        GLProtectMatrix p(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(left, right, bottom, top, 1, -1);
        GLProtectMatrix m(GL_MODELVIEW);
        glLoadIdentity();

        glClearColor(clearR,clearG,clearB,1.);
        glClear(GL_COLOR_BUFFER_BIT);
        glCheckErrorIgnoreOSXBug();

        QPointF btmLeft = toTimeLineCoordinates(0,height() - 1);
        QPointF topRight = toTimeLineCoordinates(width() - 1, 0);


        /// change the backgroud color of the portion of the timeline where images are lying
        int firstFrame,lastFrame;
        _imp->_gui->getApp()->getFrameRange(&firstFrame, &lastFrame);
        QPointF firstFrameWidgetPos = toWidgetCoordinates(firstFrame,0);
        QPointF lastFrameWidgetPos = toWidgetCoordinates(lastFrame,0);

        glScissor( firstFrameWidgetPos.x(),0,
                  lastFrameWidgetPos.x() - firstFrameWidgetPos.x(),height() );

        double bgR,bgG,bgB;
        settings->getBaseColor(&bgR, &bgG, &bgB);
        
        glEnable(GL_SCISSOR_TEST);
        glClearColor(bgR,bgG,bgB,1.);
        glClear(GL_COLOR_BUFFER_BIT);
        glCheckErrorIgnoreOSXBug();
        glDisable(GL_SCISSOR_TEST);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        QFontMetrics fontM(_imp->_font);

        double lineYPosWidget = height() - 1 - fontM.height()  - TICK_HEIGHT / 2.;
        double lineYpos = toTimeLineCoordinates(0,lineYPosWidget).y();
        double cachedLineYPos = toTimeLineCoordinates(0,lineYPosWidget + 1).y();

        /*draw the horizontal axis*/
        double txtR,txtG,txtB;
        settings->getTextColor(&txtR, &txtG, &txtB);
        double kfR,kfG,kfB;
        settings->getKeyframeColor(&kfR, &kfG, &kfB);
        
        double cursorR,cursorG,cursorB;
        settings->getTimelinePlayheadColor(&cursorR, &cursorG, &cursorB);
        
        double boundsR,boundsG,boundsB;
        settings->getTimelineBoundsColor(&boundsR, &boundsG, &boundsB);
        
        double cachedR,cachedG,cachedB;
        settings->getCachedFrameColor(&cachedR, &cachedG, &cachedB);
        
        double dcR,dcG,dcB;
        settings->getDiskCachedColor(&dcR, &dcG, &dcB);

        
        glColor4f(txtR / 2.,txtG / 2., txtB / 2., 1.);
        glBegin(GL_LINES);
        glVertex2f(btmLeft.x(), lineYpos);
        glVertex2f(topRight.x(), lineYpos);
        glEnd();
        glCheckErrorIgnoreOSXBug();

        double tickBottom = toTimeLineCoordinates( 0,height() - 1 - fontM.height() ).y();
        double tickTop = toTimeLineCoordinates(0,height() - 1 - fontM.height()  - TICK_HEIGHT).y();
        const double smallestTickSizePixel = 5.; // tick size (in pixels) for alpha = 0.
        const double largestTickSizePixel = 1000.; // tick size (in pixels) for alpha = 1.
        std::vector<double> acceptedDistances;
        acceptedDistances.push_back(1.);
        acceptedDistances.push_back(5.);
        acceptedDistances.push_back(10.);
        acceptedDistances.push_back(50.);
        const double rangePixel =  width();
        const double range_min = btmLeft.x();
        const double range_max =  topRight.x();
        const double range = range_max - range_min;
        double smallTickSize;
        bool half_tick;
        ticks_size(range_min, range_max, rangePixel, smallestTickSizePixel, &smallTickSize, &half_tick);
        int m1, m2;
        const int ticks_max = 1000;
        double offset;
        ticks_bounds(range_min, range_max, smallTickSize, half_tick, ticks_max, &offset, &m1, &m2);
        std::vector<int> ticks;
        ticks_fill(half_tick, ticks_max, m1, m2, &ticks);
        const double smallestTickSize = range * smallestTickSizePixel / rangePixel;
        const double largestTickSize = range * largestTickSizePixel / rangePixel;
        const double minTickSizeTextPixel = fontM.width( QString("00") ); // AXIS-SPECIFIC
        const double minTickSizeText = range * minTickSizeTextPixel / rangePixel;
        for (int i = m1; i <= m2; ++i) {
            double value = i * smallTickSize + offset;
            const double tickSize = ticks[i - m1] * smallTickSize;
            const double alpha = ticks_alpha(smallestTickSize, largestTickSize, tickSize);

            glColor4f(txtR,txtG,txtB, alpha);

            glBegin(GL_LINES);
            glVertex2f(value, tickBottom);
            glVertex2f(value, tickTop);
            glEnd();
            glCheckErrorIgnoreOSXBug();

            bool doRender = std::abs(std::floor(0.5 + value) - value) == 0.;
            if (doRender && tickSize > minTickSizeText) {
                const int tickSizePixel = rangePixel * tickSize / range;
                const QString s = QString::number(value);
                const int sSizePixel =  fontM.width(s);
                if (tickSizePixel > sSizePixel) {
                    const int sSizeFullPixel = sSizePixel + minTickSizeTextPixel;
                    double alphaText = 1.0; //alpha;
                    if (tickSizePixel < sSizeFullPixel) {
                        // when the text size is between sSizePixel and sSizeFullPixel,
                        // draw it with a lower alpha
                        alphaText *= (tickSizePixel - sSizePixel) / (double)minTickSizeTextPixel;
                    }
                    QColor c;
                    c.setRgbF(Natron::clamp(txtR), Natron::clamp(txtG), Natron::clamp(txtB));
                    c.setAlpha(255 * alphaText);
                    glCheckError();
                    renderText(value, btmLeft.y(), s, c, _imp->_font);
                }
            }
        }
        glCheckError();

        QPointF cursorBtm(_imp->_timeline->currentFrame(),lineYpos);
        QPointF cursorBtmWidgetCoord = toWidgetCoordinates( cursorBtm.x(),cursorBtm.y() );
        QPointF cursorTopLeft = toTimeLineCoordinates(cursorBtmWidgetCoord.x() - CURSOR_WIDTH / 2.,
                                                      cursorBtmWidgetCoord.y() - CURSOR_HEIGHT);
        QPointF cursorTopRight = toTimeLineCoordinates(cursorBtmWidgetCoord.x() + CURSOR_WIDTH / 2.,
                                                       cursorBtmWidgetCoord.y() - CURSOR_HEIGHT);
        QPointF leftBoundBtm(leftBound,lineYpos);
        QPointF leftBoundWidgetCoord = toWidgetCoordinates( leftBoundBtm.x(),leftBoundBtm.y() );
        QPointF leftBoundBtmRight = toTimeLineCoordinates( leftBoundWidgetCoord.x() + CURSOR_WIDTH / 2.,
                                                          leftBoundWidgetCoord.y() );
        QPointF leftBoundTop = toTimeLineCoordinates(leftBoundWidgetCoord.x(),
                                                     leftBoundWidgetCoord.y() - CURSOR_HEIGHT);
        QPointF rightBoundBtm(rightBound,lineYpos);
        QPointF rightBoundWidgetCoord = toWidgetCoordinates( rightBoundBtm.x(),rightBoundBtm.y() );
        QPointF rightBoundBtmLeft = toTimeLineCoordinates( rightBoundWidgetCoord.x() - CURSOR_WIDTH / 2.,
                                                          rightBoundWidgetCoord.y() );
        QPointF rightBoundTop = toTimeLineCoordinates(rightBoundWidgetCoord.x(),
                                                      rightBoundWidgetCoord.y() - CURSOR_HEIGHT);
        std::list<SequenceTime> keyframes;
        _imp->_timeline->getKeyframes(&keyframes);

        //draw an alpha cursor if the mouse is hovering the timeline
        glEnable(GL_POLYGON_SMOOTH);
        glHint(GL_POLYGON_SMOOTH_HINT,GL_DONT_CARE);
        if (_imp->_alphaCursor) {
            int currentPosBtmWidgetCoordX = _imp->_lastMouseEventWidgetCoord.x();
            int currentPosBtmWidgetCoordY = toWidgetCoordinates(0,lineYpos).y();
            QPointF currentPosBtm = toTimeLineCoordinates(currentPosBtmWidgetCoordX,currentPosBtmWidgetCoordY);
            QPointF currentPosTopLeft = toTimeLineCoordinates(currentPosBtmWidgetCoordX - CURSOR_WIDTH / 2.,
                                                              currentPosBtmWidgetCoordY - CURSOR_HEIGHT);
            QPointF currentPosTopRight = toTimeLineCoordinates(currentPosBtmWidgetCoordX + CURSOR_WIDTH / 2.,
                                                               currentPosBtmWidgetCoordY - CURSOR_HEIGHT);
            int hoveredTime = std::floor(currentPosBtm.x() + 0.5);
            QString mouseNumber( QString::number(hoveredTime) );
            QPoint mouseNumberWidgetCoord(currentPosBtmWidgetCoordX - fontM.width(mouseNumber) / 2,
                                          currentPosBtmWidgetCoordY - CURSOR_HEIGHT - 2);
            QPointF mouseNumberPos = toTimeLineCoordinates( mouseNumberWidgetCoord.x(),mouseNumberWidgetCoord.y() );
            std::list<SequenceTime>::iterator foundHoveredAsKeyframe = std::find(keyframes.begin(),keyframes.end(),hoveredTime);
            QColor currentColor;
            if ( foundHoveredAsKeyframe != keyframes.end() ) {
                glColor4f(kfR,kfG,kfB,0.4);
                currentColor.setRgbF(Natron::clamp(kfR), Natron::clamp(kfG), Natron::clamp(kfB));
            } else {
                glColor4f(cursorR, cursorG, cursorB, 0.4);
                currentColor.setRgbF(Natron::clamp(cursorR), Natron::clamp(cursorG), Natron::clamp(cursorB));
            }
            currentColor.setAlpha(100);

            
            glBegin(GL_POLYGON);
            glVertex2f( currentPosBtm.x(),currentPosBtm.y() );
            glVertex2f( currentPosTopLeft.x(),currentPosTopLeft.y() );
            glVertex2f( currentPosTopRight.x(),currentPosTopRight.y() );
            glEnd();
            glCheckError();

            renderText(mouseNumberPos.x(),mouseNumberPos.y(), mouseNumber, currentColor, _imp->_font);
        }

        //draw the bounds and the current time cursor
        std::list<SequenceTime>::iterator isCurrentTimeAKeyframe = std::find( keyframes.begin(),keyframes.end(),_imp->_timeline->currentFrame() );
        QColor actualCursorColor;
        if ( isCurrentTimeAKeyframe != keyframes.end() ) {
            glColor4f(kfR,kfG,kfB,1.);
            actualCursorColor.setRgbF(Natron::clamp(kfR), Natron::clamp(kfG), Natron::clamp(kfB));
        } else {
            glColor4f(cursorR, cursorG, cursorB,1.);
            actualCursorColor.setRgbF(Natron::clamp(cursorR), Natron::clamp(cursorG), Natron::clamp(cursorB));
        }

        QString currentFrameStr( QString::number( _imp->_timeline->currentFrame() ) );
        double cursorTextXposWidget = cursorBtmWidgetCoord.x() - fontM.width(currentFrameStr) / 2.;
        double cursorTextPos = toTimeLineCoordinates(cursorTextXposWidget,0).x();
        renderText(cursorTextPos,cursorTopLeft.y(), currentFrameStr, actualCursorColor, _imp->_font);
        glBegin(GL_POLYGON);
        glVertex2f( cursorBtm.x(),cursorBtm.y() );
        glVertex2f( cursorTopLeft.x(),cursorTopLeft.y() );
        glVertex2f( cursorTopRight.x(),cursorTopRight.y() );
        glEnd();
        glCheckErrorIgnoreOSXBug();

        QColor boundsColor;
        boundsColor.setRgbF(Natron::clamp(boundsR), Natron::clamp(boundsG), Natron::clamp(boundsB));
        
        if ( leftBound != _imp->_timeline->currentFrame() ) {
            QString leftBoundStr( QString::number(leftBound) );
            double leftBoundTextXposWidget = toWidgetCoordinates( ( leftBoundBtm.x() + leftBoundBtmRight.x() ) / 2.,0 ).x() - fontM.width(leftBoundStr) / 2.;
            double leftBoundTextPos = toTimeLineCoordinates(leftBoundTextXposWidget,0).x();
            renderText(leftBoundTextPos,leftBoundTop.y(),
                       leftBoundStr, boundsColor, _imp->_font);
        }
        glColor4f(boundsR,boundsG,boundsB,1.);
        glBegin(GL_POLYGON);
        glVertex2f( leftBoundBtm.x(),leftBoundBtm.y() );
        glVertex2f( leftBoundBtmRight.x(),leftBoundBtmRight.y() );
        glVertex2f( leftBoundTop.x(),leftBoundTop.y() );
        glEnd();
        glCheckErrorIgnoreOSXBug();

        if ( rightBound != cur ) {
            QString rightBoundStr( QString::number( rightBound ) );
            double rightBoundTextXposWidget = toWidgetCoordinates( ( rightBoundBtm.x() + rightBoundBtmLeft.x() ) / 2.,0 ).x() - fontM.width(rightBoundStr) / 2.;
            double rightBoundTextPos = toTimeLineCoordinates(rightBoundTextXposWidget,0).x();
            renderText(rightBoundTextPos,rightBoundTop.y(),
                       rightBoundStr, boundsColor, _imp->_font);
        }
        glColor4f(boundsR,boundsG,boundsB,1.);
        glCheckError();
        glBegin(GL_POLYGON);
        glVertex2f( rightBoundBtm.x(),rightBoundBtm.y() );
        glVertex2f( rightBoundBtmLeft.x(),rightBoundBtmLeft.y() );
        glVertex2f( rightBoundTop.x(),rightBoundTop.y() );
        glEnd();
        glCheckErrorIgnoreOSXBug();

        glDisable(GL_POLYGON_SMOOTH);

        //draw cached frames
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
        glCheckError();
        glLineWidth(2);
        glCheckError();
        glBegin(GL_LINES);
        for (CachedFrames::const_iterator i = _imp->cachedFrames.begin(); i != _imp->cachedFrames.end(); ++i) {
            if (i->mode == eStorageModeRAM) {
                glColor4f(cachedR,cachedG,cachedB,1.);
            } else if (i->mode == eStorageModeDisk) {
                glColor4f(dcR,dcG,dcB,1.);
            }
            glVertex2f(i->time - 0.5,cachedLineYPos);
            glVertex2f(i->time + 0.5,cachedLineYPos);
        }
        glEnd();
        
        ///now draw keyframes
        glColor4f(kfR,kfG,kfB,1.);
        std::set<SequenceTime> alreadyDrawnKeyframes;
        glBegin(GL_LINES);
        for (std::list<SequenceTime>::const_iterator i = keyframes.begin(); i != keyframes.end(); ++i) {
            std::pair<std::set<SequenceTime>::iterator,bool> success = alreadyDrawnKeyframes.insert(*i);
            if (success.second) {
                glVertex2f(*i - 0.5,lineYpos);
                glVertex2f(*i + 0.5,lineYpos);
            }
        }
        glEnd();
        glCheckErrorIgnoreOSXBug();
    } // GLProtectAttrib a(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT | GL_LINE_BIT | GL_ENABLE_BIT | GL_HINT_BIT | GL_SCISSOR_BIT | GL_TRANSFORM_BIT);

    glCheckError();
} // paintGL

void
TimeLineGui::renderText(double x,
                        double y,
                        const QString & text,
                        const QColor & color,
                        const QFont & font) const
{
    assert( QGLContext::currentContext() == context() );

    glCheckError();
    if ( text.isEmpty() ) {
        return;
    }
    {
        GLProtectAttrib a(GL_TRANSFORM_BIT);
        /*we put the ortho proj to the widget coords, draw the elements and revert back to the old orthographic proj.*/
        double h = (double)height();
        double w = (double)width();
        GLProtectMatrix p(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, w, 0, h, 1, -1);
        glMatrixMode(GL_MODELVIEW);
        
        QPointF pos = toWidgetCoordinates(x, y);
        glCheckError();
        _imp->_textRenderer.renderText(pos.x(),h - pos.y(),text,color,font);
        glCheckError();
    } // GLProtectAttrib a(GL_TRANSFORM_BIT);
    glCheckError();
}

void
TimeLineGui::onFrameChanged(SequenceTime,
                            int)
{
    update();
}

void
TimeLineGui::seek(SequenceTime time)
{
    if ( time != _imp->_timeline->currentFrame() ) {
        _imp->_gui->getApp()->setLastViewerUsingTimeline(_imp->_viewer->getNode());
        emit frameChanged(time);
        update();
    }
}

void
TimeLineGui::mousePressEvent(QMouseEvent* e)
{
    int leftBound,rightBound;
    {
        QMutexLocker k(&_imp->boundariesMutex);
        leftBound = _imp->leftBoundary;
        rightBound = _imp->rightBoundary;
    }
    if (buttonDownIsMiddle(e)) {
        centerOn(leftBound, rightBound);
    } else {
        _imp->_lastMouseEventWidgetCoord = e->pos();
        double t = toTimeLineCoordinates(e->x(),0).x();
        SequenceTime tseq = std::floor(t + 0.5);
        if (modCASIsControl(e)) {
            _imp->_state = eTimelineStateDraggingBoundary;
            int firstPos = toWidgetCoordinates(leftBound - 1,0).x();
            int lastPos = toWidgetCoordinates(rightBound + 1,0).x();
            int distFromFirst = std::abs(e->x() - firstPos);
            int distFromLast = std::abs(e->x() - lastPos);
            if (distFromFirst  > distFromLast) {
                setBoundariesInternal(leftBound, tseq, true); // moving last frame anchor
            } else {
                setBoundariesInternal( tseq, rightBound, true );   // moving first frame anchor
            }
        } else {
            _imp->_state = eTimelineStateDraggingCursor;
            _imp->_gui->setUserScrubbingTimeline(true);
            seek(tseq);
        }
    }
}

void
TimeLineGui::mouseMoveEvent(QMouseEvent* e)
{
    int leftBound,rightBound;
    {
        QMutexLocker k(&_imp->boundariesMutex);
        leftBound = _imp->leftBoundary;
        rightBound = _imp->rightBoundary;
    }
    
    _imp->_lastMouseEventWidgetCoord = e->pos();
    double t = toTimeLineCoordinates(e->x(),0).x();
    SequenceTime tseq = std::floor(t + 0.5);
    bool distortViewPort = false;
    bool onEditingFinishedOnly = appPTR->getCurrentSettings()->getRenderOnEditingFinishedOnly();
    if (_imp->_state == eTimelineStateDraggingCursor && !onEditingFinishedOnly) {
        if ( tseq != _imp->_timeline->currentFrame() ) {
            _imp->_gui->getApp()->setLastViewerUsingTimeline(_imp->_viewer->getNode());
            emit frameChanged(tseq);
        }
        distortViewPort = true;
        _imp->_alphaCursor = false;
    } else if (_imp->_state == eTimelineStateDraggingBoundary) {
        int firstPos = toWidgetCoordinates(leftBound - 1,0).x();
        int lastPos = toWidgetCoordinates(rightBound + 1,0).x();
        int distFromFirst = std::abs(e->x() - firstPos);
        int distFromLast = std::abs(e->x() - lastPos);
        if (distFromFirst  > distFromLast) { // moving last frame anchor
            if (leftBound <= tseq) {
                setBoundariesInternal(leftBound, tseq, true);
            }
        } else { // moving first frame anchor
            if (rightBound >= tseq) {
                setBoundariesInternal(tseq, rightBound, true);
            }
        }
        distortViewPort = true;
        _imp->_alphaCursor = false;
    } else {
        _imp->_alphaCursor = true;
    }

    if (distortViewPort) {
        double leftMost = toTimeLineCoordinates(0,0).x();
        double rightMost = toTimeLineCoordinates(width() - 1,0).x();
        if (tseq < leftMost) {
            centerOn(tseq, rightMost);
        } else if (tseq > rightMost) {
            centerOn(leftMost, tseq);
        } else {
            update();
        }
    } else {
        update();
    }
}

void
TimeLineGui::enterEvent(QEvent* e)
{
    _imp->_alphaCursor = true;
    update();
    QGLWidget::enterEvent(e);
}

void
TimeLineGui::leaveEvent(QEvent* e)
{
    _imp->_alphaCursor = false;
    update();
    QGLWidget::leaveEvent(e);
}

void
TimeLineGui::mouseReleaseEvent(QMouseEvent* e)
{
    if (_imp->_state == eTimelineStateDraggingCursor) {
        _imp->_gui->setUserScrubbingTimeline(false);
        _imp->_gui->refreshAllPreviews();
        bool onEditingFinishedOnly = appPTR->getCurrentSettings()->getRenderOnEditingFinishedOnly();
        if (onEditingFinishedOnly) {
            double t = toTimeLineCoordinates(e->x(),0).x();
            SequenceTime tseq = std::floor(t + 0.5);
            if ( tseq != _imp->_timeline->currentFrame() ) {
                _imp->_gui->getApp()->setLastViewerUsingTimeline(_imp->_viewer->getNode());
                emit frameChanged(tseq);
            }

        }
    }

    _imp->_state = eTimelineStateIdle;
    QGLWidget::mouseReleaseEvent(e);
}

void
TimeLineGui::wheelEvent(QWheelEvent* e)
{
    if (e->orientation() != Qt::Vertical) {
        return;
    }
    const double scaleFactor = std::pow( NATRON_WHEEL_ZOOM_PER_DELTA, e->delta() );
    double newZoomFactor = _imp->_zoomCtx.zoomFactor * scaleFactor;
    if (newZoomFactor <= 0.01) {
        newZoomFactor = 0.01;
    } else if (newZoomFactor > 1024.) {
        newZoomFactor = 1024.;
    }
    QPointF zoomCenter = toTimeLineCoordinates( e->x(), e->y() );
    double zoomRatio =   _imp->_zoomCtx.zoomFactor / newZoomFactor;
    _imp->_zoomCtx.left = zoomCenter.x() - (zoomCenter.x() - _imp->_zoomCtx.left) * zoomRatio;
    _imp->_zoomCtx.bottom = zoomCenter.y() - (zoomCenter.y() - _imp->_zoomCtx.bottom) * zoomRatio;

    _imp->_zoomCtx.zoomFactor = newZoomFactor;

    update();
}

void
TimeLineGui::setBoundariesInternal(SequenceTime first, SequenceTime last,bool emitSignal)
{
    if (first <= last) {
        {
            QMutexLocker k(&_imp->boundariesMutex);
            _imp->leftBoundary = first;
            _imp->rightBoundary = last;
        }
        if (emitSignal) {
            emit boundariesChanged(first, last);
        } else {
            update();
        }
        setFrameRangeEdited(true);
    }
}

void
TimeLineGui::setBoundaries(SequenceTime first,
                           SequenceTime last)
{
    setBoundariesInternal(first, last, false);
   
}


void
TimeLineGui::centerOn(SequenceTime left,
                      SequenceTime right)
{
    double curveWidth = right - left + 10;
    double w = width();

    _imp->_zoomCtx.left = left - 5;
    _imp->_zoomCtx.zoomFactor = w / curveWidth;

    update();
}


SequenceTime
TimeLineGui::leftBound() const
{
    QMutexLocker k(&_imp->boundariesMutex);
    return _imp->leftBoundary;
}

SequenceTime
TimeLineGui::rightBound() const
{
    QMutexLocker k(&_imp->boundariesMutex);
    return _imp->rightBoundary;
}

void
TimeLineGui::getBounds(SequenceTime* left,SequenceTime* right) const
{
    QMutexLocker k(&_imp->boundariesMutex);
    *left = _imp->leftBoundary;
    *right = _imp->rightBoundary;
}

SequenceTime
TimeLineGui::currentFrame() const
{
    return _imp->_timeline->currentFrame();
}

QPointF
TimeLineGui::toTimeLineCoordinates(double x,
                                   double y) const
{
    double w = (double)width();
    double h = (double)height();
    double bottom = _imp->_zoomCtx.bottom;
    double left = _imp->_zoomCtx.left;
    double top =  bottom +  h / _imp->_zoomCtx.zoomFactor;
    double right = left +  w / _imp->_zoomCtx.zoomFactor;

    return QPointF( ( ( (right - left) * x ) / w ) + left,( ( (bottom - top) * y ) / h ) + top );
}

QPointF
TimeLineGui::toWidgetCoordinates(double x,
                                 double y) const
{
    double w = (double)width();
    double h = (double)height();
    double bottom = _imp->_zoomCtx.bottom;
    double left = _imp->_zoomCtx.left;
    double top =  bottom +  h / _imp->_zoomCtx.zoomFactor;
    double right = left +  w / _imp->_zoomCtx.zoomFactor;

    return QPoint( ( (x - left) / (right - left) ) * w,( (y - top) / (bottom - top) ) * h );
}

void
TimeLineGui::onKeyframesIndicatorsChanged()
{
    repaint();
}

void
TimeLineGui::connectSlotsToViewerCache()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );

    Natron::CacheSignalEmitter* emitter = appPTR->getOrActivateViewerCacheSignalEmitter();
    QObject::connect( emitter, SIGNAL( addedEntry(SequenceTime) ), this, SLOT( onCachedFrameAdded(SequenceTime) ) );
    QObject::connect( emitter, SIGNAL( removedEntry(SequenceTime,int) ), this, SLOT( onCachedFrameRemoved(SequenceTime,int) ) );
    QObject::connect( emitter, SIGNAL( entryStorageChanged(SequenceTime,int,int) ), this,
                      SLOT( onCachedFrameStorageChanged(SequenceTime,int,int) ) );
    QObject::connect( emitter, SIGNAL( clearedDiskPortion() ), this, SLOT( onDiskCacheCleared() ) );
    QObject::connect( emitter, SIGNAL( clearedInMemoryPortion() ), this, SLOT( onMemoryCacheCleared() ) );
}

void
TimeLineGui::disconnectSlotsFromViewerCache()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );

    Natron::CacheSignalEmitter* emitter = appPTR->getOrActivateViewerCacheSignalEmitter();
    QObject::disconnect( emitter, SIGNAL( addedEntry(SequenceTime) ), this, SLOT( onCachedFrameAdded(SequenceTime) ) );
    QObject::disconnect( emitter, SIGNAL( removedEntry(SequenceTime,int) ), this, SLOT( onCachedFrameRemoved(SequenceTime,int) ) );
    QObject::disconnect( emitter, SIGNAL( entryStorageChanged(SequenceTime,int,int) ), this,
                         SLOT( onCachedFrameStorageChanged(SequenceTime,int,int) ) );
    QObject::disconnect( emitter, SIGNAL( clearedDiskPortion() ), this, SLOT( onDiskCacheCleared() ) );
    QObject::disconnect( emitter, SIGNAL( clearedInMemoryPortion() ), this, SLOT( onMemoryCacheCleared() ) );
}

bool
TimeLineGui::isFrameRangeEdited() const
{
    QMutexLocker k(&_imp->frameRangeEditedMutex);
    return _imp->isFrameRangeEdited;
}

void
TimeLineGui::setFrameRangeEdited(bool edited)
{
    QMutexLocker k(&_imp->frameRangeEditedMutex);
    _imp->isFrameRangeEdited = edited;
}

void
TimeLineGui::onCachedFrameAdded(SequenceTime time)
{
    _imp->cachedFrames.insert( CachedFrame(time, eStorageModeRAM) );
}

void
TimeLineGui::onCachedFrameRemoved(SequenceTime time,
                                  int /*storage*/)
{
    for (CachedFrames::iterator it = _imp->cachedFrames.begin(); it != _imp->cachedFrames.end(); ++it) {
        if (it->time == time) {
            _imp->cachedFrames.erase(it);
            break;
        }
    }
    update();
}

void
TimeLineGui::onCachedFrameStorageChanged(SequenceTime time,
                                         int /*oldStorage*/,
                                         int newStorage)
{
    for (CachedFrames::iterator it = _imp->cachedFrames.begin(); it != _imp->cachedFrames.end(); ++it) {
        if (it->time == time) {
            _imp->cachedFrames.erase(it);
            _imp->cachedFrames.insert( CachedFrame(time,(StorageModeEnum)newStorage) );
            break;
        }
    }
}

void
TimeLineGui::onMemoryCacheCleared()
{
    CachedFrames copy;

    for (CachedFrames::iterator it = _imp->cachedFrames.begin(); it != _imp->cachedFrames.end(); ++it) {
        if (it->mode == eStorageModeDisk) {
            copy.insert(*it);
        }
    }
    _imp->cachedFrames = copy;
    update();
}

void
TimeLineGui::onDiskCacheCleared()
{
    CachedFrames copy;

    for (CachedFrames::iterator it = _imp->cachedFrames.begin(); it != _imp->cachedFrames.end(); ++it) {
        if (it->mode == eStorageModeRAM) {
            copy.insert(*it);
        }
    }
    _imp->cachedFrames = copy;
    update();
}

void
TimeLineGui::clearCachedFrames()
{
    _imp->cachedFrames.clear();
    update();
}

void
TimeLineGui::onProjectFrameRangeChanged(int left,int right)
{
    if (!isFrameRangeEdited()) {
        setBoundariesInternal(left, right, true);
        setFrameRangeEdited(false);
        centerOn(left, right);
    }
    update();
}
