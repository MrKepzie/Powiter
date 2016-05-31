/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
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

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "GPUContextPool.h"

#include <set>
#include <QMutex>
#include <QWaitCondition>

#include "Engine/OSGLContext.h"

NATRON_NAMESPACE_ENTER;

struct GPUContextPoolPrivate
{
    mutable QMutex contextPoolMutex;
    QWaitCondition glContextPoolEmpty;

    // protected by contextPoolMutex
    std::set<OSGLContextPtr> glContextPool, attachedGLContexts;

    // The OpenGL context to use for sharing
    boost::weak_ptr<OSGLContext> glShareContext;

    // protected by contextPoolMutex
    int maxContexts;

    GPUContextPoolPrivate(int maxContexts)
        : contextPoolMutex()
          , glContextPoolEmpty()
          , glContextPool()
          , attachedGLContexts()
          , glShareContext()
          , maxContexts(maxContexts)
    {
    }
};

GPUContextPool::GPUContextPool(int maxContextsCount)
    : _imp( new GPUContextPoolPrivate(maxContextsCount) )
{
}

GPUContextPool::~GPUContextPool()
{
}

void
GPUContextPool::setMaxContextCount(int maxContextCount)
{
    QMutexLocker k(&_imp->contextPoolMutex);

    _imp->maxContexts = maxContextCount;
}

int
GPUContextPool::getNumCreatedGLContext() const
{
    QMutexLocker k(&_imp->contextPoolMutex);

    return (int)_imp->glContextPool.size();
}

int
GPUContextPool::getNumAttachedGLContext() const
{
    QMutexLocker k(&_imp->contextPoolMutex);

    return (int)_imp->attachedGLContexts.size();
}

OSGLContextPtr
GPUContextPool::attachGLContextToRender()
{
    QMutexLocker k(&_imp->contextPoolMutex);

    while (_imp->glContextPool.empty() && (int)_imp->attachedGLContexts.size() >= _imp->maxContexts) {
        _imp->glContextPoolEmpty.wait(&_imp->contextPoolMutex);
    }


    OSGLContextPtr shareContext = _imp->glShareContext.lock();
    OSGLContextPtr newContext;
    if ( _imp->glContextPool.empty() ) {
        assert( (int)_imp->attachedGLContexts.size() < _imp->maxContexts );
        //  Create a new one
        newContext.reset( new OSGLContext( FramebufferConfig(), shareContext.get() ) );
    } else {
        std::set<OSGLContextPtr>::iterator it = _imp->glContextPool.begin();
        newContext = *it;
        _imp->glContextPool.erase(it);
    }

    // If this is the first context, set it as the sharing context
    if (!shareContext) {
        _imp->glShareContext = newContext;
    }

    newContext->makeContextCurrent();
    _imp->attachedGLContexts.insert(newContext);

    return newContext;
}

void
GPUContextPool::releaseGLContextFromRender(const OSGLContextPtr& context)
{
    QMutexLocker k(&_imp->contextPoolMutex);

    // The thread has a context on its TLS so it must be found in the attached contexts set
    std::set<OSGLContextPtr>::iterator foundAttached = _imp->attachedGLContexts.find(context);

    assert( foundAttached != _imp->attachedGLContexts.end() );
    if ( foundAttached != _imp->attachedGLContexts.end() ) {
        _imp->attachedGLContexts.erase(foundAttached);

        // Re-insert back into the contextPool so it can be re-used
        _imp->glContextPool.insert(*foundAttached);

        // Wake-up one thread waiting in attachContextToThread().
        // No need to wake all threads because each thread releasing a context will wake up one thread.
        _imp->glContextPoolEmpty.wakeOne();
    }
}

NATRON_NAMESPACE_EXIT;
