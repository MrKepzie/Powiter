//  Natron
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Created by Alexandre GAUTHIER-FOICHAT on 6/1/2012.
 * contact: immarespond at gmail dot com
 *
 */

// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>

#include "NodeGraph.h"
#include "NodeGraphPrivate.h"

#include <cstdlib>
#include <set>
#include <map>
#include <vector>
#include <locale>
#include <algorithm> // min, max

GCC_DIAG_UNUSED_PRIVATE_FIELD_OFF
// /opt/local/include/QtGui/qmime.h:119:10: warning: private field 'type' is not used [-Wunused-private-field]
#include <QGraphicsProxyWidget>
GCC_DIAG_UNUSED_PRIVATE_FIELD_ON
#include <QGraphicsTextItem>
#include <QFileSystemModel>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QDialogButtonBox>
#include <QUndoStack>
#include <QToolButton>
#include <QThread>
#include <QDropEvent>
#include <QApplication>
#include <QCheckBox>
#include <QMimeData>
#include <QLineEdit>
#include <QDebug>
#include <QtCore/QRectF>
#include <QRegExp>
#include <QtCore/QTimer>
#include <QAction>
#include <QPainter>
CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QMutex>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include <SequenceParsing.h>

#include "Engine/AppManager.h"
#include "Engine/BackDrop.h"
#include "Engine/Dot.h"
#include "Engine/FrameEntry.h"
#include "Engine/Hash64.h"
#include "Engine/KnobFile.h"
#include "Engine/Node.h"
#include "Engine/NodeGroup.h"
#include "Engine/NodeSerialization.h"
#include "Engine/OfxEffectInstance.h"
#include "Engine/OutputSchedulerThread.h"
#include "Engine/Plugin.h"
#include "Engine/Project.h"
#include "Engine/Settings.h"
#include "Engine/ViewerInstance.h"

#include "Gui/ActionShortcuts.h"
#include "Gui/BackDropGui.h"
#include "Gui/CurveEditor.h"
#include "Gui/CurveWidget.h"
#include "Gui/DockablePanel.h"
#include "Gui/Edge.h"
#include "Gui/FloatingWidget.h"
#include "Gui/Gui.h"
#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/GuiMacros.h"
#include "Gui/Histogram.h"
#include "Gui/KnobGui.h"
#include "Gui/Label.h"
#include "Gui/Menu.h"
#include "Gui/NodeBackDropSerialization.h"
#include "Gui/NodeClipBoard.h"
#include "Gui/NodeCreationDialog.h"
#include "Gui/NodeGraphUndoRedo.h"
#include "Gui/NodeGui.h"
#include "Gui/NodeGuiSerialization.h"
#include "Gui/NodeSettingsPanel.h"
#include "Gui/SequenceFileDialog.h"
#include "Gui/TabWidget.h"
#include "Gui/TimeLineGui.h"
#include "Gui/ToolButton.h"
#include "Gui/ViewerGL.h"
#include "Gui/ViewerTab.h"

#include "Global/QtCompat.h"

using namespace Natron;
using std::cout; using std::endl;


void
NodeGraph::connectCurrentViewerToSelection(int inputNB)
{
    if ( !getLastSelectedViewer() ) {
        _imp->_gui->getApp()->createNode(  CreateNodeArgs(PLUGINID_NATRON_VIEWER,
                                                          "",
                                                          -1,-1,
                                                          true,
                                                          INT_MIN,INT_MIN,
                                                          true,
                                                          true,
                                                          true,
                                                          QString(),
                                                          CreateNodeArgs::DefaultValuesList(),
                                                          getGroup()) );
    }

    ///get a pointer to the last user selected viewer
    boost::shared_ptr<InspectorNode> v = boost::dynamic_pointer_cast<InspectorNode>( getLastSelectedViewer()->
                                                                                     getInternalNode()->getNode() );

    ///if the node is no longer active (i.e: it was deleted by the user), don't do anything.
    if ( !v->isActivated() ) {
        return;
    }

    ///get a ptr to the NodeGui
    boost::shared_ptr<NodeGuiI> gui_i = v->getNodeGui();
    boost::shared_ptr<NodeGui> gui = boost::dynamic_pointer_cast<NodeGui>(gui_i);
    assert(gui);

    ///if there's no selected node or the viewer is selected, then try refreshing that input nb if it is connected.
    bool viewerAlreadySelected = std::find(_imp->_selection.begin(),_imp->_selection.end(),gui) != _imp->_selection.end();
    if (_imp->_selection.empty() || (_imp->_selection.size() > 1) || viewerAlreadySelected) {
        v->setActiveInputAndRefresh(inputNB);
        gui->refreshEdges();

        return;
    }

    boost::shared_ptr<NodeGui> selected = _imp->_selection.front();


    if ( !selected->getNode()->canOthersConnectToThisNode() ) {
        return;
    }

    ///if the node doesn't have the input 'inputNb' created yet, populate enough input
    ///so it can be created.
    Edge* foundInput = gui->getInputArrow(inputNB);
    assert(foundInput);
  
    ///and push a connect command to the selected node.
    pushUndoCommand( new ConnectCommand(this,foundInput,foundInput->getSource(),selected) );

    ///Set the viewer as the selected node (also wipe the current selection)
    selectNode(gui,false);
} // connectCurrentViewerToSelection

void
NodeGraph::enterEvent(QEvent* e)
{
    QGraphicsView::enterEvent(e);
    
    QWidget* currentFocus = qApp->focusWidget();
    
    bool canSetFocus = !currentFocus ||
    dynamic_cast<ViewerGL*>(currentFocus) ||
    dynamic_cast<CurveWidget*>(currentFocus) ||
    dynamic_cast<Histogram*>(currentFocus) ||
    dynamic_cast<NodeGraph*>(currentFocus) ||
    dynamic_cast<QToolButton*>(currentFocus) ||
    currentFocus->objectName() == "Properties" ||
    currentFocus->objectName() == "SettingsPanel" ||
    currentFocus->objectName() == "qt_tabwidget_tabbar";
    
    if (canSetFocus) {
        setFocus();
    }

    _imp->_nodeCreationShortcutEnabled = true;
   
}

void
NodeGraph::leaveEvent(QEvent* e)
{
    QGraphicsView::leaveEvent(e);

    _imp->_nodeCreationShortcutEnabled = false;
   // setFocus();
}

void
NodeGraph::setVisibleNodeDetails(bool visible)
{
    if (visible == _imp->_detailsVisible) {
        return;
    }
    _imp->_detailsVisible = visible;
    QMutexLocker k(&_imp->_nodesMutex);
    for (std::list<boost::shared_ptr<NodeGui> >::const_iterator it = _imp->_nodes.begin(); it != _imp->_nodes.end(); ++it) {
        (*it)->setVisibleDetails(visible);
    }
}

void
NodeGraph::wheelEventInternal(bool ctrlDown,double delta)
{
    double scaleFactor = pow( NATRON_WHEEL_ZOOM_PER_DELTA, delta);
    QTransform transfo = transform();
    
    double currentZoomFactor = transfo.mapRect( QRectF(0, 0, 1, 1) ).width();
    double newZoomfactor = currentZoomFactor * scaleFactor;
    if ((newZoomfactor < 0.01 && scaleFactor < 1.) || (newZoomfactor > 50 && scaleFactor > 1.)) {
        return;
    }
    if (newZoomfactor < 0.4) {
        setVisibleNodeDetails(false);
    } else if (newZoomfactor >= 0.4) {
        setVisibleNodeDetails(true);
    }
    
    if (ctrlDown && _imp->_magnifiedNode) {
        if (!_imp->_magnifOn) {
            _imp->_magnifOn = true;
            _imp->_nodeSelectedScaleBeforeMagnif = _imp->_magnifiedNode->scale();
        }
        _imp->_magnifiedNode->setScale_natron(_imp->_magnifiedNode->scale() * scaleFactor);
    } else {

        _imp->_accumDelta += delta;
        if (std::abs(_imp->_accumDelta) > 60) {
            scaleFactor = pow( NATRON_WHEEL_ZOOM_PER_DELTA, _imp->_accumDelta );
           // setSceneRect(NATRON_SCENE_MIN,NATRON_SCENE_MIN,NATRON_SCENE_MAX,NATRON_SCENE_MAX);
            scale(scaleFactor,scaleFactor);
            _imp->_accumDelta = 0;
        }
        _imp->_refreshOverlays = true;
        
    }

}

void
NodeGraph::wheelEvent(QWheelEvent* e)
{
    if (e->orientation() != Qt::Vertical) {
        return;
    }
    wheelEventInternal(modCASIsControl(e), e->delta());
    _imp->_lastMousePos = e->pos();
    update();
}

void
NodeGraph::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Control) {
        if (_imp->_magnifOn) {
            _imp->_magnifOn = false;
            _imp->_magnifiedNode->setScale_natron(_imp->_nodeSelectedScaleBeforeMagnif);
        }
        if (_imp->_bendPointsVisible) {
            _imp->setNodesBendPointsVisible(false);
        }
    }
}

void
NodeGraph::removeNode(const boost::shared_ptr<NodeGui> & node)
{
 
    NodeGroup* isGrp = dynamic_cast<NodeGroup*>(node->getNode()->getLiveInstance());
    const std::vector<boost::shared_ptr<KnobI> > & knobs = node->getNode()->getKnobs();

    
    for (U32 i = 0; i < knobs.size(); ++i) {
        std::list<boost::shared_ptr<KnobI> > listeners;
        knobs[i]->getListeners(listeners);
        ///For all listeners make sure they belong to a node
        bool foundEffect = false;
        for (std::list<boost::shared_ptr<KnobI> >::iterator it2 = listeners.begin(); it2 != listeners.end(); ++it2) {
            EffectInstance* isEffect = dynamic_cast<EffectInstance*>( (*it2)->getHolder() );
            if (!isEffect) {
                continue;
            }
            if (isGrp && isEffect->getNode()->getGroup().get() == isGrp) {
                continue;
            }
            
            if ( isEffect && ( isEffect != node->getNode()->getLiveInstance() ) ) {
                foundEffect = true;
                break;
            }
        }
        if (foundEffect) {
            Natron::StandardButtonEnum reply = Natron::questionDialog( tr("Delete").toStdString(), tr("This node has one or several "
                                                                                                  "parameters from which other parameters "
                                                                                                  "of the project rely on through expressions "
                                                                                                  "or links. Deleting this node will "
                                                                                                  "remove these expressions  "
                                                                                                  "and undoing the action will not recover "
                                                                                                  "them. Do you wish to continue ?")
                                                                   .toStdString(), false );
            if (reply == Natron::eStandardButtonNo) {
                return;
            }
            break;
        }
    }

    node->setUserSelected(false);
    std::list<boost::shared_ptr<NodeGui> > nodesToRemove;
    nodesToRemove.push_back(node);
    pushUndoCommand( new RemoveMultipleNodesCommand(this,nodesToRemove) );
}

void
NodeGraph::deleteSelection()
{
    if ( !_imp->_selection.empty()) {
        NodeGuiList nodesToRemove = _imp->_selection;

        
        ///For all backdrops also move all the nodes contained within it
        for (NodeGuiList::iterator it = nodesToRemove.begin(); it != nodesToRemove.end(); ++it) {
            NodeGuiList nodesWithinBD = getNodesWithinBackDrop(*it);
            for (NodeGuiList::iterator it2 = nodesWithinBD.begin(); it2 != nodesWithinBD.end(); ++it2) {
                NodeGuiList::iterator found = std::find(nodesToRemove.begin(),nodesToRemove.end(),*it2);
                if ( found == nodesToRemove.end()) {
                    nodesToRemove.push_back(*it2);
                }
            }
        }


        for (NodeGuiList::iterator it = nodesToRemove.begin(); it != nodesToRemove.end(); ++it) {
            
            const std::vector<boost::shared_ptr<KnobI> > & knobs = (*it)->getNode()->getKnobs();
            bool mustBreak = false;
            
            NodeGroup* isGrp = dynamic_cast<NodeGroup*>((*it)->getNode()->getLiveInstance());
            
            for (U32 i = 0; i < knobs.size(); ++i) {
                std::list<boost::shared_ptr<KnobI> > listeners;
                knobs[i]->getListeners(listeners);

                ///For all listeners make sure they belong to a node
                bool foundEffect = false;
                for (std::list<boost::shared_ptr<KnobI> >::iterator it2 = listeners.begin(); it2 != listeners.end(); ++it2) {
                    EffectInstance* isEffect = dynamic_cast<EffectInstance*>( (*it2)->getHolder() );
                    
                    if (!isEffect) {
                        continue;
                    }
                    if (isGrp && isEffect->getNode()->getGroup().get() == isGrp) {
                        continue;
                    }
                    
                    if ( isEffect && ( isEffect != (*it)->getNode()->getLiveInstance() ) ) {
                        foundEffect = true;
                        break;
                    }
                }
                if (foundEffect) {
                    Natron::StandardButtonEnum reply = Natron::questionDialog( tr("Delete").toStdString(),
                                                                           tr("This node has one or several "
                                                                              "parameters from which other parameters "
                                                                              "of the project rely on through expressions "
                                                                              "or links. Deleting this node will "
                                                                              "remove these expressions. "
                                                                              ". Undoing the action will not recover "
                                                                              "them. \nContinue anyway ?")
                                                                           .toStdString(), false );
                    if (reply == Natron::eStandardButtonNo) {
                        return;
                    }
                    mustBreak = true;
                    break;
                }
            }
            if (mustBreak) {
                break;
            }
        }


        for (NodeGuiList::iterator it = nodesToRemove.begin(); it != nodesToRemove.end(); ++it) {
            (*it)->setUserSelected(false);
        }


        pushUndoCommand( new RemoveMultipleNodesCommand(this,nodesToRemove) );
        _imp->_selection.clear();
    }
} // deleteSelection

void
NodeGraph::selectNode(const boost::shared_ptr<NodeGui> & n,
                      bool addToSelection)
{
    if ( !n->isVisible() ) {
        return;
    }
    bool alreadyInSelection = std::find(_imp->_selection.begin(),_imp->_selection.end(),n) != _imp->_selection.end();


    assert(n);
    if (addToSelection && !alreadyInSelection) {
        _imp->_selection.push_back(n);
    } else if (!addToSelection) {
        clearSelection();
        _imp->_selection.push_back(n);
    }

    n->setUserSelected(true);

    ViewerInstance* isViewer = dynamic_cast<ViewerInstance*>( n->getNode()->getLiveInstance() );
    if (isViewer) {
        OpenGLViewerI* viewer = isViewer->getUiContext();
        const std::list<ViewerTab*> & viewerTabs = _imp->_gui->getViewersList();
        for (std::list<ViewerTab*>::const_iterator it = viewerTabs.begin(); it != viewerTabs.end(); ++it) {
            if ( (*it)->getViewer() == viewer ) {
                setLastSelectedViewer( (*it) );
            }
        }
    }

    bool magnifiedNodeSelected = false;
    if (_imp->_magnifiedNode) {
        magnifiedNodeSelected = std::find(_imp->_selection.begin(),_imp->_selection.end(),_imp->_magnifiedNode)
                                != _imp->_selection.end();
    }
    if (magnifiedNodeSelected && _imp->_magnifOn) {
        _imp->_magnifOn = false;
        _imp->_magnifiedNode->setScale_natron(_imp->_nodeSelectedScaleBeforeMagnif);
    }
}

void
NodeGraph::setLastSelectedViewer(ViewerTab* tab)
{
    _imp->lastSelectedViewer = tab;
}

ViewerTab*
NodeGraph::getLastSelectedViewer() const
{
    return _imp->lastSelectedViewer;
}

void
NodeGraph::setSelection(const std::list<boost::shared_ptr<NodeGui> >& nodes)
{
    clearSelection();
    for (std::list<boost::shared_ptr<NodeGui> >::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
        selectNode(*it, true);
    }
}

void
NodeGraph::clearSelection()
{
    {
        QMutexLocker l(&_imp->_nodesMutex);
        for (std::list<boost::shared_ptr<NodeGui> >::iterator it = _imp->_selection.begin(); it != _imp->_selection.end(); ++it) {
            (*it)->setUserSelected(false);
        }
    }

    _imp->_selection.clear();

}

void
NodeGraph::updateNavigator()
{
    if ( !areAllNodesVisible() ) {
        _imp->_navigator->setPixmap( QPixmap::fromImage( getFullSceneScreenShot() ) );
        _imp->_navigator->show();
    } else {
        _imp->_navigator->hide();
    }
}

bool
NodeGraph::areAllNodesVisible()
{
    QRectF rect = visibleSceneRect();
    QMutexLocker l(&_imp->_nodesMutex);

    for (std::list<boost::shared_ptr<NodeGui> >::iterator it = _imp->_nodes.begin(); it != _imp->_nodes.end(); ++it) {
        if ( (*it)->isVisible() ) {
            if ( !rect.contains( (*it)->boundingRectWithEdges() ) ) {
                return false;
            }
        }
    }
    return true;
}
